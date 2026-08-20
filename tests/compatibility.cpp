#include <keels2/cs2/compatibility.h>
#include <keels2/cs2/cvar_abi.h>

#include <cstring>

namespace
{

bool ValidConVarProfile(const keels2::cs2::CompatibilityProfile& profile)
{
    using namespace keels2::cs2;
    return profile.find_convar_slot == kFindConVarSlot &&
        profile.register_convar_slot == kRegisterConVarSlot &&
        profile.unregister_convar_slot == kUnregisterConVarSlot &&
        profile.get_convar_data_slot == kGetConVarDataSlot &&
        profile.call_convar_change_slot == kCallChangeCallbackSlot &&
        profile.call_convar_filter_slot == kCallFilterCallbackSlot &&
        profile.call_global_convar_change_slot == kCallGlobalChangeCallbacksSlot &&
        profile.queue_thread_set_value_slot == kQueueThreadSetValueSlot &&
        profile.convar_value_size == sizeof(ConVarValue) &&
        profile.convar_value_info_size == sizeof(ConVarValueInfo) &&
        profile.convar_creation_size == sizeof(ConVarCreation) &&
        profile.convar_ref_size == sizeof(ConVarRef) &&
        profile.convar_data_size == sizeof(ConVarData) &&
        profile.convar_object_size == sizeof(ConVarObject) &&
        profile.convar_data_type_offset == kConVarDataTypeOffset &&
        profile.convar_data_flags_offset == kConVarDataFlagsOffset &&
        profile.convar_data_value_offset == kConVarDataValueOffset &&
        profile.convar_value_info_change_provider_offset == kConVarValueInfoChangeProviderOffset &&
        profile.convar_value_info_custom_data_offset == kConVarValueInfoCustomDataOffset &&
        profile.convar_data_custom_data_offset == kConVarDataCustomDataOffset &&
        profile.convar_object_data_offset == kConVarObjectDataOffset &&
        profile.convar_bool_type == static_cast<std::int32_t>(ConVarType::boolean) &&
        profile.convar_int32_type == static_cast<std::int32_t>(ConVarType::int32) &&
        profile.convar_float32_type == static_cast<std::int32_t>(ConVarType::float32) &&
        profile.convar_string_type == static_cast<std::int32_t>(ConVarType::string);
}

bool ValidLifecycleProfile(const keels2::cs2::CompatibilityProfile& profile)
{
    using namespace keels2::cs2;
    const bool windows = std::strcmp(profile.platform, "win64") == 0;
    return profile.connect_slot == 0 &&
        profile.disconnect_slot == 1 &&
        profile.init_slot == 3 &&
        std::strcmp(profile.server_interface, "Source2Server001") == 0 &&
        std::strcmp(profile.game_clients_interface, "Source2GameClients001") == 0 &&
        profile.game_clients_validation_slot == 0 &&
        profile.game_frame_slot == 19 &&
        profile.client_connected_slot == 11 &&
        profile.client_put_in_server_slot == 13 &&
        profile.client_active_slot == 14 &&
        profile.client_fully_connected_slot == 15 &&
        profile.client_disconnecting_slot == 16 &&
        profile.client_settings_changed_slot == 19 &&
        std::strcmp(profile.game_event_manager_class, "CGameEventManager") == 0 &&
        std::strcmp(profile.game_event_module, profile.server_module) == 0 &&
        profile.game_event_load_events_slot == (windows ? 1u : 2u) &&
        profile.game_event_add_listener_slot == (windows ? 3u : 4u) &&
        profile.register_command_slot == kRegisterConCommandSlot &&
        profile.unregister_command_slot == kUnregisterConCommandSlot &&
        ValidConVarProfile(profile);
}

}

int main()
{
    const auto* linux_profile = keels2::cs2::FindCompatibilityProfile(
        {40344184, 0xd9145056b00162faull},
        "linuxsteamrt64");
    if (!linux_profile || std::strcmp(linux_profile->game_version, "2000880") != 0 ||
        std::strcmp(
            linux_profile->id,
            "cs2-2000880-linuxsteamrt64-17a2d48e2444bf4f8ecf6a126a36e8753dcfcb81") != 0 ||
        std::strcmp(linux_profile->server_module, "libserver.so") != 0 ||
        std::strcmp(linux_profile->cvar_module, "libtier0.so") != 0 ||
        !ValidLifecycleProfile(*linux_profile))
    {
        return 1;
    }

    const auto* current_linux_profile = keels2::cs2::FindCompatibilityProfile(
        {40352056, 0x023a563a82a10f52ull},
        "linuxsteamrt64");
    if (!current_linux_profile ||
        std::strcmp(current_linux_profile->game_version, "2000884") != 0 ||
        std::strcmp(
            current_linux_profile->id,
            "cs2-2000884-linuxsteamrt64-60a107b12af1a8d752ec462200852a2e7470913d") != 0 ||
        std::strcmp(current_linux_profile->server_module, "libserver.so") != 0 ||
        std::strcmp(current_linux_profile->cvar_module, "libtier0.so") != 0 ||
        !ValidLifecycleProfile(*current_linux_profile))
    {
        return 2;
    }

    const auto* next_linux_profile = keels2::cs2::FindCompatibilityProfile(
        {40353400, 0x3aa4e49b8b45ac19ull},
        "linuxsteamrt64");
    if (!next_linux_profile ||
        std::strcmp(next_linux_profile->game_version, "2000885") != 0 ||
        std::strcmp(
            next_linux_profile->id,
            "cs2-2000885-linuxsteamrt64-d05aa2d65efa96e06e3ded6dd2a95b5220a993a8") != 0 ||
        std::strcmp(next_linux_profile->server_module, "libserver.so") != 0 ||
        std::strcmp(next_linux_profile->cvar_module, "libtier0.so") != 0 ||
        !ValidLifecycleProfile(*next_linux_profile))
    {
        return 3;
    }

    const auto* previous_windows_profile = keels2::cs2::FindCompatibilityProfile(
        {32794264, 0x63eca0729c4fd8a9ull},
        "win64");
    if (!previous_windows_profile ||
        std::strcmp(previous_windows_profile->game_version, "2000879") != 0)
    {
        return 4;
    }

    const auto* windows_profile = keels2::cs2::FindCompatibilityProfile(
        {32818840, 0xda8eb43f77d5c62full},
        "win64");
    if (!windows_profile || std::strcmp(windows_profile->game_version, "2000880") != 0 ||
        std::strcmp(
            windows_profile->id,
            "cs2-2000880-win64-2369e67d8d0e4475a49dc6f4e8c99d28-52") != 0 ||
        std::strcmp(windows_profile->server_module, "server.dll") != 0 ||
        std::strcmp(windows_profile->cvar_module, "tier0.dll") != 0 ||
        !ValidLifecycleProfile(*windows_profile))
    {
        return 5;
    }

    const auto* current_windows_profile = keels2::cs2::FindCompatibilityProfile(
        {32824984, 0x286e997327894e53ull},
        "win64");
    if (!current_windows_profile ||
        std::strcmp(current_windows_profile->game_version, "2000884") != 0 ||
        std::strcmp(
            current_windows_profile->id,
            "cs2-2000884-win64-2369e67d8d0e4475a49dc6f4e8c99d28-54") != 0 ||
        std::strcmp(current_windows_profile->server_module, "server.dll") != 0 ||
        std::strcmp(current_windows_profile->cvar_module, "tier0.dll") != 0 ||
        !ValidLifecycleProfile(*current_windows_profile))
    {
        return 6;
    }

    const auto* next_windows_profile = keels2::cs2::FindCompatibilityProfile(
        {32826008, 0xb3f810b3507341c6ull},
        "win64");
    if (!next_windows_profile ||
        std::strcmp(next_windows_profile->game_version, "2000885") != 0 ||
        std::strcmp(
            next_windows_profile->id,
            "cs2-2000885-win64-2369e67d8d0e4475a49dc6f4e8c99d28-55") != 0 ||
        std::strcmp(next_windows_profile->server_module, "server.dll") != 0 ||
        std::strcmp(next_windows_profile->cvar_module, "tier0.dll") != 0 ||
        !ValidLifecycleProfile(*next_windows_profile))
    {
        return 7;
    }

    if (keels2::cs2::FindCompatibilityProfile(linux_profile->server, "win64") ||
        keels2::cs2::FindCompatibilityProfile(current_linux_profile->server, "win64") ||
        keels2::cs2::FindCompatibilityProfile(next_linux_profile->server, "win64") ||
        keels2::cs2::FindCompatibilityProfile(windows_profile->server, "linuxsteamrt64") ||
        keels2::cs2::FindCompatibilityProfile(
            current_windows_profile->server,
            "linuxsteamrt64") ||
        keels2::cs2::FindCompatibilityProfile(
            next_windows_profile->server,
            "linuxsteamrt64") ||
        keels2::cs2::FindCompatibilityProfile(
            {current_linux_profile->server.size + 1, current_linux_profile->server.fnv1a64},
            "linuxsteamrt64") ||
        keels2::cs2::FindCompatibilityProfile(
            {current_linux_profile->server.size, current_linux_profile->server.fnv1a64 + 1},
            "linuxsteamrt64") ||
        keels2::cs2::FindCompatibilityProfile(
            {current_windows_profile->server.size + 1, current_windows_profile->server.fnv1a64},
            "win64") ||
        keels2::cs2::FindCompatibilityProfile(
            {current_windows_profile->server.size, current_windows_profile->server.fnv1a64 + 1},
            "win64") ||
        keels2::cs2::FindCompatibilityProfile(
            {next_linux_profile->server.size + 1, next_linux_profile->server.fnv1a64},
            "linuxsteamrt64") ||
        keels2::cs2::FindCompatibilityProfile(
            {next_linux_profile->server.size, next_linux_profile->server.fnv1a64 + 1},
            "linuxsteamrt64") ||
        keels2::cs2::FindCompatibilityProfile(
            {next_windows_profile->server.size + 1, next_windows_profile->server.fnv1a64},
            "win64") ||
        keels2::cs2::FindCompatibilityProfile(
            {next_windows_profile->server.size, next_windows_profile->server.fnv1a64 + 1},
            "win64") ||
        keels2::cs2::FindCompatibilityProfile({1, 2}, "linuxsteamrt64") ||
        keels2::cs2::FindCompatibilityProfile(linux_profile->server, nullptr))
    {
        return 8;
    }

    return 0;
}
