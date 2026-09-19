#include <keels2/cs2/native_input.h>
#include <keels2/cs2/entity_variant.h>
#include <keels2/detail/entity_input_copy.hpp>
namespace keels2::cs2 {
KeelResult NativeInputBackend::Dispatch(const host::GameEntityInputRequest& request, KeelBool& invoked)
{
    invoked = KEEL_FALSE;
    const auto snapshot = request;
    detail::EntityInputCopy copied;
    auto result = copied.Assign(snapshot.input,snapshot.value,snapshot.queued,snapshot.delay);
    if (result != KEEL_RESULT_OK) return result;
    if (!snapshot.target.epoch || (snapshot.value.type == KEELS2_INPUT_ENTITY) != (snapshot.value_entity.epoch != 0))
        return KEEL_RESULT_INVALID_ARGUMENT;
    const host::GameEntityIdentity identities[]{snapshot.target,snapshot.activator,snapshot.caller,snapshot.value_entity};
    std::array<KeelCs2EntityIdentity,4> native{};
    for (std::size_t i = 0; i < native.size(); ++i) {
        const auto& identity = identities[i];
        if (!identity.epoch) {
            if (identity.index || identity.source2_handle) return KEEL_RESULT_INVALID_ARGUMENT;
            continue;
        }
        if (identity.index < 0 || static_cast<std::uint32_t>(identity.index) >= KEELS2_CS2_ENTITY_CAPACITY || identity.source2_handle == KEELS2_INVALID_SOURCE2_ENTITY_HANDLE)
            return KEEL_RESULT_INVALID_ARGUMENT;
        if (identity.epoch != snapshot.target.epoch) return KEEL_RESULT_NOT_FOUND;
        native[i] = {identity.index,identity.source2_handle};
    }
    std::uint32_t direct{}, queued{};
    result = environment_.InputCapabilities(direct,queued);
    if (result != KEEL_RESULT_OK) return result;
    if (!((snapshot.queued ? queued : direct) & (1u << copied.value.type))) return KEEL_RESULT_UNSUPPORTED;
    const auto bindings = environment_.InputBindings();
    KeelCs2VariantValue value{}; value.size = sizeof(value); value.type = copied.value.type;
    value.string_value = copied.value.string_value; value.int_value = copied.value.int_value; value.float_value = copied.value.float_value;
    std::memcpy(value.vector_value,copied.value.vector_value,sizeof(value.vector_value));
    std::memcpy(value.color_value,copied.value.color_value,sizeof(value.color_value));
    value.entity_handle = snapshot.value_entity.source2_handle;
    struct Owned { void* value{}; ~Owned() { KeelCs2Variant_Release(value); } } owned;
    result = KeelCs2Variant_Build(&value,&owned.value);
    if (result != KEEL_RESULT_OK) return result;
    // Allocation and capability discovery happen before the final epoch/system
    // acquisition. No native entity pointer survives either operation.
    void* system{}; std::uint64_t epoch{};
    result = environment_.InputCurrent(snapshot.target.epoch,system,epoch);
    if (result != KEEL_RESULT_OK) return result;
    if (!system || epoch != snapshot.target.epoch) return KEEL_RESULT_NOT_FOUND;
    return KeelCs2_EntityInput(system,&native[0],snapshot.activator.epoch ? &native[1] : nullptr,
        snapshot.caller.epoch ? &native[2] : nullptr,snapshot.value_entity.epoch ? &native[3] : nullptr,
        &bindings,copied.name.data(),owned.value,snapshot.queued,snapshot.delay,&invoked);
}
}
