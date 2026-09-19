#ifndef KEELS2_HOST_SCHEMA_ENTITY_SERVICE_H
#define KEELS2_HOST_SCHEMA_ENTITY_SERVICE_H

#include "game_adapter.h"

#include <keels2/entities.h>
#include <keels2/entity_construction.h>
#include <keels2/schema.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <thread>
#include <vector>

namespace keels2::host
{

class Host;

class SchemaEntityService final
{
public:
    SchemaEntityService(Host& host, GameAdapter& adapter);
    ~SchemaEntityService();
    SchemaEntityService(const SchemaEntityService&) = delete;
    SchemaEntityService& operator=(const SchemaEntityService&) = delete;

    const KeelSchemaApi& SchemaApi() const noexcept;
    const KeelEntitiesApi& EntitiesApi() const noexcept;
    const KeelPlayerActionsApi& PlayerActionsApi() const noexcept;
    const KeelPlayerManagementApi& PlayerManagementApi() const noexcept;
    const KeelEntityWritesApi& EntityWritesApi() const noexcept;
    const KeelEntityToolsApi& EntityToolsApi() const noexcept;
    const KeelEntityOutputsApi& EntityOutputsApi() const noexcept;
    const KeelEntityInputApi& EntityInputApi() const noexcept;
    const KeelEntityConstructionApi& EntityConstructionApi() const noexcept;
    const KeelEntityAccessApi& EntityAccessApi() const noexcept;
    const KeelEntityCaptureApi& EntityCaptureApi() const noexcept;
    const KeelEntityHookDataApi& EntityHookDataApi() const noexcept;
    const KeelRoundControlApi& RoundControlApi() const noexcept;
    const KeelPlayerStatisticsApi& PlayerStatisticsApi() const noexcept;
    KeelResult ReleasePlugin(KeelPluginHandle plugin);
    bool Shutdown();

private:
    friend struct SchemaEntityServiceTest;
    static KeelResult InputCapabilitiesEntry(KeelPluginHandle, std::uint32_t*, std::uint32_t*);
    static KeelResult DispatchInputEntry(KeelPluginHandle, KeelEntityHandle, const KeelEntityInputRequest*, KeelBool*);
    KeelResult EntityInput(KeelPluginHandle, KeelEntityHandle, const KeelEntityInputRequest*, std::uint32_t*, std::uint32_t*, KeelBool*);
    static KeelResult ReadDamageEntry(KeelPluginHandle plugin, const void* record, KeelDamageInfo* output);
    static KeelResult WriteDamageEntry(KeelPluginHandle plugin, void* record, const KeelDamageEdit* edit);
    static KeelResult WeaponMatchesEntry(KeelPluginHandle plugin, KeelEntityHandle pawn, const void* candidate, KeelBool* matches);
    KeelResult AccessDamage(KeelPluginHandle plugin, const void* record, KeelDamageInfo* output, const KeelDamageEdit* edit);
    KeelResult WeaponMatches(KeelPluginHandle plugin, KeelEntityHandle pawn, const void* candidate, KeelBool* matches);
    static KeelResult CaptureEntityEntry(KeelPluginHandle plugin, const void* instance, KeelEntityHandle* output);
    KeelResult CaptureEntity(KeelPluginHandle plugin, const void* instance, KeelEntityHandle* output);
    static KeelResult VisitEntitiesEntry(KeelPluginHandle plugin, const KeelEntityAccessSpec* entities,
        std::uint32_t count, KeelEntityAccessCallback callback, void* user_data);
    KeelResult VisitEntities(KeelPluginHandle plugin, const KeelEntityAccessSpec* entities,
        std::uint32_t count, KeelEntityAccessCallback callback, void* user_data);
    static KeelResult PlayerStatCapabilitiesEntry(KeelPluginHandle plugin, std::uint32_t* readable, std::uint32_t* writable);
    static KeelResult ReadPlayerStatEntry(KeelPluginHandle plugin, KeelEntityHandle entity, std::uint32_t key, std::int32_t* value);
    static KeelResult WritePlayerStatEntry(KeelPluginHandle plugin, KeelEntityHandle entity, std::uint32_t key, std::int32_t value);
    KeelResult PlayerStatCapabilities(KeelPluginHandle plugin, std::uint32_t* readable, std::uint32_t* writable);
    KeelResult AccessPlayerStat(KeelPluginHandle plugin, KeelEntityHandle entity, std::uint32_t key, std::int32_t& value, bool write);
    static KeelResult RoundCapabilitiesEntry(KeelPluginHandle plugin, std::uint32_t* capabilities);
    static KeelResult TerminateRoundEntry(KeelPluginHandle plugin, const KeelRoundTermination* request);
    KeelResult RoundCapabilities(KeelPluginHandle plugin, std::uint32_t* capabilities);
    KeelResult TerminateRound(KeelPluginHandle plugin, const KeelRoundTermination* request);
    static KeelResult ToolCapabilitiesEntry(KeelPluginHandle plugin, std::uint32_t* flags);
    static KeelResult TeleportEntry(KeelPluginHandle plugin, KeelEntityHandle entity, const KeelEntityTeleport* request);
    static KeelResult SetModelEntry(KeelPluginHandle plugin, KeelEntityHandle entity, const char* model);
    static KeelResult RemoveEntry(KeelPluginHandle plugin, KeelEntityHandle entity);
    KeelResult EntityTool(KeelPluginHandle plugin, KeelEntityHandle entity, std::uint32_t kind,
        const KeelEntityTeleport* request, const char* model, std::uint32_t* capabilities);
    static KeelResult WriteCapabilitiesEntry(KeelPluginHandle plugin, std::uint32_t* capabilities);
    static KeelResult WriteFieldEntry(KeelPluginHandle plugin, KeelEntityHandle entity, KeelSchemaFieldHandle field, const void* value, std::uint32_t size);
    KeelResult WriteCapabilities(KeelPluginHandle plugin, std::uint32_t* capabilities);
    KeelResult WriteField(KeelPluginHandle plugin, KeelEntityHandle entity, KeelSchemaFieldHandle field, const void* value, std::uint32_t size);
    static KeelResult ManagementCapabilitiesEntry(KeelPluginHandle plugin, std::uint32_t* capabilities);
    static KeelResult ManagePlayerEntry(KeelPluginHandle plugin, KeelEntityHandle entity, const KeelPlayerManagementAction* action);
    KeelResult ManagementCapabilities(KeelPluginHandle plugin, std::uint32_t* capabilities);
    KeelResult ManagePlayer(KeelPluginHandle plugin, KeelEntityHandle entity, const KeelPlayerManagementAction* action);
    static KeelResult PlayerActionEntry(KeelPluginHandle plugin, KeelEntityHandle entity, const KeelPlayerAction* action);
    KeelResult PlayerAction(KeelPluginHandle plugin, KeelEntityHandle entity, const KeelPlayerAction* action);
    struct FieldRecord
    {
        KeelPluginHandle owner{};
        std::shared_ptr<GameSchemaField> field;
    };

    struct EntityRecord
    {
        KeelPluginHandle owner{};
        GameEntityIdentity entity;
        struct Construction
        {
            std::atomic<bool> closed{};
            std::uint64_t token{};
            KeelPluginHandle owner{};
            std::thread::id thread;
            bool initializing{true}, busy{};
        };
        std::shared_ptr<Construction> construction{};
        bool construction_owner{};
    };
    using Construction = EntityRecord::Construction;
    struct OutputRecord
    {
        KeelPluginHandle owner{};
        KeelEntityOutputHandle handle{};
        std::uint32_t phases{};
        std::int32_t priority{};
        GameEntityIdentity entity{};
        std::weak_ptr<Construction> construction;
        std::string class_name, output_name;
        KeelEntityOutputCallback callback{};
        void* user_data{};
        bool enabled{true};
    };
    struct OutputInvocation { std::vector<std::shared_ptr<OutputRecord>> callbacks; };
    static KeelResult OutputsReadyEntry(KeelPluginHandle);
    static KeelResult SubscribeOutputEntry(KeelPluginHandle, const KeelEntityOutputSpec*, KeelEntityOutputHandle*);
    static KeelResult UnsubscribeOutputEntry(KeelPluginHandle, KeelEntityOutputHandle);
    static std::uint32_t DispatchOutputsEntry(const KeelEntityOutputEvent*, std::uint64_t, void*);
    KeelResult ConfigureOutputs(KeelPluginHandle, const KeelEntityOutputSpec*, KeelEntityOutputHandle*);
    KeelResult ValidateOutputIdentity(const GameEntityIdentity&, const std::weak_ptr<Construction>&);
    void PruneOutputs(std::uint64_t epoch = 0);
    void ReleaseOutputs(KeelPluginHandle);
    bool StopOutputs();
    std::uint32_t DispatchOutputs(const KeelEntityOutputEvent*, std::uint64_t);
    class ConstructionOperation;
    static bool EntityAccessible(const EntityRecord& record, KeelPluginHandle plugin) noexcept;
    static KeelResult ConstructionReadyEntry(KeelPluginHandle);
    static KeelResult CreateEntityEntry(KeelPluginHandle, const char*, KeelEntityHandle*);
    static KeelResult DescribeConstructionEntry(KeelPluginHandle, KeelEntityHandle, KeelEntityInfo*);
    static KeelResult SetConstructionEntry(KeelPluginHandle, KeelEntityHandle, const KeelEntityKeyValue*);
    static KeelResult TeleportConstructionEntry(KeelPluginHandle, KeelEntityHandle, const KeelEntityTeleport*);
    static KeelResult SpawnConstructionEntry(KeelPluginHandle, KeelEntityHandle, KeelBool*);
    static KeelResult ObserveConstructionEntry(KeelPluginHandle, std::uint32_t, KeelEntityHandle*);
    static KeelResult VisitConstructionEntry(KeelPluginHandle, KeelEntityHandle, const char*, KeelEntityAccessCallback, void*);
    KeelResult ConstructionReady(KeelPluginHandle);
    KeelResult CreateEntity(KeelPluginHandle, const char*, KeelEntityHandle*);
    KeelResult DescribeConstruction(KeelPluginHandle, KeelEntityHandle, KeelEntityInfo*);
    KeelResult ChangeConstruction(KeelPluginHandle, KeelEntityHandle, const KeelEntityKeyValue*, const KeelEntityTeleport*, KeelBool*);
    KeelResult ObserveConstruction(KeelPluginHandle, std::uint32_t, KeelEntityHandle*);
    KeelResult VisitConstruction(KeelPluginHandle, KeelEntityHandle, const char*, KeelEntityAccessCallback, void*);
    void PruneConstructions(KeelPluginHandle);
    KeelResult CancelConstruction(const std::shared_ptr<Construction>&);

    static KeelResult ResolveFieldEntry(
        KeelPluginHandle plugin,
        const KeelSchemaFieldSpec* spec,
        KeelSchemaFieldHandle* field);
    static KeelResult ReleaseFieldEntry(
        KeelPluginHandle plugin,
        KeelSchemaFieldHandle field);
    static KeelResult DescribeFieldEntry(
        KeelPluginHandle plugin,
        KeelSchemaFieldHandle field,
        KeelSchemaFieldInfo* info);
    static KeelResult FindEntityByIndexEntry(
        KeelPluginHandle plugin,
        std::int32_t index,
        KeelEntityHandle* entity);
    static KeelResult FindEntityBySource2HandleEntry(
        KeelPluginHandle plugin,
        std::uint32_t source2_handle,
        KeelEntityHandle* entity);
    static KeelResult ReleaseEntityEntry(
        KeelPluginHandle plugin,
        KeelEntityHandle entity);
    static KeelResult DescribeEntityEntry(
        KeelPluginHandle plugin,
        KeelEntityHandle entity,
        KeelEntityInfo* info);
    static KeelResult EqualEntityEntry(
        KeelPluginHandle plugin,
        KeelEntityHandle left,
        KeelEntityHandle right,
        KeelBool* equal);
    static KeelResult ReadEntityFieldEntry(
        KeelPluginHandle plugin,
        KeelEntityHandle entity,
        KeelSchemaFieldHandle field,
        void* value,
        std::uint32_t value_size);

    KeelResult ResolveField(
        KeelPluginHandle plugin,
        const KeelSchemaFieldSpec* spec,
        KeelSchemaFieldHandle* field);
    KeelResult ReleaseField(
        KeelPluginHandle plugin,
        KeelSchemaFieldHandle field);
    KeelResult DescribeField(
        KeelPluginHandle plugin,
        KeelSchemaFieldHandle field,
        KeelSchemaFieldInfo* info);
    KeelResult FindEntityByIndex(
        KeelPluginHandle plugin,
        std::int32_t index,
        KeelEntityHandle* entity);
    KeelResult FindEntityBySource2Handle(
        KeelPluginHandle plugin,
        std::uint32_t source2_handle,
        KeelEntityHandle* entity);
    KeelResult ReleaseEntity(
        KeelPluginHandle plugin,
        KeelEntityHandle entity);
    KeelResult DescribeEntity(
        KeelPluginHandle plugin,
        KeelEntityHandle entity,
        KeelEntityInfo* info);
    KeelResult EqualEntity(
        KeelPluginHandle plugin,
        KeelEntityHandle left,
        KeelEntityHandle right,
        KeelBool* equal);
    KeelResult ReadEntityField(
        KeelPluginHandle plugin,
        KeelEntityHandle entity,
        KeelSchemaFieldHandle field,
        void* value,
        std::uint32_t value_size);

    bool PluginReady(KeelPluginHandle plugin) const noexcept;
    static bool ValidSchemaName(const char* name) noexcept;
    static bool ValidValueType(KeelSchemaValueType type) noexcept;
    static std::string FieldCacheKey(
        const std::string& profile,
        const KeelSchemaFieldSpec& spec);

    Host& host_;
    GameAdapter& adapter_;
    KeelSchemaApi schema_api_{};
    KeelEntitiesApi entities_api_{};
    KeelPlayerActionsApi player_actions_api_{};
    KeelPlayerManagementApi player_management_api_{};
    KeelEntityWritesApi entity_writes_api_{};
    KeelEntityToolsApi entity_tools_api_{};
    KeelEntityInputApi entity_input_api_{};
    KeelEntityOutputsApi entity_outputs_api_{};
    std::unordered_map<KeelEntityOutputHandle,std::shared_ptr<OutputRecord>> outputs_;
    std::unordered_map<std::uint64_t,std::shared_ptr<OutputInvocation>> output_invocations_;
    KeelEntityOutputHandle next_output_{1};
    unsigned output_depth_{};
    bool outputs_started_{}, outputs_retained_{}, outputs_starting_{}, outputs_stopping_{};
    unsigned entity_input_depth_{};
    KeelEntityConstructionApi entity_construction_api_{};
    unsigned construction_depth_{};
    unsigned entity_tools_depth_ = 0;
    KeelEntityAccessApi entity_access_api_{};
    KeelEntityCaptureApi entity_capture_api_{};
    KeelEntityHookDataApi entity_hook_data_api_{};
    unsigned hook_data_depth_ = 0;
    unsigned entity_access_depth_ = 0;
    KeelRoundControlApi round_control_api_{};
    KeelPlayerStatisticsApi player_statistics_api_{};
    mutable std::mutex registry_mutex_;
    std::unordered_map<std::string, std::shared_ptr<GameSchemaField>> field_cache_;
    std::unordered_map<KeelSchemaFieldHandle, FieldRecord> fields_;
    std::unordered_map<KeelEntityHandle, EntityRecord> entities_;
    KeelSchemaFieldHandle next_field_{1};
    KeelEntityHandle next_entity_{1};
    std::atomic<bool> shutting_down_{};

    static std::atomic<SchemaEntityService*> active_;
};

}

#endif
