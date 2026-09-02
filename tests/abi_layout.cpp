#include <keels2/bootstrap_api.h>
#include <keels2/convar.h>
#include <keels2/cs2/cvar_abi.h>
#include <keels2/entities.h>
#include <keels2/game_adapter.hpp>
#include <keels2/keelhook.hpp>
#include <keels2/lifecycle.h>
#include <keels2/plugins.h>
#include <keels2/schema.h>
#include <keels2/services.h>
#include <keels2/source2_authoring.h>
#include <keels2/source2_callbacks.h>
#include <keels2/plugin.h>
#include <keels2/source2.hpp>
#include <keels2/source2_runtime.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

struct AbiPair
{
    std::int32_t first;
    float second;
};

template <>
struct keels2::kh::AggregateTraits<AbiPair>
{
    static consteval auto Fields()
    {
        return keels2::kh::Fields(
            keels2::kh::Field<std::int32_t>(offsetof(AbiPair, first)),
            keels2::kh::Field<float>(offsetof(AbiPair, second)));
    }
};

static_assert(sizeof(void*) == 8);
static_assert(KEELS2_HOST_ABI_VERSION == 12);
static_assert(KEELS2_PLUGIN_ABI_VERSION == 4);
static_assert(KEELS2_PLUGIN_MANIFEST_VERSION == 1);
static_assert(KEELHOOK_API_VERSION_3 == 3);
static_assert(KEELHOOK_API_VERSION == 4);
static_assert(KEELS2_SOURCE2_API_VERSION_1 == 1);
static_assert(KEELS2_SOURCE2_API_VERSION == 2);
static_assert(KEELS2_SOURCE2_RUNTIME_API_VERSION == 1);
static_assert(KEELS2_SERVICES_API_VERSION == 1);
static_assert(KEELHOOK_MAX_AGGREGATE_ALIGNMENT == 16);
static_assert(sizeof(KeelHostCompatibilityTargetInfo) == 40);
static_assert(sizeof(KeelHostCompatibilityInfo) == 392);
static_assert(sizeof(KeelHostStartInfo) == 56);
static_assert(sizeof(keels2::host::GameAdapterHostApi) == 24);
static_assert(sizeof(keels2::host::GameAdapterProvider) == 40);
static_assert(sizeof(KeelHostQuery) == 32);
static_assert(sizeof(KeelPluginInfo) == 40);
static_assert(sizeof(KeelPluginDependency) == 24);
static_assert(sizeof(KeelPluginManifest) == 24);
static_assert(sizeof(KeelCommandInvocation) == 24);
static_assert(sizeof(KeelCommandSpec) == 48);
static_assert(sizeof(KeelHostApi) == 40);
static_assert(sizeof(KeelServiceSpec) == 24);
static_assert(sizeof(KeelServicesApi) == 32);
static_assert(sizeof(KeelPluginSnapshot) == 1624);
static_assert(sizeof(KeelPluginEvent) == 1640);
static_assert(sizeof(KeelPluginSubscriptionSpec) == 32);
static_assert(sizeof(KeelPluginsApi) == 72);
static_assert(sizeof(KeelSource2InterfaceInfo) == 64);
static_assert(sizeof(KeelSource2ApiV1) == 16);
static_assert(sizeof(KeelSource2Api) == 24);
static_assert(sizeof(KeelSource2RuntimeApi) == 32);
static_assert(sizeof(KeelConVarScalar) == 8);
static_assert(sizeof(KeelConVarValue) == 16);
static_assert(sizeof(KeelConVarChange) == 56);
static_assert(sizeof(KeelConVarSpec) == 112);
static_assert(sizeof(KeelConVarInfo) == 96);
static_assert(sizeof(KeelConVarApi) == 56);
static_assert(sizeof(KeelSchemaFieldSpec) == 32);
static_assert(sizeof(KeelSchemaFieldInfo) == 64);
static_assert(sizeof(KeelSchemaApi) == 32);
static_assert(sizeof(KeelEntityInfo) == 24);
static_assert(sizeof(KeelEntitiesApi) == 56);
static_assert(sizeof(KeelHookScalar) == 16);
static_assert(sizeof(KeelHookValue) == 24);
static_assert(sizeof(KeelHookAggregateField) == 24);
static_assert(sizeof(KeelHookAggregate) == 24);
static_assert(sizeof(KeelHookPrototype) == 48);
static_assert(sizeof(KeelHookTargetSpec) == 72);
static_assert(sizeof(KeelHookVirtualTargetSpec) == 40);
static_assert(sizeof(KeelHookFrame) == 56);
static_assert(sizeof(KeelHookCallbackSpec) == 32);
static_assert(sizeof(KeelHookApiV3) == 48);
static_assert(sizeof(KeelHookApi) == 72);
static_assert(sizeof(KeelLifecycleGameFrame) == 16);
static_assert(sizeof(KeelLifecycleClientConnected) == 48);
static_assert(sizeof(KeelLifecycleClientPutInServer) == 32);
static_assert(sizeof(KeelLifecycleClientActive) == 32);
static_assert(sizeof(KeelLifecycleClientFullyConnected) == 8);
static_assert(sizeof(KeelLifecycleClientDisconnecting) == 40);
static_assert(sizeof(KeelLifecycleClientSettingsChanged) == 8);
static_assert(sizeof(KeelLifecycleEvent) == 24);
static_assert(sizeof(KeelLifecycleSubscriptionSpec) == 32);
static_assert(sizeof(KeelLifecycleApi) == 24);
static_assert(sizeof(KeelSource2CommandSpec) == 48);
static_assert(sizeof(KeelSource2AuthoringApi) == 48);
static_assert(sizeof(KeelSource2LevelInit) == 24);
static_assert(sizeof(KeelSource2LevelShutdown) == 8);
static_assert(sizeof(KeelSource2GameEvent) == 24);
static_assert(sizeof(KeelSource2ClientConnect) == 56);
static_assert(sizeof(KeelSource2ClientCommand) == 16);
static_assert(sizeof(KeelSource2CallbackEvent) == 24);
static_assert(sizeof(KeelSource2SubscriptionSpec) == 40);
static_assert(sizeof(KeelSource2CallbacksApi) == 24);
static_assert(offsetof(KeelHostQuery, abi_version) == 4);
static_assert(offsetof(KeelHostQuery, host_version) == 8);
static_assert(offsetof(KeelHostQuery, platform) == 24);
static_assert(offsetof(KeelPluginInfo, name) == 8);
static_assert(offsetof(KeelPluginInfo, description) == 32);
static_assert(offsetof(KeelPluginDependency, name) == 8);
static_assert(offsetof(KeelPluginManifest, dependencies) == 16);
static_assert(offsetof(KeelCommandInvocation, name) == 8);
static_assert(offsetof(KeelCommandSpec, name) == 8);
static_assert(offsetof(KeelCommandSpec, callback) == 32);
static_assert(offsetof(KeelHostApi, log) == 8);
static_assert(offsetof(KeelHostApi, query_service) == 32);
static_assert(offsetof(KeelServiceSpec, name) == 8);
static_assert(offsetof(KeelServiceSpec, service) == 16);
static_assert(offsetof(KeelServicesApi, publish) == 8);
static_assert(offsetof(KeelServicesApi, release) == 24);
static_assert(offsetof(KeelHookFrame, arguments) == 24);
static_assert(offsetof(KeelHookFrame, result) == 32);
static_assert(offsetof(KeelHookApiV3, resolve_target) == 8);
static_assert(offsetof(KeelHookApiV3, remove_callback) == 32);
static_assert(offsetof(KeelHookApiV3, resolve_virtual_target) == 40);
static_assert(offsetof(KeelHookApi, resolve_target) == 8);
static_assert(offsetof(KeelHookApi, call_original) == 48);
static_assert(offsetof(KeelHookApi, recall) == 56);
static_assert(offsetof(KeelHookApi, set_callback_enabled) == 64);
static_assert(offsetof(KeelSource2InterfaceInfo, instance) == 24);
static_assert(offsetof(KeelSource2InterfaceInfo, compatibility_profile) == 56);
static_assert(offsetof(KeelSource2Api, query_named_interface) == 16);
static_assert(offsetof(KeelSource2RuntimeApi, server_command) == 8);
static_assert(offsetof(KeelSource2RuntimeApi, find_user_message) == 24);
static_assert(offsetof(KeelPluginSnapshot, handle) == 8);
static_assert(offsetof(KeelPluginSnapshot, name) == 16);
static_assert(offsetof(KeelPluginSnapshot, diagnostic) == 1108);
static_assert(offsetof(KeelPluginsApi, count) == 8);
static_assert(offsetof(KeelPluginsApi, unsubscribe) == 64);
static_assert(offsetof(KeelConVarValue, value) == 8);
static_assert(offsetof(KeelConVarSpec, default_value) == 32);
static_assert(offsetof(KeelConVarSpec, callback) == 96);
static_assert(offsetof(KeelConVarApi, create) == 8);
static_assert(offsetof(KeelConVarApi, describe) == 48);
static_assert(offsetof(KeelSchemaFieldInfo, class_name) == 32);
static_assert(offsetof(KeelSchemaFieldInfo, compatibility_profile) == 56);
static_assert(offsetof(KeelSchemaApi, resolve_field) == 8);
static_assert(offsetof(KeelSchemaApi, describe_field) == 24);
static_assert(offsetof(KeelEntityInfo, epoch) == 16);
static_assert(offsetof(KeelEntitiesApi, find_by_index) == 8);
static_assert(offsetof(KeelEntitiesApi, read_field) == 48);
static_assert(offsetof(KeelSource2CallbackEvent, payload) == 16);
static_assert(offsetof(KeelSource2SubscriptionSpec, game_event) == 16);
static_assert(offsetof(KeelSource2CallbacksApi, subscribe) == 8);
static_assert(offsetof(KeelHostCompatibilityTargetInfo, name) == 16);
static_assert(offsetof(KeelHostCompatibilityInfo, targets) == 384);
static_assert(offsetof(KeelHostStartInfo, compatibility) == 48);
static_assert(offsetof(keels2::host::GameAdapterProvider, create) == 24);
static_assert(offsetof(keels2::host::GameAdapterProvider, destroy) == 32);
static_assert(std::is_standard_layout_v<KeelHostCompatibilityTargetInfo>);
static_assert(std::is_standard_layout_v<KeelHostCompatibilityInfo>);
static_assert(std::is_standard_layout_v<KeelHostStartInfo>);
static_assert(std::is_standard_layout_v<keels2::host::GameAdapterHostApi>);
static_assert(std::is_standard_layout_v<keels2::host::GameAdapterProvider>);
static_assert(std::is_standard_layout_v<KeelHostQuery>);
static_assert(std::is_standard_layout_v<KeelPluginInfo>);
static_assert(std::is_standard_layout_v<KeelPluginDependency>);
static_assert(std::is_standard_layout_v<KeelPluginManifest>);
static_assert(std::is_standard_layout_v<KeelCommandInvocation>);
static_assert(std::is_standard_layout_v<KeelCommandSpec>);
static_assert(std::is_standard_layout_v<KeelHostApi>);
static_assert(std::is_standard_layout_v<KeelServiceSpec>);
static_assert(std::is_standard_layout_v<KeelServicesApi>);
static_assert(std::is_standard_layout_v<KeelPluginSnapshot>);
static_assert(std::is_standard_layout_v<KeelPluginEvent>);
static_assert(std::is_standard_layout_v<KeelPluginSubscriptionSpec>);
static_assert(std::is_standard_layout_v<KeelPluginsApi>);
static_assert(std::is_standard_layout_v<KeelSource2InterfaceInfo>);
static_assert(std::is_standard_layout_v<KeelSource2ApiV1>);
static_assert(std::is_standard_layout_v<KeelSource2Api>);
static_assert(std::is_standard_layout_v<KeelSource2RuntimeApi>);
static_assert(std::is_standard_layout_v<KeelConVarValue>);
static_assert(std::is_standard_layout_v<KeelConVarChange>);
static_assert(std::is_standard_layout_v<KeelConVarSpec>);
static_assert(std::is_standard_layout_v<KeelConVarInfo>);
static_assert(std::is_standard_layout_v<KeelConVarApi>);
static_assert(std::is_standard_layout_v<KeelSchemaFieldSpec>);
static_assert(std::is_standard_layout_v<KeelSchemaFieldInfo>);
static_assert(std::is_standard_layout_v<KeelSchemaApi>);
static_assert(std::is_standard_layout_v<KeelEntityInfo>);
static_assert(std::is_standard_layout_v<KeelEntitiesApi>);
static_assert(std::is_standard_layout_v<KeelHookScalar>);
static_assert(std::is_standard_layout_v<KeelHookValue>);
static_assert(std::is_standard_layout_v<KeelHookAggregateField>);
static_assert(std::is_standard_layout_v<KeelHookAggregate>);
static_assert(std::is_standard_layout_v<KeelHookPrototype>);
static_assert(std::is_standard_layout_v<KeelHookTargetSpec>);
static_assert(std::is_standard_layout_v<KeelHookVirtualTargetSpec>);
static_assert(std::is_standard_layout_v<KeelHookFrame>);
static_assert(std::is_standard_layout_v<KeelHookCallbackSpec>);
static_assert(std::is_standard_layout_v<KeelHookApi>);
static_assert(std::is_standard_layout_v<KeelLifecycleGameFrame>);
static_assert(std::is_standard_layout_v<KeelLifecycleClientConnected>);
static_assert(std::is_standard_layout_v<KeelLifecycleClientPutInServer>);
static_assert(std::is_standard_layout_v<KeelLifecycleClientActive>);
static_assert(std::is_standard_layout_v<KeelLifecycleClientFullyConnected>);
static_assert(std::is_standard_layout_v<KeelLifecycleClientDisconnecting>);
static_assert(std::is_standard_layout_v<KeelLifecycleClientSettingsChanged>);
static_assert(std::is_standard_layout_v<KeelLifecycleEvent>);
static_assert(std::is_standard_layout_v<KeelLifecycleSubscriptionSpec>);
static_assert(std::is_standard_layout_v<KeelLifecycleApi>);
static_assert(std::is_standard_layout_v<KeelSource2CommandSpec>);
static_assert(std::is_standard_layout_v<KeelSource2AuthoringApi>);
static_assert(std::is_standard_layout_v<KeelSource2LevelInit>);
static_assert(std::is_standard_layout_v<KeelSource2LevelShutdown>);
static_assert(std::is_standard_layout_v<KeelSource2GameEvent>);
static_assert(std::is_standard_layout_v<KeelSource2ClientConnect>);
static_assert(std::is_standard_layout_v<KeelSource2ClientCommand>);
static_assert(std::is_standard_layout_v<KeelSource2CallbackEvent>);
static_assert(std::is_standard_layout_v<KeelSource2SubscriptionSpec>);
static_assert(std::is_standard_layout_v<KeelSource2CallbacksApi>);
static_assert(!std::is_copy_constructible_v<keels2::kh::Target>);
static_assert(!std::is_copy_assignable_v<keels2::kh::Target>);
static_assert(std::is_move_constructible_v<keels2::kh::Target>);
static_assert(std::is_move_assignable_v<keels2::kh::Target>);
static_assert(!std::is_copy_constructible_v<keels2::kh::Callback>);
static_assert(!std::is_copy_assignable_v<keels2::kh::Callback>);
static_assert(std::is_move_constructible_v<keels2::kh::Callback>);
static_assert(std::is_move_assignable_v<keels2::kh::Callback>);
static_assert(!std::is_copy_constructible_v<keels2::kh::Hook>);
static_assert(!std::is_copy_assignable_v<keels2::kh::Hook>);
static_assert(std::is_move_constructible_v<keels2::kh::Hook>);
static_assert(std::is_move_assignable_v<keels2::kh::Hook>);
static_assert(!std::is_copy_constructible_v<keels2::kh::Call<void>>);
static_assert(!std::is_copy_assignable_v<keels2::kh::Call<void>>);
static_assert(!std::is_move_constructible_v<keels2::kh::Call<void>>);
static_assert(!std::is_move_assignable_v<keels2::kh::Call<void>>);
static_assert(keels2::kh::MethodPrototype<std::int32_t(float)>::value.argument_count == 2);
static_assert(keels2::kh::MethodPrototype<std::int32_t(float)>::arguments[0] == KH_VALUE_POINTER);
static_assert(keels2::kh::MethodPrototype<std::int32_t(float)>::arguments[1] == KH_VALUE_FLOAT32);
static_assert(keels2::kh::Prototype<AbiPair(AbiPair)>::value.return_type == KH_VALUE_AGGREGATE);
static_assert(
    keels2::kh::Prototype<AbiPair(AbiPair)>::value.return_aggregate->byte_size == sizeof(AbiPair));
static_assert(
    keels2::kh::Prototype<AbiPair(AbiPair)>::value.argument_aggregates[0]->field_count == 2);
static_assert(keels2::kh::Prototype<AbiPair(AbiPair)>::value.fixed_argument_count == 1);

void CheckMethodResolution(
    keels2::kh::Service& service,
    const keels2::kh::TargetSpec& spec,
    keels2::kh::Target& target)
{
    static_cast<void>(service.ResolveMethod<AbiPair(AbiPair)>(spec, target));
}

int main()
{
    return 0;
}
