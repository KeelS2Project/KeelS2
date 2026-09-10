#ifndef KEELS2_HOST_FACTORY_SERVICE_H
#define KEELS2_HOST_FACTORY_SERVICE_H

#include <keels2/bootstrap_api.h>
#include <keels2/factories.h>
#include <keels2/keelhook.h>

#include <array>
#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace keels2::host
{

class Host;
class KeelHookService;

class FactoryService final
{
public:
    FactoryService(Host& host, KeelHookService& hooks,
        KeelCreateInterfaceFn engine, KeelCreateInterfaceFn server);
    ~FactoryService();
    bool Initialize();
    const KeelFactoriesApi& Api() const noexcept;
    void Activate(KeelPluginHandle plugin);
    KeelResult Deactivate(KeelPluginHandle plugin);
    KeelResult ReleasePlugin(KeelPluginHandle plugin);
    bool Shutdown();
    bool Pinned(KeelPluginHandle plugin) const;
    std::vector<std::string> Diagnostics() const;
    KeelResult QueryNamed(KeelPluginHandle plugin, KeelSource2Factory factory,
        const char* name, KeelSource2InterfaceInfo* info);

private:
    struct Subscription
    {
        KeelFactorySubscriptionHandle handle{};
        KeelPluginHandle owner{};
        KeelSource2Factory factory{};
        std::string name;
        std::int32_t priority{};
        std::uint32_t flags{};
        KeelFactoryCallback callback{};
        void* user_data{};
        std::atomic<bool> enabled{};
        std::atomic<std::uint32_t> active{};
    };
    struct Boundary
    {
        FactoryService* service{};
        KeelSource2Factory factory{};
        KeelCreateInterfaceFn function{};
        KeelHookTargetHandle target{};
        KeelHookCallbackHandle callback{};
    };
    struct Description
    {
        std::string name;
        std::string module;
        std::string path;
    };
    struct DispatchScope
    {
        Boundary* boundary{};
        const char* name{};
    };

    static KeelResult SubscribeEntry(KeelPluginHandle plugin,
        const KeelFactorySubscriptionSpec* spec, KeelFactorySubscriptionHandle* output);
    static KeelResult UnsubscribeEntry(KeelPluginHandle plugin,
        KeelFactorySubscriptionHandle subscription);
    static KeelResult OriginalEntry(KeelPluginHandle plugin,
        KeelSource2Factory factory, const char* name, KeelFactoryResult* result);
    static KeelHookAction DispatchEntry(KeelHookFrame* frame, void* data);
    KeelHookAction Dispatch(Boundary& boundary, KeelHookFrame& frame);
    KeelResult Subscribe(KeelPluginHandle plugin,
        const KeelFactorySubscriptionSpec* spec, KeelFactorySubscriptionHandle* output);
    KeelResult Unsubscribe(KeelPluginHandle plugin, KeelFactorySubscriptionHandle handle);
    KeelResult Query(KeelPluginHandle plugin, KeelSource2Factory factory,
        const char* name, KeelFactoryResult& result, bool original);
    Boundary* FindBoundary(KeelSource2Factory factory) noexcept;
    static bool ValidName(const char* name) noexcept;
    static void Wait(std::atomic<std::uint32_t>& count) noexcept;
    static void Leave(std::atomic<std::uint32_t>& count) noexcept;

    Host& host_;
    KeelHookService& hooks_;
    KeelFactoriesApi api_{};
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<Boundary>> boundaries_;
    std::unordered_map<KeelFactorySubscriptionHandle, std::shared_ptr<Subscription>> subscriptions_;
    std::unordered_set<KeelPluginHandle> pinned_;
    std::list<Description> descriptions_;
    KeelFactorySubscriptionHandle next_{1};
    std::atomic<std::uint32_t> queries_{};
    bool stopping_{};
    static std::atomic<FactoryService*> active_;
    static thread_local std::array<DispatchScope, 64> stack_;
    static thread_local std::size_t depth_;
    static thread_local std::uint32_t bypass_;
};

}

#endif
