#include <keels2/cs2/native_bridge.h>
#include <in_buttons.h>
#include <keels2/cs2/cvar_abi.h>

#include <igameevents.h>
#include <eiface.h>
#include <entity2/entityclass.h>
#include <entity2/entityinstance.h>
#include <entity2/entitysystem.h>
#include <schemasystem/schemasystem.h>
#include <networksystem/inetworkmessages.h>
#include <playerslot.h>
#include <tier1/bufferstring.h>
#include <tier1/convar.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <new>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
template <typename Function>
Function NativeActionFunction(void* address)
{
    static_assert(sizeof(Function) == sizeof(address));
    Function function{};
    std::memcpy(&function, &address, sizeof(function));
    return function;
}
}

static_assert(sizeof(ConVarRef) == sizeof(keels2::cs2::ConVarRef));
static_assert(alignof(ConVarRef) == alignof(keels2::cs2::ConVarRef));
static_assert(std::is_trivially_copyable_v<ConVarRef>);
static_assert(sizeof(CConVarRef<bool>) == sizeof(keels2::cs2::ConVarObject));
static_assert(sizeof(CConVarRef<std::int32_t>) == sizeof(keels2::cs2::ConVarObject));
static_assert(sizeof(CConVarRef<float>) == sizeof(keels2::cs2::ConVarObject));
static_assert(sizeof(CConVarRef<CUtlString>) == sizeof(keels2::cs2::ConVarObject));

namespace
{

class GameEventListener final : public IGameEventListener2
{
public:
    GameEventListener(
        IGameEventManager2& manager,
        KeelCs2GameEventCallback callback,
        void* user_data)
        : manager_(manager), callback_(callback), user_data_(user_data)
    {
    }

    ~GameEventListener() override
    {
        manager_.RemoveListener(this);
    }

    void FireGameEvent(IGameEvent* event) override
    {
        if (event && callback_)
        {
            callback_(event, event->GetName(), user_data_);
        }
    }

    bool Listen(const char* name)
    {
        return name && name[0] && manager_.AddListener(this, name, true);
    }

private:
    IGameEventManager2& manager_;
    KeelCs2GameEventCallback callback_{};
    void* user_data_{};
};

struct BuiltinType
{
    SchemaBuiltinType_t schema_type;
    std::uint32_t size;
    std::uint32_t alignment;
};

bool PublicBuiltin(KeelSchemaValueType value_type, BuiltinType& type) noexcept
{
    switch (value_type)
    {
        case KEELS2_SCHEMA_CHAR:
            type = {SCHEMA_BUILTIN_TYPE_CHAR, 1, 1};
            return true;
        case KEELS2_SCHEMA_VECTOR3:
            type = {SCHEMA_BUILTIN_TYPE_FLOAT32, sizeof(Vector), alignof(Vector)};
            return true;
        case KEELS2_SCHEMA_INT8:
            type = {SCHEMA_BUILTIN_TYPE_INT8, 1, 1};
            return true;
        case KEELS2_SCHEMA_UINT8:
            type = {SCHEMA_BUILTIN_TYPE_UINT8, 1, 1};
            return true;
        case KEELS2_SCHEMA_INT16:
            type = {SCHEMA_BUILTIN_TYPE_INT16, 2, 2};
            return true;
        case KEELS2_SCHEMA_UINT16:
            type = {SCHEMA_BUILTIN_TYPE_UINT16, 2, 2};
            return true;
        case KEELS2_SCHEMA_INT32:
            type = {SCHEMA_BUILTIN_TYPE_INT32, 4, 4};
            return true;
        case KEELS2_SCHEMA_UINT32:
        case KEELS2_SCHEMA_ENTITY_HANDLE:
            type = {SCHEMA_BUILTIN_TYPE_UINT32, 4, 4};
            return true;
        case KEELS2_SCHEMA_INT64:
            type = {SCHEMA_BUILTIN_TYPE_INT64, 8, 8};
            return true;
        case KEELS2_SCHEMA_UINT64:
            type = {SCHEMA_BUILTIN_TYPE_UINT64, 8, 8};
            return true;
        case KEELS2_SCHEMA_FLOAT32:
            type = {SCHEMA_BUILTIN_TYPE_FLOAT32, 4, 4};
            return true;
        case KEELS2_SCHEMA_FLOAT64:
            type = {SCHEMA_BUILTIN_TYPE_FLOAT64, 8, 8};
            return true;
        case KEELS2_SCHEMA_BOOL:
            type = {SCHEMA_BUILTIN_TYPE_BOOL, 1, 1};
            return true;
        default:
            return false;
    }
}

bool PowerOfTwo(std::uint32_t value) noexcept
{
    return value && (value & (value - 1)) == 0;
}

bool ValidClass(const CSchemaClassInfo* info, bool allow_unknown_alignment = false) noexcept
{
    return info && info->m_nSize > 0 && info->m_nSize <= 16 * 1024 * 1024 &&
        ((PowerOfTwo(info->m_nAlignment) && info->m_nAlignment <= 128) ||
            (allow_unknown_alignment && info->m_nAlignment == UINT8_MAX)) &&
        info->m_nFieldCount <= 8192 &&
        (info->m_nFieldCount == 0 || info->m_pFields) &&
        (info->m_nBaseClassCount == 0 || info->m_pBaseClasses);
}

bool LiveIdentity(const CEntityIdentity* identity) noexcept
{
    constexpr std::uint32_t rejected = EF_IS_INVALID_EHANDLE |
        EF_DELETE_IN_PROGRESS | EF_MARKED_FOR_DELETE |
        EF_IS_PRE_SPAWN | EF_IS_CONSTRUCTION_IN_PROGRESS;
    return identity && identity->m_pInstance && identity->m_pClass &&
        (static_cast<std::uint32_t>(identity->m_flags) & rejected) == 0 &&
        identity->m_pInstance->m_pEntity == identity;
}

CEntityIdentity* IdentityByIndex(CEntitySystem* system, std::int32_t index) noexcept
{
    if (!system || index < 0 || index >= MAX_TOTAL_ENTITIES)
    {
        return nullptr;
    }
    CEntityIdentity* chunk =
        system->m_EntityList.m_pIdentityChunks[static_cast<std::size_t>(index) /
            MAX_ENTITIES_IN_LIST];
    if (!chunk)
    {
        return nullptr;
    }
    CEntityIdentity* identity = &chunk[static_cast<std::size_t>(index) % MAX_ENTITIES_IN_LIST];
    return identity->GetEntityIndex().Get() == index && LiveIdentity(identity)
        ? identity
        : nullptr;
}

CEntityIdentity* IdentityByHandle(CEntitySystem* system, std::uint32_t handle) noexcept
{
    if (handle == INVALID_EHANDLE_INDEX)
    {
        return nullptr;
    }
    const CEntityHandle source_handle(handle);
    CEntityIdentity* identity = IdentityByIndex(system, source_handle.GetEntryIndex());
    return identity && static_cast<std::uint32_t>(identity->GetRefEHandle().ToInt()) == handle
        ? identity
        : nullptr;
}

enum class HierarchyResult
{
    absent,
    found,
    incompatible
};

HierarchyResult FindBaseOffset(
    const CSchemaClassInfo* current,
    const CSchemaClassInfo* target,
    std::uint64_t offset,
    std::array<const CSchemaClassInfo*, 64>& path,
    std::size_t depth,
    std::size_t& visited,
    bool& found,
    std::uint64_t& result) noexcept
{
    if (!ValidClass(current) || !target || depth >= path.size() || ++visited > 256)
    {
        return HierarchyResult::incompatible;
    }
    if (std::find(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(depth), current) !=
        path.begin() + static_cast<std::ptrdiff_t>(depth))
    {
        return HierarchyResult::incompatible;
    }
    path[depth] = current;
    if (current == target)
    {
        if (found && result != offset)
        {
            return HierarchyResult::incompatible;
        }
        found = true;
        result = offset;
        return HierarchyResult::found;
    }

    HierarchyResult state = HierarchyResult::absent;
    for (std::uint32_t index{}; index < current->m_nBaseClassCount; ++index)
    {
        const SchemaBaseClassInfoData_t& base = current->m_pBaseClasses[index];
        if (!ValidClass(base.m_pClass) ||
            base.m_nOffset > static_cast<std::uint32_t>(current->m_nSize) ||
            static_cast<std::uint64_t>(base.m_nOffset) +
                    static_cast<std::uint64_t>(base.m_pClass->m_nSize) >
                static_cast<std::uint64_t>(current->m_nSize) ||
            offset > UINT64_MAX - base.m_nOffset)
        {
            return HierarchyResult::incompatible;
        }
        const HierarchyResult nested = FindBaseOffset(
            base.m_pClass,
            target,
            offset + base.m_nOffset,
            path,
            depth + 1,
            visited,
            found,
            result);
        if (nested == HierarchyResult::incompatible)
        {
            return nested;
        }
        if (nested == HierarchyResult::found)
        {
            state = nested;
        }
    }
    return state;
}

}

extern "C" void* KeelCs2_CreateGameEventListener(
    void* manager,
    KeelCs2GameEventCallback callback,
    void* user_data)
{
    if (!manager || !callback)
    {
        return nullptr;
    }
    try
    {
        return new GameEventListener(
            *static_cast<IGameEventManager2*>(manager),
            callback,
            user_data);
    }
    catch (...)
    {
        return nullptr;
    }
}

extern "C" KeelResult KeelCs2_CommandCaller(const void* context, std::int32_t* slot)
{
    if (!context || !slot)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    const auto& caller = *static_cast<const CCommandContext*>(context);
    const CPlayerSlot player = caller.GetPlayerSlot();
    if (player.Get() != -1 && !player.IsValid())
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *slot = player.Get();
    return KEEL_RESULT_OK;
}

extern "C" KeelResult KeelCs2_ServerCommand(void* engine_server, const char* command)
{
    if (!engine_server || !command || !command[0])
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    static_cast<IVEngineServer2*>(engine_server)->ServerCommand(command);
    return KEEL_RESULT_OK;
}

extern "C" KeelResult KeelCs2_ClientConsolePrint(
    void* engine_server,
    std::int32_t slot,
    const char* message)
{
    const CPlayerSlot player(slot);
    if (!engine_server || !player.IsValid() || !message || !message[0])
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    static_cast<IVEngineServer2*>(engine_server)->ClientPrintf(player, message);
    return KEEL_RESULT_OK;
}

extern "C" KeelResult KeelCs2_FindUserMessage(
    void* network_messages,
    const char* name,
    std::uint32_t* message_id)
{
    if (!network_messages || !name || !name[0] || !message_id)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *message_id = 0;
    auto* messages = static_cast<INetworkMessages*>(network_messages);
    INetworkMessageInternal* message = messages->FindNetworkMessagePartial(name);
    if (!message)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    const NetMessageInfo_t* info = messages->GetNetMessageInfo(message);
    if (!info)
    {
        return KEEL_RESULT_INCOMPATIBLE;
    }
    *message_id = info->m_MessageId;
    return KEEL_RESULT_OK;
}

extern "C" void KeelCs2_DestroyGameEventListener(void* listener)
{
    delete static_cast<GameEventListener*>(listener);
}

extern "C" std::uint32_t KeelCs2_ListenForGameEvent(void* listener, const char* name)
{
    try
    {
        return listener && static_cast<GameEventListener*>(listener)->Listen(name) ? 1u : 0u;
    }
    catch (...)
    {
        return 0;
    }
}

extern "C" std::uint32_t KeelCs2_WriteRejectionMessage(
    void* buffer,
    const char* message,
    std::uint32_t length)
{
    if (!buffer || !message)
    {
        return 0;
    }
    try
    {
        auto* destination = static_cast<CBufferString*>(buffer);
        destination->Clear();
        const auto bounded = static_cast<int>(std::min<std::uint32_t>(length, 255u));
        destination->Insert(0, message, bounded);
        return 1;
    }
    catch (...)
    {
        return 0;
    }
}

extern "C" KeelResult KeelCs2_ResolveSchemaField(
    void* schema_system,
    const char* module_name,
    const char* class_name,
    const char* field_name,
    KeelSchemaValueType value_type,
    KeelCs2SchemaField* output)
{
    if (output)
    {
        *output = {};
    }
    BuiltinType expected{};
    if (!schema_system || !module_name || !module_name[0] || !class_name ||
        !class_name[0] || !field_name || !field_name[0] || !output ||
        !PublicBuiltin(value_type, expected))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        auto* schema = static_cast<ISchemaSystem*>(schema_system);
        CSchemaSystemTypeScope* raw_scope = schema->FindTypeScopeForModule(module_name);
        auto* scope = reinterpret_cast<ISchemaSystemTypeScope*>(raw_scope);
        if (!scope)
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        CSchemaClassInfo* class_info = scope->FindDeclaredClass(class_name).Get();
        if (!class_info)
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        if (!ValidClass(class_info))
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        for (std::uint32_t index{}; index < class_info->m_nFieldCount; ++index)
        {
            const SchemaClassFieldData_t& field = class_info->m_pFields[index];
            if (!field.m_pszName || std::strcmp(field.m_pszName, field_name) != 0)
            {
                continue;
            }
            if (!field.m_pType || field.m_nSingleInheritanceOffset < 0)
            {
                return KEEL_RESULT_INCOMPATIBLE;
            }
            if (value_type == KEELS2_SCHEMA_VECTOR3)
            {
                if (field.m_pType->m_eTypeCategory != SCHEMA_TYPE_ATOMIC ||
                    field.m_pType->m_eAtomicCategory != SCHEMA_ATOMIC_PLAIN)
                    return KEEL_RESULT_INCOMPATIBLE;
                const auto* atomic = static_cast<const CSchemaType_Atomic*>(field.m_pType);
                if (std::string_view(atomic->m_sTypeName.Get()) != "Vector" ||
                    atomic->m_nSize != expected.size || atomic->m_nAlignment != expected.alignment)
                    return KEEL_RESULT_INCOMPATIBLE;
            }
            else if (value_type == KEELS2_SCHEMA_ENTITY_HANDLE)
            {
                if (field.m_pType->m_eTypeCategory != SCHEMA_TYPE_ATOMIC ||
                    field.m_pType->m_eAtomicCategory != SCHEMA_ATOMIC_T)
                {
                    return KEEL_RESULT_INCOMPATIBLE;
                }
                const auto* atomic = static_cast<const CSchemaType_Atomic_T*>(field.m_pType);
                const std::string_view name(atomic->m_sTypeName.Get());
                if (!name.starts_with("CHandle<") || !name.ends_with('>') ||
                    atomic->m_nSize != expected.size || atomic->m_nAlignment != expected.alignment ||
                    !atomic->m_pTemplateType ||
                    atomic->m_pTemplateType->m_eTypeCategory != SCHEMA_TYPE_DECLARED_CLASS)
                {
                    return KEEL_RESULT_INCOMPATIBLE;
                }
            }
            else
            {
                if (field.m_pType->m_eTypeCategory != SCHEMA_TYPE_BUILTIN ||
                    field.m_pType->m_eAtomicCategory != SCHEMA_ATOMIC_INVALID)
                {
                    return KEEL_RESULT_INCOMPATIBLE;
                }
                const auto* builtin = static_cast<const CSchemaType_Builtin*>(field.m_pType);
                if (builtin->m_eBuiltinType != expected.schema_type || builtin->m_nSize != expected.size)
                {
                    return KEEL_RESULT_INCOMPATIBLE;
                }
            }
            const std::uint64_t end =
                static_cast<std::uint64_t>(field.m_nSingleInheritanceOffset) + expected.size;
            if (end > static_cast<std::uint64_t>(class_info->m_nSize) ||
                static_cast<std::uint32_t>(field.m_nSingleInheritanceOffset) %
                    expected.alignment != 0)
            {
                return KEEL_RESULT_INCOMPATIBLE;
            }
            *output = {
                class_info,
                field.m_nSingleInheritanceOffset,
                expected.size,
                expected.alignment,
                value_type
            };
            return KEEL_RESULT_OK;
        }
        return KEEL_RESULT_NOT_FOUND;
    }
    catch (...)
    {
        *output = {};
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

extern "C" void* KeelCs2_ReadGameEntitySystem(
    void* game_resource_service,
    std::uint32_t offset)
{
    if (!game_resource_service || offset < sizeof(void*) ||
        offset > 4096 || offset % alignof(void*) != 0)
    {
        return nullptr;
    }
    void* entity_system{};
    std::memcpy(
        &entity_system,
        static_cast<const std::byte*>(game_resource_service) + offset,
        sizeof(entity_system));
    return entity_system;
}

extern "C" KeelResult KeelCs2_FindEntityByIndex(
    void* entity_system,
    std::int32_t index,
    KeelCs2EntityIdentity* output)
{
    if (output)
    {
        *output = {};
    }
    if (!entity_system || !output || index < 0 || index >= MAX_TOTAL_ENTITIES)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        CEntityIdentity* identity = IdentityByIndex(
            static_cast<CEntitySystem*>(entity_system),
            index);
        if (!identity)
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        *output = {
            index,
            static_cast<std::uint32_t>(identity->GetRefEHandle().ToInt())
        };
        return KEEL_RESULT_OK;
    }
    catch (...)
    {
        *output = {};
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

extern "C" KeelResult KeelCs2_FindEntityBySource2Handle(
    void* entity_system,
    std::uint32_t source2_handle,
    KeelCs2EntityIdentity* output)
{
    if (output)
    {
        *output = {};
    }
    if (!entity_system || !output || source2_handle == INVALID_EHANDLE_INDEX)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        CEntityIdentity* identity = IdentityByHandle(
            static_cast<CEntitySystem*>(entity_system),
            source2_handle);
        if (!identity)
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        *output = {identity->GetEntityIndex().Get(), source2_handle};
        return KEEL_RESULT_OK;
    }
    catch (...)
    {
        *output = {};
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

extern "C" KeelResult KeelCs2_ValidateEntity(
    void* entity_system,
    const KeelCs2EntityIdentity* entity)
{
    if (!entity_system || !entity || entity->index < 0 ||
        entity->source2_handle == INVALID_EHANDLE_INDEX)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        CEntityIdentity* identity = IdentityByHandle(
            static_cast<CEntitySystem*>(entity_system),
            entity->source2_handle);
        return identity && identity->GetEntityIndex().Get() == entity->index
            ? KEEL_RESULT_OK
            : KEEL_RESULT_NOT_FOUND;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

extern "C" KeelResult KeelCs2_EntityInput(void* system, const KeelCs2EntityIdentity* entity,
    const KeelCs2EntityIdentity* activator, const KeelCs2EntityIdentity* caller,
    const KeelCs2EntityIdentity* value_entity, const KeelCs2EntityInputBindings* bindings,
    const char* input, const void* native_value, KeelBool queued, float delay, KeelBool* invoked)
{
    if (invoked) *invoked = KEEL_FALSE;
    if (!system || !entity || !bindings || !bindings->accept || !bindings->queue || !input || !*input ||
        !native_value || !invoked || queued > KEEL_TRUE || !std::isfinite(delay) || delay < 0 || (!queued && delay != 0))
        return KEEL_RESULT_INVALID_ARGUMENT;
    std::array<char,128> name{};
    std::size_t length{};
    while (length < name.size() && input[length]) {
        const auto c = static_cast<unsigned char>(input[length]);
        if (c < 32 || c == 127) return KEEL_RESULT_INVALID_ARGUMENT;
        name[length++] = static_cast<char>(c);
    }
    if (length == name.size()) return KEEL_RESULT_INVALID_ARGUMENT;
    const auto& value = *static_cast<const CVariant*>(native_value);
    switch (value.m_type) {
        case FIELD_VOID: case FIELD_CSTRING: case FIELD_BOOLEAN: case FIELD_INT32:
        case FIELD_FLOAT32: case FIELD_VECTOR: case FIELD_QANGLE: case FIELD_EHANDLE: break;
        case FIELD_COLOR32: if (queued) return KEEL_RESULT_UNSUPPORTED; break;
        default: return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if ((value.m_type == FIELD_EHANDLE) != (value_entity != nullptr) ||
        (value_entity && static_cast<std::uint32_t>(value.m_hEntity.ToInt()) != value_entity->source2_handle))
        return KEEL_RESULT_INVALID_ARGUMENT;
    const KeelCs2EntityIdentity* identities[]{entity,activator,caller,value_entity};
    std::array<CEntityInstance*,4> instances{};
    for (std::size_t i = 0; i < instances.size(); ++i) if (identities[i]) {
        const auto selected = *identities[i];
        if (selected.index < 0 || selected.index >= MAX_TOTAL_ENTITIES || selected.source2_handle == INVALID_EHANDLE_INDEX)
            return KEEL_RESULT_INVALID_ARGUMENT;
        auto* found = IdentityByHandle(static_cast<CEntitySystem*>(system),selected.source2_handle);
        if (!found || found->GetEntityIndex().Get() != selected.index) return KEEL_RESULT_NOT_FOUND;
        instances[i] = found->m_pInstance;
    }
    void* function = queued ? bindings->queue : bindings->accept;
    try {
        *invoked = KEEL_TRUE;
        // These functions have no reviewed acceptance return value or Source 1
        // output ID. Engine callbacks may delete participants or change maps.
        if (queued)
            NativeActionFunction<void (*)(void*,void*,const char*,void*,void*,const void*,float,void*,void*)>(function)(
                system,instances[0],name.data(),instances[1],instances[2],native_value,delay,nullptr,nullptr);
        else
            NativeActionFunction<void (*)(void*,const char*,void*,void*,const void*)>(function)(
                instances[0],name.data(),instances[1],instances[2],native_value);
        return KEEL_RESULT_OK;
    } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

extern "C" KeelResult KeelCs2_CaptureEntity(void* entity_system, const void* instance, KeelCs2EntityIdentity* output)
{
    if (output) *output = {};
    if (!entity_system || !instance || !output ||
        reinterpret_cast<std::uintptr_t>(instance) % alignof(CEntityInstance)) return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        // Only the caller-provided instance is read before registry membership
        // is established. Never dereference its backlink until it matches a
        // canonical, aligned identity slot in a registered entity chunk.
        auto* system = static_cast<CEntitySystem*>(entity_system);
        const auto* backlink = static_cast<const CEntityInstance*>(instance)->m_pEntity;
        if (!backlink) return KEEL_RESULT_NOT_FOUND;
        const auto address = reinterpret_cast<std::uintptr_t>(backlink);
        for (std::size_t chunk_index = 0; chunk_index < std::size(system->m_EntityList.m_pIdentityChunks); ++chunk_index)
        {
            const auto* chunk = system->m_EntityList.m_pIdentityChunks[chunk_index];
            if (!chunk) continue;
            const auto begin = reinterpret_cast<std::uintptr_t>(chunk);
            constexpr std::size_t bytes = sizeof(CEntityIdentity) * MAX_ENTITIES_IN_LIST;
            if (address < begin || address - begin >= bytes || (address - begin) % sizeof(CEntityIdentity)) continue;
            const auto index = static_cast<std::int32_t>(chunk_index * MAX_ENTITIES_IN_LIST +
                (address - begin) / sizeof(CEntityIdentity));
            auto* identity = IdentityByIndex(system, index);
            if (!identity || identity != backlink || identity->m_pInstance != instance) return KEEL_RESULT_NOT_FOUND;
            const auto* type = identity->m_pClass->GetSchemaBinding();
            if (!ValidClass(type) || !type->m_pszName || !type->m_pszName[0] ||
                reinterpret_cast<std::uintptr_t>(instance) % static_cast<unsigned>(type->m_nAlignment))
                return KEEL_RESULT_INCOMPATIBLE;
            *output = {index, static_cast<std::uint32_t>(identity->GetRefEHandle().ToInt())};
            return KEEL_RESULT_OK;
        }
        return KEEL_RESULT_NOT_FOUND;
    }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

extern "C" KeelResult KeelCs2_ResolveEntityPointer(void* entity_system, const KeelCs2EntityIdentity* entity,
    const char* class_name, void** output)
{
    if (output) *output = nullptr;
    if (!entity_system || !entity || !class_name || !class_name[0] || !output ||
        entity->index < 0 || entity->source2_handle == INVALID_EHANDLE_INDEX) return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        auto* system = static_cast<CEntitySystem*>(entity_system);
        auto* identity = IdentityByHandle(system, entity->source2_handle);
        if (!identity || identity->GetEntityIndex().Get() != entity->index) return KEEL_RESULT_NOT_FOUND;
        auto* instance = identity->m_pInstance;
        auto* descriptor = identity->m_pClass;
        const auto* type = descriptor->GetSchemaBinding();
        if (!ValidClass(type) || !type->m_pszName || std::strcmp(type->m_pszName, class_name) ||
            reinterpret_cast<std::uintptr_t>(instance) % type->m_nAlignment) return KEEL_RESULT_INCOMPATIBLE;
        identity = IdentityByHandle(system, entity->source2_handle);
        if (!identity || identity->GetEntityIndex().Get() != entity->index ||
            identity->m_pInstance != instance || identity->m_pClass != descriptor) return KEEL_RESULT_NOT_FOUND;
        *output = instance;
        return KEEL_RESULT_OK;
    }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

extern "C" KeelResult KeelCs2_ReadEntityField(
    void* entity_system,
    const KeelCs2EntityIdentity* entity,
    const KeelCs2SchemaField* field,
    void* value,
    std::uint32_t value_size)
{
    if (!entity_system || !entity || !field || !field->declaring_class || !value ||
        !field->value_size || value_size != field->value_size ||
        !PowerOfTwo(field->value_alignment) || field->value_alignment > 128 ||
        field->offset < 0)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        CEntityIdentity* identity = IdentityByHandle(
            static_cast<CEntitySystem*>(entity_system),
            entity->source2_handle);
        if (!identity || identity->GetEntityIndex().Get() != entity->index)
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        CSchemaClassInfo* dynamic_class = identity->m_pClass->GetSchemaBinding();
        auto* declaring_class = static_cast<CSchemaClassInfo*>(field->declaring_class);
        if (!ValidClass(dynamic_class) || !ValidClass(declaring_class))
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        std::array<const CSchemaClassInfo*, 64> path{};
        std::size_t visited{};
        bool found{};
        std::uint64_t base_offset{};
        const HierarchyResult hierarchy = FindBaseOffset(
            dynamic_class,
            declaring_class,
            0,
            path,
            0,
            visited,
            found,
            base_offset);
        if (hierarchy == HierarchyResult::incompatible)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        if (!found)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        const std::uint64_t field_offset = base_offset +
            static_cast<std::uint32_t>(field->offset);
        if (field_offset > static_cast<std::uint64_t>(dynamic_class->m_nSize) ||
            field->value_size >
                static_cast<std::uint64_t>(dynamic_class->m_nSize) - field_offset)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        const std::uintptr_t instance =
            reinterpret_cast<std::uintptr_t>(identity->m_pInstance);
        if (field_offset > UINTPTR_MAX - instance)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        const std::uintptr_t address = instance + static_cast<std::uintptr_t>(field_offset);
        if (address % field->value_alignment != 0)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        std::memcpy(value, reinterpret_cast<const void*>(address), field->value_size);
        return KEEL_RESULT_OK;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

extern "C" KeelResult KeelCs2_ResolveEntityWriteClass(void* schema_system, const char* module, void** base_class)
{
    if (base_class) *base_class = nullptr;
    if (!schema_system || !module || !*module || !base_class) return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        auto* scope = reinterpret_cast<ISchemaSystemTypeScope*>(
            static_cast<ISchemaSystem*>(schema_system)->FindTypeScopeForModule(module));
        auto* base = scope ? scope->FindDeclaredClass("CBaseEntity").Get() : nullptr;
        if (!ValidClass(base)) return KEEL_RESULT_INCOMPATIBLE;
        *base_class = base;
        return KEEL_RESULT_OK;
    }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

extern "C" KeelResult KeelCs2_WriteEntityField(void* entity_system, void* base_class,
    const KeelCs2EntityIdentity* entity, const KeelCs2SchemaField* field,
    const void* value, std::uint32_t value_size, void* notify)
{
    BuiltinType expected{};
    if (!entity_system || !base_class || !entity || !field || !field->declaring_class ||
        !value || !notify || !PublicBuiltin(field->value_type, expected) ||
        field->value_type == KEELS2_SCHEMA_ENTITY_HANDLE || field->value_size != expected.size ||
        field->value_alignment != expected.alignment || value_size != expected.size || field->offset < 0 || value_size > 12)
        return KEEL_RESULT_INVALID_ARGUMENT;
    std::array<std::byte, 12> snapshot{};
    std::memcpy(snapshot.data(), value, value_size);
    if (field->value_type == KEELS2_SCHEMA_BOOL && std::to_integer<unsigned>(snapshot[0]) > 1)
        return KEEL_RESULT_INVALID_ARGUMENT;
    if (field->value_type == KEELS2_SCHEMA_FLOAT32 || field->value_type == KEELS2_SCHEMA_VECTOR3)
        for (unsigned at = 0; at < value_size; at += sizeof(float))
        {
            float component{}; std::memcpy(&component, snapshot.data() + at, sizeof(component));
            if (!std::isfinite(component)) return KEEL_RESULT_INVALID_ARGUMENT;
        }
    if (field->value_type == KEELS2_SCHEMA_FLOAT64)
    {
        double number{}; std::memcpy(&number, snapshot.data(), sizeof(number));
        if (!std::isfinite(number)) return KEEL_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        CEntityIdentity* identity = IdentityByHandle(
            static_cast<CEntitySystem*>(entity_system),
            entity->source2_handle);
        if (!identity || identity->GetEntityIndex().Get() != entity->index)
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        CSchemaClassInfo* dynamic_class = identity->m_pClass->GetSchemaBinding();
        auto* declaring_class = static_cast<CSchemaClassInfo*>(field->declaring_class);
        if (!ValidClass(dynamic_class) || !ValidClass(declaring_class))
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        std::array<const CSchemaClassInfo*, 64> path{};
        std::size_t visited{};
        bool found{};
        std::uint64_t base_offset{};
        const HierarchyResult hierarchy = FindBaseOffset(
            dynamic_class,
            declaring_class,
            0,
            path,
            0,
            visited,
            found,
            base_offset);
        if (hierarchy == HierarchyResult::incompatible)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        if (!found)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        const std::uint64_t field_offset = base_offset +
            static_cast<std::uint32_t>(field->offset);
        if (field_offset > static_cast<std::uint64_t>(dynamic_class->m_nSize) ||
            field->value_size >
                static_cast<std::uint64_t>(dynamic_class->m_nSize) - field_offset)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        const std::uintptr_t instance =
            reinterpret_cast<std::uintptr_t>(identity->m_pInstance);
        if (field_offset > UINTPTR_MAX - instance)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        const std::uintptr_t address = instance + static_cast<std::uintptr_t>(field_offset);
        if (address % field->value_alignment != 0)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        if (field->value_size - 1 > UINTPTR_MAX - address) return KEEL_RESULT_INCOMPATIBLE;
        auto* base = static_cast<CSchemaClassInfo*>(base_class);
        if (!ValidClass(base) || !base->m_pszName || std::strcmp(base->m_pszName,"CBaseEntity") != 0)
            return KEEL_RESULT_INCOMPATIBLE;
        path = {}; visited = 0; found = false; base_offset = 0;
        if (FindBaseOffset(dynamic_class, base, 0, path, 0, visited, found, base_offset) != HierarchyResult::found ||
            !found || base_offset != 0) return KEEL_RESULT_INCOMPATIBLE;
        void** table{};
        std::memcpy(&table, reinterpret_cast<const void*>(instance), sizeof(table));
        if (!table || table[KEELS2_CS2_NETWORK_STATE_CHANGED_SLOT] != notify) return KEEL_RESULT_INCOMPATIBLE;
        if (std::memcmp(reinterpret_cast<const void*>(address), snapshot.data(), field->value_size) == 0)
            return KEEL_RESULT_OK;
        // A full dirty notification also covers scalar fields without requiring
        // serializer-specific field paths. It does not execute game setters.
        const NetworkStateChangedData changed(true);
        std::memcpy(reinterpret_cast<void*>(address), snapshot.data(), field->value_size);
        using Notify = void (*)(void*, const NetworkStateChangedData&);
        NativeActionFunction<Notify>(notify)(reinterpret_cast<void*>(instance), changed);
        return KEEL_RESULT_OK;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

extern "C" KeelResult KeelCs2_PlayerAction(void* entity_system, void* schema_system, const char* module,
    const KeelCs2EntityIdentity* entity, const KeelPlayerAction* action,
    const KeelCs2PlayerActionBindings* bindings)
{
#if defined(_WIN32)
    constexpr std::uint32_t teleport_slot = 163;
#else
    constexpr std::uint32_t teleport_slot = 162;
#endif
    if (!entity_system || !schema_system || !module || !entity || !action || !bindings ||
        action->size != sizeof(KeelPlayerAction) ||
        (action->kind != KEELS2_PLAYER_ACTION_IMPULSE && action->kind != KEELS2_PLAYER_ACTION_KILL) ||
        !std::isfinite(action->damage) || action->damage < 0 || action->damage > 100000 ||
        !bindings->damage_construct || !bindings->damage_apply || !bindings->damage_destroy ||
        bindings->teleport_slot != teleport_slot || bindings->suicide_slot != 384 || bindings->damage_info_size != 0x118)
        return KEEL_RESULT_INVALID_ARGUMENT;
    for (const auto value : action->impulse)
        if (!std::isfinite(value) || std::abs(value) > 4096 ||
            (action->kind == KEELS2_PLAYER_ACTION_KILL && value != 0))
            return KEEL_RESULT_INVALID_ARGUMENT;
    if (action->kind == KEELS2_PLAYER_ACTION_KILL && action->damage != 0)
        return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        auto* identity = IdentityByHandle(static_cast<CEntitySystem*>(entity_system), entity->source2_handle);
        if (!identity || identity->GetEntityIndex().Get() != entity->index)
            return KEEL_RESULT_NOT_FOUND;
        auto* dynamic_class = identity->m_pClass->GetSchemaBinding();
        if (!ValidClass(dynamic_class) || !dynamic_class->m_pszName ||
            std::strcmp(dynamic_class->m_pszName, "CCSPlayerPawn") != 0)
            return KEEL_RESULT_INCOMPATIBLE;
        const auto read = [&](const char* name, KeelSchemaValueType type, void* output, std::uint32_t size) {
            KeelCs2SchemaField field{};
            const auto result = KeelCs2_ResolveSchemaField(schema_system, module, "CBaseEntity", name, type, &field);
            return result == KEEL_RESULT_OK ? KeelCs2_ReadEntityField(entity_system, entity, &field, output, size) : result;
        };
        std::uint8_t life{};
        auto result = read("m_lifeState", KEELS2_SCHEMA_UINT8, &life, sizeof(life));
        if (result != KEEL_RESULT_OK)
            return result;
        if (life != LIFE_ALIVE)
            return KEEL_RESULT_NOT_READY;
        void* instance = identity->m_pInstance;
        void** table{};
        std::memcpy(&table, instance, sizeof(table));
        if (!table)
            return KEEL_RESULT_INCOMPATIBLE;
        if (action->kind == KEELS2_PLAYER_ACTION_KILL)
        {
            using Suicide = void (*)(void*, bool, bool);
            if (!table[bindings->suicide_slot])
                return KEEL_RESULT_INCOMPATIBLE;
            NativeActionFunction<Suicide>(table[bindings->suicide_slot])(instance, false, true);
            return KEEL_RESULT_OK;
        }
        Vector velocity;
        result = read("m_vecAbsVelocity", KEELS2_SCHEMA_VECTOR3, &velocity, sizeof(velocity));
        if (result != KEEL_RESULT_OK)
            return result;
        velocity += Vector(action->impulse[0], action->impulse[1], action->impulse[2]);
        if (!velocity.IsValid())
            return KEEL_RESULT_INCOMPATIBLE;
        CSchemaClassInfo* damage_class{};
        if (action->damage > 0)
        {
            auto* scope = reinterpret_cast<ISchemaSystemTypeScope*>(
                static_cast<ISchemaSystem*>(schema_system)->FindTypeScopeForModule(module));
            damage_class = scope ? scope->FindDeclaredClass("CTakeDamageInfo").Get() : nullptr;
            if (!ValidClass(damage_class) || damage_class->m_nSize != static_cast<int>(bindings->damage_info_size) ||
                damage_class->m_nAlignment > alignof(std::max_align_t))
                return KEEL_RESULT_INCOMPATIBLE;
        }
        using Teleport = void (*)(void*, const Vector*, const QAngle*, const Vector*);
        if (!table[bindings->teleport_slot])
            return KEEL_RESULT_INCOMPATIBLE;
        NativeActionFunction<Teleport>(table[bindings->teleport_slot])(instance, nullptr, nullptr, &velocity);
        if (action->damage > 0)
        {
            if (KeelCs2_ValidateEntity(entity_system, entity) != KEEL_RESULT_OK)
                return KEEL_RESULT_NOT_FOUND;
            using Construct = void (*)(void*, void*, void*, void*, float, std::uint32_t, std::int32_t);
            using Apply = void (*)(void*, void*, void*);
            using Destroy = void (*)(void*);
            std::vector<std::max_align_t> storage((bindings->damage_info_size + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
            auto* world = IdentityByIndex(static_cast<CEntitySystem*>(entity_system), 0);
            void* source = world ? world->m_pInstance : nullptr;
            NativeActionFunction<Construct>(bindings->damage_construct)(storage.data(), source, source, nullptr, action->damage, 0, 0);
            struct DamageLifetime
            {
                Destroy destroy;
                void* storage;
                ~DamageLifetime() { destroy(storage); }
            } lifetime{NativeActionFunction<Destroy>(bindings->damage_destroy), storage.data()};
            NativeActionFunction<Apply>(bindings->damage_apply)(instance, storage.data(), nullptr);
        }
        return KEEL_RESULT_OK;
    }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

namespace
{
KeelResult ManagementController(void* system, const KeelCs2EntityIdentity* entity,
    const KeelCs2PlayerManagementBindings* bindings, void*& instance)
{
    instance = nullptr;
    if (!system || !entity || !bindings || !bindings->controller_vtable ||
        !bindings->change_team || !bindings->switch_team || !bindings->respawn || !bindings->set_pawn)
        return KEEL_RESULT_INVALID_ARGUMENT;
    auto* identity = IdentityByHandle(static_cast<CEntitySystem*>(system), entity->source2_handle);
    if (!identity || identity->GetEntityIndex().Get() != entity->index)
        return KEEL_RESULT_NOT_FOUND;
    const auto* type = identity->m_pClass->GetSchemaBinding();
    if (!ValidClass(type) || !type->m_pszName || std::strcmp(type->m_pszName, "CCSPlayerController"))
        return KEEL_RESULT_INCOMPATIBLE;
    void** table{};
    std::memcpy(&table, identity->m_pInstance, sizeof(table));
#if defined(_WIN32)
    constexpr std::size_t change_slot = 103, respawn_slot = 270;
#else
    constexpr std::size_t change_slot = 102, respawn_slot = 272;
#endif
    if (table != bindings->controller_vtable || table[change_slot] != bindings->change_team || table[respawn_slot] != bindings->respawn)
        return KEEL_RESULT_INCOMPATIBLE;
    instance = identity->m_pInstance;
    return KEEL_RESULT_OK;
}

KeelResult ManagementField(void* system, void* schema, const char* module,
    const KeelCs2EntityIdentity* controller, const char* class_name, const char* field_name,
    KeelSchemaValueType type, void* output, std::uint32_t size)
{
    KeelCs2SchemaField field{};
    const auto result = KeelCs2_ResolveSchemaField(schema, module, class_name, field_name, type, &field);
    return result == KEEL_RESULT_OK ? KeelCs2_ReadEntityField(system, controller, &field, output, size) : result;
}

KeelResult RespawnPawn(void* system, void* schema, const char* module,
    const KeelCs2EntityIdentity* controller, KeelCs2EntityIdentity& pawn, void*& instance)
{
    instance = nullptr;
    std::uint8_t team{};
    auto result = ManagementField(system, schema, module, controller, "CBaseEntity", "m_iTeamNum",
        KEELS2_SCHEMA_UINT8, &team, sizeof(team));
    if (result != KEEL_RESULT_OK) return result;
    if (team != 2 && team != 3) return KEEL_RESULT_NOT_READY;
    std::uint32_t handle{};
    result = ManagementField(system, schema, module, controller, "CCSPlayerController", "m_hPlayerPawn",
        KEELS2_SCHEMA_ENTITY_HANDLE, &handle, sizeof(handle));
    if (result != KEEL_RESULT_OK) return result;
    if (handle == INVALID_EHANDLE_INDEX) return KEEL_RESULT_NOT_READY;
    result = KeelCs2_FindEntityBySource2Handle(system, handle, &pawn);
    if (result != KEEL_RESULT_OK) return result;
    auto* identity = IdentityByHandle(static_cast<CEntitySystem*>(system), handle);
    if (!identity) return KEEL_RESULT_NOT_FOUND;
    const auto* type = identity->m_pClass->GetSchemaBinding();
    if (!ValidClass(type) || !type->m_pszName || std::strcmp(type->m_pszName, "CCSPlayerPawn"))
        return KEEL_RESULT_INCOMPATIBLE;
    instance = identity->m_pInstance;
    return KEEL_RESULT_OK;
}
}

namespace
{
KeelResult RoundPointerOffset(const CSchemaClassInfo* proxy, const CSchemaClassInfo* rules, std::int32_t& offset)
{
    if (!ValidClass(proxy) || !ValidClass(rules) || !proxy->m_pszName || !rules->m_pszName ||
        std::strcmp(proxy->m_pszName,"CCSGameRulesProxy") || std::strcmp(rules->m_pszName,"CCSGameRules") ||
        proxy->m_nAlignment < alignof(void*) || rules->m_nAlignment < alignof(void*) || rules->m_nSize < static_cast<int>(sizeof(void*)))
        return KEEL_RESULT_INCOMPATIBLE;
    const SchemaClassFieldData_t* selected{};
    for (std::uint32_t i = 0; i < proxy->m_nFieldCount; ++i)
    {
        const auto& field = proxy->m_pFields[i];
        if (!field.m_pszName || std::strcmp(field.m_pszName,"m_pGameRules")) continue;
        if (selected) return KEEL_RESULT_INCOMPATIBLE;
        selected = &field;
    }
    if (!selected) return KEEL_RESULT_NOT_FOUND;
    const auto* type = selected->m_pType;
    if (!type || type->m_eTypeCategory != SCHEMA_TYPE_POINTER || type->m_eAtomicCategory != SCHEMA_ATOMIC_INVALID)
        return KEEL_RESULT_INCOMPATIBLE;
    const auto* object = static_cast<const CSchemaType_Ptr*>(type)->m_pObjectType;
    if (!object || object->m_eTypeCategory != SCHEMA_TYPE_DECLARED_CLASS || object->m_eAtomicCategory != SCHEMA_ATOMIC_INVALID ||
        static_cast<const CSchemaType_DeclaredClass*>(object)->m_pClassInfo != rules)
        return KEEL_RESULT_INCOMPATIBLE;
    offset = selected->m_nSingleInheritanceOffset;
    if (offset < static_cast<int>(sizeof(void*)) || static_cast<std::uint32_t>(offset) % alignof(void*) ||
        static_cast<std::uint64_t>(offset) + sizeof(void*) > static_cast<std::uint64_t>(proxy->m_nSize))
        return KEEL_RESULT_INCOMPATIBLE;
    return KEEL_RESULT_OK;
}
KeelResult RoundInstance(CEntityIdentity* identity, const KeelCs2RoundContext& context,
    const KeelCs2RoundBindings& bindings, void*& rules)
{
    if (!identity || identity->m_pClass->GetSchemaBinding() != context.proxy_class) return KEEL_RESULT_INCOMPATIBLE;
    const auto proxy_address = reinterpret_cast<std::uintptr_t>(identity->m_pInstance);
    const auto* proxy_class = static_cast<const CSchemaClassInfo*>(context.proxy_class);
    const auto* rules_class = static_cast<const CSchemaClassInfo*>(context.rules_class);
    std::int32_t offset{};
    const auto valid = RoundPointerOffset(proxy_class,rules_class,offset);
    if (valid != KEEL_RESULT_OK) return valid;
    if (offset != context.pointer_offset || proxy_address % proxy_class->m_nAlignment ||
        static_cast<std::uintptr_t>(offset) + sizeof(void*) - 1 > UINTPTR_MAX - proxy_address)
        return KEEL_RESULT_INCOMPATIBLE;
    void** table{};
    std::memcpy(&table,identity->m_pInstance,sizeof(table));
    if (table != bindings.proxy_vtable) return KEEL_RESULT_INCOMPATIBLE;
    std::memcpy(&rules,reinterpret_cast<const void*>(proxy_address + static_cast<std::uintptr_t>(offset)),sizeof(rules));
    if (!rules) return KEEL_RESULT_NOT_READY;
    const auto rules_address = reinterpret_cast<std::uintptr_t>(rules);
    if (rules_address % rules_class->m_nAlignment || sizeof(void*) - 1 > UINTPTR_MAX - rules_address)
        return KEEL_RESULT_INCOMPATIBLE;
    std::memcpy(&table,rules,sizeof(table));
    return table == bindings.rules_vtable ? KEEL_RESULT_OK : KEEL_RESULT_INCOMPATIBLE;
}
}

extern "C" KeelResult KeelCs2_ResolveRoundSchema(void* schema_system, const char* module, KeelCs2RoundContext* context)
{
    if (context) *context = {};
    if (!schema_system || !module || !*module || !context) return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        auto* schema = static_cast<ISchemaSystem*>(schema_system);
        auto* scope = reinterpret_cast<ISchemaSystemTypeScope*>(schema->FindTypeScopeForModule(module));
        if (!scope) return KEEL_RESULT_NOT_FOUND;
        auto* proxy = scope->FindDeclaredClass("CCSGameRulesProxy").Get();
        auto* rules = scope->FindDeclaredClass("CCSGameRules").Get();
        if (!proxy || !rules) return KEEL_RESULT_NOT_FOUND;
        std::int32_t offset{};
        const auto result = RoundPointerOffset(proxy,rules,offset);
        if (result != KEEL_RESULT_OK) return result;
        context->proxy_class = proxy; context->rules_class = rules; context->pointer_offset = offset;
        return KEEL_RESULT_OK;
    }
    catch (...) { *context = {}; return KEEL_RESULT_ENGINE_FAILURE; }
}

extern "C" KeelResult KeelCs2_FindRoundContext(void* entity_system,
    const KeelCs2RoundBindings* bindings, KeelCs2RoundContext* context)
{
    if (!entity_system || !bindings || !bindings->rules_vtable || !bindings->proxy_vtable || !bindings->terminate || !context)
        return KEEL_RESULT_INVALID_ARGUMENT;
    context->proxy = {}; context->proxy_instance = nullptr; context->rules = nullptr;
    try
    {
        std::int32_t offset{};
        const auto valid = RoundPointerOffset(static_cast<const CSchemaClassInfo*>(context->proxy_class),
            static_cast<const CSchemaClassInfo*>(context->rules_class),offset);
        if (valid != KEEL_RESULT_OK) return valid;
        if (offset != context->pointer_offset) return KEEL_RESULT_INCOMPATIBLE;
        CEntityIdentity* selected{};
        // The engine list has a fixed maximum. Do not follow unbounded links or
        // cache a borrowed game-rules pointer across calls/maps.
        for (std::int32_t i = 0; i < MAX_TOTAL_ENTITIES; ++i)
        {
            auto* candidate = IdentityByIndex(static_cast<CEntitySystem*>(entity_system),i);
            if (!candidate) continue;
            const auto* type = candidate->m_pClass->GetSchemaBinding();
            if (!type || !type->m_pszName || std::strcmp(type->m_pszName,"CCSGameRulesProxy")) continue;
            if (type != context->proxy_class || selected) return KEEL_RESULT_INCOMPATIBLE;
            selected = candidate;
        }
        if (!selected) return KEEL_RESULT_NOT_FOUND;
        void* rules{};
        const auto result = RoundInstance(selected,*context,*bindings,rules);
        if (result != KEEL_RESULT_OK) return result;
        context->proxy = {selected->GetEntityIndex().Get(),static_cast<std::uint32_t>(selected->GetRefEHandle().ToInt())};
        context->proxy_instance = selected->m_pInstance; context->rules = rules;
        return KEEL_RESULT_OK;
    }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

extern "C" KeelResult KeelCs2_TerminateRound(void* entity_system, const KeelCs2RoundContext* context,
    const KeelRoundTermination* input, const KeelCs2RoundBindings* bindings)
{
    if (!entity_system || !context || !context->proxy_instance || !context->rules || !input || input->size != sizeof(*input) ||
        !bindings || !bindings->rules_vtable || !bindings->proxy_vtable || !bindings->terminate) return KEEL_RESULT_INVALID_ARGUMENT;
    const auto request = *input;
    const bool reason = request.reason == 1 || (request.reason >= 4 && request.reason <= 14) || (request.reason >= 16 && request.reason <= 22);
    if (!reason || request.reserved || !std::isfinite(request.delay) || request.delay < 0 || request.delay > 3600 ||
        (request.team != 0 && request.team != 2 && request.team != 3)) return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        auto* proxy = IdentityByHandle(static_cast<CEntitySystem*>(entity_system),context->proxy.source2_handle);
        if (!proxy || proxy->GetEntityIndex().Get() != context->proxy.index || proxy->m_pInstance != context->proxy_instance)
            return KEEL_RESULT_NOT_FOUND;
        void* rules{};
        const auto result = RoundInstance(proxy,*context,*bindings,rules);
        if (result != KEEL_RESULT_OK) return result;
        if (rules != context->rules) return KEEL_RESULT_NOT_FOUND;
        const auto team = static_cast<std::uint32_t>(request.team);
#if defined(_WIN32)
        using Terminate = void (*)(void*,float,std::uint32_t,const std::uint32_t*);
        NativeActionFunction<Terminate>(bindings->terminate)(rules,request.delay,request.reason,team ? &team : nullptr);
#else
        using Terminate = void (*)(void*,std::uint32_t,const std::uint32_t*,float);
        NativeActionFunction<Terminate>(bindings->terminate)(rules,request.reason,team ? &team : nullptr,request.delay);
#endif
        // The callback can destroy the proxy/rules or change maps. No borrowed
        // object, identity or schema pointer may be dereferenced after dispatch.
        return KEEL_RESULT_OK;
    }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

extern "C" KeelResult KeelCs2_PrepareRespawn(void* system, void* schema, const char* module,
    const KeelCs2EntityIdentity* controller, const KeelCs2PlayerManagementBindings* bindings,
    KeelCs2EntityIdentity* prepared_pawn)
{
    if (prepared_pawn) *prepared_pawn = {};
    if (!schema || !module || !*module || !prepared_pawn) return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        void* instance{};
        auto result = ManagementController(system, controller, bindings, instance);
        if (result != KEEL_RESULT_OK) return result;
        KeelCs2EntityIdentity pawn{};
        void* pawn_instance{};
        result = RespawnPawn(system, schema, module, controller, pawn, pawn_instance);
        if (result != KEEL_RESULT_OK) return result;
        using SetPawn = void (*)(void*, void*, bool, bool, bool, bool);
        NativeActionFunction<SetPawn>(bindings->set_pawn)(instance, pawn_instance, true, false, false, false);
        // Never dereference captured engine pointers after this call. The adapter
        // rechecks its map epoch, controller and pawn before the second phase.
        *prepared_pawn = pawn;
        return KEEL_RESULT_OK;
    }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

extern "C" KeelResult KeelCs2_ManagePlayer(void* system, void* schema, const char* module,
    const KeelCs2EntityIdentity* controller, const KeelCs2EntityIdentity* prepared_pawn,
    const KeelPlayerManagementAction* action, const KeelCs2PlayerManagementBindings* bindings)
{
    if (!schema || !module || !*module || !action || action->size != sizeof(*action) || action->reserved ||
        (action->kind != KEELS2_PLAYER_MANAGEMENT_RESPAWN &&
         action->kind != KEELS2_PLAYER_MANAGEMENT_CHANGE_TEAM &&
         action->kind != KEELS2_PLAYER_MANAGEMENT_SWITCH_TEAM) ||
        (action->kind == KEELS2_PLAYER_MANAGEMENT_RESPAWN ? action->team != 0 : action->team < (action->kind == KEELS2_PLAYER_MANAGEMENT_SWITCH_TEAM ? 2 : 1) || action->team > 3))
        return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        void* instance{};
        auto result = ManagementController(system, controller, bindings, instance);
        if (result != KEEL_RESULT_OK) return result;
        if (action->kind == KEELS2_PLAYER_MANAGEMENT_RESPAWN)
        {
            if (!prepared_pawn) return KEEL_RESULT_INVALID_ARGUMENT;
            KeelCs2EntityIdentity pawn{};
            void* pawn_instance{};
            result = RespawnPawn(system, schema, module, controller, pawn, pawn_instance);
            if (result != KEEL_RESULT_OK) return result;
            if (pawn.index != prepared_pawn->index || pawn.source2_handle != prepared_pawn->source2_handle)
                return KEEL_RESULT_NOT_FOUND;
            std::uint32_t active{};
            result = ManagementField(system, schema, module, controller, "CBasePlayerController", "m_hPawn",
                KEELS2_SCHEMA_ENTITY_HANDLE, &active, sizeof(active));
            if (result != KEEL_RESULT_OK) return result;
            if (active != pawn.source2_handle) return KEEL_RESULT_NOT_READY;
            using Respawn = void (*)(void*);
            NativeActionFunction<Respawn>(bindings->respawn)(instance);
        }
        else
        {
            using Team = void (*)(void*, std::int32_t);
            NativeActionFunction<Team>(action->kind == KEELS2_PLAYER_MANAGEMENT_CHANGE_TEAM
                ? bindings->change_team : bindings->switch_team)(instance, action->team);
        }
        return KEEL_RESULT_OK;
    }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

extern "C" KeelResult KeelCs2_ReadPlayerButtons(void* entity_system, void* schema_system, const char* module,
    const KeelCs2EntityIdentity* pawn, uint64_t* buttons, void** component)
{
    if (buttons) *buttons = 0;
    if (component) *component = nullptr;
    if (!entity_system || !schema_system || !module || !*module || !pawn || !buttons || !component)
        return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        auto* scope = reinterpret_cast<ISchemaSystemTypeScope*>(
            static_cast<ISchemaSystem*>(schema_system)->FindTypeScopeForModule(module));
        if (!scope) return KEEL_RESULT_NOT_FOUND;
        auto* pawn_class = scope->FindDeclaredClass("CBasePlayerPawn").Get();
        auto* movement_class = scope->FindDeclaredClass("CPlayer_MovementServices").Get();
        if (!pawn_class || !movement_class) return KEEL_RESULT_NOT_FOUND;
        if (!ValidClass(pawn_class) || !ValidClass(movement_class, true))
            return KEEL_RESULT_INCOMPATIBLE;
        const auto field = [](const CSchemaClassInfo* type, const char* name) -> const SchemaClassFieldData_t* {
            const SchemaClassFieldData_t* result{};
            for (std::uint32_t i = 0; i < type->m_nFieldCount; ++i)
            {
                const auto& candidate = type->m_pFields[i];
                if (candidate.m_pszName && std::strcmp(candidate.m_pszName, name) == 0)
                {
                    if (result) return nullptr;
                    result = &candidate;
                }
            }
            return result;
        };
        const auto* movement = field(pawn_class, "m_pMovementServices");
        const auto* state = field(movement_class, "m_nButtons");
        if (!movement || !state) return KEEL_RESULT_NOT_FOUND;
        const auto category = [](const CSchemaType* type, SchemaTypeCategory_t expected) {
            return type && type->m_eTypeCategory == expected && type->m_eAtomicCategory == SCHEMA_ATOMIC_INVALID;
        };
        const auto declared = [&](const CSchemaType* type, const CSchemaClassInfo* expected) {
            return category(type, SCHEMA_TYPE_DECLARED_CLASS) &&
                static_cast<const CSchemaType_DeclaredClass*>(type)->m_pClassInfo == expected;
        };
        if (!category(state->m_pType, SCHEMA_TYPE_DECLARED_CLASS)) return KEEL_RESULT_INCOMPATIBLE;
        const auto* buttons_class = static_cast<const CSchemaType_DeclaredClass*>(state->m_pType)->m_pClassInfo;
        if (!ValidClass(buttons_class, true) || !buttons_class->m_pszName ||
            std::strcmp(buttons_class->m_pszName, "CInButtonState") != 0)
            return KEEL_RESULT_INCOMPATIBLE;
        const auto* masks = field(buttons_class, "m_pButtonStates");
        if (!masks) return KEEL_RESULT_NOT_FOUND;
        if (!category(movement->m_pType, SCHEMA_TYPE_POINTER) ||
            !declared(static_cast<const CSchemaType_Ptr*>(movement->m_pType)->m_pObjectType, movement_class) ||
            !category(masks->m_pType, SCHEMA_TYPE_FIXED_ARRAY))
            return KEEL_RESULT_INCOMPATIBLE;
        const auto* array = static_cast<const CSchemaType_FixedArray*>(masks->m_pType);
        if (array->m_nElementCount != 3 || array->m_nElementSize != sizeof(uint64_t) ||
            array->m_nElementAlignment != alignof(uint64_t) || !category(array->m_pElementType, SCHEMA_TYPE_BUILTIN))
            return KEEL_RESULT_INCOMPATIBLE;
        const auto* integer = static_cast<const CSchemaType_Builtin*>(array->m_pElementType);
        if (integer->m_eBuiltinType != SCHEMA_BUILTIN_TYPE_UINT64 || integer->m_nSize != sizeof(uint64_t))
            return KEEL_RESULT_INCOMPATIBLE;
        const auto fits = [](const CSchemaClassInfo* type, const SchemaClassFieldData_t* member, std::size_t size, std::size_t alignment) {
            return member->m_nSingleInheritanceOffset >= 0 &&
                static_cast<std::uint64_t>(member->m_nSingleInheritanceOffset) + size <= static_cast<std::uint64_t>(type->m_nSize) &&
                static_cast<std::uint32_t>(member->m_nSingleInheritanceOffset) % alignment == 0 &&
                (type->m_nAlignment == UINT8_MAX || type->m_nAlignment >= alignment);
        };
        const auto button_alignment = buttons_class->m_nAlignment == UINT8_MAX
            ? alignof(uint64_t) : buttons_class->m_nAlignment;
        if (!fits(pawn_class, movement, sizeof(void*), alignof(void*)) ||
            !fits(movement_class, state, static_cast<std::size_t>(buttons_class->m_nSize), button_alignment) ||
            !fits(buttons_class, masks, sizeof(uint64_t) * 3, alignof(uint64_t)))
            return KEEL_RESULT_INCOMPATIBLE;
        const KeelCs2SchemaField pointer_field{pawn_class, movement->m_nSingleInheritanceOffset,
            sizeof(void*), alignof(void*), 0};
        void* movement_object{};
        auto result = KeelCs2_ReadEntityField(entity_system, pawn, &pointer_field, &movement_object, sizeof(movement_object));
        if (result != KEEL_RESULT_OK) return result;
        if (!movement_object) return KEEL_RESULT_NOT_READY;
        const auto base = reinterpret_cast<std::uintptr_t>(movement_object);
        const auto offset = static_cast<std::uint64_t>(state->m_nSingleInheritanceOffset) + static_cast<std::uint64_t>(masks->m_nSingleInheritanceOffset);
        const auto component_alignment = movement_class->m_nAlignment == UINT8_MAX
            ? button_alignment : movement_class->m_nAlignment;
        if (base % component_alignment != 0 || offset > UINTPTR_MAX - base ||
            sizeof(uint64_t) > UINTPTR_MAX - (base + offset) || (base + offset) % alignof(uint64_t))
            return KEEL_RESULT_INCOMPATIBLE;
        uint64_t held{};
        std::memcpy(&held, reinterpret_cast<const void*>(base + offset), sizeof(held));
        void* current{};
        result = KeelCs2_ReadEntityField(entity_system, pawn, &pointer_field, &current, sizeof(current));
        if (result != KEEL_RESULT_OK) return result;
        if (current != movement_object) return KEEL_RESULT_NOT_FOUND;
        struct Mapping { InputBitMask_t source; uint64_t target; };
        constexpr Mapping mappings[] = {
            {IN_FORWARD, KEELS2_BUTTON_FORWARD}, {IN_BACK, KEELS2_BUTTON_BACK},
            {IN_MOVELEFT, KEELS2_BUTTON_LEFT}, {IN_MOVERIGHT, KEELS2_BUTTON_RIGHT},
            {IN_USE, KEELS2_BUTTON_USE}, {IN_RELOAD, KEELS2_BUTTON_RELOAD},
            {IN_JUMP, KEELS2_BUTTON_JUMP}, {IN_DUCK, KEELS2_BUTTON_DUCK},
            {IN_ATTACK, KEELS2_BUTTON_ATTACK}, {IN_ATTACK2, KEELS2_BUTTON_ATTACK2},
            {IN_SPEED, KEELS2_BUTTON_WALK}, {IN_SCORE, KEELS2_BUTTON_SCORE},
            {IN_ZOOM, KEELS2_BUTTON_ZOOM}, {IN_LOOK_AT_WEAPON, KEELS2_BUTTON_INSPECT},
            {IN_USEORRELOAD, KEELS2_BUTTON_USE_OR_RELOAD}
        };
        uint64_t output{};
        for (const auto& mapping : mappings) if (held & static_cast<uint64_t>(mapping.source)) output |= mapping.target;
        *buttons = output;
        *component = movement_object;
        return KEEL_RESULT_OK;
    }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

extern "C" KeelResult KeelCs2_ReadControllerInput(void* entity_system, void* schema_system, const char* module,
    const KeelCs2EntityIdentity* controller, uint64_t* buttons, void** component, uint32_t* pawn_handle)
{
    if (buttons) *buttons = 0;
    if (component) *component = nullptr;
    if (pawn_handle) *pawn_handle = UINT32_MAX;
    if (!entity_system || !schema_system || !module || !*module || !controller || !buttons || !component || !pawn_handle)
        return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        KeelCs2SchemaField field{};
        auto result = KeelCs2_ResolveSchemaField(schema_system, module, "CBasePlayerController", "m_hPawn", KEELS2_SCHEMA_ENTITY_HANDLE, &field);
        if (result != KEEL_RESULT_OK) return result;
        CEntityHandle active;
        result = KeelCs2_ReadEntityField(entity_system, controller, &field, &active, sizeof(active));
        if (result != KEEL_RESULT_OK) return result;
        if (!active.IsValid()) return KEEL_RESULT_NOT_READY;
        KeelCs2EntityIdentity pawn{};
        result = KeelCs2_FindEntityBySource2Handle(entity_system, static_cast<uint32_t>(active.ToInt()), &pawn);
        if (result != KEEL_RESULT_OK) return result;
        uint64_t held{}; void* movement{};
        result = KeelCs2_ReadPlayerButtons(entity_system, schema_system, module, &pawn, &held, &movement);
        if (result != KEEL_RESULT_OK) return result;
        CEntityHandle current;
        result = KeelCs2_ReadEntityField(entity_system, controller, &field, &current, sizeof(current));
        if (result != KEEL_RESULT_OK) return result;
        if (current != active || KeelCs2_ValidateEntity(entity_system, &pawn) != KEEL_RESULT_OK) return KEEL_RESULT_NOT_FOUND;
        *buttons = held; *component = movement; *pawn_handle = pawn.source2_handle;
        return KEEL_RESULT_OK;
    }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

namespace
{
struct StatFailure { KeelResult result; };
void StatRequire(bool valid, KeelResult result = KEEL_RESULT_INCOMPATIBLE)
{
    if (!valid) throw StatFailure{result};
}
bool StatKey(std::uint32_t key)
{
    return key == KEELS2_PLAYER_STAT_MONEY || key == KEELS2_PLAYER_STAT_MATCH_KILLS ||
        key == KEELS2_PLAYER_STAT_MATCH_DEATHS || key == KEELS2_PLAYER_STAT_MATCH_ASSISTS;
}
CSchemaClassInfo* StatClass(ISchemaSystemTypeScope* scope, const char* name)
{
    auto* type = scope->FindDeclaredClass(name).Get();
    StatRequire(type != nullptr,KEEL_RESULT_NOT_FOUND);
    StatRequire(ValidClass(type) && type->m_pszName && std::strcmp(type->m_pszName,name) == 0);
    return type;
}
const SchemaClassFieldData_t& StatField(const CSchemaClassInfo* type, const char* name)
{
    const SchemaClassFieldData_t* result{};
    StatRequire(ValidClass(type));
    for (std::uint32_t i = 0; i < type->m_nFieldCount; ++i)
    {
        const auto& field = type->m_pFields[i];
        if (!field.m_pszName || std::strcmp(field.m_pszName,name)) continue;
        StatRequire(result == nullptr);
        result = &field;
    }
    StatRequire(result != nullptr,KEEL_RESULT_NOT_FOUND);
    StatRequire(result->m_pType != nullptr);
    return *result;
}
std::uint32_t StatSpan(const CSchemaClassInfo* type, std::int32_t offset, std::uint32_t size, std::uint32_t alignment)
{
    StatRequire(ValidClass(type) && offset >= 0 && size && PowerOfTwo(alignment) && type->m_nAlignment >= alignment);
    const auto start = static_cast<std::uint32_t>(offset);
    StatRequire(start % alignment == 0 && static_cast<std::uint64_t>(start)+size <= static_cast<std::uint64_t>(type->m_nSize));
    return start;
}
void StatDeclared(const CSchemaType* type, const CSchemaClassInfo* expected)
{
    StatRequire(type && type->m_eTypeCategory == SCHEMA_TYPE_DECLARED_CLASS && type->m_eAtomicCategory == SCHEMA_ATOMIC_INVALID &&
        static_cast<const CSchemaType_DeclaredClass*>(type)->m_pClassInfo == expected);
}
std::uint32_t StatBase(const CSchemaClassInfo* derived, const CSchemaClassInfo* base)
{
    std::array<const CSchemaClassInfo*,64> path{};
    std::size_t visited{};
    bool found{};
    std::uint64_t offset{};
    StatRequire(FindBaseOffset(derived,base,0,path,0,visited,found,offset) == HierarchyResult::found && found && offset <= INT32_MAX);
    return static_cast<std::uint32_t>(offset);
}
void StatChainer(const CSchemaType* type)
{
    StatRequire(type != nullptr);
    // This pinned CS2 ABI has a 40-byte chainer: owner pointer at offset0,
    // serializer path index at32. No plugin-controlled pointer layout is used.
    if (type->m_eTypeCategory == SCHEMA_TYPE_ATOMIC && type->m_eAtomicCategory == SCHEMA_ATOMIC_PLAIN)
    {
        const auto* atomic = static_cast<const CSchemaType_Atomic*>(type);
        const auto* name = atomic->m_sTypeName.Get();
        StatRequire(name && std::strcmp(name,"CNetworkVarChainer") == 0 && atomic->m_nSize == 40 && atomic->m_nAlignment == 8);
        return;
    }
    StatRequire(type->m_eTypeCategory == SCHEMA_TYPE_DECLARED_CLASS && type->m_eAtomicCategory == SCHEMA_ATOMIC_INVALID);
    const auto* info = static_cast<const CSchemaType_DeclaredClass*>(type)->m_pClassInfo;
    StatRequire(ValidClass(info) && info->m_pszName && std::strcmp(info->m_pszName,"CNetworkVarChainer") == 0 &&
        info->m_nSize == 40 && info->m_nAlignment == 8);
}
const char* StatComponentName(std::uint32_t key)
{
    return key == KEELS2_PLAYER_STAT_MONEY ? "CCSPlayerController_InGameMoneyServices" : "CCSPlayerController_ActionTrackingServices";
}
void* StatAddress(void* system, const KeelCs2EntityIdentity& entity, const KeelCs2PlayerStatSchema& schema,
    const KeelCs2PlayerStatisticsBindings& bindings, void*& controller)
{
    StatRequire(system && StatKey(schema.key) && bindings.controller_vtable && bindings.money_vtable &&
        bindings.tracking_vtable && bindings.notify,KEEL_RESULT_INVALID_ARGUMENT);
    const auto* root = static_cast<const CSchemaClassInfo*>(schema.controller_class);
    const auto* type = static_cast<const CSchemaClassInfo*>(schema.component_class);
    StatRequire(ValidClass(root) && ValidClass(type) && root->m_pszName && type->m_pszName &&
        std::strcmp(root->m_pszName,"CCSPlayerController") == 0 && std::strcmp(type->m_pszName,StatComponentName(schema.key)) == 0);
    const auto pointer_offset = StatSpan(root,schema.pointer_offset,sizeof(void*),alignof(void*));
    const auto chain_offset = StatSpan(type,schema.chain_offset,40,8);
    const auto value_offset = StatSpan(type,schema.value_offset,4,4);
    StatRequire(pointer_offset >= sizeof(void*) && chain_offset >= sizeof(void*) && value_offset >= sizeof(void*) &&
        (value_offset+4 <= chain_offset || value_offset >= chain_offset+40));
    auto* identity = IdentityByHandle(static_cast<CEntitySystem*>(system),entity.source2_handle);
    StatRequire(identity && identity->GetEntityIndex().Get() == entity.index,KEEL_RESULT_NOT_FOUND);
    StatRequire(identity->m_pClass->GetSchemaBinding() == root);
    controller = identity->m_pInstance;
    const auto address = reinterpret_cast<std::uintptr_t>(controller);
    StatRequire(address % root->m_nAlignment == 0 && pointer_offset+sizeof(void*)-1 <= UINTPTR_MAX-address);
    void** table{};
    std::memcpy(&table,controller,sizeof(table));
    StatRequire(table == bindings.controller_vtable && table[KEELS2_CS2_NETWORK_STATE_CHANGED_SLOT] == bindings.notify);
    void* component{};
    std::memcpy(&component,reinterpret_cast<const void*>(address+pointer_offset),sizeof(component));
    StatRequire(component != nullptr,KEEL_RESULT_NOT_READY);
    const auto component_address = reinterpret_cast<std::uintptr_t>(component);
    StatRequire(component_address % type->m_nAlignment == 0 &&
        static_cast<std::uint32_t>(type->m_nSize)-1 <= UINTPTR_MAX-component_address);
    std::memcpy(&table,component,sizeof(table));
    StatRequire(table == (schema.key == KEELS2_PLAYER_STAT_MONEY ? bindings.money_vtable : bindings.tracking_vtable));
    void* owner{};
    std::memcpy(&owner,reinterpret_cast<const void*>(component_address+chain_offset),sizeof(owner));
    StatRequire(owner == controller);
    return reinterpret_cast<void*>(component_address+value_offset);
}
}

extern "C" KeelResult KeelCs2_ResolvePlayerStatSchema(void* schema_system, const char* module, std::uint32_t key,
    KeelCs2PlayerStatSchema* output)
{
    if (output) *output = {};
    if (!output || !schema_system || !module || !*module || !StatKey(key)) return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        auto* scope = reinterpret_cast<ISchemaSystemTypeScope*>(static_cast<ISchemaSystem*>(schema_system)->FindTypeScopeForModule(module));
        StatRequire(scope != nullptr,KEEL_RESULT_NOT_FOUND);
        auto* controller = StatClass(scope,"CCSPlayerController");
        auto* component = StatClass(scope,StatComponentName(key));
        auto* base = StatClass(scope,"CPlayerControllerComponent");
        auto* entity = StatClass(scope,"CBaseEntity");
        StatRequire(StatBase(controller,entity) == 0);
        const auto& pointer = StatField(controller,key == KEELS2_PLAYER_STAT_MONEY ? "m_pInGameMoneyServices" : "m_pActionTrackingServices");
        StatRequire(pointer.m_pType->m_eTypeCategory == SCHEMA_TYPE_POINTER && pointer.m_pType->m_eAtomicCategory == SCHEMA_ATOMIC_INVALID);
        StatDeclared(static_cast<const CSchemaType_Ptr*>(pointer.m_pType)->m_pObjectType,component);
        const auto pointer_offset = StatSpan(controller,pointer.m_nSingleInheritanceOffset,sizeof(void*),alignof(void*));
        const auto& chainer = StatField(base,"__m_pChainEntity");
        StatChainer(chainer.m_pType);
        const auto chain_offset = StatBase(component,base)+StatSpan(base,chainer.m_nSingleInheritanceOffset,40,8);
        StatSpan(component,static_cast<std::int32_t>(chain_offset),40,8);
        auto* values = component;
        std::uint32_t embedded_offset{};
        if (key != KEELS2_PLAYER_STAT_MONEY)
        {
            auto* match = StatClass(scope,"CSMatchStats_t");
            values = StatClass(scope,"CSPerRoundStats_t");
            const auto& embedded = StatField(component,"m_matchStats");
            StatDeclared(embedded.m_pType,match);
            embedded_offset = StatSpan(component,embedded.m_nSingleInheritanceOffset,
                static_cast<std::uint32_t>(match->m_nSize),match->m_nAlignment)+StatBase(match,values);
        }
        const char* name = key == KEELS2_PLAYER_STAT_MONEY ? "m_iAccount" : key == KEELS2_PLAYER_STAT_MATCH_KILLS ? "m_iKills" :
            key == KEELS2_PLAYER_STAT_MATCH_DEATHS ? "m_iDeaths" : "m_iAssists";
        const auto& field = StatField(values,name);
        StatRequire(field.m_pType->m_eTypeCategory == SCHEMA_TYPE_BUILTIN && field.m_pType->m_eAtomicCategory == SCHEMA_ATOMIC_INVALID);
        const auto* integer = static_cast<const CSchemaType_Builtin*>(field.m_pType);
        StatRequire(integer->m_eBuiltinType == SCHEMA_BUILTIN_TYPE_INT32 && integer->m_nSize == 4);
        const auto value_offset = embedded_offset+StatSpan(values,field.m_nSingleInheritanceOffset,4,4);
        StatSpan(component,static_cast<std::int32_t>(value_offset),4,4);
        StatRequire(pointer_offset >= sizeof(void*) && chain_offset >= sizeof(void*) && value_offset >= sizeof(void*) &&
            (value_offset+4 <= chain_offset || value_offset >= chain_offset+40));
        *output = {controller,component,static_cast<std::int32_t>(pointer_offset),static_cast<std::int32_t>(chain_offset),
            static_cast<std::int32_t>(value_offset),key};
        return KEEL_RESULT_OK;
    }
    catch (const StatFailure& failure) { return failure.result; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
extern "C" KeelResult KeelCs2_ReadPlayerStat(void* system, const KeelCs2EntityIdentity* entity,
    const KeelCs2PlayerStatSchema* schema, const KeelCs2PlayerStatisticsBindings* bindings, std::int32_t* output)
{
    if (output) *output = 0;
    if (!entity || !schema || !bindings || !output) return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        void* controller{};
        const auto* address = StatAddress(system,*entity,*schema,*bindings,controller);
        std::memcpy(output,address,sizeof(*output));
        return KEEL_RESULT_OK;
    }
    catch (const StatFailure& failure) { return failure.result; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
extern "C" KeelResult KeelCs2_WritePlayerStat(void* system, const KeelCs2EntityIdentity* entity,
    const KeelCs2PlayerStatSchema* schema, const KeelCs2PlayerStatisticsBindings* bindings, std::int32_t value)
{
    if (!entity || !schema || !bindings || value < 0) return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        void* controller{};
        auto* address = StatAddress(system,*entity,*schema,*bindings,controller);
        std::int32_t previous{};
        std::memcpy(&previous,address,sizeof(previous));
        if (previous == value) return KEEL_RESULT_OK;
        const NetworkStateChangedData changed(true);
        const auto notify = NativeActionFunction<void (*)(void*,const NetworkStateChangedData&)>(bindings->notify);
        std::memcpy(address,&value,sizeof(value));
        notify(controller,changed);
        // Component, controller and schema lifetimes can change in callbacks.
        // Do not touch any of those borrowed pointers after notification.
        return KEEL_RESULT_OK;
    }
    catch (const StatFailure& failure) { return failure.result; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

namespace
{
constexpr const char* kDamageFields[]{"m_flDamage","m_bitsDamageType","m_iDamageCustom",
    "m_hInflictor","m_hAttacker","m_hAbility","m_vecDamageForce","m_vecDamagePosition"};
constexpr std::uint32_t kDamageSizes[]{4,4,4,4,4,4,12,12};
void HookFieldType(const CSchemaType* type, unsigned index)
{
    StatRequire(type != nullptr);
    if (index == 1)
    {
        StatRequire(type->m_eTypeCategory == SCHEMA_TYPE_DECLARED_ENUM && type->m_eAtomicCategory == SCHEMA_ATOMIC_INVALID);
        const auto* info = static_cast<const CSchemaType_DeclaredEnum*>(type)->m_pEnumInfo;
        StatRequire(info && info->m_pszName && std::strcmp(info->m_pszName,"DamageTypes_t") == 0 &&
            info->m_nSize == 4 && info->m_nAlignment == 4);
    }
    else if (index >= 3 && index <= 5)
    {
        StatRequire(type->m_eTypeCategory == SCHEMA_TYPE_ATOMIC && type->m_eAtomicCategory == SCHEMA_ATOMIC_T);
        const auto* atomic = static_cast<const CSchemaType_Atomic_T*>(type);
        const auto* name = atomic->m_sTypeName.Get();
        StatRequire(name && std::string_view(name).starts_with("CHandle<") && std::string_view(name).ends_with('>') &&
            atomic->m_nSize == 4 && atomic->m_nAlignment == 4 && atomic->m_pTemplateType &&
            atomic->m_pTemplateType->m_eTypeCategory == SCHEMA_TYPE_DECLARED_CLASS);
    }
    else if (index >= 6)
    {
        StatRequire(type->m_eTypeCategory == SCHEMA_TYPE_ATOMIC && type->m_eAtomicCategory == SCHEMA_ATOMIC_PLAIN);
        const auto* atomic = static_cast<const CSchemaType_Atomic*>(type);
        const auto* name = atomic->m_sTypeName.Get();
        StatRequire(name && std::strcmp(name,"Vector") == 0 && atomic->m_nSize == 12 && atomic->m_nAlignment == 4);
    }
    else
    {
        StatRequire(type->m_eTypeCategory == SCHEMA_TYPE_BUILTIN && type->m_eAtomicCategory == SCHEMA_ATOMIC_INVALID);
        const auto* builtin = static_cast<const CSchemaType_Builtin*>(type);
        StatRequire(builtin->m_nSize == 4 &&
            builtin->m_eBuiltinType == (index == 0 ? SCHEMA_BUILTIN_TYPE_FLOAT32 : SCHEMA_BUILTIN_TYPE_INT32));
    }
}
void DamageOffsets(const KeelCs2DamageSchema& schema)
{
    const auto* type = static_cast<const CSchemaClassInfo*>(schema.class_info);
    StatRequire(ValidClass(type) && type->m_pszName && std::strcmp(type->m_pszName,"CTakeDamageInfo") == 0);
    for (unsigned i = 0; i < 8; ++i)
    {
        const auto offset = StatSpan(type,schema.offsets[i],kDamageSizes[i],4);
        StatRequire(offset >= sizeof(void*));
        for (unsigned j = 0; j < i; ++j)
            StatRequire(offset + kDamageSizes[i] <= static_cast<unsigned>(schema.offsets[j]) ||
                static_cast<unsigned>(schema.offsets[j]) + kDamageSizes[j] <= offset);
    }
}
const std::byte* DamageAddress(const KeelCs2DamageSchema& schema, const void* record)
{
    StatRequire(record != nullptr,KEEL_RESULT_INVALID_ARGUMENT);
    DamageOffsets(schema);
    const auto* type = static_cast<const CSchemaClassInfo*>(schema.class_info);
    const auto address = reinterpret_cast<std::uintptr_t>(record);
    StatRequire(address % type->m_nAlignment == 0 && static_cast<unsigned>(type->m_nSize)-1 <= UINTPTR_MAX-address);
    return static_cast<const std::byte*>(record);
}
bool DamageNumbers(float damage, const float* force, const float* position)
{
    if (!std::isfinite(damage)) return false;
    for (unsigned i = 0; i < 3; ++i)
        if (!std::isfinite(force[i]) || !std::isfinite(position[i])) return false;
    return true;
}
}

extern "C" KeelResult KeelCs2_ResolveDamageSchema(void* system, const char* module, KeelCs2DamageSchema* output)
{
    if (output) *output = {};
    if (!system || !module || !*module || !output) return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        auto* scope = reinterpret_cast<ISchemaSystemTypeScope*>(static_cast<ISchemaSystem*>(system)->FindTypeScopeForModule(module));
        StatRequire(scope != nullptr,KEEL_RESULT_NOT_FOUND);
        auto* type = StatClass(scope,"CTakeDamageInfo");
        KeelCs2DamageSchema result{}; result.class_info = type;
        for (unsigned i = 0; i < 8; ++i)
        {
            const auto& field = StatField(type,kDamageFields[i]);
            HookFieldType(field.m_pType,i);
            result.offsets[i] = static_cast<std::int32_t>(StatSpan(type,field.m_nSingleInheritanceOffset,kDamageSizes[i],4));
        }
        DamageOffsets(result);
        *output = result;
        return KEEL_RESULT_OK;
    }
    catch (const StatFailure& failure) { return failure.result; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

extern "C" KeelResult KeelCs2_ReadDamage(const KeelCs2DamageSchema* schema, const void* record, KeelDamageInfo* output)
{
    const bool sized = output && output->size == sizeof(*output);
    if (output) { *output = {}; output->size = sizeof(*output); output->inflictor = output->attacker = output->ability = UINT32_MAX; }
    if (!schema || !sized) return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        const auto* address = DamageAddress(*schema,record);
        KeelDamageInfo result{}; result.size = sizeof(result);
        void* values[]{&result.damage,&result.damage_type,&result.damage_custom,&result.inflictor,&result.attacker,
            &result.ability,result.force,result.position};
        for (unsigned i = 0; i < 8; ++i) std::memcpy(values[i],address+schema->offsets[i],kDamageSizes[i]);
        if (!DamageNumbers(result.damage,result.force,result.position)) return KEEL_RESULT_INCOMPATIBLE;
        *output = result;
        return KEEL_RESULT_OK;
    }
    catch (const StatFailure& failure) { return failure.result; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

extern "C" KeelResult KeelCs2_WriteDamage(const KeelCs2DamageSchema* schema, void* record, const KeelDamageEdit* input)
{
    if (!schema || !input || input->size != sizeof(*input)) return KEEL_RESULT_INVALID_ARGUMENT;
    const auto edit = *input;
    if (edit.reserved || edit.damage < 0 || !DamageNumbers(edit.damage,edit.force,edit.position)) return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        auto* address = const_cast<std::byte*>(DamageAddress(*schema,record));
        // All metadata/value checks precede the first mutation. Untouched
        // members include handles, cached attacker metadata and owning vectors.
        std::memcpy(address+schema->offsets[0],&edit.damage,4);
        std::memcpy(address+schema->offsets[1],&edit.damage_type,4);
        std::memcpy(address+schema->offsets[6],edit.force,12);
        std::memcpy(address+schema->offsets[7],edit.position,12);
        return KEEL_RESULT_OK;
    }
    catch (const StatFailure& failure) { return failure.result; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

extern "C" KeelResult KeelCs2_ResolveWeaponSchema(void* system, const char* module, KeelCs2WeaponSchema* output)
{
    if (output) *output = {};
    if (!system || !module || !*module || !output) return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        auto* scope = reinterpret_cast<ISchemaSystemTypeScope*>(static_cast<ISchemaSystem*>(system)->FindTypeScopeForModule(module));
        StatRequire(scope != nullptr,KEEL_RESULT_NOT_FOUND);
        auto* pawn = StatClass(scope,"CCSPlayerPawn");
        auto* base = StatClass(scope,"CBasePlayerPawn");
        auto* component = StatClass(scope,"CPlayer_WeaponServices");
        auto* component_base = StatClass(scope,"CPlayerPawnComponent");
        const auto& pointer = StatField(base,"m_pWeaponServices");
        StatRequire(pointer.m_pType->m_eTypeCategory == SCHEMA_TYPE_POINTER && pointer.m_pType->m_eAtomicCategory == SCHEMA_ATOMIC_INVALID);
        StatDeclared(static_cast<const CSchemaType_Ptr*>(pointer.m_pType)->m_pObjectType,component);
        const auto pointer_offset = StatBase(pawn,base)+StatSpan(base,pointer.m_nSingleInheritanceOffset,sizeof(void*),alignof(void*));
        StatSpan(pawn,static_cast<std::int32_t>(pointer_offset),sizeof(void*),alignof(void*));
        const auto& chain = StatField(component_base,"__m_pChainEntity");
        StatChainer(chain.m_pType);
        const auto chain_offset = StatBase(component,component_base)+StatSpan(component_base,chain.m_nSingleInheritanceOffset,40,8);
        StatSpan(component,static_cast<std::int32_t>(chain_offset),40,8);
        StatRequire(pointer_offset >= sizeof(void*) && chain_offset >= sizeof(void*));
        *output = {pawn,component,static_cast<std::int32_t>(pointer_offset),static_cast<std::int32_t>(chain_offset)};
        return KEEL_RESULT_OK;
    }
    catch (const StatFailure& failure) { return failure.result; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

extern "C" KeelResult KeelCs2_WeaponMatches(void* system, const KeelCs2EntityIdentity* pawn,
    const KeelCs2WeaponSchema* schema, const void* candidate, KeelBool* matches)
{
    if (matches) *matches = KEEL_FALSE;
    if (!system || !pawn || !schema || !candidate || !matches) return KEEL_RESULT_INVALID_ARGUMENT;
    try
    {
        const auto* root = static_cast<const CSchemaClassInfo*>(schema->pawn_class);
        const auto* type = static_cast<const CSchemaClassInfo*>(schema->component_class);
        StatRequire(ValidClass(root) && ValidClass(type) && root->m_pszName && type->m_pszName &&
            std::strcmp(root->m_pszName,"CCSPlayerPawn") == 0 && std::strcmp(type->m_pszName,"CPlayer_WeaponServices") == 0);
        const auto pointer_offset = StatSpan(root,schema->pointer_offset,sizeof(void*),alignof(void*));
        const auto chain_offset = StatSpan(type,schema->chain_offset,40,8);
        StatRequire(pointer_offset >= sizeof(void*) && chain_offset >= sizeof(void*));
        const auto* identity = IdentityByHandle(static_cast<CEntitySystem*>(system),pawn->source2_handle);
        StatRequire(identity && identity->GetEntityIndex().Get() == pawn->index,KEEL_RESULT_NOT_FOUND);
        StatRequire(identity->m_pClass->GetSchemaBinding() == root);
        const auto address = reinterpret_cast<std::uintptr_t>(identity->m_pInstance);
        StatRequire(address % root->m_nAlignment == 0 && pointer_offset+sizeof(void*)-1 <= UINTPTR_MAX-address);
        const void* component{};
        std::memcpy(&component,reinterpret_cast<const void*>(address+pointer_offset),sizeof(component));
        if (component != candidate) return KEEL_RESULT_OK;
        const auto component_address = reinterpret_cast<std::uintptr_t>(component);
        StatRequire(component_address % type->m_nAlignment == 0 && chain_offset+40-1 <= UINTPTR_MAX-component_address);
        const void* owner{};
        std::memcpy(&owner,reinterpret_cast<const void*>(component_address+chain_offset),sizeof(owner));
        StatRequire(owner == identity->m_pInstance,KEEL_RESULT_NOT_FOUND);
        *matches = KEEL_TRUE;
        return KEEL_RESULT_OK;
    }
    catch (const StatFailure& failure) { return failure.result; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

namespace
{
CEntityIdentity* CreatedIdentity(CEntitySystem* system, const KeelCs2EntityIdentity& entity,
    bool require_pre_spawn) noexcept
{
    if (!system || entity.index < 0 || entity.index >= MAX_TOTAL_ENTITIES ||
        entity.source2_handle == INVALID_EHANDLE_INDEX) return nullptr;
    const CEntityHandle handle(entity.source2_handle);
    if (handle.GetEntryIndex() != entity.index) return nullptr;
    auto* chunk = system->m_EntityList.m_pIdentityChunks[static_cast<std::size_t>(entity.index) / MAX_ENTITIES_IN_LIST];
    if (!chunk) return nullptr;
    auto* identity = &chunk[static_cast<std::size_t>(entity.index) % MAX_ENTITIES_IN_LIST];
    constexpr std::uint32_t rejected = EF_IS_INVALID_EHANDLE | EF_DELETE_IN_PROGRESS |
        EF_MARKED_FOR_DELETE | EF_IS_CONSTRUCTION_IN_PROGRESS;
    if (identity->GetEntityIndex().Get() != entity.index ||
        static_cast<std::uint32_t>(identity->GetRefEHandle().ToInt()) != entity.source2_handle ||
        !identity->m_pInstance || !identity->m_pClass ||
        reinterpret_cast<std::uintptr_t>(identity->m_pInstance) % alignof(CEntityInstance) ||
        (static_cast<std::uint32_t>(identity->m_flags) & rejected) ||
        (require_pre_spawn && (!(identity->m_flags & EF_IS_PRE_SPAWN) ||
            (identity->m_flags & (EF_SPAWN_IN_PROGRESS | EF_IS_ANONYMOUS_ALLOCATION)))) ||
        identity->m_pInstance->m_pEntity != identity) return nullptr;
    return identity;
}
KeelResult CheckCreatedClass(CEntityIdentity* identity, void* base)
{
    if (!identity) return KEEL_RESULT_NOT_FOUND;
    auto* type = identity->m_pClass->GetSchemaBinding();
    auto* required = static_cast<CSchemaClassInfo*>(base);
    StatRequire(ValidClass(type) && ValidClass(required) && required->m_pszName &&
        std::strcmp(required->m_pszName,"CBaseEntity") == 0 && StatBase(type,required) == 0);
    const auto instance = reinterpret_cast<std::uintptr_t>(identity->m_pInstance);
    StatRequire(instance % type->m_nAlignment == 0 && static_cast<std::uint32_t>(type->m_nSize) >= sizeof(CEntityInstance) &&
        static_cast<std::uint32_t>(type->m_nSize)-1 <= UINTPTR_MAX-instance);
    return KEEL_RESULT_OK;
}
KeelResult DispatchCreatedEntity(void* system, const KeelCs2EntityIdentity* entity, void* base,
    const KeelCs2EntityConstructionBindings* bindings, const void* key_values, KeelBool* invoked, bool spawn)
{
    if (invoked) *invoked = KEEL_FALSE;
    if (!system || !entity || (spawn && !base) || !bindings || !bindings->create || !bindings->spawn || !bindings->remove || !invoked)
        return KEEL_RESULT_INVALID_ARGUMENT;
    try {
        auto* identity = CreatedIdentity(static_cast<CEntitySystem*>(system),*entity,spawn);
        const auto result = spawn ? CheckCreatedClass(identity,base) : identity ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
        if (result != KEEL_RESULT_OK) return result;
        if (!spawn && (identity->m_flags & EF_SPAWN_IN_PROGRESS)) return KEEL_RESULT_BUSY;
        if (!spawn && (identity->m_flags & EF_IS_ANONYMOUS_ALLOCATION)) return KEEL_RESULT_INCOMPATIBLE;
        auto* instance = identity->m_pInstance;
        *invoked = KEEL_TRUE;
        if (spawn) NativeActionFunction<void (*)(void*,const void*)>(bindings->spawn)(instance,key_values);
        else NativeActionFunction<void (*)(void*)>(bindings->remove)(instance);
        return KEEL_RESULT_OK;
    } catch (const StatFailure& failure) { return failure.result; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
}
extern "C" KeelResult KeelCs2_CreateEntity(const KeelCs2EntityConstructionBindings* bindings,
    const char* class_name, void** instance)
{
    if (instance) *instance = nullptr;
    if (!bindings || !bindings->create || !bindings->spawn || !bindings->remove || !class_name || !*class_name || !instance)
        return KEEL_RESULT_INVALID_ARGUMENT;
    std::array<char,128> copy{};
    std::size_t length{};
    while (length < copy.size() && class_name[length]) {
        const auto c = static_cast<unsigned char>(class_name[length]);
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '_')
            return KEEL_RESULT_INVALID_ARGUMENT;
        copy[length++] = static_cast<char>(c);
    }
    if (length == copy.size()) return KEEL_RESULT_INVALID_ARGUMENT;
    try {
        *instance = NativeActionFunction<void* (*)(const char*,int)>(bindings->create)(copy.data(),-1);
        return *instance ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    } catch (...) { *instance = nullptr; return KEEL_RESULT_ENGINE_FAILURE; }
}
extern "C" KeelResult KeelCs2_CaptureCreatedEntity(void* system, const void* instance,
    KeelCs2EntityIdentity* output)
{
    if (output) *output = {};
    if (!system || !instance || !output) return KEEL_RESULT_INVALID_ARGUMENT;
    try {
        auto* registry = static_cast<CEntitySystem*>(system);
        for (std::size_t chunk_index = 0; chunk_index < std::size(registry->m_EntityList.m_pIdentityChunks); ++chunk_index) {
            auto* chunk = registry->m_EntityList.m_pIdentityChunks[chunk_index];
            if (!chunk) continue;
            for (std::size_t offset = 0; offset < MAX_ENTITIES_IN_LIST; ++offset) {
                auto* candidate = &chunk[offset];
                if (candidate->m_pInstance != instance) continue;
                const KeelCs2EntityIdentity entity{static_cast<std::int32_t>(chunk_index*MAX_ENTITIES_IN_LIST+offset),
                    static_cast<std::uint32_t>(candidate->GetRefEHandle().ToInt())};
                if (!CreatedIdentity(registry,entity,false)) return KEEL_RESULT_NOT_FOUND;
                *output = entity;
                return KEEL_RESULT_OK;
            }
        }
        return KEEL_RESULT_NOT_FOUND;
    } catch (const StatFailure& failure) { return failure.result; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
extern "C" KeelResult KeelCs2_ValidateCreatedEntity(void* system, const KeelCs2EntityIdentity* entity,
    void* base, KeelBool require_pre_spawn)
{
    if (!system || !entity || !base || (require_pre_spawn != KEEL_FALSE && require_pre_spawn != KEEL_TRUE))
        return KEEL_RESULT_INVALID_ARGUMENT;
    try { return CheckCreatedClass(CreatedIdentity(static_cast<CEntitySystem*>(system),*entity,require_pre_spawn != KEEL_FALSE),base); }
    catch (const StatFailure& failure) { return failure.result; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
extern "C" KeelResult KeelCs2_SnapshotEntityHandles(void* system, uint32_t* handles, uint32_t count)
{
    static_assert(KEELS2_CS2_ENTITY_CAPACITY == MAX_TOTAL_ENTITIES);
    if (!system || !handles || count != KEELS2_CS2_ENTITY_CAPACITY) return KEEL_RESULT_INVALID_ARGUMENT;
    auto* registry = static_cast<CEntitySystem*>(system);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto* chunk = registry->m_EntityList.m_pIdentityChunks[index / MAX_ENTITIES_IN_LIST];
        const auto* identity = chunk ? &chunk[index % MAX_ENTITIES_IN_LIST] : nullptr;
        handles[index] = identity && identity->m_pInstance ?
            static_cast<std::uint32_t>(identity->GetRefEHandle().ToInt()) : INVALID_EHANDLE_INDEX;
    }
    return KEEL_RESULT_OK;
}
extern "C" KeelResult KeelCs2_ValidatePendingEntity(void* system, const KeelCs2EntityIdentity* entity)
{
    if (!system || !entity) return KEEL_RESULT_INVALID_ARGUMENT;
    return CreatedIdentity(static_cast<CEntitySystem*>(system),*entity,true) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
}
extern "C" KeelResult KeelCs2_FinishCreatedSpawn(void* system, const KeelCs2EntityIdentity* entity,
    const KeelCs2EntityConstructionBindings* bindings)
{
    if (!system || !entity || !bindings || !bindings->create || !bindings->spawn || !bindings->remove)
        return KEEL_RESULT_INVALID_ARGUMENT;
    auto* identity = CreatedIdentity(static_cast<CEntitySystem*>(system),*entity,false);
    if (!identity) return KEEL_RESULT_NOT_FOUND;
    if (identity->m_flags & EF_SPAWN_IN_PROGRESS) return KEEL_RESULT_BUSY;
    if (!(identity->m_flags & EF_IS_PRE_SPAWN)) return KEEL_RESULT_OK;
    if (identity->m_flags & EF_IS_ANONYMOUS_ALLOCATION) return KEEL_RESULT_INCOMPATIBLE;
    KeelBool invoked{};
    const auto result = KeelCs2_RemoveCreatedEntity(system,entity,bindings,&invoked);
    return result == KEEL_RESULT_OK ? KEEL_RESULT_ENGINE_FAILURE : result;
}
extern "C" KeelResult KeelCs2_SpawnCreatedEntity(void* system, const KeelCs2EntityIdentity* entity,
    void* base, const KeelCs2EntityConstructionBindings* bindings, const void* key_values, KeelBool* invoked)
{ return DispatchCreatedEntity(system,entity,base,bindings,key_values,invoked,true); }
extern "C" KeelResult KeelCs2_RemoveCreatedEntity(void* system, const KeelCs2EntityIdentity* entity,
    const KeelCs2EntityConstructionBindings* bindings, KeelBool* invoked)
{ return DispatchCreatedEntity(system,entity,nullptr,bindings,nullptr,invoked,false); }

extern "C" KeelResult KeelCs2_ResolveCreatedEntityPointer(void* system, const KeelCs2EntityIdentity* entity,
    const char* class_name, void** output)
{
    if (output) *output = nullptr;
    if (!system || !entity || !class_name || !*class_name || !output) return KEEL_RESULT_INVALID_ARGUMENT;
    try {
        auto* identity = CreatedIdentity(static_cast<CEntitySystem*>(system),*entity,true);
        if (!identity) return KEEL_RESULT_NOT_FOUND;
        auto* type = identity->m_pClass->GetSchemaBinding();
        if (!ValidClass(type) || !type->m_pszName || std::strcmp(type->m_pszName,class_name) ||
            type->m_nSize < static_cast<int>(sizeof(CEntityInstance)) ||
            reinterpret_cast<std::uintptr_t>(identity->m_pInstance) % type->m_nAlignment ||
            static_cast<std::uint32_t>(type->m_nSize)-1 > UINTPTR_MAX-reinterpret_cast<std::uintptr_t>(identity->m_pInstance))
            return KEEL_RESULT_INCOMPATIBLE;
        *output = identity->m_pInstance;
        return KEEL_RESULT_OK;
    } catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}

extern "C" KeelResult KeelCs2_ResolveEntityToolBase(void* system, const char* module, std::uint32_t kind, void** output)
{
    if (output) *output = nullptr;
    if (!system || !module || !*module || !output ||
        (kind != KEELS2_ENTITY_TOOL_TELEPORT && kind != KEELS2_ENTITY_TOOL_SET_MODEL && kind != KEELS2_ENTITY_TOOL_REMOVE))
        return KEEL_RESULT_INVALID_ARGUMENT;
    try {
        auto* scope = reinterpret_cast<ISchemaSystemTypeScope*>(static_cast<ISchemaSystem*>(system)->FindTypeScopeForModule(module));
        if (!scope) return KEEL_RESULT_NOT_READY;
        *output = StatClass(scope,kind == KEELS2_ENTITY_TOOL_SET_MODEL ? "CBaseModelEntity" : "CBaseEntity");
        return KEEL_RESULT_OK;
    } catch (const StatFailure& failure) { return failure.result; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
namespace {
KeelResult PrepareEntityTool(void* system, const KeelCs2EntityIdentity* entity, void* base,
    KeelCs2EntityToolContext* context, bool created)
{
    if (context) *context = {};
    if (!system || !entity || !base || !context) return KEEL_RESULT_INVALID_ARGUMENT;
    try {
        auto* identity = created ? CreatedIdentity(static_cast<CEntitySystem*>(system),*entity,true) :
            IdentityByHandle(static_cast<CEntitySystem*>(system),entity->source2_handle);
        if (!identity || identity->GetEntityIndex().Get() != entity->index) return KEEL_RESULT_NOT_FOUND;
        auto* type = identity->m_pClass->GetSchemaBinding();
        auto* required = static_cast<CSchemaClassInfo*>(base);
        StatRequire(ValidClass(type) && ValidClass(required) && type->m_pszName && required->m_pszName);
        StatRequire(std::strcmp(required->m_pszName,"CBaseEntity") == 0 || (!created && std::strcmp(required->m_pszName,"CBaseModelEntity") == 0));
        StatRequire(StatBase(type,required) == 0);
        const auto instance = reinterpret_cast<std::uintptr_t>(identity->m_pInstance);
        StatRequire(instance % type->m_nAlignment == 0 && static_cast<std::uint32_t>(type->m_nSize) >= sizeof(void*) &&
            sizeof(void*)-1 <= UINTPTR_MAX-instance);
        if (created) StatRequire(static_cast<std::uint32_t>(type->m_nSize) >= sizeof(CEntityInstance) &&
            static_cast<std::uint32_t>(type->m_nSize)-1 <= UINTPTR_MAX-instance);
        std::size_t length{};
        while (length < sizeof(context->class_name) && type->m_pszName[length]) ++length;
        StatRequire(length && length < sizeof(context->class_name));
        context->class_info = type; context->base_class = base;
        std::memcpy(context->class_name,type->m_pszName,length+1);
        return KEEL_RESULT_OK;
    } catch (const StatFailure& failure) { return failure.result; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
KeelResult ApplyEntityTool(void* system, const KeelCs2EntityIdentity* entity,
    const KeelCs2EntityToolContext* context, const KeelCs2EntityToolBindings* bindings,
    const KeelCs2EntityToolClass* target, std::uint32_t kind, const KeelEntityTeleport* request, const char* model, bool created)
{
#if defined(_WIN32)
    constexpr std::uint32_t slot = 163;
#else
    constexpr std::uint32_t slot = 162;
#endif
    if (!system || !entity || !context || !bindings || !target || !target->vtable || !target->teleport ||
        !bindings->set_model || !bindings->remove || bindings->teleport_slot != slot || bindings->reserved)
        return KEEL_RESULT_INVALID_ARGUMENT;
    KeelEntityTeleport copy{};
    std::array<char,KEELS2_ENTITY_MODEL_MAX_BYTES+1> asset{};
    if (kind == KEELS2_ENTITY_TOOL_TELEPORT) {
        if (!request || model || request->size != sizeof(*request) || !request->flags || (request->flags & ~7u))
            return KEEL_RESULT_INVALID_ARGUMENT;
        copy = *request;
        const float* vectors[]{copy.position,copy.angles,copy.velocity};
        for (unsigned i = 0; i < 3; ++i) if (copy.flags & (1u<<i))
            for (unsigned j = 0; j < 3; ++j) if (!std::isfinite(vectors[i][j])) return KEEL_RESULT_INVALID_ARGUMENT;
    } else if (kind == KEELS2_ENTITY_TOOL_SET_MODEL) {
        if (request || !model || !*model) return KEEL_RESULT_INVALID_ARGUMENT;
        std::size_t length{};
        while (length < asset.size() && model[length]) {
            const auto c = static_cast<unsigned char>(model[length]);
            if (c < 32 || c == 127) return KEEL_RESULT_INVALID_ARGUMENT;
            asset[length++] = static_cast<char>(c);
        }
        if (length == asset.size()) return KEEL_RESULT_INVALID_ARGUMENT;
    } else if (kind != KEELS2_ENTITY_TOOL_REMOVE || request || model) return KEEL_RESULT_INVALID_ARGUMENT;
    try {
        KeelCs2EntityToolContext current{};
        const auto result = PrepareEntityTool(system,entity,context->base_class,&current,created);
        if (result != KEEL_RESULT_OK) return result;
        if (current.class_info != context->class_info || std::memcmp(current.class_name,context->class_name,sizeof(current.class_name)))
            return KEEL_RESULT_INCOMPATIBLE;
        const auto* base = static_cast<CSchemaClassInfo*>(current.base_class);
        if (std::strcmp(base->m_pszName,kind == KEELS2_ENTITY_TOOL_SET_MODEL ? "CBaseModelEntity" : "CBaseEntity"))
            return KEEL_RESULT_INCOMPATIBLE;
        auto* identity = created ? CreatedIdentity(static_cast<CEntitySystem*>(system),*entity,true) :
            IdentityByHandle(static_cast<CEntitySystem*>(system),entity->source2_handle);
        if (!identity || identity->GetEntityIndex().Get() != entity->index) return KEEL_RESULT_NOT_FOUND;
        auto* instance = identity->m_pInstance;
        void** table{}; std::memcpy(&table,instance,sizeof(table));
        if (table != target->vtable || table[slot] != target->teleport) return KEEL_RESULT_INCOMPATIBLE;
        // All validation is finished; callbacks may delete this entity. Do not
        // access its identity, class, instance or vtable after invoking the game.
        if (kind == KEELS2_ENTITY_TOOL_TELEPORT) {
            Vector position(copy.position[0],copy.position[1],copy.position[2]);
            QAngle angles(copy.angles[0],copy.angles[1],copy.angles[2]);
            Vector velocity(copy.velocity[0],copy.velocity[1],copy.velocity[2]);
            NativeActionFunction<void (*)(void*,const Vector*,const QAngle*,const Vector*)>(target->teleport)(instance,
                copy.flags & KEELS2_TELEPORT_POSITION ? &position : nullptr,
                copy.flags & KEELS2_TELEPORT_ANGLES ? &angles : nullptr,
                copy.flags & KEELS2_TELEPORT_VELOCITY ? &velocity : nullptr);
        } else if (kind == KEELS2_ENTITY_TOOL_SET_MODEL)
            static_cast<void>(NativeActionFunction<void* (*)(void*,const char*)>(bindings->set_model)(instance,asset.data()));
        else NativeActionFunction<void (*)(void*)>(bindings->remove)(instance);
        return KEEL_RESULT_OK;
    } catch (const StatFailure& failure) { return failure.result; }
    catch (...) { return KEEL_RESULT_ENGINE_FAILURE; }
}
}
extern "C" KeelResult KeelCs2_PrepareEntityTool(void* system, const KeelCs2EntityIdentity* entity, void* base,
    KeelCs2EntityToolContext* context)
{ return PrepareEntityTool(system,entity,base,context,false); }
extern "C" KeelResult KeelCs2_PrepareCreatedEntityTool(void* system, const KeelCs2EntityIdentity* entity, void* base,
    KeelCs2EntityToolContext* context)
{ return PrepareEntityTool(system,entity,base,context,true); }
extern "C" KeelResult KeelCs2_ApplyEntityTool(void* system, const KeelCs2EntityIdentity* entity,
    const KeelCs2EntityToolContext* context, const KeelCs2EntityToolBindings* bindings,
    const KeelCs2EntityToolClass* target, std::uint32_t kind, const KeelEntityTeleport* request, const char* model)
{ return ApplyEntityTool(system,entity,context,bindings,target,kind,request,model,false); }
extern "C" KeelResult KeelCs2_TeleportCreatedEntity(void* system, const KeelCs2EntityIdentity* entity,
    const KeelCs2EntityToolContext* context, const KeelCs2EntityToolBindings* bindings,
    const KeelCs2EntityToolClass* target, const KeelEntityTeleport* request)
{ return ApplyEntityTool(system,entity,context,bindings,target,KEELS2_ENTITY_TOOL_TELEPORT,request,nullptr,true); }
