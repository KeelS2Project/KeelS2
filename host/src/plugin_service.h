#ifndef KEELS2_HOST_PLUGIN_SERVICE_H
#define KEELS2_HOST_PLUGIN_SERVICE_H

#include <keels2/plugins.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace keels2::host
{

class Host;
struct PluginRecord;

class PluginService final
{
public:
    explicit PluginService(Host& host);
    ~PluginService();
    PluginService(const PluginService&) = delete;
    PluginService& operator=(const PluginService&) = delete;

    const KeelPluginsApi& Api() const noexcept;
    void Activate(KeelPluginHandle plugin);
    KeelResult Deactivate(KeelPluginHandle plugin);
    KeelResult ReleasePlugin(KeelPluginHandle plugin);
    void Publish(KeelPluginEventType event, const KeelPluginSnapshot& snapshot);
    void PublishAllLoaded();
    bool Shutdown();

private:
    struct Subscription;

    static KeelResult CountEntry(KeelPluginHandle plugin, std::uint32_t* count);
    static KeelResult AtEntry(
        KeelPluginHandle plugin,
        std::uint32_t index,
        KeelPluginSnapshot* snapshot);
    static KeelResult GetEntry(
        KeelPluginHandle plugin,
        KeelPluginHandle target,
        KeelPluginSnapshot* snapshot);
    static KeelResult FindEntry(
        KeelPluginHandle plugin,
        const char* name,
        KeelPluginSnapshot* snapshot);
    static KeelResult PauseEntry(KeelPluginHandle plugin, KeelPluginHandle target);
    static KeelResult ResumeEntry(KeelPluginHandle plugin, KeelPluginHandle target);
    static KeelResult SubscribeEntry(
        KeelPluginHandle plugin,
        const KeelPluginSubscriptionSpec* spec,
        KeelPluginSubscriptionHandle* subscription);
    static KeelResult UnsubscribeEntry(
        KeelPluginHandle plugin,
        KeelPluginSubscriptionHandle subscription);

    KeelResult Count(KeelPluginHandle plugin, std::uint32_t* count);
    KeelResult At(
        KeelPluginHandle plugin,
        std::uint32_t index,
        KeelPluginSnapshot* snapshot);
    KeelResult Get(
        KeelPluginHandle plugin,
        KeelPluginHandle target,
        KeelPluginSnapshot* snapshot);
    KeelResult Find(
        KeelPluginHandle plugin,
        const char* name,
        KeelPluginSnapshot* snapshot);
    KeelResult Pause(KeelPluginHandle plugin, KeelPluginHandle target);
    KeelResult Resume(KeelPluginHandle plugin, KeelPluginHandle target);
    KeelResult Subscribe(
        KeelPluginHandle plugin,
        const KeelPluginSubscriptionSpec* spec,
        KeelPluginSubscriptionHandle* subscription);
    KeelResult Unsubscribe(
        KeelPluginHandle plugin,
        KeelPluginSubscriptionHandle subscription);

    void Dispatch(const KeelPluginEvent& event);
    static bool ValidEvent(KeelPluginEventType event) noexcept;
    static bool IsCurrentOwner(KeelPluginHandle plugin) noexcept;
    static bool IsCurrentSubscription(const Subscription* subscription) noexcept;
    static void LeaveActive(std::atomic<std::uint32_t>& active) noexcept;
    static void WaitForZero(std::atomic<std::uint32_t>& active) noexcept;

    Host& host_;
    KeelPluginsApi api_{};
    std::mutex registry_mutex_;
    std::unordered_map<KeelPluginSubscriptionHandle, std::shared_ptr<Subscription>> subscriptions_;
    KeelPluginSubscriptionHandle next_subscription_{1};
    std::uint64_t next_subscription_sequence_{1};
    std::atomic<std::uint64_t> next_event_sequence_{1};
    bool shutting_down_{};
    bool shutdown_complete_{};

    static std::atomic<PluginService*> active_;
    static thread_local std::array<const Subscription*, 64> callback_stack_;
    static thread_local std::size_t callback_depth_;
};

}

#endif
