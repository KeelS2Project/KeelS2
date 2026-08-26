#include "schema_entity_service.h"

#include "host.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace keels2::host
{

std::atomic<SchemaEntityService*> SchemaEntityService::active_{};

SchemaEntityService::SchemaEntityService(Host& host, GameAdapter& adapter)
    : host_(host), adapter_(adapter)
{
    SchemaEntityService* expected{};
    if (!active_.compare_exchange_strong(expected, this, std::memory_order_acq_rel))
    {
        throw std::runtime_error("schema and entity service already exists");
    }
    schema_api_ = {
        sizeof(KeelSchemaApi),
        KEELS2_SCHEMA_API_VERSION,
        &ResolveFieldEntry,
        &ReleaseFieldEntry,
        &DescribeFieldEntry
    };
    entities_api_ = {
        sizeof(KeelEntitiesApi),
        KEELS2_ENTITIES_API_VERSION,
        &FindEntityByIndexEntry,
        &FindEntityBySource2HandleEntry,
        &ReleaseEntityEntry,
        &DescribeEntityEntry,
        &EqualEntityEntry,
        &ReadEntityFieldEntry
    };
}

SchemaEntityService::~SchemaEntityService()
{
    active_.store(nullptr, std::memory_order_release);
    static_cast<void>(Shutdown());
}

const KeelSchemaApi& SchemaEntityService::SchemaApi() const noexcept
{
    return schema_api_;
}

const KeelEntitiesApi& SchemaEntityService::EntitiesApi() const noexcept
{
    return entities_api_;
}

KeelResult SchemaEntityService::ReleasePlugin(KeelPluginHandle plugin)
{
    if (!plugin)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock lock(registry_mutex_);
    std::erase_if(fields_, [plugin](const auto& entry) {
        return entry.second.owner == plugin;
    });
    std::erase_if(entities_, [plugin](const auto& entry) {
        return entry.second.owner == plugin;
    });
    return KEEL_RESULT_OK;
}

bool SchemaEntityService::Shutdown()
{
    std::scoped_lock lock(registry_mutex_);
    if (shutting_down_.exchange(true, std::memory_order_acq_rel))
    {
        return true;
    }
    fields_.clear();
    entities_.clear();
    field_cache_.clear();
    return true;
}

KeelResult SchemaEntityService::ResolveFieldEntry(
    KeelPluginHandle plugin,
    const KeelSchemaFieldSpec* spec,
    KeelSchemaFieldHandle* field)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);
    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return service->ResolveField(plugin, spec, field);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while resolving a schema field");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::ReleaseFieldEntry(
    KeelPluginHandle plugin,
    KeelSchemaFieldHandle field)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);
    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return service->ReleaseField(plugin, field);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while releasing a schema field");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::DescribeFieldEntry(
    KeelPluginHandle plugin,
    KeelSchemaFieldHandle field,
    KeelSchemaFieldInfo* info)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);
    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return service->DescribeField(plugin, field, info);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while describing a schema field");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::FindEntityByIndexEntry(
    KeelPluginHandle plugin,
    std::int32_t index,
    KeelEntityHandle* entity)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);
    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return service->FindEntityByIndex(plugin, index, entity);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while finding an entity by index");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::FindEntityBySource2HandleEntry(
    KeelPluginHandle plugin,
    std::uint32_t source2_handle,
    KeelEntityHandle* entity)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);
    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return service->FindEntityBySource2Handle(plugin, source2_handle, entity);
    }
    catch (...)
    {
        service->host_.Write(
            KEEL_LOG_ERROR,
            "exception while finding an entity by Source 2 handle");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::ReleaseEntityEntry(
    KeelPluginHandle plugin,
    KeelEntityHandle entity)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);
    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return service->ReleaseEntity(plugin, entity);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while releasing an entity handle");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::DescribeEntityEntry(
    KeelPluginHandle plugin,
    KeelEntityHandle entity,
    KeelEntityInfo* info)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);
    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return service->DescribeEntity(plugin, entity, info);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while describing an entity handle");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::EqualEntityEntry(
    KeelPluginHandle plugin,
    KeelEntityHandle left,
    KeelEntityHandle right,
    KeelBool* equal)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);
    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return service->EqualEntity(plugin, left, right, equal);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while comparing entity handles");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::ReadEntityFieldEntry(
    KeelPluginHandle plugin,
    KeelEntityHandle entity,
    KeelSchemaFieldHandle field,
    void* value,
    std::uint32_t value_size)
{
    SchemaEntityService* service = active_.load(std::memory_order_acquire);
    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return service->ReadEntityField(plugin, entity, field, value, value_size);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while reading an entity field");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult SchemaEntityService::ResolveField(
    KeelPluginHandle plugin,
    const KeelSchemaFieldSpec* spec,
    KeelSchemaFieldHandle* output)
{
    if (output)
    {
        *output = 0;
    }
    if (!spec || !output || spec->size != sizeof(KeelSchemaFieldSpec) ||
        spec->reserved != 0 || spec->module != KEELS2_SCHEMA_MODULE_SERVER ||
        !ValidValueType(spec->value_type) || !ValidSchemaName(spec->class_name) ||
        !ValidSchemaName(spec->field_name))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock state_lock(host_.state_mutex_);
    if (!PluginReady(plugin))
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (!adapter_.IsGameThread())
    {
        return KEEL_RESULT_WRONG_THREAD;
    }

    const std::string key = FieldCacheKey(host_.compatibility_profile_, *spec);
    std::shared_ptr<GameSchemaField> descriptor;
    {
        std::scoped_lock lock(registry_mutex_);
        if (shutting_down_.load(std::memory_order_acquire))
        {
            return KEEL_RESULT_NOT_READY;
        }
        const auto cached = field_cache_.find(key);
        if (cached != field_cache_.end())
        {
            descriptor = cached->second;
        }
    }
    if (!descriptor)
    {
        auto resolved = std::make_shared<GameSchemaField>();
        std::string error;
        const KeelResult result = adapter_.ResolveSchemaField(*spec, *resolved, error);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        std::scoped_lock lock(registry_mutex_);
        if (shutting_down_.load(std::memory_order_acquire))
        {
            return KEEL_RESULT_NOT_READY;
        }
        descriptor = field_cache_.emplace(key, std::move(resolved)).first->second;
    }

    std::scoped_lock lock(registry_mutex_);
    if (shutting_down_.load(std::memory_order_acquire) || next_field_ == 0)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
    const KeelSchemaFieldHandle handle = next_field_++;
    fields_.emplace(handle, FieldRecord{plugin, std::move(descriptor)});
    *output = handle;
    return KEEL_RESULT_OK;
}

KeelResult SchemaEntityService::ReleaseField(
    KeelPluginHandle plugin,
    KeelSchemaFieldHandle field)
{
    if (!plugin || !field)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock lock(registry_mutex_);
    const auto record = fields_.find(field);
    if (record == fields_.end() || record->second.owner != plugin)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    fields_.erase(record);
    return KEEL_RESULT_OK;
}

KeelResult SchemaEntityService::DescribeField(
    KeelPluginHandle plugin,
    KeelSchemaFieldHandle field,
    KeelSchemaFieldInfo* info)
{
    if (!info || info->size != sizeof(KeelSchemaFieldInfo) || !field)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    const std::uint32_t size = info->size;
    *info = {};
    info->size = size;
    std::scoped_lock state_lock(host_.state_mutex_);
    if (!PluginReady(plugin))
    {
        return KEEL_RESULT_NOT_READY;
    }
    std::scoped_lock lock(registry_mutex_);
    const auto record = fields_.find(field);
    if (record == fields_.end() || record->second.owner != plugin || !record->second.field)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    const GameSchemaField& value = *record->second.field;
    *info = {
        sizeof(KeelSchemaFieldInfo),
        value.module,
        value.value_type,
        value.value_size,
        value.value_alignment,
        value.offset,
        0,
        value.class_name.c_str(),
        value.field_name.c_str(),
        value.module_name.c_str(),
        value.compatibility_profile.c_str()
    };
    return KEEL_RESULT_OK;
}

KeelResult SchemaEntityService::FindEntityByIndex(
    KeelPluginHandle plugin,
    std::int32_t index,
    KeelEntityHandle* output)
{
    if (output)
    {
        *output = 0;
    }
    if (!output || index < 0)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock state_lock(host_.state_mutex_);
    if (!PluginReady(plugin))
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (!adapter_.IsGameThread())
    {
        return KEEL_RESULT_WRONG_THREAD;
    }
    GameEntityIdentity identity;
    std::string error;
    const KeelResult result = adapter_.FindEntityByIndex(index, identity, error);
    if (result != KEEL_RESULT_OK)
    {
        return result;
    }
    std::scoped_lock lock(registry_mutex_);
    if (shutting_down_.load(std::memory_order_acquire) || next_entity_ == 0)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
    const KeelEntityHandle handle = next_entity_++;
    entities_.emplace(handle, EntityRecord{plugin, identity});
    *output = handle;
    return KEEL_RESULT_OK;
}

KeelResult SchemaEntityService::FindEntityBySource2Handle(
    KeelPluginHandle plugin,
    std::uint32_t source2_handle,
    KeelEntityHandle* output)
{
    if (output)
    {
        *output = 0;
    }
    if (!output || source2_handle == KEELS2_INVALID_SOURCE2_ENTITY_HANDLE)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock state_lock(host_.state_mutex_);
    if (!PluginReady(plugin))
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (!adapter_.IsGameThread())
    {
        return KEEL_RESULT_WRONG_THREAD;
    }
    GameEntityIdentity identity;
    std::string error;
    const KeelResult result = adapter_.FindEntityBySource2Handle(
        source2_handle,
        identity,
        error);
    if (result != KEEL_RESULT_OK)
    {
        return result;
    }
    std::scoped_lock lock(registry_mutex_);
    if (shutting_down_.load(std::memory_order_acquire) || next_entity_ == 0)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
    const KeelEntityHandle handle = next_entity_++;
    entities_.emplace(handle, EntityRecord{plugin, identity});
    *output = handle;
    return KEEL_RESULT_OK;
}

KeelResult SchemaEntityService::ReleaseEntity(
    KeelPluginHandle plugin,
    KeelEntityHandle entity)
{
    if (!plugin || !entity)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock lock(registry_mutex_);
    const auto record = entities_.find(entity);
    if (record == entities_.end() || record->second.owner != plugin)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    entities_.erase(record);
    return KEEL_RESULT_OK;
}

KeelResult SchemaEntityService::DescribeEntity(
    KeelPluginHandle plugin,
    KeelEntityHandle entity,
    KeelEntityInfo* info)
{
    if (!info || info->size != sizeof(KeelEntityInfo) || !entity)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    const std::uint32_t size = info->size;
    *info = {};
    info->size = size;
    std::scoped_lock state_lock(host_.state_mutex_);
    if (!PluginReady(plugin))
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (!adapter_.IsGameThread())
    {
        return KEEL_RESULT_WRONG_THREAD;
    }
    GameEntityIdentity identity;
    {
        std::scoped_lock lock(registry_mutex_);
        const auto record = entities_.find(entity);
        if (record == entities_.end() || record->second.owner != plugin)
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        identity = record->second.entity;
    }
    std::string error;
    const KeelResult result = adapter_.ValidateEntity(identity, error);
    if (result != KEEL_RESULT_OK)
    {
        return result;
    }
    *info = {
        sizeof(KeelEntityInfo),
        identity.index,
        identity.source2_handle,
        0,
        identity.epoch
    };
    return KEEL_RESULT_OK;
}

KeelResult SchemaEntityService::EqualEntity(
    KeelPluginHandle plugin,
    KeelEntityHandle left,
    KeelEntityHandle right,
    KeelBool* equal)
{
    if (equal)
    {
        *equal = KEEL_FALSE;
    }
    if (!equal || !left || !right)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock state_lock(host_.state_mutex_);
    if (!PluginReady(plugin))
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (!adapter_.IsGameThread())
    {
        return KEEL_RESULT_WRONG_THREAD;
    }
    GameEntityIdentity left_identity;
    GameEntityIdentity right_identity;
    {
        std::scoped_lock lock(registry_mutex_);
        const auto left_record = entities_.find(left);
        const auto right_record = entities_.find(right);
        if (left_record == entities_.end() || right_record == entities_.end() ||
            left_record->second.owner != plugin || right_record->second.owner != plugin)
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        left_identity = left_record->second.entity;
        right_identity = right_record->second.entity;
    }
    std::string error;
    KeelResult result = adapter_.ValidateEntity(left_identity, error);
    if (result != KEEL_RESULT_OK)
    {
        return result;
    }
    result = adapter_.ValidateEntity(right_identity, error);
    if (result != KEEL_RESULT_OK)
    {
        return result;
    }
    *equal = left_identity.epoch == right_identity.epoch &&
            left_identity.source2_handle == right_identity.source2_handle
        ? KEEL_TRUE
        : KEEL_FALSE;
    return KEEL_RESULT_OK;
}

KeelResult SchemaEntityService::ReadEntityField(
    KeelPluginHandle plugin,
    KeelEntityHandle entity,
    KeelSchemaFieldHandle field,
    void* value,
    std::uint32_t value_size)
{
    if (!value || !value_size || !entity || !field)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::memset(value, 0, value_size);
    std::scoped_lock state_lock(host_.state_mutex_);
    if (!PluginReady(plugin))
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (!adapter_.IsGameThread())
    {
        return KEEL_RESULT_WRONG_THREAD;
    }
    GameEntityIdentity identity;
    std::shared_ptr<GameSchemaField> descriptor;
    {
        std::scoped_lock lock(registry_mutex_);
        const auto entity_record = entities_.find(entity);
        const auto field_record = fields_.find(field);
        if (entity_record == entities_.end() || field_record == fields_.end() ||
            entity_record->second.owner != plugin || field_record->second.owner != plugin ||
            !field_record->second.field)
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        identity = entity_record->second.entity;
        descriptor = field_record->second.field;
    }
    if (value_size != descriptor->value_size)
    {
        return KEEL_RESULT_INCOMPATIBLE;
    }
    std::string error;
    return adapter_.ReadEntityField(
        identity,
        *descriptor,
        value,
        value_size,
        error);
}

bool SchemaEntityService::PluginReady(KeelPluginHandle plugin) const noexcept
{
    PluginRecord* owner = host_.PluginByHandle(plugin);
    return !shutting_down_.load(std::memory_order_acquire) &&
        host_.accepting_resources_ && owner &&
        owner->accepting_resources;
}

bool SchemaEntityService::ValidSchemaName(const char* name) noexcept
{
    if (!name || !name[0])
    {
        return false;
    }
    std::size_t length{};
    for (; name[length]; ++length)
    {
        if (length >= 255)
        {
            return false;
        }
        const unsigned char character = static_cast<unsigned char>(name[length]);
        if (!std::isalnum(character) && character != '_' && character != ':')
        {
            return false;
        }
    }
    return true;
}

bool SchemaEntityService::ValidValueType(KeelSchemaValueType type) noexcept
{
    return type >= KEELS2_SCHEMA_CHAR && type <= KEELS2_SCHEMA_BOOL;
}

std::string SchemaEntityService::FieldCacheKey(
    const std::string& profile,
    const KeelSchemaFieldSpec& spec)
{
    std::string key;
    key.reserve(profile.size() + std::strlen(spec.class_name) +
        std::strlen(spec.field_name) + 32);
    key.append(profile);
    key.push_back('\x1f');
    key.append(std::to_string(spec.module));
    key.push_back('\x1f');
    key.append(spec.class_name);
    key.push_back('\x1f');
    key.append(spec.field_name);
    key.push_back('\x1f');
    key.append(std::to_string(spec.value_type));
    return key;
}

}
