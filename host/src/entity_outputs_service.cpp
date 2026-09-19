#include "schema_entity_service.h"
#include "host.h"
#include "game_adapter_loader.h"
#include "keelhook_service.h"
#include <algorithm>
#include <cstring>

namespace keels2::host {
namespace {
bool OutputName(const char* text, std::size_t limit, std::string& copy)
{
    copy.clear();
    if (!text) return true;
    std::size_t size{};
    while (size <= limit && text[size]) {
        const auto byte = static_cast<unsigned char>(text[size]);
        if (byte < 32 || byte == 127) return false;
        ++size;
    }
    if (size > limit) return false;
    copy.assign(text,size); return true;
}
bool SameOutputEntity(const GameEntityIdentity& a, const GameEntityIdentity& b)
{
    return a.index == b.index && a.source2_handle == b.source2_handle && a.epoch == b.epoch;
}
}
const KeelEntityOutputsApi& SchemaEntityService::EntityOutputsApi() const noexcept { return entity_outputs_api_; }
KeelResult SchemaEntityService::OutputsReadyEntry(KeelPluginHandle plugin)
{
    try {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->ConfigureOutputs(plugin,nullptr,nullptr) : KEEL_RESULT_NOT_READY;
    } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
KeelResult SchemaEntityService::SubscribeOutputEntry(KeelPluginHandle plugin, const KeelEntityOutputSpec* spec, KeelEntityOutputHandle* output)
{
    if (output) *output = 0;
    if (!spec || !output || spec->size != sizeof(*spec)) return KEEL_RESULT_INVALID_ARGUMENT;
    try {
        auto copy = *spec; std::string classname, name;
        if (!copy.callback || !copy.phases || (copy.phases & ~KEELS2_OUTPUT_BOTH) || copy.reserved ||
            !OutputName(copy.class_name,255,classname) || !OutputName(copy.output_name,127,name)) return KEEL_RESULT_INVALID_ARGUMENT;
        copy.class_name = classname.c_str(); copy.output_name = name.c_str();
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->ConfigureOutputs(plugin,&copy,output) : KEEL_RESULT_NOT_READY;
    } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
KeelResult SchemaEntityService::UnsubscribeOutputEntry(KeelPluginHandle plugin, KeelEntityOutputHandle handle)
{
    if (!plugin || !handle) return KEEL_RESULT_INVALID_ARGUMENT;
    try {
        auto* service = active_.load(std::memory_order_acquire);
        if (!service) return KEEL_RESULT_NOT_READY;
        std::scoped_lock state_lock(service->host_.state_mutex_);
        if (!service->adapter_.IsGameThread()) return KEEL_RESULT_WRONG_THREAD;
        const auto found = service->outputs_.find(handle);
        if (found == service->outputs_.end() || found->second->owner != plugin) return KEEL_RESULT_NOT_FOUND;
        found->second->enabled = false; service->outputs_.erase(found); return KEEL_RESULT_OK;
    } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
KeelResult SchemaEntityService::ValidateOutputIdentity(const GameEntityIdentity& identity, const std::weak_ptr<Construction>& construction)
{
    const auto pending = construction.lock();
    if (pending && pending->closed.load(std::memory_order_acquire)) return KEEL_RESULT_NOT_FOUND;
    if (pending && pending->token) {
        if (pending->initializing) return KEEL_RESULT_BUSY;
        const auto api = host_.adapter_module_ ? host_.adapter_module_->EntityConstruction() : GameAdapterEntityConstructionApi{};
        if (!api.describe) return KEEL_RESULT_UNSUPPORTED;
        GameEntityIdentity current{};
        const auto result = api.describe(&adapter_,pending->token,&current);
        if (pending->closed.load(std::memory_order_acquire)) return KEEL_RESULT_NOT_FOUND;
        return result != KEEL_RESULT_OK ? result : SameOutputEntity(current,identity) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }
    std::string error;
    return adapter_.ValidateEntity(identity,error);
}
void SchemaEntityService::PruneOutputs(std::uint64_t epoch)
{
    std::vector<std::shared_ptr<OutputRecord>> records;
    for (const auto& [handle,record] : outputs_) {
        static_cast<void>(handle);
        if (record->entity.epoch) records.push_back(record);
    }
    // Adapter validation can call back into the host. Retain records separately
    // and never carry unordered_map iterators across those calls.
    for (const auto& record : records) {
        const bool stale = epoch ? record->entity.epoch != epoch :
            ValidateOutputIdentity(record->entity,record->construction) == KEEL_RESULT_NOT_FOUND;
        if (stale) { record->enabled = false; outputs_.erase(record->handle); }
    }
}
KeelResult SchemaEntityService::ConfigureOutputs(KeelPluginHandle plugin, const KeelEntityOutputSpec* spec, KeelEntityOutputHandle* output)
{
    std::scoped_lock state_lock(host_.state_mutex_);
    if (!PluginReady(plugin) || outputs_stopping_) return KEEL_RESULT_NOT_READY;
    if (!adapter_.IsGameThread()) return KEEL_RESULT_WRONG_THREAD;
    auto* owner = host_.PluginByHandle(plugin);
    if (!owner || owner->transitioning || owner->cleanup_pending || owner->active_native_operations == UINT32_MAX ||
        outputs_starting_ || output_depth_ >= KEELS2_OUTPUT_MAX_DEPTH) return KEEL_RESULT_BUSY;
    ++owner->active_native_operations; ++output_depth_;
    struct Hold { std::uint32_t& active; unsigned& depth; ~Hold() { --active; --depth; } } hold{owner->active_native_operations,output_depth_};
    const auto api = host_.adapter_module_ ? host_.adapter_module_->EntityOutputs() : GameAdapterEntityOutputsApi{};
    if (!api.start || !api.stop) return KEEL_RESULT_UNSUPPORTED;
    EntityRecord captured;
    if (spec && spec->entity) {
        std::scoped_lock registry_lock(registry_mutex_);
        const auto found = entities_.find(spec->entity);
        if (found == entities_.end() || !EntityAccessible(found->second,plugin)) return KEEL_RESULT_NOT_FOUND;
        captured = found->second;
    }
    if (!host_.keelhook_) {
        host_.keelhook_ = std::make_unique<KeelHookService>(host_);
#if defined(_WIN32)
        constexpr const char* host_name = "keels2_host.dll";
#else
        constexpr const char* host_name = "libkeels2_host.so";
#endif
        host_.keelhook_->Authorize(0,host_.bootstrap_directory_/host_name,true);
    }
    {
        outputs_starting_ = true;
        struct Starting { bool& flag; ~Starting() { flag = false; } } starting{outputs_starting_};
        if (outputs_retained_ && !outputs_started_) {
            const auto result = api.stop(&adapter_);
            if (result != KEEL_RESULT_OK) return result;
            outputs_retained_ = false;
        }
        outputs_retained_ = true;
        const auto result = api.start(&adapter_,&host_.keelhook_->Api(),&KeelHookService::DeferInternal,&DispatchOutputsEntry,this);
        if (result != KEEL_RESULT_OK) {
            if (!outputs_started_ && api.stop(&adapter_) == KEEL_RESULT_OK) outputs_retained_ = false;
            return result;
        }
        outputs_started_ = true;
    }
    PruneOutputs();
    if (!PluginReady(plugin) || outputs_stopping_) return KEEL_RESULT_NOT_READY;
    if (!spec) return KEEL_RESULT_OK;
    if (spec->entity) {
        const auto result = ValidateOutputIdentity(captured.entity,captured.construction);
        if (result != KEEL_RESULT_OK) return result;
        std::scoped_lock registry_lock(registry_mutex_);
        const auto found = entities_.find(spec->entity);
        if (found == entities_.end() || !EntityAccessible(found->second,plugin) ||
            !SameOutputEntity(found->second.entity,captured.entity)) return KEEL_RESULT_NOT_FOUND;
    }
    if (!PluginReady(plugin) || outputs_stopping_) return KEEL_RESULT_NOT_READY;
    const auto count = std::count_if(outputs_.begin(),outputs_.end(),[plugin](const auto& pair) { return pair.second->owner == plugin; });
    if (!next_output_ || count >= KEELS2_OUTPUT_MAX_SUBSCRIPTIONS || outputs_.size() >= KEELS2_OUTPUT_MAX_HOST_SUBSCRIPTIONS) return KEEL_RESULT_BUSY;
    auto record = std::make_shared<OutputRecord>();
    record->handle = next_output_++; record->owner = plugin; record->phases = spec->phases; record->priority = spec->priority;
    if (spec->entity) { record->entity = captured.entity; record->construction = captured.construction; }
    record->class_name = spec->class_name; record->output_name = spec->output_name;
    record->callback = spec->callback; record->user_data = spec->user_data;
    outputs_.emplace(record->handle,record); *output = record->handle; return KEEL_RESULT_OK;
}
void SchemaEntityService::ReleaseOutputs(KeelPluginHandle plugin)
{
    std::erase_if(outputs_,[plugin](const auto& pair) {
        if (pair.second->owner != plugin) return false;
        pair.second->enabled = false; return true;
    });
}
bool SchemaEntityService::StopOutputs()
{
    outputs_stopping_ = true;
    for (auto& [handle,record] : outputs_) { static_cast<void>(handle); record->enabled = false; }
    outputs_.clear();
    if (outputs_starting_ || output_depth_ || !output_invocations_.empty()) return false;
    if (outputs_retained_) {
        const auto api = host_.adapter_module_ ? host_.adapter_module_->EntityOutputs() : GameAdapterEntityOutputsApi{};
        if (!api.stop || api.stop(&adapter_) != KEEL_RESULT_OK) return false;
        outputs_retained_ = outputs_started_ = false;
    }
    return true;
}
std::uint32_t SchemaEntityService::DispatchOutputsEntry(const KeelEntityOutputEvent* event, std::uint64_t invocation, void* data)
{
    try { return static_cast<SchemaEntityService*>(data)->DispatchOutputs(event,invocation); }
    catch (...) { return event && event->phase == KEELS2_OUTPUT_PRE ? KEELS2_OUTPUT_BLOCK : KEELS2_OUTPUT_CONTINUE; }
}
std::uint32_t SchemaEntityService::DispatchOutputs(const KeelEntityOutputEvent* event, std::uint64_t token)
{
    std::scoped_lock state_lock(host_.state_mutex_);
    if (!event) { output_invocations_.erase(token); return KEELS2_OUTPUT_CONTINUE; }
    if (!token || event->size != sizeof(*event) || outputs_stopping_ || !adapter_.IsGameThread()) return KEELS2_OUTPUT_CONTINUE;
    if (output_depth_ >= KEELS2_OUTPUT_MAX_DEPTH) return event->phase == KEELS2_OUTPUT_PRE ? KEELS2_OUTPUT_BLOCK : KEELS2_OUTPUT_CONTINUE;
    ++output_depth_;
    struct Depth { unsigned& value; ~Depth() { --value; } } depth{output_depth_};
    std::shared_ptr<OutputInvocation> invocation;
    if (event->phase == KEELS2_OUTPUT_PRE) {
        if (output_invocations_.contains(token) || output_invocations_.size() >= 64) return KEELS2_OUTPUT_BLOCK;
        PruneOutputs(event->entity.epoch);
        invocation = std::make_shared<OutputInvocation>();
        const GameEntityIdentity identity{event->entity.index,event->entity.source2_handle,event->entity.epoch};
        for (const auto& [handle,record] : outputs_) {
            static_cast<void>(handle);
            if (record->enabled && (!record->entity.epoch || SameOutputEntity(record->entity,identity)) &&
                (record->class_name.empty() || record->class_name == event->class_name) &&
                (record->output_name.empty() || record->output_name == event->output_name)) invocation->callbacks.push_back(record);
        }
        std::sort(invocation->callbacks.begin(),invocation->callbacks.end(),[](const auto& a, const auto& b) {
            return a->priority != b->priority ? a->priority > b->priority : a->handle < b->handle;
        });
        output_invocations_.emplace(token,invocation);
    } else if (event->phase == KEELS2_OUTPUT_POST) {
        const auto found = output_invocations_.find(token);
        if (found == output_invocations_.end()) return KEELS2_OUTPUT_CONTINUE;
        invocation = found->second;
    } else return KEELS2_OUTPUT_CONTINUE;
    bool block{};
    const auto invoke = [&](const std::shared_ptr<OutputRecord>& record) {
        if (!record->enabled || !(record->phases & event->phase) || !PluginReady(record->owner)) return;
        auto* owner = host_.PluginByHandle(record->owner);
        if (!owner || owner->state != PluginState::loaded || owner->transitioning || owner->cleanup_pending ||
            owner->active_native_operations == UINT32_MAX) return;
        ++owner->active_native_operations;
        struct Hold { std::uint32_t& value; ~Hold() { --value; } } hold{owner->active_native_operations};
        auto observation = *event; std::uint32_t action = UINT32_MAX;
        try { action = record->callback(&observation,record->user_data); } catch (...) {}
        if (action == KEELS2_OUTPUT_BLOCK && event->phase == KEELS2_OUTPUT_PRE) block = true;
        else if (action != KEELS2_OUTPUT_CONTINUE) {
            record->enabled = false; outputs_.erase(record->handle);
            if (event->phase == KEELS2_OUTPUT_PRE) block = true;
        }
    };
    if (event->phase == KEELS2_OUTPUT_PRE) for (const auto& record : invocation->callbacks) invoke(record);
    else for (auto iterator = invocation->callbacks.rbegin(); iterator != invocation->callbacks.rend(); ++iterator) invoke(*iterator);
    return block ? KEELS2_OUTPUT_BLOCK : KEELS2_OUTPUT_CONTINUE;
}
}
