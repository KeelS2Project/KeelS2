#ifndef KEELS2_BOOTSTRAP_API_H
#define KEELS2_BOOTSTRAP_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELS2_HOST_ABI_VERSION 12u

#define KEELS2_HOST_START_FAILED 0u
#define KEELS2_HOST_START_RUNNING 1u
#define KEELS2_HOST_START_RETAINED 2u

typedef void* (*KeelCreateInterfaceFn)(const char* name, int* return_code);

typedef struct KeelHostCompatibilityTargetInfo
{
    uint32_t size;
    uint32_t occurrence;
    int64_t offset;
    const char* name;
    const char* module;
    const char* pattern;
} KeelHostCompatibilityTargetInfo;

typedef struct KeelHostCompatibilityInfo
{
    uint32_t size;
    const char* profile;
    const char* game_version;
    const char* server_interface;
    const char* server_module;
    const char* game_clients_interface;
    const char* cvar_interface;
    const char* cvar_module;
    uint32_t server_validation_slot;
    uint32_t game_clients_validation_slot;
    uint32_t game_frame_slot;
    uint32_t client_connected_slot;
    uint32_t client_put_in_server_slot;
    uint32_t client_active_slot;
    uint32_t client_fully_connected_slot;
    uint32_t client_disconnecting_slot;
    uint32_t client_settings_changed_slot;
    uint32_t register_command_slot;
    uint32_t unregister_command_slot;
    uint32_t command_creation_size;
    uint32_t command_callback_info_size;
    uint32_t completion_callback_info_size;
    uint32_t command_ref_size;
    uint32_t command_size;
    uint32_t command_argument_count_offset;
    uint32_t command_argument_values_offset;
    uint32_t find_convar_slot;
    uint32_t register_convar_slot;
    uint32_t unregister_convar_slot;
    uint32_t get_convar_data_slot;
    uint32_t call_convar_change_slot;
    uint32_t call_convar_filter_slot;
    uint32_t call_global_convar_change_slot;
    uint32_t queue_thread_set_value_slot;
    uint32_t convar_value_size;
    uint32_t convar_value_info_size;
    uint32_t convar_creation_size;
    uint32_t convar_ref_size;
    uint32_t convar_data_size;
    uint32_t convar_object_size;
    uint32_t convar_data_type_offset;
    uint32_t convar_data_flags_offset;
    uint32_t convar_data_value_offset;
    uint32_t convar_value_info_change_provider_offset;
    uint32_t convar_value_info_custom_data_offset;
    uint32_t convar_data_custom_data_offset;
    uint32_t convar_object_data_offset;
    int32_t convar_bool_type;
    int32_t convar_int32_type;
    int32_t convar_float32_type;
    int32_t convar_string_type;
    const char* engine_service_interface;
    const char* engine_service_module;
    const char* game_event_manager_class;
    const char* game_event_module;
    uint32_t engine_service_register_loop_mode_slot;
    uint32_t engine_service_unregister_loop_mode_slot;
    uint32_t loop_mode_factory_create_slot;
    uint32_t loop_mode_factory_destroy_slot;
    uint32_t loop_mode_init_slot;
    uint32_t loop_mode_shutdown_slot;
    uint32_t game_event_load_events_slot;
    uint32_t game_event_add_listener_slot;
    uint32_t client_connect_slot;
    uint32_t client_command_slot;
    const char* schema_interface;
    const char* schema_module;
    const char* schema_server_module;
    uint32_t schema_validation_slot;
    const char* game_resource_interface;
    const char* game_resource_module;
    const char* entity_system_module;
    uint32_t game_resource_validation_slot;
    uint32_t game_entity_system_offset;
    uint32_t target_count;
    uint32_t reserved;
    const KeelHostCompatibilityTargetInfo* targets;
} KeelHostCompatibilityInfo;

typedef struct KeelHostStartInfo
{
    uint32_t size;
    uint32_t abi_version;
    KeelCreateInterfaceFn engine_factory;
    KeelCreateInterfaceFn server_factory;
    const char* bootstrap_directory;
    const char* game;
    const char* platform;
    const KeelHostCompatibilityInfo* compatibility;
} KeelHostStartInfo;

typedef uint32_t (*KeelHostStartFn)(const KeelHostStartInfo* info);
typedef uint32_t (*KeelHostCompleteStartupFn)(void);
typedef uint32_t (*KeelHostStopFn)(void);

#ifdef __cplusplus
}
#endif

#endif
