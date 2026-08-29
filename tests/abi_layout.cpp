#include <keels2/bootstrap_api.h>
#include <keels2/cs2/cvar_abi.h>
#include <keels2/keelhook.hpp>
#include <keels2/lifecycle.h>
#include <keels2/source2_authoring.h>
#include <keels2/source2_callbacks.h>
#include <keels2/plugin.h>
#include <keels2/source2.hpp>

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
static_assert(KEELHOOK_MAX_AGGREGATE_ALIGNMENT == 16);
static_assert(sizeof(KeelHostCompatibilityInfo) == 376);
static_assert(sizeof(KeelHostStartInfo) == 56);
static_assert(sizeof(KeelHostQuery) == 32);
static_assert(sizeof(KeelPluginInfo) == 40);
static_assert(sizeof(KeelCommandInvocation) == 24);
static_assert(sizeof(KeelCommandSpec) == 48);
static_assert(sizeof(KeelHostApi) == 40);
static_assert(sizeof(KeelSource2InterfaceInfo) == 64);
static_assert(sizeof(KeelSource2ApiV1) == 16);
static_assert(sizeof(KeelSource2Api) == 24);
static_assert(sizeof(KeelHookScalar) == 16);
static_assert(sizeof(KeelHookValue) == 24);
static_assert(sizeof(KeelHookAggregateField) == 24);
static_assert(sizeof(KeelHookAggregate) == 24);
static_assert(sizeof(KeelHookPrototype) == 48);
static_assert(sizeof(KeelHookTargetSpec) == 72);
static_assert(sizeof(KeelHookVirtualTargetSpec) == 40);
static_assert(sizeof(KeelHookFrame) == 56);
static_assert(sizeof(KeelHookCallbackSpec) == 32);
static_assert(sizeof(KeelHookApi) == 48);
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
static_assert(std::is_standard_layout_v<KeelHostCompatibilityInfo>);
static_assert(std::is_standard_layout_v<KeelHostStartInfo>);
static_assert(std::is_standard_layout_v<KeelHostQuery>);
static_assert(std::is_standard_layout_v<KeelPluginInfo>);
static_assert(std::is_standard_layout_v<KeelCommandInvocation>);
static_assert(std::is_standard_layout_v<KeelCommandSpec>);
static_assert(std::is_standard_layout_v<KeelHostApi>);
static_assert(std::is_standard_layout_v<KeelSource2InterfaceInfo>);
static_assert(std::is_standard_layout_v<KeelSource2ApiV1>);
static_assert(std::is_standard_layout_v<KeelSource2Api>);
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
