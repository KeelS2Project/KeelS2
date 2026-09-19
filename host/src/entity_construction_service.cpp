#include "schema_entity_service.h"
#include "host.h"
#include "game_adapter_loader.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace keels2::host {
namespace {
bool CopyText(const char* input, std::size_t maximum, std::string& output, bool allow_empty = false)
{
    if (!input) return false;
    std::size_t length{};
    while (length <= maximum && input[length]) ++length;
    if (length > maximum || (!length && !allow_empty)) return false;
    output.assign(input,length); return true;
}
bool SameEntity(const GameEntityIdentity& left, const GameEntityIdentity& right)
{
    return left.index == right.index && left.source2_handle == right.source2_handle && left.epoch == right.epoch;
}
bool ValidIdentity(const GameEntityIdentity& identity)
{
    return identity.index >= 0 && identity.epoch && identity.source2_handle != KEELS2_INVALID_SOURCE2_ENTITY_HANDLE;
}
}
class SchemaEntityService::ConstructionOperation
{
public:
    ConstructionOperation(SchemaEntityService& service, KeelPluginHandle plugin)
        : service_(service), lock_(service.host_.state_mutex_)
    {
        if (!service_.PluginReady(plugin)) { result_ = KEEL_RESULT_NOT_READY; return; }
        if (!service_.adapter_.IsGameThread()) { result_ = KEEL_RESULT_WRONG_THREAD; return; }
        auto* owner = service_.host_.PluginByHandle(plugin);
        if (!owner || owner->cleanup_pending || owner->transitioning || owner->active_native_operations == UINT32_MAX ||
            service_.construction_depth_ >= KEELS2_ENTITY_CONSTRUCTION_MAX_DEPTH) { result_ = KEEL_RESULT_BUSY; return; }
        owner_ = owner; ++owner_->active_native_operations; ++service_.construction_depth_;
        if (!service_.host_.adapter_module_) { result_ = KEEL_RESULT_UNSUPPORTED; return; }
        api = service_.host_.adapter_module_->EntityConstruction();
        if (!api.ready) result_ = KEEL_RESULT_UNSUPPORTED;
    }
    ~ConstructionOperation() { if (owner_) { --owner_->active_native_operations; --service_.construction_depth_; } }
    KeelResult Result() const { return result_; }
    GameAdapterEntityConstructionApi api{};
private:
    SchemaEntityService& service_;
    std::unique_lock<std::recursive_mutex> lock_;
    PluginRecord* owner_{};
    KeelResult result_{KEEL_RESULT_OK};
};
bool SchemaEntityService::EntityAccessible(const EntityRecord& record, KeelPluginHandle plugin) noexcept
{
    return record.owner == plugin && (!record.construction || !record.construction->closed.load(std::memory_order_acquire));
}
const KeelEntityConstructionApi& SchemaEntityService::EntityConstructionApi() const noexcept { return entity_construction_api_; }
KeelResult SchemaEntityService::ConstructionReadyEntry(KeelPluginHandle plugin)
{
    try { auto* service = active_.load(std::memory_order_acquire); return service ? service->ConstructionReady(plugin) : KEEL_RESULT_NOT_READY; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
KeelResult SchemaEntityService::CreateEntityEntry(KeelPluginHandle plugin, const char* name, KeelEntityHandle* output)
{
    if (output) *output = 0;
    try { auto* service = active_.load(std::memory_order_acquire); return service ? service->CreateEntity(plugin,name,output) : KEEL_RESULT_NOT_READY; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
KeelResult SchemaEntityService::DescribeConstructionEntry(KeelPluginHandle plugin, KeelEntityHandle entity, KeelEntityInfo* info)
{
    const bool sized = info && info->size == sizeof(*info);
    if (info) { *info = {}; info->size = sizeof(*info); }
    if (!sized || !entity) return KEEL_RESULT_INVALID_ARGUMENT;
    try { auto* service = active_.load(std::memory_order_acquire); return service ? service->DescribeConstruction(plugin,entity,info) : KEEL_RESULT_NOT_READY; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
KeelResult SchemaEntityService::SetConstructionEntry(KeelPluginHandle plugin, KeelEntityHandle entity, const KeelEntityKeyValue* input)
{
    if (!input || input->size != sizeof(*input) || !entity) return KEEL_RESULT_INVALID_ARGUMENT;
    try {
        auto value = *input; std::string name, text;
        if (!CopyText(value.name,KEELS2_ENTITY_KEY_MAX_NAME,name) || value.type < KEELS2_ENTITY_KEY_STRING || value.type > KEELS2_ENTITY_KEY_COLOR)
            return KEEL_RESULT_INVALID_ARGUMENT;
        if (value.type == KEELS2_ENTITY_KEY_STRING && !CopyText(value.string_value,KEELS2_ENTITY_KEY_MAX_STRING,text,true)) return KEEL_RESULT_INVALID_ARGUMENT;
        if (value.type == KEELS2_ENTITY_KEY_BOOL && value.int_value != 0 && value.int_value != 1) return KEEL_RESULT_INVALID_ARGUMENT;
        if (value.type == KEELS2_ENTITY_KEY_FLOAT && !std::isfinite(value.float_value)) return KEEL_RESULT_INVALID_ARGUMENT;
        if (value.type == KEELS2_ENTITY_KEY_VECTOR || value.type == KEELS2_ENTITY_KEY_ANGLES)
            for (const float v : value.vector_value) if (!std::isfinite(v)) return KEEL_RESULT_INVALID_ARGUMENT;
        value.name = name.c_str(); value.string_value = text.c_str();
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->ChangeConstruction(plugin,entity,&value,nullptr,nullptr) : KEEL_RESULT_NOT_READY;
    } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
KeelResult SchemaEntityService::TeleportConstructionEntry(KeelPluginHandle plugin, KeelEntityHandle entity, const KeelEntityTeleport* input)
{
    if (!input || input->size != sizeof(*input) || !entity) return KEEL_RESULT_INVALID_ARGUMENT;
    const auto request = *input;
    if (!request.flags || (request.flags & ~7u)) return KEEL_RESULT_INVALID_ARGUMENT;
    const float* vectors[]{request.position,request.angles,request.velocity};
    for (unsigned i = 0; i < 3; ++i) if (request.flags & (1u<<i))
        for (unsigned j = 0; j < 3; ++j) if (!std::isfinite(vectors[i][j])) return KEEL_RESULT_INVALID_ARGUMENT;
    try { auto* service = active_.load(std::memory_order_acquire); return service ? service->ChangeConstruction(plugin,entity,nullptr,&request,nullptr) : KEEL_RESULT_NOT_READY; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
KeelResult SchemaEntityService::SpawnConstructionEntry(KeelPluginHandle plugin, KeelEntityHandle entity, KeelBool* invoked)
{
    if (invoked) *invoked = KEEL_FALSE;
    if (!entity || !invoked) return KEEL_RESULT_INVALID_ARGUMENT;
    try { auto* service = active_.load(std::memory_order_acquire); return service ? service->ChangeConstruction(plugin,entity,nullptr,nullptr,invoked) : KEEL_RESULT_NOT_READY; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
KeelResult SchemaEntityService::ObserveConstructionEntry(KeelPluginHandle plugin, std::uint32_t source, KeelEntityHandle* output)
{
    if (output) *output = 0;
    if (!output || source == KEELS2_INVALID_SOURCE2_ENTITY_HANDLE) return KEEL_RESULT_INVALID_ARGUMENT;
    try { auto* service = active_.load(std::memory_order_acquire); return service ? service->ObserveConstruction(plugin,source,output) : KEEL_RESULT_NOT_READY; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
KeelResult SchemaEntityService::VisitConstructionEntry(KeelPluginHandle plugin, KeelEntityHandle entity, const char* name,
    KeelEntityAccessCallback callback, void* data)
{
    if (!entity || !callback) return KEEL_RESULT_INVALID_ARGUMENT;
    try {
        std::string copied;
        if (!CopyText(name,255,copied) || !ValidSchemaName(copied.c_str())) return KEEL_RESULT_INVALID_ARGUMENT;
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->VisitConstruction(plugin,entity,copied.c_str(),callback,data) : KEEL_RESULT_NOT_READY;
    } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
KeelResult SchemaEntityService::ConstructionReady(KeelPluginHandle plugin)
{
    ConstructionOperation operation(*this,plugin);
    return operation.Result() == KEEL_RESULT_OK ? operation.api.ready(&adapter_) : operation.Result();
}
void SchemaEntityService::PruneConstructions(KeelPluginHandle plugin)
{
    std::vector<std::shared_ptr<Construction>> pending;
    {
        std::scoped_lock lock(registry_mutex_);
        for (const auto& [handle,record] : entities_) {
            static_cast<void>(handle);
            if (record.owner == plugin && record.construction_owner && record.construction && record.construction->token &&
                !record.construction->initializing && !record.construction->busy) pending.push_back(record.construction);
        }
    }
    const auto api = host_.adapter_module_->EntityConstruction();
    for (const auto& state : pending) {
        GameEntityIdentity identity{};
        const auto result = api.describe(&adapter_,state->token,&identity);
        // Drop the adapter record as well: a stale identity can still occupy
        // its pending quota until cancellation detaches the token.
        if (result == KEEL_RESULT_NOT_FOUND) static_cast<void>(CancelConstruction(state));
    }
}
KeelResult SchemaEntityService::CreateEntity(KeelPluginHandle plugin, const char* input, KeelEntityHandle* output)
{
    if (!output) return KEEL_RESULT_INVALID_ARGUMENT;
    std::string name;
    if (!CopyText(input,KEELS2_ENTITY_KEY_MAX_NAME,name) || !std::all_of(name.begin(),name.end(),[](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    })) return KEEL_RESULT_INVALID_ARGUMENT;
    ConstructionOperation operation(*this,plugin);
    if (operation.Result() != KEEL_RESULT_OK) return operation.Result();
    auto result = operation.api.ready(&adapter_);
    if (result != KEEL_RESULT_OK) return result;
    PruneConstructions(plugin);
    auto state = std::make_shared<Construction>(); state->thread = std::this_thread::get_id(); state->owner = plugin;
    KeelEntityHandle handle{};
    {
        std::scoped_lock lock(registry_mutex_);
        const auto count = std::count_if(entities_.begin(),entities_.end(),[plugin](const auto& item) {
            const auto& record = item.second;
            return record.owner == plugin && record.construction_owner && record.construction &&
                !record.construction->closed.load(std::memory_order_acquire) && (record.construction->initializing || record.construction->token);
        });
        if (count >= KEELS2_ENTITY_CONSTRUCTION_MAX_PENDING || !next_entity_) return KEEL_RESULT_BUSY;
        handle = next_entity_++; entities_.emplace(handle,EntityRecord{plugin,{},state,true});
    }
    GameEntityIdentity identity{}; std::uint64_t token{};
    result = operation.api.create(&adapter_,name.c_str(),&token,&identity);
    const bool valid = result == KEEL_RESULT_OK && token && ValidIdentity(identity);
    bool accepted{};
    {
        std::scoped_lock lock(registry_mutex_);
        state->token = token; state->initializing = false;
        const auto found = entities_.find(handle);
        accepted = valid && !state->closed.load(std::memory_order_acquire) && PluginReady(plugin) && found != entities_.end();
        if (accepted) found->second.entity = identity;
        else { if (found != entities_.end()) entities_.erase(found); state->closed.store(true,std::memory_order_release); }
    }
    if (!accepted) {
        static_cast<void>(CancelConstruction(state));
        if (result != KEEL_RESULT_OK) return result;
        return valid ? KEEL_RESULT_NOT_FOUND : KEEL_RESULT_INCOMPATIBLE;
    }
    *output = handle; return KEEL_RESULT_OK;
}
KeelResult SchemaEntityService::DescribeConstruction(KeelPluginHandle plugin, KeelEntityHandle entity, KeelEntityInfo* info)
{
    ConstructionOperation operation(*this,plugin);
    if (operation.Result() != KEEL_RESULT_OK) return operation.Result();
    EntityRecord record;
    {
        std::scoped_lock lock(registry_mutex_);
        const auto found = entities_.find(entity);
        if (found == entities_.end() || !EntityAccessible(found->second,plugin) || !found->second.construction) return KEEL_RESULT_NOT_FOUND;
        record = found->second;
        if (record.construction->initializing) return KEEL_RESULT_BUSY;
        if (!record.construction->token) return KEEL_RESULT_NOT_FOUND;
    }
    GameEntityIdentity identity{};
    const auto result = operation.api.describe(&adapter_,record.construction->token,&identity);
    if (record.construction->closed.load(std::memory_order_acquire)) return KEEL_RESULT_NOT_FOUND;
    if (result != KEEL_RESULT_OK) return result;
    if (!SameEntity(record.entity,identity)) return KEEL_RESULT_INCOMPATIBLE;
    *info = {sizeof(*info),identity.index,identity.source2_handle,0,identity.epoch}; return KEEL_RESULT_OK;
}
KeelResult SchemaEntityService::ChangeConstruction(KeelPluginHandle plugin, KeelEntityHandle entity,
    const KeelEntityKeyValue* value, const KeelEntityTeleport* request, KeelBool* invoked)
{
    ConstructionOperation operation(*this,plugin);
    if (operation.Result() != KEEL_RESULT_OK) return operation.Result();
    std::shared_ptr<Construction> state; std::uint64_t token{};
    {
        std::scoped_lock lock(registry_mutex_);
        const auto found = entities_.find(entity);
        if (found == entities_.end() || !EntityAccessible(found->second,plugin) || !found->second.construction_owner || !found->second.construction)
            return KEEL_RESULT_NOT_FOUND;
        state = found->second.construction;
        if (state->initializing || state->busy) return KEEL_RESULT_BUSY;
        token = state->token; if (!token) return KEEL_RESULT_NOT_FOUND;
        state->busy = true;
    }
    struct Busy { SchemaEntityService& service; std::shared_ptr<Construction> state;
        ~Busy() { std::scoped_lock lock(service.registry_mutex_); state->busy = false; } } busy{*this,state};
    if (value) return operation.api.set(&adapter_,token,value);
    if (request) return operation.api.teleport(&adapter_,token,request);
    KeelBool dispatched{};
    const auto result = operation.api.spawn(&adapter_,token,&dispatched);
    if (dispatched) { std::scoped_lock lock(registry_mutex_); state->token = 0; }
    *invoked = dispatched ? KEEL_TRUE : KEEL_FALSE;
    return dispatched > KEEL_TRUE ? KEEL_RESULT_INCOMPATIBLE : result;
}
KeelResult SchemaEntityService::ObserveConstruction(KeelPluginHandle plugin, std::uint32_t source, KeelEntityHandle* output)
{
    ConstructionOperation operation(*this,plugin);
    if (operation.Result() != KEEL_RESULT_OK) return operation.Result();
    EntityRecord original; bool found_original{};
    {
        std::scoped_lock lock(registry_mutex_);
        for (const auto& [handle,record] : entities_) {
            static_cast<void>(handle);
            if (record.construction_owner && record.construction && !record.construction->initializing && record.construction->token &&
                !record.construction->closed.load(std::memory_order_acquire) && record.entity.source2_handle == source) {
                original = record; found_original = true; break;
            }
        }
    }
    if (!found_original) return KEEL_RESULT_NOT_FOUND;
    const auto token = original.construction->token;
    GameEntityIdentity identity{};
    const auto result = operation.api.describe(&adapter_,token,&identity);
    if (result != KEEL_RESULT_OK) return result;
    if (!SameEntity(original.entity,identity)) return KEEL_RESULT_INCOMPATIBLE;
    std::scoped_lock lock(registry_mutex_);
    if (original.construction->closed.load(std::memory_order_acquire) || original.construction->token != token || !PluginReady(plugin)) return KEEL_RESULT_NOT_FOUND;
    const auto count = std::count_if(entities_.begin(),entities_.end(),[plugin](const auto& item) {
        return item.second.owner == plugin && item.second.construction && !item.second.construction_owner;
    });
    if (count >= KEELS2_ENTITY_CONSTRUCTION_MAX_OBSERVERS || !next_entity_) return KEEL_RESULT_BUSY;
    const auto handle = next_entity_++;
    entities_.emplace(handle,EntityRecord{plugin,identity,original.construction,false}); *output = handle; return KEEL_RESULT_OK;
}
KeelResult SchemaEntityService::VisitConstruction(KeelPluginHandle plugin, KeelEntityHandle entity, const char* name,
    KeelEntityAccessCallback callback, void* data)
{
    ConstructionOperation operation(*this,plugin);
    if (operation.Result() != KEEL_RESULT_OK) return operation.Result();
    std::shared_ptr<Construction> state; std::uint64_t token{};
    {
        std::scoped_lock lock(registry_mutex_);
        const auto found = entities_.find(entity);
        if (found == entities_.end() || !EntityAccessible(found->second,plugin) || !found->second.construction) return KEEL_RESULT_NOT_FOUND;
        state = found->second.construction;
        if (state->initializing) return KEEL_RESULT_BUSY;
        token = state->token; if (!token) return KEEL_RESULT_NOT_FOUND;
    }
    return operation.api.visit(&adapter_,token,name,callback,data);
}
KeelResult SchemaEntityService::CancelConstruction(const std::shared_ptr<Construction>& state)
{
    auto* owner = host_.PluginByHandle(state->owner);
    // Cleanup may be called by a guarded operation at the counter limit.
    // Its existing hold already prevents unload; never abandon a token here.
    auto* incremented = owner && owner->active_native_operations != UINT32_MAX ? owner : nullptr;
    if (incremented) ++incremented->active_native_operations;
    struct Hold { PluginRecord* owner; ~Hold() { if (owner) --owner->active_native_operations; } } hold{incremented};
    std::uint64_t token{};
    {
        std::scoped_lock lock(registry_mutex_);
        state->closed.store(true,std::memory_order_release); token = state->token; state->token = 0;
    }
    if (!token) return KEEL_RESULT_OK;
    const auto api = host_.adapter_module_ ? host_.adapter_module_->EntityConstruction() : GameAdapterEntityConstructionApi{};
    const auto result = api.cancel ? api.cancel(&adapter_,token) : KEEL_RESULT_UNSUPPORTED;
    return result == KEEL_RESULT_NOT_FOUND ? KEEL_RESULT_OK : result;
}
}
