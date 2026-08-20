#ifndef KEELS2_HOST_SOURCE2_CALLBACKS_SERVICE_H
#define KEELS2_HOST_SOURCE2_CALLBACKS_SERVICE_H

#include "game_adapter.h"

#include <keels2/source2_callbacks.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace keels2::host
{

class Host;
class KeelHookService;

class Source2CallbacksService final
{
public:
    Source2CallbacksService(Host& host, GameAdapter& adapter, KeelHookService& hooks);
    ~Source2CallbacksService();
    Source2CallbacksService(const Source2CallbacksService&) = delete;
    Source2CallbacksService& operator=(const Source2CallbacksService&) = delete;

    const KeelSource2CallbacksApi& Api() const noexcept;
    void Activate(KeelPluginHandle plugin);
    KeelResult Deactivate(KeelPluginHandle plugin);
    KeelResult ReleasePlugin(KeelPluginHandle plugin);
    bool Shutdown();

private:
    struct Subscription;

    static KeelResult SubscribeEntry(
        KeelPluginHandle plugin,
        const KeelSource2SubscriptionSpec* spec,
        KeelSource2SubscriptionHandle* subscription);
    static KeelResult UnsubscribeEntry(
        KeelPluginHandle plugin,
        KeelSource2SubscriptionHandle subscription);
    static KeelBool DispatchEntry(KeelSource2CallbackEvent& event, void* user_data);

    KeelResult Subscribe(
        KeelPluginHandle plugin,
        const KeelSource2SubscriptionSpec* spec,
        KeelSource2SubscriptionHandle* subscription);
    KeelResult Unsubscribe(
        KeelPluginHandle plugin,
        KeelSource2SubscriptionHandle subscription);
    KeelBool Dispatch(KeelSource2CallbackEvent& event);

    static bool ValidType(KeelSource2CallbackType type) noexcept;
    static bool ValidEventName(const char* name, std::string& output) noexcept;
    static bool ValidEnvelope(const KeelSource2CallbackEvent& event) noexcept;
    static bool IsCurrentOwner(KeelPluginHandle plugin) noexcept;
    static bool IsCurrentCallback(const Subscription* subscription) noexcept;
    static void LeaveActive(std::atomic<std::uint32_t>& active) noexcept;
    static void WaitForZero(std::atomic<std::uint32_t>& active) noexcept;

    Host& host_;
    GameAdapter& adapter_;
    KeelHookService& hooks_;
    KeelSource2CallbacksApi api_{};
    std::mutex registry_mutex_;
    std::unordered_map<KeelSource2SubscriptionHandle, std::shared_ptr<Subscription>> subscriptions_;
    std::unordered_set<std::string> listened_events_;
    KeelSource2SubscriptionHandle next_subscription_{1};
    std::uint64_t next_sequence_{1};
    bool shutting_down_{};
    bool shutdown_complete_{};

    static std::atomic<Source2CallbacksService*> active_;
    static thread_local std::array<const Subscription*, 64> callback_stack_;
    static thread_local std::size_t callback_depth_;
};

}

#endif
