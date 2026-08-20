#include "lifecycle_service.h"

#include "host.h"
#include "keelhook_service.h"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace keels2::host
{

struct LifecycleService::Subscription
{
    KeelLifecycleSubscriptionHandle handle{};
    KeelPluginHandle owner{};
    KeelLifecycleEventType event{};
    KeelLifecycleCallback callback{};
    void* user_data{};
    std::uint64_t sequence{};
    std::atomic<bool> enabled{};
    std::atomic<std::uint32_t> active{};
};

std::atomic<LifecycleService*> LifecycleService::active_{};
thread_local std::array<const LifecycleService::Subscription*, 64> LifecycleService::callback_stack_{};
thread_local std::size_t LifecycleService::callback_depth_{};

LifecycleService::LifecycleService(Host& host, GameAdapter& adapter, KeelHookService& hooks)
    : host_(host), adapter_(adapter), hooks_(hooks)
{
    LifecycleService* expected{};
    if (!active_.compare_exchange_strong(expected, this, std::memory_order_acq_rel))
    {
        throw std::runtime_error("lifecycle service already exists");
    }
    api_ = {
        sizeof(KeelLifecycleApi),
        KEELS2_LIFECYCLE_API_VERSION,
        &SubscribeEntry,
        &UnsubscribeEntry
    };
}

LifecycleService::~LifecycleService()
{
    active_.store(nullptr, std::memory_order_release);
    static_cast<void>(Shutdown());
}

const KeelLifecycleApi& LifecycleService::Api() const noexcept
{
    return api_;
}

void LifecycleService::Activate(KeelPluginHandle plugin)
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

KeelResult LifecycleService::Deactivate(KeelPluginHandle plugin)
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

KeelResult LifecycleService::ReleasePlugin(KeelPluginHandle plugin)
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

bool LifecycleService::Shutdown()
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
    std::scoped_lock lock(registry_mutex_);
    subscriptions_.clear();
    shutdown_complete_ = true;
    return true;
}

KeelResult LifecycleService::SubscribeEntry(
    KeelPluginHandle plugin,
    const KeelLifecycleSubscriptionSpec* spec,
    KeelLifecycleSubscriptionHandle* subscription)
{
    LifecycleService* service = active_.load(std::memory_order_acquire);
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
        service->host_.Write(KEEL_LOG_ERROR, "exception while subscribing to a lifecycle event");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult LifecycleService::UnsubscribeEntry(
    KeelPluginHandle plugin,
    KeelLifecycleSubscriptionHandle subscription)
{
    LifecycleService* service = active_.load(std::memory_order_acquire);
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
        service->host_.Write(KEEL_LOG_ERROR, "exception while unsubscribing from a lifecycle event");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

void LifecycleService::DispatchEntry(const KeelLifecycleEvent& event, void* user_data)
{
    auto* service = static_cast<LifecycleService*>(user_data);
    if (service && active_.load(std::memory_order_acquire) == service)
    {
        service->Dispatch(event);
    }
}

KeelResult LifecycleService::Subscribe(
    KeelPluginHandle plugin,
    const KeelLifecycleSubscriptionSpec* spec,
    KeelLifecycleSubscriptionHandle* output)
{
    if (!spec || spec->size != sizeof(KeelLifecycleSubscriptionSpec) || !output ||
        !ValidEvent(spec->event) || spec->reserved != 0 || !spec->callback)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *output = 0;
    {
        std::scoped_lock lock(registry_mutex_);
        if (shutting_down_)
        {
            return KEEL_RESULT_NOT_READY;
        }
        if (!installed_[spec->event])
        {
            std::string error;
            const KeelResult result = adapter_.EnableLifecycleEvent(
                spec->event,
                hooks_.Api(),
                0,
                &DispatchEntry,
                this,
                error);
            if (result != KEEL_RESULT_OK)
            {
                if (!error.empty())
                {
                    host_.Write(KEEL_LOG_ERROR, error);
                }
                return result;
            }
            installed_[spec->event] = true;
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
    subscription->event = spec->event;
    subscription->callback = spec->callback;
    subscription->user_data = spec->user_data;
    subscription->sequence = next_sequence_++;
    subscription->enabled.store(
        owner->state == PluginState::loaded && !owner->loading,
        std::memory_order_release);
    const auto handle = subscription->handle;
    subscriptions_.emplace(handle, std::move(subscription));
    *output = handle;
    return KEEL_RESULT_OK;
}

KeelResult LifecycleService::Unsubscribe(
    KeelPluginHandle plugin,
    KeelLifecycleSubscriptionHandle handle)
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

void LifecycleService::Dispatch(const KeelLifecycleEvent& event)
{
    if (event.size != sizeof(KeelLifecycleEvent) || !ValidEvent(event.type) ||
        event.reserved != 0 || !event.payload || event.payload_size == 0)
    {
        return;
    }
    std::vector<std::shared_ptr<Subscription>> callbacks;
    {
        std::scoped_lock lock(registry_mutex_);
        if (shutting_down_)
        {
            return;
        }
        for (const auto& [handle, subscription] : subscriptions_)
        {
            static_cast<void>(handle);
            if (subscription->event == event.type &&
                subscription->enabled.load(std::memory_order_acquire))
            {
                callbacks.push_back(subscription);
            }
        }
    }
    std::sort(callbacks.begin(), callbacks.end(), [](const auto& left, const auto& right) {
        return left->sequence < right->sequence;
    });
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
            host_.Write(KEEL_LOG_ERROR, "lifecycle callback recursion limit reached");
            continue;
        }
        callback_stack_[callback_depth_++] = callback.get();
        try
        {
            callback->callback(&event, callback->user_data);
        }
        catch (...)
        {
            host_.Write(KEEL_LOG_ERROR, "plugin threw during a lifecycle callback");
        }
        callback_stack_[--callback_depth_] = nullptr;
        LeaveActive(callback->active);
    }
}

bool LifecycleService::ValidEvent(KeelLifecycleEventType event) noexcept
{
    return event >= KEELS2_LIFECYCLE_GAME_FRAME &&
        event <= KEELS2_LIFECYCLE_CLIENT_SETTINGS_CHANGED;
}

bool LifecycleService::IsCurrentOwner(KeelPluginHandle plugin) noexcept
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

bool LifecycleService::IsCurrentCallback(const Subscription* subscription) noexcept
{
    return std::find(
        callback_stack_.begin(),
        callback_stack_.begin() + static_cast<std::ptrdiff_t>(callback_depth_),
        subscription) != callback_stack_.begin() + static_cast<std::ptrdiff_t>(callback_depth_);
}

void LifecycleService::LeaveActive(std::atomic<std::uint32_t>& active) noexcept
{
    if (active.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        active.notify_all();
    }
}

void LifecycleService::WaitForZero(std::atomic<std::uint32_t>& active) noexcept
{
    std::uint32_t value = active.load(std::memory_order_acquire);
    while (value != 0)
    {
        active.wait(value, std::memory_order_acquire);
        value = active.load(std::memory_order_acquire);
    }
}

}
