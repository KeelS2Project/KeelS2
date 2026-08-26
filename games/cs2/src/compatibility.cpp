#include <keels2/cs2/compatibility.h>
#include <keels2/cs2/cvar_abi.h>

#include <array>
#include <cstring>
#include <limits>

namespace keels2::cs2
{

namespace
{

static_assert(sizeof(CommandCreation) <= std::numeric_limits<std::uint32_t>::max());
static_assert(sizeof(CommandCallbackInfo) <= std::numeric_limits<std::uint32_t>::max());
static_assert(sizeof(CompletionCallbackInfo) <= std::numeric_limits<std::uint32_t>::max());
static_assert(sizeof(CommandRef) <= std::numeric_limits<std::uint32_t>::max());
static_assert(sizeof(ConVarValue) <= std::numeric_limits<std::uint32_t>::max());
static_assert(sizeof(ConVarValueInfo) <= std::numeric_limits<std::uint32_t>::max());
static_assert(sizeof(ConVarCreation) <= std::numeric_limits<std::uint32_t>::max());
static_assert(sizeof(ConVarRef) <= std::numeric_limits<std::uint32_t>::max());
static_assert(sizeof(ConVarData) <= std::numeric_limits<std::uint32_t>::max());
static_assert(sizeof(ConVarObject) <= std::numeric_limits<std::uint32_t>::max());

constexpr CompatibilityProfile Profile(
    const char* id,
    const char* game_version,
    const char* platform_name,
    platform::FileFingerprint server,
    const char* server_module,
    const char* cvar_module,
    const char* engine_module,
    bool schema_entities)
{
    const char* schema_module = schema_entities
        ? (platform_name[0] == 'w' ? "schemasystem.dll" : "libschemasystem.so")
        : nullptr;
    return {
        id,
        game_version,
        platform_name,
        server,
        "Source2ServerConfig001",
        0,
        1,
        "Source2Server001",
        server_module,
        3,
        "Source2GameClients001",
        0,
        19,
        11,
        13,
        14,
        15,
        16,
        19,
        kCvarInterfaceVersion,
        cvar_module,
        kRegisterConCommandSlot,
        kUnregisterConCommandSlot,
        static_cast<std::uint32_t>(sizeof(CommandCreation)),
        static_cast<std::uint32_t>(sizeof(CommandCallbackInfo)),
        static_cast<std::uint32_t>(sizeof(CompletionCallbackInfo)),
        static_cast<std::uint32_t>(sizeof(CommandRef)),
        kCommandSize,
        kCommandArgumentCountOffset,
        kCommandArgumentValuesOffset,
        kFindConVarSlot,
        kRegisterConVarSlot,
        kUnregisterConVarSlot,
        kGetConVarDataSlot,
        kCallChangeCallbackSlot,
        kCallFilterCallbackSlot,
        kCallGlobalChangeCallbacksSlot,
        kQueueThreadSetValueSlot,
        static_cast<std::uint32_t>(sizeof(ConVarValue)),
        static_cast<std::uint32_t>(sizeof(ConVarValueInfo)),
        static_cast<std::uint32_t>(sizeof(ConVarCreation)),
        static_cast<std::uint32_t>(sizeof(ConVarRef)),
        static_cast<std::uint32_t>(sizeof(ConVarData)),
        static_cast<std::uint32_t>(sizeof(ConVarObject)),
        kConVarDataTypeOffset,
        kConVarDataFlagsOffset,
        kConVarDataValueOffset,
        kConVarValueInfoChangeProviderOffset,
        kConVarValueInfoCustomDataOffset,
        kConVarDataCustomDataOffset,
        kConVarObjectDataOffset,
        static_cast<std::int32_t>(ConVarType::boolean),
        static_cast<std::int32_t>(ConVarType::int32),
        static_cast<std::int32_t>(ConVarType::float32),
        static_cast<std::int32_t>(ConVarType::string),
        "EngineServiceMgr001",
        engine_module,
        "CGameEventManager",
        server_module,
        13,
        14,
        2,
        3,
        0,
        1,
        platform_name[0] == 'w' ? 1u : 2u,
        platform_name[0] == 'w' ? 3u : 4u,
        12,
        17,
        schema_entities ? "SchemaSystem_001" : nullptr,
        schema_module,
        schema_entities ? server_module : nullptr,
        0,
        schema_entities ? "GameResourceServiceServerV001" : nullptr,
        schema_entities ? engine_module : nullptr,
        schema_entities ? server_module : nullptr,
        0,
        schema_entities ? (platform_name[0] == 'w' ? 88u : 80u) : 0u
    };
}

constexpr std::array profiles{
    Profile(
        "cs2-2000880-linuxsteamrt64-17a2d48e2444bf4f8ecf6a126a36e8753dcfcb81",
        "2000880",
        "linuxsteamrt64",
        {40344184, 0xd9145056b00162faull},
        "libserver.so",
        "libtier0.so",
        "libengine2.so",
        false),
    Profile(
        "cs2-2000884-linuxsteamrt64-60a107b12af1a8d752ec462200852a2e7470913d",
        "2000884",
        "linuxsteamrt64",
        {40352056, 0x023a563a82a10f52ull},
        "libserver.so",
        "libtier0.so",
        "libengine2.so",
        false),
    Profile(
        "cs2-2000885-linuxsteamrt64-d05aa2d65efa96e06e3ded6dd2a95b5220a993a8",
        "2000885",
        "linuxsteamrt64",
        {40353400, 0x3aa4e49b8b45ac19ull},
        "libserver.so",
        "libtier0.so",
        "libengine2.so",
        true),
    Profile(
        "cs2-2000888-linuxsteamrt64-40541272-6311ef1c33faf243",
        "2000888",
        "linuxsteamrt64",
        {40541272, 0x6311ef1c33faf243ull},
        "libserver.so",
        "libtier0.so",
        "libengine2.so",
        true),
    Profile(
        "cs2-2000879-win64-2369e67d8d0e4475a49dc6f4e8c99d28-51",
        "2000879",
        "win64",
        {32794264, 0x63eca0729c4fd8a9ull},
        "server.dll",
        "tier0.dll",
        "engine2.dll",
        false),
    Profile(
        "cs2-2000880-win64-2369e67d8d0e4475a49dc6f4e8c99d28-52",
        "2000880",
        "win64",
        {32818840, 0xda8eb43f77d5c62full},
        "server.dll",
        "tier0.dll",
        "engine2.dll",
        false),
    Profile(
        "cs2-2000884-win64-2369e67d8d0e4475a49dc6f4e8c99d28-54",
        "2000884",
        "win64",
        {32824984, 0x286e997327894e53ull},
        "server.dll",
        "tier0.dll",
        "engine2.dll",
        false),
    Profile(
        "cs2-2000885-win64-2369e67d8d0e4475a49dc6f4e8c99d28-55",
        "2000885",
        "win64",
        {32826008, 0xb3f810b3507341c6ull},
        "server.dll",
        "tier0.dll",
        "engine2.dll",
        true),
    Profile(
        "cs2-2000888-win64-33003672-02e0bdaaa3f43453",
        "2000888",
        "win64",
        {33003672, 0x02e0bdaaa3f43453ull},
        "server.dll",
        "tier0.dll",
        "engine2.dll",
        true)
};

}

const CompatibilityProfile* FindCompatibilityProfile(
    const platform::FileFingerprint& server,
    const char* platform_name)
{
    if (!platform_name)
    {
        return nullptr;
    }
    for (const auto& profile : profiles)
    {
        if (profile.server == server && std::strcmp(profile.platform, platform_name) == 0)
        {
            return &profile;
        }
    }
    return nullptr;
}

const CompatibilityProfile& FixtureCompatibilityProfile(const char* platform_name)
{
    static const CompatibilityProfile linux_profile = [] {
        CompatibilityProfile profile = Profile(
            "test-fixture-linuxsteamrt64",
            "test",
            "linuxsteamrt64",
            {},
            "libserver.so",
            "keels2_bootstrap_integration",
            "keels2_bootstrap_integration",
            true);
        profile.schema_module = "keels2_schema_entity_fixture.so";
        profile.game_resource_module = "keels2_schema_entity_fixture.so";
        profile.entity_system_module = "keels2_schema_entity_fixture.so";
        return profile;
    }();
    static const CompatibilityProfile windows_profile = [] {
        CompatibilityProfile profile = Profile(
            "test-fixture-win64",
            "test",
            "win64",
            {},
            "server.dll",
            "keels2_bootstrap_integration.exe",
            "keels2_bootstrap_integration.exe",
            true);
        profile.schema_module = "keels2_schema_entity_fixture.dll";
        profile.game_resource_module = "keels2_schema_entity_fixture.dll";
        profile.entity_system_module = "keels2_schema_entity_fixture.dll";
        return profile;
    }();
    return platform_name && std::strcmp(platform_name, "win64") == 0 ? windows_profile : linux_profile;
}

}
