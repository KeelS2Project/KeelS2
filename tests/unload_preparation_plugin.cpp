#include <keels2/authoring.hpp>
#include <keels2/convar.h>

#include <cstring>
#include <cstdlib>
#include <stdexcept>

namespace
{
using namespace keels2::authoring;
unsigned mode{};
unsigned unloads{};
unsigned frames{};
unsigned pause_mode{1};
unsigned pauses{};

class CleanupPlugin final : public Plugin
{
public:
    static constexpr PluginInfo Info{
        "Unload Preparation", "KeelS2 Tests", "1.0.0", "Tests recoverable plugin cleanup"};

    bool Load() override
    {
        if (const char* replacement = std::getenv("KEELS2_TEST_PAUSED_REPLACEMENT"))
        {
            pause_mode = std::strcmp(replacement, "throw") == 0 ? 2u : 0u;
            mode = std::strcmp(replacement, "retain") == 0 ? 0u : 1u;
#if defined(_WIN32)
            if (_putenv_s("KEELS2_TEST_PAUSED_REPLACEMENT", "") != 0)
                return false;
#else
            if (unsetenv("KEELS2_TEST_PAUSED_REPLACEMENT") != 0)
                return false;
#endif
        }

        const void* service{};

        if (!HostContext().PluginHandle() ||
            HostContext().QueryService("keels2.unload", 1, &service) != KEEL_RESULT_OK || !service)
            return false;

        if (HostContext().QueryService(KEELS2_CONVAR_SERVICE_NAME, KEELS2_CONVAR_API_VERSION, &service) != KEEL_RESULT_OK)
            return false;

        convars_ = static_cast<const KeelConVarApi*>(service);

        if (HostContext().QueryService(KEELS2_PAUSE_SERVICE_NAME, KEELS2_PAUSE_API_VERSION, &service) != KEEL_RESULT_OK)
            return false;

        pause_ = static_cast<const KeelPauseApi*>(service);
        const void* incompatible{};

        if (HostContext().QueryService(KEELS2_PAUSE_SERVICE_NAME, 999, &incompatible) != KEEL_RESULT_INCOMPATIBLE ||
            incompatible)
            return false;

        const auto spec = Definition();

        if (!convars_ || convars_->create(HostContext().PluginHandle(), &spec, &variable_) != KEEL_RESULT_OK)
            return false;

        return CheckGameThread() == KEEL_RESULT_OK &&
               CreateCommand("cleanup_test", "Controls cleanup fixture", &CleanupPlugin::Command);
    }

    bool PreparePause() override
    {
        ++pauses;

        if (CheckGameThread() != KEEL_RESULT_OK)
            return false;

        if (pause_mode == 2)
            throw std::runtime_error("pause fixture failure");

        if (pause_mode != 1)
            return false;

        const auto owner = HostContext().PluginHandle();

        if (pause_->set_prepare_callback(owner, nullptr, nullptr) != KEEL_RESULT_BUSY)
            return false;

        auto spec = Definition();
        spec.name = "keels2_pause_new";
        KeelConVarHandle rejected{};

        if (convars_->create(owner, &spec, &rejected) != KEEL_RESULT_NOT_READY || rejected)
            return false;

        if (variable_ && convars_->release(owner, variable_) != KEEL_RESULT_OK)
            return false;

        variable_ = 0;
        LogMessage("pause released its ConVar; new registration remained blocked");
        return true;
    }

    void OnPluginResumed(const PluginSnapshot& plugin) override
    {
        if (plugin.id != HostContext().PluginHandle())
            return;

        const auto spec = Definition();

        if (convars_->create(HostContext().PluginHandle(), &spec, &variable_) != KEEL_RESULT_OK)
            LogError("resume failed to reacquire its ConVar");
    }

    bool PrepareUnload() override
    {
        if (CheckGameThread() != KEEL_RESULT_OK)
        {
            LogError("cleanup lost native access");
            return false;
        }

        if (mode == 2)
        {
            throw std::runtime_error("cleanup fixture failure");
        }

        if (mode != 1)
            return false;

        const auto owner = HostContext().PluginHandle();
        auto spec = Definition();
        spec.name = "keels2_cleanup_new";
        KeelConVarHandle rejected{};

        if (convars_->create(owner, &spec, &rejected) != KEEL_RESULT_NOT_READY || rejected)
        {
            LogError("cleanup acquired a new ConVar");
            return false;
        }

        if (variable_ && convars_->release(owner, variable_) != KEEL_RESULT_OK)
        {
            LogError("cleanup could not release its ConVar");
            return false;
        }

        variable_ = 0;
        LogMessage("cleanup released its ConVar; new registration remained blocked");
        return true;
    }

    void Unload() override
    {
        ++unloads;
        LogMessage("cleanup fixture unloaded");
    }

    void OnGameFrame(bool, bool, bool) override
    {
        ++frames;
    }

private:
    const KeelConVarApi* convars_{};
    const KeelPauseApi* pause_{};
    KeelConVarHandle variable_{};

    static KeelConVarSpec Definition()
    {
        KeelConVarSpec spec{};
        spec.size = sizeof(spec);
        spec.type = KEELS2_CONVAR_INT32;
        spec.name = "keels2_cleanup_owned";
        spec.description = "Unload preparation resource fixture";
        spec.default_value = {sizeof(KeelConVarValue), KEELS2_CONVAR_INT32, {}};
        return spec;
    }

    void Command(const CCommandContext&, const CCommand& command)
    {
        if (command.ArgC() == 2)
        {
            mode = std::strcmp(command[1], "allow") == 0 ? 1u : 2u;
        }
    }
};
}

KEELS2_PLUGIN(CleanupPlugin)

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_CleanupMode(unsigned value)
{
    mode = value;
}

extern "C" KEELS2_PLUGIN_EXPORT unsigned KeelTest_CleanupUnloads()
{
    return unloads;
}

extern "C" KEELS2_PLUGIN_EXPORT unsigned KeelTest_CleanupFrames()
{
    return frames;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_PauseMode(unsigned value)
{
    pause_mode = value;
}

extern "C" KEELS2_PLUGIN_EXPORT unsigned KeelTest_PauseCalls()
{
    return pauses;
}
