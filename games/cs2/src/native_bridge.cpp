#include <keels2/cs2/native_bridge.h>
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
#include <new>
#include <type_traits>

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

bool ValidClass(const CSchemaClassInfo* info) noexcept
{
    return info && info->m_nSize > 0 && info->m_nSize <= 16 * 1024 * 1024 &&
        PowerOfTwo(info->m_nAlignment) && info->m_nAlignment <= 128 &&
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
            if (!field.m_pType ||
                field.m_pType->m_eTypeCategory != SCHEMA_TYPE_BUILTIN ||
                field.m_pType->m_eAtomicCategory != SCHEMA_ATOMIC_INVALID)
            {
                return KEEL_RESULT_INCOMPATIBLE;
            }
            const auto* builtin = static_cast<const CSchemaType_Builtin*>(field.m_pType);
            if (builtin->m_eBuiltinType != expected.schema_type ||
                builtin->m_nSize != expected.size || field.m_nSingleInheritanceOffset < 0)
            {
                return KEEL_RESULT_INCOMPATIBLE;
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
