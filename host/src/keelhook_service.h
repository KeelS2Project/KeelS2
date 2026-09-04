#ifndef KEELS2_HOST_KEELHOOK_SERVICE_H
#define KEELS2_HOST_KEELHOOK_SERVICE_H

#include <keels2/keelhook.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace keels2::host
{

class Host;

class KeelHookService final
{
public:
    struct CallbackSnapshot
    {
        KeelHookCallbackHandle handle{};
        KeelPluginHandle owner{};
        std::uint32_t phases{};
        std::int32_t priority{};
        bool enabled{};
        std::uint32_t active{};
    };

    struct TargetSnapshot
    {
        KeelHookTargetHandle handle{};
        KeelHookMechanism mechanism{};
        std::uintptr_t address{};
        std::uintptr_t closure{};
        std::uintptr_t virtual_slot{};
        std::uintptr_t installed_address{};
        std::string module_path;
        std::size_t leases{};
        std::size_t bindings{};
        bool physical_enabled{};
        bool physical_intact{};
        std::uint32_t active{};
        std::vector<CallbackSnapshot> callbacks;
    };

    explicit KeelHookService(Host& host);
    ~KeelHookService();
    KeelHookService(const KeelHookService&) = delete;
    KeelHookService& operator=(const KeelHookService&) = delete;

    const KeelHookApi& Api() const noexcept;
    const KeelHookApiV4& ApiV4() const noexcept;
    const KeelHookApiV3& ApiV3() const noexcept;
    void Authorize(KeelPluginHandle plugin, const std::filesystem::path& path, bool active);
    void Activate(KeelPluginHandle plugin);
    KeelResult Deactivate(KeelPluginHandle plugin);
    KeelResult ReleasePlugin(KeelPluginHandle plugin);
    std::vector<TargetSnapshot> Snapshots() const;
    bool Shutdown();

private:
    void Log(KeelLogLevel level, const std::string& message);

    class Implementation;
    Host& host_;
    std::unique_ptr<Implementation> implementation_;
};

}

#endif
