#ifndef KEELS2_HOST_LIFECYCLE_SERVICE_H
#define KEELS2_HOST_LIFECYCLE_SERVICE_H

#include "game_adapter.h"

#include <keels2/lifecycle.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace keels2::host
{

class Host;
class KeelHookService;

class LifecycleService final
{
public:
    LifecycleService(Host& host, GameAdapter& adapter, KeelHookService& hooks);
    ~LifecycleService();
    LifecycleService(const LifecycleService&) = delete;
    LifecycleService& operator=(const LifecycleService&) = delete;

    const KeelLifecycleApi& Api() const noexcept;
    void Activate(KeelPluginHandle plugin);
    KeelResult Deactivate(KeelPluginHandle plugin);
    KeelResult ReleasePlugin(KeelPluginHandle plugin);
    bool Shutdown();

private:
    struct Subscription;

    static KeelResult SubscribeEntry(
        KeelPluginHandle plugin,
        const KeelLifecycleSubscriptionSpec* spec,
        KeelLifecycleSubscriptionHandle* subscription);
    static KeelResult UnsubscribeEntry(
        KeelPluginHandle plugin,
        KeelLifecycleSubscriptionHandle subscription);
    static void DispatchEntry(const KeelLifecycleEvent& event, void* user_data);

    KeelResult Subscribe(
        KeelPluginHandle plugin,
        const KeelLifecycleSubscriptionSpec* spec,
        KeelLifecycleSubscriptionHandle* subscription);
    KeelResult Unsubscribe(
        KeelPluginHandle plugin,
        KeelLifecycleSubscriptionHandle subscription);
    void Dispatch(const KeelLifecycleEvent& event);

    static bool ValidEvent(KeelLifecycleEventType event) noexcept;
    static bool IsCurrentOwner(KeelPluginHandle plugin) noexcept;
    static bool IsCurrentCallback(const Subscription* subscription) noexcept;
    static void LeaveActive(std::atomic<std::uint32_t>& active) noexcept;
    static void WaitForZero(std::atomic<std::uint32_t>& active) noexcept;

    Host& host_;
    GameAdapter& adapter_;
    KeelHookService& hooks_;
    KeelLifecycleApi api_{};
    std::mutex registry_mutex_;
    std::unordered_map<KeelLifecycleSubscriptionHandle, std::shared_ptr<Subscription>> subscriptions_;
    std::array<bool, KEELS2_LIFECYCLE_CLIENT_SETTINGS_CHANGED + 1> installed_{};
    KeelLifecycleSubscriptionHandle next_subscription_{1};
    std::uint64_t next_sequence_{1};
    bool shutting_down_{};
    bool shutdown_complete_{};

    static std::atomic<LifecycleService*> active_;
    static thread_local std::array<const Subscription*, 64> callback_stack_;
    static thread_local std::size_t callback_depth_;
};

}

#endif
