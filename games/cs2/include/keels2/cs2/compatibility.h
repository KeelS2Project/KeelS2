#ifndef KEELS2_CS2_COMPATIBILITY_H
#define KEELS2_CS2_COMPATIBILITY_H

#include <keels2/platform/file_fingerprint.h>

#include <cstdint>

namespace keels2::cs2
{

struct CompatibilityProfile
{
    const char* id;
    const char* game_version;
    const char* platform;
    platform::FileFingerprint server;
    const char* server_config_interface;
    std::uint32_t connect_slot;
    std::uint32_t disconnect_slot;
    const char* server_interface;
    const char* server_module;
    std::uint32_t init_slot;
    const char* game_clients_interface;
    std::uint32_t game_clients_validation_slot;
    std::uint32_t game_frame_slot;
    std::uint32_t client_connected_slot;
    std::uint32_t client_put_in_server_slot;
    std::uint32_t client_active_slot;
    std::uint32_t client_fully_connected_slot;
    std::uint32_t client_disconnecting_slot;
    std::uint32_t client_settings_changed_slot;
    const char* cvar_interface;
    const char* cvar_module;
    std::uint32_t register_command_slot;
    std::uint32_t unregister_command_slot;
    std::uint32_t command_creation_size;
    std::uint32_t command_callback_info_size;
    std::uint32_t completion_callback_info_size;
    std::uint32_t command_ref_size;
    std::uint32_t command_size;
    std::uint32_t command_argument_count_offset;
    std::uint32_t command_argument_values_offset;
    std::uint32_t find_convar_slot;
    std::uint32_t register_convar_slot;
    std::uint32_t unregister_convar_slot;
    std::uint32_t get_convar_data_slot;
    std::uint32_t call_convar_change_slot;
    std::uint32_t call_convar_filter_slot;
    std::uint32_t call_global_convar_change_slot;
    std::uint32_t queue_thread_set_value_slot;
    std::uint32_t convar_value_size;
    std::uint32_t convar_value_info_size;
    std::uint32_t convar_creation_size;
    std::uint32_t convar_ref_size;
    std::uint32_t convar_data_size;
    std::uint32_t convar_object_size;
    std::uint32_t convar_data_type_offset;
    std::uint32_t convar_data_flags_offset;
    std::uint32_t convar_data_value_offset;
    std::uint32_t convar_value_info_change_provider_offset;
    std::uint32_t convar_value_info_custom_data_offset;
    std::uint32_t convar_data_custom_data_offset;
    std::uint32_t convar_object_data_offset;
    std::int32_t convar_bool_type;
    std::int32_t convar_int32_type;
    std::int32_t convar_float32_type;
    std::int32_t convar_string_type;
    const char* engine_service_interface;
    const char* engine_service_module;
    const char* game_event_manager_class;
    const char* game_event_module;
    std::uint32_t engine_service_register_loop_mode_slot;
    std::uint32_t engine_service_unregister_loop_mode_slot;
    std::uint32_t loop_mode_factory_create_slot;
    std::uint32_t loop_mode_factory_destroy_slot;
    std::uint32_t loop_mode_init_slot;
    std::uint32_t loop_mode_shutdown_slot;
    std::uint32_t game_event_load_events_slot;
    std::uint32_t game_event_add_listener_slot;
    std::uint32_t client_connect_slot;
    std::uint32_t client_command_slot;
};

const CompatibilityProfile* FindCompatibilityProfile(
    const platform::FileFingerprint& server,
    const char* platform_name);
const CompatibilityProfile& FixtureCompatibilityProfile(const char* platform_name);

}

#endif
