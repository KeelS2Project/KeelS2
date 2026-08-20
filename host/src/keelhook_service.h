#ifndef KEELS2_HOST_KEELHOOK_SERVICE_H
#define KEELS2_HOST_KEELHOOK_SERVICE_H

#include <keels2/keelhook.h>

#include <filesystem>
#include <memory>
#include <string>

namespace keels2::host
{

class Host;

class KeelHookService final
{
public:
    explicit KeelHookService(Host& host);
    ~KeelHookService();
    KeelHookService(const KeelHookService&) = delete;
    KeelHookService& operator=(const KeelHookService&) = delete;

    const KeelHookApi& Api() const noexcept;
    void Authorize(KeelPluginHandle plugin, const std::filesystem::path& path, bool active);
    void Activate(KeelPluginHandle plugin);
    KeelResult Deactivate(KeelPluginHandle plugin);
    KeelResult ReleasePlugin(KeelPluginHandle plugin);
    bool Shutdown();

private:
    void Log(KeelLogLevel level, const std::string& message);

    class Implementation;
    Host& host_;
    std::unique_ptr<Implementation> implementation_;
};

}

#endif
