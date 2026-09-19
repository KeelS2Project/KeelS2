#include <keels2/cs2/native_construction.h>
#include <vector>

namespace keels2::cs2 {
KeelResult NativeConstructionBackend::Ready()
{
    return environment_.Ready();
}

KeelResult NativeConstructionBackend::Current(const host::GameEntityIdentity& entity, void*& system)
{
    system = nullptr;

    if (!entity.epoch)
        return KEEL_RESULT_NOT_FOUND;

    std::uint64_t epoch{};
    const auto result = environment_.Current(entity.epoch,system,epoch);

    if (result != KEEL_RESULT_OK)
        return result;

    return system && epoch == entity.epoch ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
}

KeelResult NativeConstructionBackend::Create(const char* name, host::GameEntityIdentity& output)
{
    output = {};
    void* base{};
    auto result = environment_.ResolveBase(base);

    if (result != KEEL_RESULT_OK)
        return result;

    void* system{};
    std::uint64_t epoch{};
    result = environment_.Current(0,system,epoch);

    if (result != KEEL_RESULT_OK)
        return result;

    if (!system || !epoch)
        return KEEL_RESULT_NOT_READY;

    std::vector<std::uint32_t> before(KEELS2_CS2_ENTITY_CAPACITY);
    result = KeelCs2_SnapshotEntityHandles(system,before.data(),static_cast<std::uint32_t>(before.size()));

    if (result != KEEL_RESULT_OK)
        return result;

    const auto bindings = environment_.Bindings();
    void* instance{};
    result = KeelCs2_CreateEntity(&bindings,name,&instance);

    if (result != KEEL_RESULT_OK)
        return result;

    void* current{};
    std::uint64_t current_epoch{};
    result = environment_.Current(epoch,current,current_epoch);

    if (result != KEEL_RESULT_OK || current != system || current_epoch != epoch)
        return result == KEEL_RESULT_OK ? KEEL_RESULT_NOT_FOUND : result;

    KeelCs2EntityIdentity native{};
    result = KeelCs2_CaptureCreatedEntity(current,instance,&native);

    if (result != KEEL_RESULT_OK)
        return result;

    if (native.index < 0 || static_cast<std::size_t>(native.index) >= before.size())
        return KEEL_RESULT_INCOMPATIBLE;

    if (before[static_cast<std::size_t>(native.index)] == native.source2_handle)
        return KEEL_RESULT_ALREADY_EXISTS;

    result = KeelCs2_ValidateCreatedEntity(current,&native,base,KEEL_TRUE);

    if (result != KEEL_RESULT_OK) {
        KeelBool invoked{};
        static_cast<void>(KeelCs2_RemoveCreatedEntity(current,&native,&bindings,&invoked));
        return result;
    }

    output = {native.index,native.source2_handle,epoch};
    return KEEL_RESULT_OK;
}

KeelResult NativeConstructionBackend::Validate(const host::GameEntityIdentity& entity)
{
    void* system{};
    const auto result = Current(entity,system);

    if (result != KEEL_RESULT_OK)
        return result;

    const KeelCs2EntityIdentity native{entity.index,entity.source2_handle};
    return KeelCs2_ValidatePendingEntity(system,&native);
}

KeelResult
NativeConstructionBackend::Spawn(const host::GameEntityIdentity& entity, const void* values, KeelBool& invoked)
{
    invoked = KEEL_FALSE;
    void* base{};
    auto result = environment_.ResolveBase(base);

    if (result != KEEL_RESULT_OK)
        return result;

    void* system{};
    result = Current(entity,system);

    if (result != KEEL_RESULT_OK)
        return result;

    const auto bindings = environment_.Bindings();
    const KeelCs2EntityIdentity native{entity.index,entity.source2_handle};
    result = KeelCs2_SpawnCreatedEntity(system,&native,base,&bindings,values,&invoked);

    if (!invoked)
        return result;

    void* current{};
    const auto ready = Current(entity,current);

    if (ready != KEEL_RESULT_OK || current != system) return result != KEEL_RESULT_OK ? result :
        ready == KEEL_RESULT_OK ? KEEL_RESULT_NOT_FOUND : ready;

    const auto completed = KeelCs2_FinishCreatedSpawn(current,&native,&bindings);
    return result == KEEL_RESULT_OK ? completed : result;
}

KeelResult NativeConstructionBackend::Cancel(const host::GameEntityIdentity& entity)
{
    void* system{};
    auto result = Current(entity,system);

    if (result != KEEL_RESULT_OK)
        return result;

    const KeelCs2EntityIdentity native{entity.index,entity.source2_handle};
    result = KeelCs2_ValidatePendingEntity(system,&native);

    if (result != KEEL_RESULT_OK)
        return result;

    const auto bindings = environment_.Bindings();
    KeelBool invoked{};
    return KeelCs2_RemoveCreatedEntity(system,&native,&bindings,&invoked);
}

KeelResult NativeConstructionBackend::Teleport(const host::GameEntityIdentity& entity,
                                               const KeelEntityTeleport& request)
{
    void* base{};
    auto result = environment_.ResolveBase(base);

    if (result != KEEL_RESULT_OK)
        return result;

    void* system{};
    result = Current(entity,system);

    if (result != KEEL_RESULT_OK)
        return result;

    const KeelCs2EntityIdentity native{entity.index,entity.source2_handle};
    KeelCs2EntityToolContext context{};
    result = KeelCs2_PrepareCreatedEntityTool(system,&native,base,&context);

    if (result != KEEL_RESULT_OK)
        return result;

    KeelCs2EntityToolBindings bindings{};
    KeelCs2EntityToolClass target{};
    result = environment_.TeleportTarget(context.class_name,bindings,target);

    if (result != KEEL_RESULT_OK)
        return result;

    void* current{};
    result = Current(entity,current);

    if (result != KEEL_RESULT_OK || current != system)
        return result == KEEL_RESULT_OK ? KEEL_RESULT_NOT_FOUND : result;

    return KeelCs2_TeleportCreatedEntity(current,&native,&context,&bindings,&target,&request);
}

KeelResult NativeConstructionBackend::Visit(const host::GameEntityIdentity& entity, const char* name,
    KeelEntityAccessCallback callback, void* data)
{
    if (!callback)
        return KEEL_RESULT_INVALID_ARGUMENT;

    void* system{};
    auto result = Current(entity,system);

    if (result != KEEL_RESULT_OK)
        return result;

    const KeelCs2EntityIdentity native{entity.index,entity.source2_handle};
    void* pointer{};
    result = KeelCs2_ResolveCreatedEntityPointer(system,&native,name,&pointer);
    return result == KEEL_RESULT_OK ? callback(data,&pointer,1) : result;
}
}
