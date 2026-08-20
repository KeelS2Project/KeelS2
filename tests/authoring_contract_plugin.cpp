#include <keels2/keels2.hpp>

#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace
{

std::uint32_t g_mode{};
std::uint32_t g_load_count{};
std::uint32_t g_unload_count{};
std::uint32_t g_active_count{};
std::uint32_t g_settings_count{};
std::uint32_t g_self_remove_count{};
std::uint32_t g_throw_command_count{};
bool g_source2_ready{};
bool g_unload_source2_null{};
bool g_active_arguments_valid{};
bool g_settings_arguments_valid{};
bool g_self_remove_succeeded{};

class UnrelatedPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "Unrelated",
        "KeelS2",
        "0.5C",
        "Type-safety fixture"
    };

    void Command(const CCommandContext&, const CCommand&)
    {
    }
};

class AuthoringContractPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "Authoring Contract",
        "KeelS2",
        "0.5C",
        "Validates the ordinary C++ plugin facade"
    };

    AuthoringContractPlugin()
    {
        if (g_mode == 4)
        {
            throw std::runtime_error("authoring constructor test");
        }
    }

    bool Load() override
    {
        ++g_load_count;
        g_source2_ready = GetSource2Server<void>() &&
            GetSource2GameClients<void>() && GetCVarSystem<void>();
        if (CreateCommand(
                "authoring_wrong_owner",
                "Must reject a member callback from another plugin type",
                &UnrelatedPlugin::Command) ||
            !CreateCommand(
                "authoring_self_remove",
                "Removes itself while its callback is active",
                &AuthoringContractPlugin::SelfRemove) ||
            !CreateCommand(
                "authoring_throw",
                "Throws inside the command adapter boundary",
                &AuthoringContractPlugin::ThrowCommand,
                std::uint64_t{0x2000}))
        {
            return false;
        }
        if (g_mode == 1)
        {
            return false;
        }
        if (g_mode == 2)
        {
            throw std::runtime_error("authoring load test");
        }
        return true;
    }

    void Unload() override
    {
        ++g_unload_count;
        g_unload_source2_null = !GetSource2Server<void>() &&
            !GetSource2GameClients<void>() && !GetCVarSystem<void>();
        if (g_mode == 5)
        {
            throw std::runtime_error("authoring unload test");
        }
    }

    void OnClientActive(
        CPlayerSlot slot,
        bool load_game,
        const char* name,
        uint64 xuid) override
    {
        ++g_active_count;
        g_active_arguments_valid = slot.Get() == 4 && !load_game && name &&
            std::strcmp(name, "Keel") == 0 && xuid == 76561198000000004ull;
        if (name && std::strcmp(name, "throw") == 0)
        {
            throw std::runtime_error("authoring lifecycle test");
        }
    }

    void OnClientSettingsChanged(CPlayerSlot slot) override
    {
        ++g_settings_count;
        g_settings_arguments_valid = slot.Get() == 4;
    }

private:
    void SelfRemove(const CCommandContext&, const CCommand&)
    {
        ++g_self_remove_count;
        g_self_remove_succeeded = RemoveCommand("authoring_self_remove");
    }

    void ThrowCommand(const CCommandContext&, const CCommand&)
    {
        ++g_throw_command_count;
        throw std::runtime_error("authoring command test");
    }
};

}

KEELS2_PLUGIN(AuthoringContractPlugin)

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_AuthoringReset()
{
    g_mode = 0;
    g_load_count = 0;
    g_unload_count = 0;
    g_active_count = 0;
    g_settings_count = 0;
    g_self_remove_count = 0;
    g_throw_command_count = 0;
    g_source2_ready = false;
    g_unload_source2_null = false;
    g_active_arguments_valid = false;
    g_settings_arguments_valid = false;
    g_self_remove_succeeded = false;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_AuthoringSetMode(std::uint32_t mode)
{
    g_mode = mode;
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_AuthoringLoadCount()
{
    return g_load_count;
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_AuthoringUnloadCount()
{
    return g_unload_count;
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_AuthoringActiveCount()
{
    return g_active_count;
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_AuthoringSettingsCount()
{
    return g_settings_count;
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_AuthoringSelfRemoveCount()
{
    return g_self_remove_count;
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_AuthoringThrowCommandCount()
{
    return g_throw_command_count;
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelTest_AuthoringSource2Ready()
{
    return g_source2_ready ? KEEL_TRUE : KEEL_FALSE;
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelTest_AuthoringUnloadSource2Null()
{
    return g_unload_source2_null ? KEEL_TRUE : KEEL_FALSE;
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelTest_AuthoringActiveArgumentsValid()
{
    return g_active_arguments_valid ? KEEL_TRUE : KEEL_FALSE;
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelTest_AuthoringSettingsArgumentsValid()
{
    return g_settings_arguments_valid ? KEEL_TRUE : KEEL_FALSE;
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelTest_AuthoringSelfRemoveSucceeded()
{
    return g_self_remove_succeeded ? KEEL_TRUE : KEEL_FALSE;
}

extern "C" KEELS2_PLUGIN_EXPORT void* KeelTest_AuthoringCreateCommand(const char* name)
{
    if (!name)
    {
        return nullptr;
    }
    const char* arguments[]{name};
    try
    {
        return new CCommand(1, arguments);
    }
    catch (...)
    {
        return nullptr;
    }
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_AuthoringDestroyCommand(void* command)
{
    delete static_cast<CCommand*>(command);
}

extern "C" KEELS2_PLUGIN_EXPORT void* KeelTest_AuthoringCreateCommandContext()
{
    try
    {
        return new CCommandContext(CT_NO_TARGET, CPlayerSlot{-1});
    }
    catch (...)
    {
        return nullptr;
    }
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_AuthoringDestroyCommandContext(void* context)
{
    delete static_cast<CCommandContext*>(context);
}
