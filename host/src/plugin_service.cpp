#include "plugin_service.h"

#include "host.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace keels2::host
{

struct PluginService::Subscription
{
    KeelPluginSubscriptionHandle handle{};
    KeelPluginHandle owner{};
    KeelPluginEventType event{};
    std::uint64_t sequence{};
    KeelPluginEventCallback callback{};
    void* user_data{};
    std::atomic<bool> enabled{};
    std::atomic<std::uint32_t> active{};
};

std::atomic<PluginService*> PluginService::active_{};
thread_local std::array<const PluginService::Subscription*, 64> PluginService::callback_stack_{};
thread_local std::size_t PluginService::callback_depth_{};

PluginService::PluginService(Host& host) : host_(host)
{
    PluginService* expected{};
    if (!active_.compare_exchange_strong(expected, this, std::memory_order_acq_rel))
    {
        throw std::runtime_error("plugin service already exists");
    }
    api_ = {
        sizeof(KeelPluginsApi),
        KEELS2_PLUGINS_API_VERSION,
        &CountEntry,
        &AtEntry,
        &GetEntry,
        &FindEntry,
        &PauseEntry,
        &ResumeEntry,
        &SubscribeEntry,
        &UnsubscribeEntry
    };
}

PluginService::~PluginService()
{
    active_.store(nullptr, std::memory_order_release);
    static_cast<void>(Shutdown());
}

const KeelPluginsApi& PluginService::Api() const noexcept
{
    return api_;
}

void PluginService::Activate(KeelPluginHandle plugin)
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

KeelResult PluginService::Deactivate(KeelPluginHandle plugin)
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

KeelResult PluginService::ReleasePlugin(KeelPluginHandle plugin)
{
    const KeelResult deactivated = Deactivate(plugin);
    if (deactivated != KEEL_RESULT_OK)
    {
        return deactivated;
    }
    std::scoped_lock lock(registry_mutex_);
    for (auto iterator = subscriptions_.begin(); iterator != subscriptions_.end();)
    {
        if (iterator->second->owner == plugin)
        {
            iterator = subscriptions_.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
    return KEEL_RESULT_OK;
}

void PluginService::Publish(
    KeelPluginEventType event,
    const KeelPluginSnapshot& snapshot)
{
    if (!ValidEvent(event) || event == KEELS2_PLUGIN_EVENT_ALL_LOADED)
    {
        return;
    }
    const std::uint64_t sequence = next_event_sequence_.fetch_add(1, std::memory_order_relaxed);
    if (sequence == 0)
    {
        host_.Write(KEEL_LOG_ERROR, "plugin event sequence space is exhausted");
        return;
    }
    const KeelPluginEvent envelope{sizeof(KeelPluginEvent), event, sequence, snapshot};
    Dispatch(envelope);
}

void PluginService::PublishAllLoaded()
{
    const std::uint64_t sequence = next_event_sequence_.fetch_add(1, std::memory_order_relaxed);
    if (sequence == 0)
    {
        host_.Write(KEEL_LOG_ERROR, "plugin event sequence space is exhausted");
        return;
    }
    KeelPluginSnapshot snapshot{};
    snapshot.size = sizeof(snapshot);
    const KeelPluginEvent envelope{
        sizeof(KeelPluginEvent),
        KEELS2_PLUGIN_EVENT_ALL_LOADED,
        sequence,
        snapshot
    };
    Dispatch(envelope);
}

bool PluginService::Shutdown()
{
    std::vector<std::shared_ptr<Subscription>> subscriptions;
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
            if (IsCurrentSubscription(subscription.get()))
            {
                return false;
            }
            subscriptions.push_back(subscription);
        }
    }
    for (const auto& subscription : subscriptions)
    {
        WaitForZero(subscription->active);
    }
    std::scoped_lock lock(registry_mutex_);
    subscriptions_.clear();
    shutdown_complete_ = true;
    return true;
}

#define KEELS2_PLUGIN_SERVICE_ENTRY(name, expression, failure_message) \
    PluginService* service = active_.load(std::memory_order_acquire); \
    if (!service) \
    { \
        return KEEL_RESULT_NOT_READY; \
    } \
    try \
    { \
        return (expression); \
    } \
    catch (...) \
    { \
        service->host_.Write(KEEL_LOG_ERROR, failure_message); \
        return KEEL_RESULT_ENGINE_FAILURE; \
    }

KeelResult PluginService::CountEntry(KeelPluginHandle plugin, std::uint32_t* count)
{
    KEELS2_PLUGIN_SERVICE_ENTRY(CountEntry, service->Count(plugin, count),
        "exception while counting plugins")
}

KeelResult PluginService::AtEntry(
    KeelPluginHandle plugin,
    std::uint32_t index,
    KeelPluginSnapshot* snapshot)
{
    KEELS2_PLUGIN_SERVICE_ENTRY(AtEntry, service->At(plugin, index, snapshot),
        "exception while reading a plugin snapshot")
}

KeelResult PluginService::GetEntry(
    KeelPluginHandle plugin,
    KeelPluginHandle target,
    KeelPluginSnapshot* snapshot)
{
    KEELS2_PLUGIN_SERVICE_ENTRY(GetEntry, service->Get(plugin, target, snapshot),
        "exception while reading a plugin snapshot")
}

KeelResult PluginService::FindEntry(
    KeelPluginHandle plugin,
    const char* name,
    KeelPluginSnapshot* snapshot)
{
    KEELS2_PLUGIN_SERVICE_ENTRY(FindEntry, service->Find(plugin, name, snapshot),
        "exception while finding a plugin")
}

KeelResult PluginService::PauseEntry(KeelPluginHandle plugin, KeelPluginHandle target)
{
    KEELS2_PLUGIN_SERVICE_ENTRY(PauseEntry, service->Pause(plugin, target),
        "exception while pausing a plugin")
}

KeelResult PluginService::ResumeEntry(KeelPluginHandle plugin, KeelPluginHandle target)
{
    KEELS2_PLUGIN_SERVICE_ENTRY(ResumeEntry, service->Resume(plugin, target),
        "exception while resuming a plugin")
}

KeelResult PluginService::SubscribeEntry(
    KeelPluginHandle plugin,
    const KeelPluginSubscriptionSpec* spec,
    KeelPluginSubscriptionHandle* subscription)
{
    KEELS2_PLUGIN_SERVICE_ENTRY(SubscribeEntry, service->Subscribe(plugin, spec, subscription),
        "exception while subscribing to plugin events")
}

KeelResult PluginService::UnsubscribeEntry(
    KeelPluginHandle plugin,
    KeelPluginSubscriptionHandle subscription)
{
    KEELS2_PLUGIN_SERVICE_ENTRY(UnsubscribeEntry, service->Unsubscribe(plugin, subscription),
        "exception while unsubscribing from plugin events")
}

#undef KEELS2_PLUGIN_SERVICE_ENTRY

KeelResult PluginService::Count(KeelPluginHandle plugin, std::uint32_t* count)
{
    if (!count)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *count = 0;
    std::scoped_lock lock(host_.state_mutex_);
    const PluginRecord* owner = host_.PluginByHandle(plugin);
    if (!host_.accepting_resources_ || !owner || !owner->accepting_resources)
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (host_.plugins_.size() > UINT32_MAX)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
    *count = static_cast<std::uint32_t>(host_.plugins_.size());
    return KEEL_RESULT_OK;
}

KeelResult PluginService::At(
    KeelPluginHandle plugin,
    std::uint32_t index,
    KeelPluginSnapshot* snapshot)
{
    if (!snapshot || snapshot->size != sizeof(KeelPluginSnapshot))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock lock(host_.state_mutex_);
    const PluginRecord* owner = host_.PluginByHandle(plugin);
    if (!host_.accepting_resources_ || !owner || !owner->accepting_resources)
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (index >= host_.plugins_.size())
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    host_.FillPluginSnapshot(*host_.plugins_[index], *snapshot);
    return KEEL_RESULT_OK;
}

KeelResult PluginService::Get(
    KeelPluginHandle plugin,
    KeelPluginHandle target,
    KeelPluginSnapshot* snapshot)
{
    if (!target || !snapshot || snapshot->size != sizeof(KeelPluginSnapshot))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock lock(host_.state_mutex_);
    const PluginRecord* owner = host_.PluginByHandle(plugin);
    if (!host_.accepting_resources_ || !owner || !owner->accepting_resources)
    {
        return KEEL_RESULT_NOT_READY;
    }
    const PluginRecord* target_record = host_.PluginByHandle(target);
    if (!target_record)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    host_.FillPluginSnapshot(*target_record, *snapshot);
    return KEEL_RESULT_OK;
}

KeelResult PluginService::Find(
    KeelPluginHandle plugin,
    const char* name,
    KeelPluginSnapshot* snapshot)
{
    if (!Host::ValidPluginName(name) || !snapshot || snapshot->size != sizeof(KeelPluginSnapshot))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock lock(host_.state_mutex_);
    const PluginRecord* owner = host_.PluginByHandle(plugin);
    if (!host_.accepting_resources_ || !owner || !owner->accepting_resources)
    {
        return KEEL_RESULT_NOT_READY;
    }
    const PluginRecord* match{};
    for (const auto& candidate : host_.plugins_)
    {
        if (candidate->selectable && !candidate->name.empty() &&
            Host::EqualInsensitive(candidate->name, name))
        {
            if (match)
            {
                return KEEL_RESULT_AMBIGUOUS;
            }
            match = candidate.get();
        }
    }
    if (!match)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    host_.FillPluginSnapshot(*match, *snapshot);
    return KEEL_RESULT_OK;
}

KeelResult PluginService::Pause(KeelPluginHandle plugin, KeelPluginHandle target)
{
    std::unique_lock lock(host_.state_mutex_);
    const PluginRecord* owner = host_.PluginByHandle(plugin);
    if (!host_.accepting_resources_ || !owner || !owner->accepting_resources ||
        owner->state != PluginState::loaded || owner->loading)
    {
        return KEEL_RESULT_NOT_READY;
    }
    return host_.PausePlugin(target, lock, false);
}

KeelResult PluginService::Resume(KeelPluginHandle plugin, KeelPluginHandle target)
{
    std::unique_lock lock(host_.state_mutex_);
    const PluginRecord* owner = host_.PluginByHandle(plugin);
    if (!host_.accepting_resources_ || !owner || !owner->accepting_resources ||
        owner->state != PluginState::loaded || owner->loading)
    {
        return KEEL_RESULT_NOT_READY;
    }
    return host_.ResumePlugin(target, lock, false);
}

KeelResult PluginService::Subscribe(
    KeelPluginHandle plugin,
    const KeelPluginSubscriptionSpec* spec,
    KeelPluginSubscriptionHandle* output)
{
    if (!spec || !output)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *output = 0;
    if (spec->size != sizeof(KeelPluginSubscriptionSpec) || !ValidEvent(spec->event) ||
        spec->reserved != 0 || !spec->callback)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock host_lock(host_.state_mutex_);
    PluginRecord* owner = host_.PluginByHandle(plugin);
    if (!host_.accepting_resources_ || !owner || !owner->accepting_resources)
    {
        return KEEL_RESULT_NOT_READY;
    }
    std::scoped_lock registry_lock(registry_mutex_);
    if (shutting_down_ || next_subscription_ == 0 || next_subscription_sequence_ == 0)
    {
        return KEEL_RESULT_NOT_READY;
    }
    auto subscription = std::make_shared<Subscription>();
    subscription->handle = next_subscription_++;
    subscription->owner = plugin;
    subscription->event = spec->event;
    subscription->sequence = next_subscription_sequence_++;
    subscription->callback = spec->callback;
    subscription->user_data = spec->user_data;
    subscription->enabled.store(
        owner->state == PluginState::loaded && !owner->loading,
        std::memory_order_release);
    const KeelPluginSubscriptionHandle handle = subscription->handle;
    subscriptions_.emplace(handle, std::move(subscription));
    *output = handle;
    return KEEL_RESULT_OK;
}

KeelResult PluginService::Unsubscribe(
    KeelPluginHandle plugin,
    KeelPluginSubscriptionHandle handle)
{
    if (!handle)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::shared_ptr<Subscription> subscription;
    {
        std::scoped_lock host_lock(host_.state_mutex_);
        const PluginRecord* owner = host_.PluginByHandle(plugin);
        if (!owner || !owner->accepting_resources)
        {
            return KEEL_RESULT_NOT_READY;
        }
        std::scoped_lock registry_lock(registry_mutex_);
        const auto iterator = subscriptions_.find(handle);
        if (iterator == subscriptions_.end() || iterator->second->owner != plugin)
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        subscription = iterator->second;
        subscription->enabled.store(false, std::memory_order_release);
        subscriptions_.erase(iterator);
    }
    if (!IsCurrentSubscription(subscription.get()))
    {
        WaitForZero(subscription->active);
    }
    return KEEL_RESULT_OK;
}

void PluginService::Dispatch(const KeelPluginEvent& event)
{
    std::vector<std::shared_ptr<Subscription>> matches;
    {
        std::scoped_lock lock(registry_mutex_);
        if (shutting_down_)
        {
            return;
        }
        for (const auto& [handle, subscription] : subscriptions_)
        {
            static_cast<void>(handle);
            if (subscription->event == event.type)
            {
                matches.push_back(subscription);
            }
        }
    }
    std::sort(matches.begin(), matches.end(), [](const auto& left, const auto& right) {
        return left->sequence < right->sequence;
    });
    for (const auto& subscription : matches)
    {
        subscription->active.fetch_add(1, std::memory_order_acq_rel);
        if (!subscription->enabled.load(std::memory_order_acquire))
        {
            LeaveActive(subscription->active);
            continue;
        }
        if (callback_depth_ == callback_stack_.size())
        {
            LeaveActive(subscription->active);
            host_.Write(KEEL_LOG_ERROR, "plugin event callback recursion limit reached");
            continue;
        }
        callback_stack_[callback_depth_++] = subscription.get();
        try
        {
            subscription->callback(&event, subscription->user_data);
        }
        catch (...)
        {
            host_.Write(KEEL_LOG_ERROR, "plugin threw during a plugin event callback");
        }
        callback_stack_[--callback_depth_] = nullptr;
        LeaveActive(subscription->active);
    }
}

bool PluginService::ValidEvent(KeelPluginEventType event) noexcept
{
    return event >= KEELS2_PLUGIN_EVENT_LOADED && event <= KEELS2_PLUGIN_EVENT_ALL_LOADED;
}

bool PluginService::IsCurrentOwner(KeelPluginHandle plugin) noexcept
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

bool PluginService::IsCurrentSubscription(const Subscription* subscription) noexcept
{
    return std::find(
        callback_stack_.begin(),
        callback_stack_.begin() + static_cast<std::ptrdiff_t>(callback_depth_),
        subscription) != callback_stack_.begin() + static_cast<std::ptrdiff_t>(callback_depth_);
}

void PluginService::LeaveActive(std::atomic<std::uint32_t>& active) noexcept
{
    if (active.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        active.notify_all();
    }
}

void PluginService::WaitForZero(std::atomic<std::uint32_t>& active) noexcept
{
    std::uint32_t value = active.load(std::memory_order_acquire);
    while (value != 0)
    {
        active.wait(value, std::memory_order_acquire);
        value = active.load(std::memory_order_acquire);
    }
}

}
