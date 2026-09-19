#include <keels2/cs2/native_outputs.h>
#include <keels2/cs2/entity_variant.h>
#include <keels2/keelhook.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace keels2::cs2 {
KeelResult NativeOutputHooks::Start(void* function, const char* profile, const KeelHookApi& hooks,
    host::GameHookDefer defer, host::GameEntityOutputCallback callback, void* data)
{
    if (!function || !profile || !*profile || !defer || !callback || hooks.size != sizeof(hooks) ||
        hooks.api_version != KEELHOOK_API_VERSION || !hooks.resolve_target || !hooks.add_callback ||
        !hooks.remove_callback || !hooks.release_target) return KEEL_RESULT_INVALID_ARGUMENT;
    if (callback_) return hook_ && function_ == function && defer_ == defer && callback_ == callback && user_data_ == data &&
        hooks_.resolve_target == hooks.resolve_target && hooks_.add_callback == hooks.add_callback &&
        hooks_.remove_callback == hooks.remove_callback && hooks_.release_target == hooks.release_target
            ? KEEL_RESULT_OK : KEEL_RESULT_BUSY;
    if (target_ || hook_) return KEEL_RESULT_BUSY;
    // The caller verifies the profile before resolving this address. Direct
    // KeelHook targets must not contain a pattern resolver profile.
    const KeelHookTargetSpec spec{sizeof(spec),KH_TARGET_ADDRESS,KH_MECHANISM_DETOUR,0,
        nullptr,nullptr,nullptr,nullptr,function,0,0,0};
    constexpr auto prototype = kh::Prototype<void(void*,void*,void*,void*,float,void*,void*)>::value;
    auto result = hooks.resolve_target(0,&spec,&prototype,&target_);
    if (result != KEEL_RESULT_OK) { target_ = 0; return result; }
    hooks_ = hooks; defer_ = defer; callback_ = callback; user_data_ = data; function_ = function;
    const KeelHookCallbackSpec subscription{sizeof(subscription),KH_PHASE_BOTH,
        std::numeric_limits<std::int32_t>::max(),0,&Entry,this};
    result = hooks_.add_callback(0,target_,&subscription,&hook_);
    if (result != KEEL_RESULT_OK) {
        callback_ = nullptr; user_data_ = nullptr; hook_ = 0;
        if (hooks_.release_target(0,target_) == KEEL_RESULT_OK) target_ = 0;
    }
    return result;
}
KeelResult NativeOutputHooks::Stop()
{
    if (std::any_of(pending_.begin(),pending_.end(),[](const auto& value) { return value.owner != nullptr; }))
        return KEEL_RESULT_BUSY;
    if (hook_) {
        const auto result = hooks_.remove_callback(0,hook_);
        if (result != KEEL_RESULT_OK) return result;
        hook_ = 0;
    }
    if (target_) {
        const auto result = hooks_.release_target(0,target_);
        if (result != KEEL_RESULT_OK) return result;
        target_ = 0;
    }
    callback_ = nullptr; user_data_ = nullptr; function_ = nullptr; defer_ = nullptr; hooks_ = {};
    return KEEL_RESULT_OK;
}
void NativeOutputHooks::Complete(void* data) noexcept
{
    auto& pending = *static_cast<Pending*>(data);
    auto* owner = pending.owner;
    if (!owner) return;
    try { if (owner->callback_) owner->callback_(nullptr,pending.token,owner->user_data_); } catch (...) {}
    pending = {};
}
KeelHookAction NativeOutputHooks::Entry(KeelHookFrame* frame, void* data)
{
    if (!frame || !data || frame->size != sizeof(*frame)) return KH_ACTION_CONTINUE;
    try { return static_cast<NativeOutputHooks*>(data)->Dispatch(*frame); }
    catch (...) { return frame->phase == KH_PHASE_PRE ? KH_ACTION_SUPERSEDE : KH_ACTION_CONTINUE; }
}
KeelHookAction NativeOutputHooks::Dispatch(KeelHookFrame& frame)
{
    if (!environment_.OutputOnThread()) return KH_ACTION_CONTINUE;
    if (!callback_ || frame.target != target_ || frame.argument_count != 7 || !frame.arguments) return KH_ACTION_CONTINUE;
    if (frame.phase == KH_PHASE_POST) {
        const auto found = std::find_if(pending_.begin(),pending_.end(),[&](const auto& value) { return value.owner && value.frame == &frame; });
        if (found == pending_.end()) return KH_ACTION_CONTINUE;
        auto event = found->event;
        event.phase = KEELS2_OUTPUT_POST;
        event.flags = frame.flags & KH_FRAME_ORIGINAL_CALLED ? KEELS2_OUTPUT_ORIGINAL_CALLED : 0;
        callback_(&event,found->token,user_data_);
        return KH_ACTION_CONTINUE;
    }
    if (frame.phase != KH_PHASE_PRE || !next_token_) return KH_ACTION_CONTINUE;
    for (unsigned i = 0; i < 7; ++i)
        if (frame.arguments[i].type != (i == 4 ? KH_VALUE_FLOAT32 : KH_VALUE_POINTER)) return KH_ACTION_CONTINUE;
    const float delay = frame.arguments[4].scalar.float32;
    if (!std::isfinite(delay)) return KH_ACTION_CONTINUE;
    void* system{}; std::uint64_t epoch{};
    if (environment_.OutputCurrent(0,system,epoch) != KEEL_RESULT_OK || !system || !epoch) return KH_ACTION_CONTINUE;
    const auto found = std::find_if(pending_.begin(),pending_.end(),[](const auto& value) { return value.owner == nullptr; });
    if (found == pending_.end()) return KH_ACTION_CONTINUE;
    auto& pending = *found;
    if (KeelCs2_CaptureOutputContext(system,frame.arguments[0].scalar.pointer,frame.arguments[1].scalar.pointer,
        frame.arguments[2].scalar.pointer,&pending.native) != KEEL_RESULT_OK) return KH_ACTION_CONTINUE;
    pending.event = {}; auto& event = pending.event;
    event.size = sizeof(event); event.phase = KEELS2_OUTPUT_PRE; event.delay = delay;
    KeelEntityInfo* identities[]{&event.entity,&event.activator,&event.caller};
    for (unsigned i = 0; i < 3; ++i) {
        *identities[i] = {sizeof(KeelEntityInfo),-1,UINT32_MAX,0,0};
        if (pending.native.instances[i]) *identities[i] = {sizeof(KeelEntityInfo),pending.native.entities[i].index,
            pending.native.entities[i].source2_handle,0,epoch};
    }
    std::memcpy(event.class_name,pending.native.class_name,sizeof(event.class_name));
    std::memcpy(event.schema_name,pending.native.schema_name,sizeof(event.schema_name));
    std::memcpy(event.output_name,pending.native.output_name,sizeof(event.output_name));
    event.value.size = sizeof(event.value);
    event.value_status = KeelCs2Variant_Read(frame.arguments[3].scalar.pointer,&event.value);
    pending.epoch = epoch; pending.frame = &frame; pending.token = next_token_++; pending.owner = this;
    if (defer_(&frame,&Complete,&pending) != KEEL_RESULT_OK) { pending = {}; return KH_ACTION_CONTINUE; }
    bool block{};
    try { block = callback_(&event,pending.token,user_data_) == KEELS2_OUTPUT_BLOCK; }
    catch (...) { block = true; }
    void* current{}; std::uint64_t current_epoch{};
    if (environment_.OutputCurrent(epoch,current,current_epoch) != KEEL_RESULT_OK || current != system || current_epoch != epoch ||
        KeelCs2_ValidateOutputContext(current,&pending.native) != KEEL_RESULT_OK) block = true;
    return block ? KH_ACTION_SUPERSEDE : KH_ACTION_CONTINUE;
}
}
