#include "schema_entity_service.h"
#include "host.h"
#include "game_adapter_loader.h"
#include <keels2/detail/entity_input_copy.hpp>

namespace keels2::host {
const KeelEntityInputApi& SchemaEntityService::EntityInputApi() const noexcept { return entity_input_api_; }
KeelResult SchemaEntityService::InputCapabilitiesEntry(KeelPluginHandle plugin, std::uint32_t* direct, std::uint32_t* queued)
{
    if (direct) *direct = 0;
    if (queued) *queued = 0;
    if (!direct || !queued) return KEEL_RESULT_INVALID_ARGUMENT;
    try {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->EntityInput(plugin,0,nullptr,direct,queued,nullptr) : KEEL_RESULT_NOT_READY;
    } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
KeelResult SchemaEntityService::DispatchInputEntry(KeelPluginHandle plugin, KeelEntityHandle target,
    const KeelEntityInputRequest* request, KeelBool* invoked)
{
    if (invoked) *invoked = KEEL_FALSE;
    if (!target || !request || !invoked || request->size != sizeof(*request)) return KEEL_RESULT_INVALID_ARGUMENT;
    const auto snapshot = *request;
    if ((snapshot.value.type == KEELS2_INPUT_ENTITY) != (snapshot.value_entity != 0)) return KEEL_RESULT_INVALID_ARGUMENT;
    detail::EntityInputCopy copied;
    const auto result = copied.Assign(snapshot.input,snapshot.value,snapshot.queued,snapshot.delay);
    if (result != KEEL_RESULT_OK) return result;
    auto owned = snapshot; owned.input = copied.name.data(); owned.value = copied.value;
    try {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->EntityInput(plugin,target,&owned,nullptr,nullptr,invoked) : KEEL_RESULT_NOT_READY;
    } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
KeelResult SchemaEntityService::EntityInput(KeelPluginHandle plugin, KeelEntityHandle target,
    const KeelEntityInputRequest* request, std::uint32_t* direct, std::uint32_t* queued, KeelBool* invoked)
{
    std::scoped_lock state_lock(host_.state_mutex_);
    if (!PluginReady(plugin)) return KEEL_RESULT_NOT_READY;
    if (!adapter_.IsGameThread()) return KEEL_RESULT_WRONG_THREAD;
    auto* owner = host_.PluginByHandle(plugin);
    if (!owner || owner->cleanup_pending || owner->transitioning || owner->active_native_operations == UINT32_MAX ||
        entity_input_depth_ >= KEELS2_INPUT_MAX_DEPTH) return KEEL_RESULT_BUSY;
    ++owner->active_native_operations; ++entity_input_depth_;
    struct Hold { std::uint32_t& active; unsigned& depth; ~Hold() { --active; --depth; } } hold{owner->active_native_operations,entity_input_depth_};
    if (!host_.adapter_module_) return KEEL_RESULT_UNSUPPORTED;
    const auto api = host_.adapter_module_->EntityInput();
    if (!api.capabilities || !api.dispatch) return KEEL_RESULT_UNSUPPORTED;
    if (!request) {
        std::uint32_t direct_types{}, queued_types{};
        const auto result = api.capabilities(&adapter_,&direct_types,&queued_types);
        if (result == KEEL_RESULT_OK) {
            constexpr std::uint32_t known = (1u << (KEELS2_INPUT_ENTITY+1))-1;
            if ((direct_types | queued_types) & ~known) return KEEL_RESULT_INCOMPATIBLE;
            *direct = direct_types; *queued = queued_types;
        }
        return result;
    }
    GameEntityInputRequest call{};
    const KeelEntityHandle handles[]{target,request->activator,request->caller,request->value_entity};
    GameEntityIdentity* identities[]{&call.target,&call.activator,&call.caller,&call.value_entity};
    {
        std::scoped_lock lock(registry_mutex_);
        for (std::size_t i = 0; i < std::size(handles); ++i) if (handles[i]) {
            const auto found = entities_.find(handles[i]);
            if (found == entities_.end() || !EntityAccessible(found->second,plugin)) return KEEL_RESULT_NOT_FOUND;
            *identities[i] = found->second.entity;
            if (!identities[i]->epoch || identities[i]->epoch != call.target.epoch) return KEEL_RESULT_NOT_FOUND;
        }
    }
    call.input = request->input; call.value = request->value; call.queued = request->queued; call.delay = request->delay;
    // All participant handles are captured together. Adapter dispatch performs
    // the final current-map/live-identity check without a registry mutex held.
    return api.dispatch(&adapter_,&call,invoked);
}
}
