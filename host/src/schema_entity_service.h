#ifndef KEELS2_HOST_SCHEMA_ENTITY_SERVICE_H
#define KEELS2_HOST_SCHEMA_ENTITY_SERVICE_H

#include "game_adapter.h"

#include <keels2/entities.h>
#include <keels2/schema.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

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
    KeelResult ReleasePlugin(KeelPluginHandle plugin);
    bool Shutdown();

private:
    struct FieldRecord
    {
        KeelPluginHandle owner{};
        std::shared_ptr<GameSchemaField> field;
    };

    struct EntityRecord
    {
        KeelPluginHandle owner{};
        GameEntityIdentity entity;
    };

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
