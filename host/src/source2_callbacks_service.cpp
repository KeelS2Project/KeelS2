#include "source2_callbacks_service.h"

#include "host.h"
#include "keelhook_service.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace keels2::host
{

struct Source2CallbacksService::Subscription
{
    KeelSource2SubscriptionHandle handle{};
    KeelPluginHandle owner{};
    KeelSource2CallbackType type{};
    std::int32_t priority{};
    std::uint64_t sequence{};
    std::string game_event;
    KeelSource2Callback callback{};
    void* user_data{};
    std::atomic<bool> enabled{};
    std::atomic<std::uint32_t> active{};
};

std::atomic<Source2CallbacksService*> Source2CallbacksService::active_{};
thread_local std::array<const Source2CallbacksService::Subscription*, 64>
    Source2CallbacksService::callback_stack_{};
thread_local std::size_t Source2CallbacksService::callback_depth_{};

Source2CallbacksService::Source2CallbacksService(
    Host& host,
    GameAdapter& adapter,
    KeelHookService& hooks)
    : host_(host), adapter_(adapter), hooks_(hooks)
{
    Source2CallbacksService* expected{};
    if (!active_.compare_exchange_strong(expected, this, std::memory_order_acq_rel))
    {
        throw std::runtime_error("Source 2 callback service already exists");
    }
    api_ = {
        sizeof(KeelSource2CallbacksApi),
        KEELS2_SOURCE2_CALLBACKS_API_VERSION,
        &SubscribeEntry,
        &UnsubscribeEntry
    };
    std::string error;
    const KeelResult result = adapter_.InitializeSource2Callbacks(
        hooks_.Api(),
        0,
        &DispatchEntry,
        this,
        error);
    if (result != KEEL_RESULT_OK)
    {
        active_.store(nullptr, std::memory_order_release);
        throw std::runtime_error(
            error.empty() ? "Source 2 callback hooks could not be initialized" : error);
    }
}

Source2CallbacksService::~Source2CallbacksService()
{
    active_.store(nullptr, std::memory_order_release);
    static_cast<void>(Shutdown());
}

const KeelSource2CallbacksApi& Source2CallbacksService::Api() const noexcept
{
    return api_;
}

void Source2CallbacksService::Activate(KeelPluginHandle plugin)
{
    std::scoped_lock lock(registry_mutex_);
    if (shutting_down_)
    {
        return;
    }
    for (const auto& [handle, subscription] : subscriptions_)
    {
        static_cast<void>(handle);
        if (subscription->owner == plugin)
        {
            subscription->enabled.store(true, std::memory_order_release);
        }
    }
}

KeelResult Source2CallbacksService::Deactivate(KeelPluginHandle plugin)
{
    std::vector<std::shared_ptr<Subscription>> owned;
    {
        std::scoped_lock lock(registry_mutex_);
        if (IsCurrentOwner(plugin))
        {
            return KEEL_RESULT_BUSY;
        }
        for (const auto& [handle, subscription] : subscriptions_)
        {
            static_cast<void>(handle);
            if (subscription->owner == plugin)
            {
                subscription->enabled.store(false, std::memory_order_release);
                owned.push_back(subscription);
            }
        }
    }
    for (const auto& subscription : owned)
    {
        WaitForZero(subscription->active);
    }
    return KEEL_RESULT_OK;
}

KeelResult Source2CallbacksService::ReleasePlugin(KeelPluginHandle plugin)
{
    const KeelResult result = Deactivate(plugin);
    if (result != KEEL_RESULT_OK)
    {
        return result;
    }
    std::scoped_lock lock(registry_mutex_);
    std::erase_if(subscriptions_, [plugin](const auto& entry) {
        return entry.second->owner == plugin;
    });
    return KEEL_RESULT_OK;
}

bool Source2CallbacksService::Shutdown()
{
    std::vector<std::shared_ptr<Subscription>> subscriptions;
    bool current{};
    {
        std::scoped_lock lock(registry_mutex_);
        if (shutdown_complete_)
        {
            return true;
        }
        shutting_down_ = true;
        subscriptions.reserve(subscriptions_.size());
        for (const auto& [handle, subscription] : subscriptions_)
        {
            static_cast<void>(handle);
            subscription->enabled.store(false, std::memory_order_release);
            subscriptions.push_back(subscription);
            current = current || IsCurrentCallback(subscription.get());
        }
    }
    if (current)
    {
        return false;
    }
    for (const auto& subscription : subscriptions)
    {
        WaitForZero(subscription->active);
    }
    adapter_.ShutdownSource2Callbacks();
    std::scoped_lock lock(registry_mutex_);
    subscriptions_.clear();
    listened_events_.clear();
    shutdown_complete_ = true;
    return true;
}

KeelResult Source2CallbacksService::SubscribeEntry(
    KeelPluginHandle plugin,
    const KeelSource2SubscriptionSpec* spec,
    KeelSource2SubscriptionHandle* subscription)
{
    Source2CallbacksService* service = active_.load(std::memory_order_acquire);
    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return service->Subscribe(plugin, spec, subscription);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while subscribing to a Source 2 callback");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult Source2CallbacksService::UnsubscribeEntry(
    KeelPluginHandle plugin,
    KeelSource2SubscriptionHandle subscription)
{
    Source2CallbacksService* service = active_.load(std::memory_order_acquire);
    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return service->Unsubscribe(plugin, subscription);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while unsubscribing from a Source 2 callback");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelBool Source2CallbacksService::DispatchEntry(
    KeelSource2CallbackEvent& event,
    void* user_data)
{
    auto* service = static_cast<Source2CallbacksService*>(user_data);
    return service && active_.load(std::memory_order_acquire) == service
        ? service->Dispatch(event)
        : KEEL_TRUE;
}

KeelResult Source2CallbacksService::Subscribe(
    KeelPluginHandle plugin,
    const KeelSource2SubscriptionSpec* spec,
    KeelSource2SubscriptionHandle* output)
{
    if (!spec || spec->size != sizeof(KeelSource2SubscriptionSpec) || !output ||
        !ValidType(spec->type) || spec->reserved != 0 || !spec->callback)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *output = 0;
    std::string game_event;
    if (spec->type == KEELS2_SOURCE2_GAME_EVENT)
    {
        if (!ValidEventName(spec->game_event, game_event))
        {
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
    }
    else if (spec->game_event && spec->game_event[0])
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }

    {
        std::scoped_lock host_lock(host_.state_mutex_);
        PluginRecord* owner = host_.PluginByHandle(plugin);
        if (!host_.accepting_resources_ || !owner || !owner->accepting_resources)
        {
            return KEEL_RESULT_NOT_READY;
        }
    }

    if (!game_event.empty())
    {
        std::scoped_lock lock(registry_mutex_);
        if (shutting_down_)
        {
            return KEEL_RESULT_NOT_READY;
        }
        if (!listened_events_.contains(game_event))
        {
            std::string error;
            const KeelResult result = adapter_.ListenForGameEvent(game_event.c_str(), error);
            if (result != KEEL_RESULT_OK)
            {
                if (!error.empty())
                {
                    host_.Write(KEEL_LOG_ERROR, error);
                }
                return result;
            }
            listened_events_.insert(game_event);
        }
    }

    std::scoped_lock host_lock(host_.state_mutex_);
    std::scoped_lock registry_lock(registry_mutex_);
    PluginRecord* owner = host_.PluginByHandle(plugin);
    if (shutting_down_ || !host_.accepting_resources_ || !owner || !owner->accepting_resources)
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (next_subscription_ == 0 || next_sequence_ == 0)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
    auto subscription = std::make_shared<Subscription>();
    subscription->handle = next_subscription_++;
    subscription->owner = plugin;
    subscription->type = spec->type;
    subscription->priority = spec->priority;
    subscription->sequence = next_sequence_++;
    subscription->game_event = std::move(game_event);
    subscription->callback = spec->callback;
    subscription->user_data = spec->user_data;
    subscription->enabled.store(
        owner->state == PluginState::loaded && !owner->loading,
        std::memory_order_release);
    const auto handle = subscription->handle;
    subscriptions_.emplace(handle, std::move(subscription));
    *output = handle;
    return KEEL_RESULT_OK;
}

KeelResult Source2CallbacksService::Unsubscribe(
    KeelPluginHandle plugin,
    KeelSource2SubscriptionHandle handle)
{
    std::shared_ptr<Subscription> subscription;
    {
        std::scoped_lock lock(registry_mutex_);
        const auto iterator = subscriptions_.find(handle);
        if (iterator == subscriptions_.end() || iterator->second->owner != plugin)
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        subscription = iterator->second;
        subscription->enabled.store(false, std::memory_order_release);
        if (IsCurrentCallback(subscription.get()))
        {
            return KEEL_RESULT_BUSY;
        }
    }
    WaitForZero(subscription->active);
    std::scoped_lock lock(registry_mutex_);
    const auto iterator = subscriptions_.find(handle);
    if (iterator != subscriptions_.end() && iterator->second == subscription)
    {
        subscriptions_.erase(iterator);
    }
    return KEEL_RESULT_OK;
}

KeelBool Source2CallbacksService::Dispatch(KeelSource2CallbackEvent& event)
{
    if (!ValidEnvelope(event))
    {
        return KEEL_TRUE;
    }
    const char* event_name{};
    if (event.type == KEELS2_SOURCE2_GAME_EVENT)
    {
        const auto* payload = static_cast<const KeelSource2GameEvent*>(event.payload);
        event_name = payload->name;
    }
    std::vector<std::shared_ptr<Subscription>> callbacks;
    {
        std::scoped_lock lock(registry_mutex_);
        if (shutting_down_)
        {
            return KEEL_TRUE;
        }
        for (const auto& [handle, subscription] : subscriptions_)
        {
            static_cast<void>(handle);
            if (subscription->type == event.type &&
                subscription->enabled.load(std::memory_order_acquire) &&
                (event.type != KEELS2_SOURCE2_GAME_EVENT ||
                    subscription->game_event == event_name))
            {
                callbacks.push_back(subscription);
            }
        }
    }
    std::sort(callbacks.begin(), callbacks.end(), [](const auto& left, const auto& right) {
        return left->priority != right->priority
            ? left->priority > right->priority
            : left->sequence < right->sequence;
    });

    bool accepted = true;
    std::array<char, KEELS2_SOURCE2_REJECTION_CAPACITY> winning_rejection{};
    for (const auto& callback : callbacks)
    {
        callback->active.fetch_add(1, std::memory_order_acq_rel);
        if (!callback->enabled.load(std::memory_order_acquire))
        {
            LeaveActive(callback->active);
            continue;
        }
        if (callback_depth_ == callback_stack_.size())
        {
            LeaveActive(callback->active);
            host_.Write(KEEL_LOG_ERROR, "Source 2 callback recursion limit reached");
            continue;
        }

        KeelSource2CallbackEvent callback_event = event;
        KeelSource2ClientConnect connect_payload{};
        std::array<char, KEELS2_SOURCE2_REJECTION_CAPACITY> local_rejection{};
        if (event.type == KEELS2_SOURCE2_CLIENT_CONNECT)
        {
            connect_payload = *static_cast<const KeelSource2ClientConnect*>(event.payload);
            connect_payload.rejection_message = local_rejection.data();
            connect_payload.rejection_capacity = static_cast<std::uint32_t>(local_rejection.size());
            callback_event.payload = &connect_payload;
        }

        callback_stack_[callback_depth_++] = callback.get();
        KeelBool result{KEEL_TRUE};
        try
        {
            result = callback->callback(&callback_event, callback->user_data);
        }
        catch (...)
        {
            host_.Write(KEEL_LOG_ERROR, "plugin threw during a Source 2 callback");
        }
        callback_stack_[--callback_depth_] = nullptr;
        LeaveActive(callback->active);

        if ((event.type == KEELS2_SOURCE2_CLIENT_CONNECT ||
                event.type == KEELS2_SOURCE2_CLIENT_COMMAND) &&
            result == KEEL_FALSE)
        {
            if (accepted && event.type == KEELS2_SOURCE2_CLIENT_CONNECT)
            {
                local_rejection.back() = '\0';
                const char* source = local_rejection[0]
                    ? local_rejection.data()
                    : "Connection rejected by a KeelS2 plugin";
                const std::size_t length = std::min(
                    std::strlen(source),
                    winning_rejection.size() - 1);
                std::memcpy(winning_rejection.data(), source, length);
                winning_rejection[length] = '\0';
            }
            accepted = false;
        }
    }

    if (!accepted && event.type == KEELS2_SOURCE2_CLIENT_CONNECT)
    {
        auto* payload = const_cast<KeelSource2ClientConnect*>(
            static_cast<const KeelSource2ClientConnect*>(event.payload));
        if (payload->rejection_message && payload->rejection_capacity != 0)
        {
            const std::size_t length = std::min(
                std::strlen(winning_rejection.data()),
                static_cast<std::size_t>(payload->rejection_capacity - 1));
            std::memcpy(payload->rejection_message, winning_rejection.data(), length);
            payload->rejection_message[length] = '\0';
        }
    }
    return accepted ? KEEL_TRUE : KEEL_FALSE;
}

bool Source2CallbacksService::ValidType(KeelSource2CallbackType type) noexcept
{
    return type >= KEELS2_SOURCE2_LEVEL_INIT && type <= KEELS2_SOURCE2_CLIENT_COMMAND;
}

bool Source2CallbacksService::ValidEventName(const char* name, std::string& output) noexcept
{
    if (!name)
    {
        return false;
    }
    std::size_t length{};
    while (length < 32 && name[length])
    {
        ++length;
    }
    if (length == 0 || length == 32)
    {
        return false;
    }
    output.assign(name, length);
    return true;
}

bool Source2CallbacksService::ValidEnvelope(const KeelSource2CallbackEvent& event) noexcept
{
    if (event.size != sizeof(KeelSource2CallbackEvent) || !ValidType(event.type) ||
        event.reserved != 0 || !event.payload)
    {
        return false;
    }
    switch (event.type)
    {
        case KEELS2_SOURCE2_LEVEL_INIT:
        {
            const auto* payload = static_cast<const KeelSource2LevelInit*>(event.payload);
            return event.payload_size == sizeof(KeelSource2LevelInit) &&
                payload->size == sizeof(KeelSource2LevelInit) && payload->reserved == 0;
        }
        case KEELS2_SOURCE2_LEVEL_SHUTDOWN:
        {
            const auto* payload = static_cast<const KeelSource2LevelShutdown*>(event.payload);
            return event.payload_size == sizeof(KeelSource2LevelShutdown) &&
                payload->size == sizeof(KeelSource2LevelShutdown) && payload->reserved == 0;
        }
        case KEELS2_SOURCE2_GAME_EVENT:
        {
            const auto* payload = static_cast<const KeelSource2GameEvent*>(event.payload);
            return event.payload_size == sizeof(KeelSource2GameEvent) &&
                payload->size == sizeof(KeelSource2GameEvent) && payload->event &&
                payload->name && payload->name[0] && payload->reserved == 0;
        }
        case KEELS2_SOURCE2_CLIENT_CONNECT:
        {
            const auto* payload = static_cast<const KeelSource2ClientConnect*>(event.payload);
            return event.payload_size == sizeof(KeelSource2ClientConnect) &&
                payload->size == sizeof(KeelSource2ClientConnect) && payload->name &&
                payload->network_id && payload->reserved == 0 &&
                payload->reserved_message == 0 &&
                (payload->unknown == KEEL_FALSE || payload->unknown == KEEL_TRUE) &&
                (!payload->rejection_capacity || payload->rejection_message);
        }
        case KEELS2_SOURCE2_CLIENT_COMMAND:
        {
            const auto* payload = static_cast<const KeelSource2ClientCommand*>(event.payload);
            return event.payload_size == sizeof(KeelSource2ClientCommand) &&
                payload->size == sizeof(KeelSource2ClientCommand) && payload->command;
        }
        default:
            return false;
    }
}

bool Source2CallbacksService::IsCurrentOwner(KeelPluginHandle plugin) noexcept
{
    for (std::size_t index{}; index < callback_depth_; ++index)
    {
        if (callback_stack_[index] && callback_stack_[index]->owner == plugin)
        {
            return true;
        }
    }
    return false;
}

bool Source2CallbacksService::IsCurrentCallback(const Subscription* subscription) noexcept
{
    return std::find(
        callback_stack_.begin(),
        callback_stack_.begin() + static_cast<std::ptrdiff_t>(callback_depth_),
        subscription) != callback_stack_.begin() + static_cast<std::ptrdiff_t>(callback_depth_);
}

void Source2CallbacksService::LeaveActive(std::atomic<std::uint32_t>& active) noexcept
{
    if (active.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        active.notify_all();
    }
}

void Source2CallbacksService::WaitForZero(std::atomic<std::uint32_t>& active) noexcept
{
    std::uint32_t value = active.load(std::memory_order_acquire);
    while (value != 0)
    {
        active.wait(value, std::memory_order_acquire);
        value = active.load(std::memory_order_acquire);
    }
}

}
