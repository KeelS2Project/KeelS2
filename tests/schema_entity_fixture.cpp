#include <keels2/keelhook.hpp>
#include <keels2/schema.hpp>
#include <eiface.h>
#include <entity2/entityclass.h>
#include <entity2/entityinstance.h>
#include <entity2/entitysystem.h>
#include <schemasystem/schemasystem.h>
#include <google/protobuf/message.h>
#include <steam/steamclientpublic.h>

#if defined(KEELS2_SCHEMA_FIXTURE_NATIVE_TEST)
#include <keels2/cs2/native_bridge.h>
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <limits>
#include <string>
#include <stdexcept>
#include <vector>

#if defined(_WIN32)
#define KEELS2_SCHEMA_FIXTURE_EXPORT __declspec(dllexport)
#else
#define KEELS2_SCHEMA_FIXTURE_EXPORT __attribute__((visibility("default")))
#endif

namespace
{

struct RawInterface
{
    void** vtable;
};

template <typename Function>
void* FunctionAddress(Function function)
{
    static_assert(sizeof(Function) == sizeof(void*));
    void* address{};
    std::memcpy(&address, &function, sizeof(address));
    return address;
}

void ValidationSlot(void*)
{
}

alignas(CSchemaType_Builtin)
std::array<std::byte, sizeof(CSchemaType_Builtin)> g_int32_type_storage{};
SchemaClassFieldData_t g_base_fields[3]{};
CSchemaClassInfo g_damage_class{};
CSchemaClassInfo g_base_class{};
SchemaBaseClassInfoData_t g_derived_bases[1]{};
CSchemaClassInfo g_derived_class{};
std::uint32_t g_schema_lookup_count{};

CSchemaClassInfo* DeclaredClass(const char* name)
{
    ++g_schema_lookup_count;
    if (name && std::strcmp(name, "CBaseEntity") == 0)
    {
        return &g_base_class;
    }
    if (name && std::strcmp(name, "CCSPlayerPawn") == 0)
    {
        return &g_derived_class;
    }
    if (name && std::strcmp(name, "CTakeDamageInfo") == 0)
        return &g_damage_class;
    return nullptr;
}

#if defined(_WIN32)
SchemaMetaInfoHandle_t<CSchemaClassInfo>* FindDeclaredClass(
    void*,
    SchemaMetaInfoHandle_t<CSchemaClassInfo>* output,
    const char* name)
{
    *output = DeclaredClass(name);
    return output;
}
#else
SchemaMetaInfoHandle_t<CSchemaClassInfo> FindDeclaredClass(void*, const char* name)
{
    return DeclaredClass(name);
}
#endif

std::array<void*, 3> g_scope_vtable = [] {
    std::array<void*, 3> table{};
    table[2] = FunctionAddress(&FindDeclaredClass);
    return table;
}();
RawInterface g_scope{g_scope_vtable.data()};

CSchemaSystemTypeScope* FindTypeScopeForModule(void*, const char* module, const char** binding)
{
#if defined(_WIN32)
    constexpr const char* expected = "server.dll";
#else
    constexpr const char* expected = "libserver.so";
#endif
    if (binding)
    {
        *binding = module;
    }
    return module && std::strcmp(module, expected) == 0
        ? reinterpret_cast<CSchemaSystemTypeScope*>(&g_scope)
        : nullptr;
}

std::array<void*, 14> g_schema_vtable = [] {
    std::array<void*, 14> table{};
    table[0] = FunctionAddress(&ValidationSlot);
    table[13] = FunctionAddress(&FindTypeScopeForModule);
    return table;
}();
RawInterface g_schema_system{g_schema_vtable.data()};

constexpr std::size_t Align(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

constexpr std::size_t kHealthOffset = Align(sizeof(CEntityInstance), alignof(int32));
constexpr std::size_t kEntitySize = Align(kHealthOffset + 32, 16);
constexpr std::int32_t kEntityIndex = 7;

alignas(CGameEntitySystem)
std::array<std::byte, sizeof(CGameEntitySystem)> g_entity_system_storage{};
alignas(CEntityIdentity)
std::array<std::byte, sizeof(CEntityIdentity) * MAX_ENTITIES_IN_LIST> g_identity_storage{};
alignas(CEntityInstance)
std::array<std::byte, kEntitySize> g_entity_storage{};
alignas(CEntityClass)
std::array<std::byte, sizeof(CEntityClass)> g_entity_class_storage{};
CEntityClassInfo g_entity_class_info{};

std::array<void*, 1> g_entity_system_vtable{
    FunctionAddress(&ValidationSlot)
};

#if defined(_WIN32)
constexpr std::size_t kEntitySystemOffset = 88;
#else
constexpr std::size_t kEntitySystemOffset = 80;
#endif

alignas(void*) std::array<std::byte, 128> g_game_resource_storage{};
std::array<void*, 1> g_game_resource_vtable{
    FunctionAddress(&ValidationSlot)
};

CGameEntitySystem* EntitySystem()
{
    return reinterpret_cast<CGameEntitySystem*>(g_entity_system_storage.data());
}

CEntityIdentity* Identity()
{
    auto* chunk = reinterpret_cast<CEntityIdentity*>(g_identity_storage.data());
    return &chunk[kEntityIndex];
}

CEntityInstance* EntityInstance()
{
    return reinterpret_cast<CEntityInstance*>(g_entity_storage.data());
}

CEntityClass* EntityClass()
{
    return reinterpret_cast<CEntityClass*>(g_entity_class_storage.data());
}

void StorePointer(std::byte* destination, void* value)
{
    std::memcpy(destination, &value, sizeof(value));
}

void SetGameEntitySystem(bool ready)
{
    void* value = ready ? EntitySystem() : nullptr;
    StorePointer(g_game_resource_storage.data() + kEntitySystemOffset, value);
}

void SetHandle(CEntityIdentity& identity, int serial)
{
    const CEntityHandle handle(kEntityIndex, serial);
    identity.m_EHandle = handle;
}

void SetHealth(int32 health)
{
    std::memcpy(g_entity_storage.data() + kHealthOffset, &health, sizeof(health));
}

void Reset()
{
    g_schema_lookup_count = 0;
    g_int32_type_storage.fill(std::byte{});
    g_entity_system_storage.fill(std::byte{});
    g_identity_storage.fill(std::byte{});
    g_entity_storage.fill(std::byte{});
    g_entity_class_storage.fill(std::byte{});
    g_game_resource_storage.fill(std::byte{});

    auto* int32_type = reinterpret_cast<CSchemaType_Builtin*>(g_int32_type_storage.data());
    int32_type->m_eTypeCategory = SCHEMA_TYPE_BUILTIN;
    int32_type->m_eAtomicCategory = SCHEMA_ATOMIC_INVALID;
    int32_type->m_eBuiltinType = SCHEMA_BUILTIN_TYPE_INT32;
    int32_type->m_nSize = sizeof(int32);

    g_base_fields[0] = {
        "m_iHealth",
        int32_type,
        static_cast<int>(kHealthOffset),
        0,
        nullptr
    };
    g_base_class = {};
    g_base_class.m_pszName = "CBaseEntity";
    g_base_class.m_nSize = static_cast<int>(kEntitySize);
    g_base_class.m_nFieldCount = 1;
    g_base_class.m_nAlignment = 16;
    g_base_class.m_pFields = g_base_fields;

    g_derived_bases[0] = {0, &g_base_class};
    g_derived_class = {};
    g_derived_class.m_pszName = "CCSPlayerPawn";
    g_derived_class.m_nSize = static_cast<int>(kEntitySize);
    g_derived_class.m_nAlignment = 16;
    g_derived_class.m_nBaseClassCount = 1;
    g_derived_class.m_pBaseClasses = g_derived_bases;

    void** entity_system_vtable = g_entity_system_vtable.data();
    StorePointer(g_entity_system_storage.data(), entity_system_vtable);
    EntitySystem()->m_EntityList.m_pIdentityChunks[0] =
        reinterpret_cast<CEntityIdentity*>(g_identity_storage.data());

    g_entity_class_info = {};
    g_entity_class_info.m_pszClassname = "player";
    g_entity_class_info.m_pszCPPClassname = "CCSPlayerPawn";
    g_entity_class_info.m_pSchemaBinding = &g_derived_class;
    EntityClass()->m_pClassInfo = &g_entity_class_info;

    CEntityIdentity* identity = Identity();
    identity->m_pInstance = EntityInstance();
    identity->m_pClass = EntityClass();
    SetHandle(*identity, 12);
    identity->m_flags = static_cast<EntityFlags_t>(0);
    EntityInstance()->m_pEntity = identity;
    SetHealth(42);

    void** game_resource_vtable = g_game_resource_vtable.data();
    StorePointer(g_game_resource_storage.data(), game_resource_vtable);
    SetGameEntitySystem(false);
}

struct Initialize
{
    Initialize()
    {
        Reset();
    }
} g_initialize;

#if defined(KEELS2_SCHEMA_FIXTURE_NATIVE_TEST)
int RunHandleChecks()
{
    static_assert(keels2::schema::detail::ValueType<CEntityHandle>() == KEELS2_SCHEMA_ENTITY_HANDLE);
    static_assert(sizeof(CEntityHandle) == sizeof(uint32));
    Reset();
#if defined(_WIN32)
    constexpr const char* module = "server.dll";
#else
    constexpr const char* module = "libserver.so";
#endif
    alignas(CSchemaType_Atomic_T) std::array<std::byte, sizeof(CSchemaType_Atomic_T)> storage{};
    alignas(CSchemaType) std::array<std::byte, sizeof(CSchemaType)> parameter_storage{};
    auto* type = reinterpret_cast<CSchemaType_Atomic_T*>(storage.data());
    auto* parameter = reinterpret_cast<CSchemaType*>(parameter_storage.data());
    parameter->m_eTypeCategory = SCHEMA_TYPE_DECLARED_CLASS;
    type->m_sTypeName = "CHandle< CCSPlayerPawn >";
    type->m_eTypeCategory = SCHEMA_TYPE_ATOMIC;
    type->m_eAtomicCategory = SCHEMA_ATOMIC_T;
    type->m_nSize = sizeof(uint32);
    type->m_nAlignment = alignof(uint32);
    type->m_pTemplateType = parameter;
    g_base_fields[0].m_pType = type;
    g_base_fields[0].m_pszName = "m_hPlayerPawn";
    KeelCs2SchemaField field{};
    auto resolve = [&](KeelSchemaValueType value_type = KEELS2_SCHEMA_ENTITY_HANDLE) {
        return KeelCs2_ResolveSchemaField(&g_schema_system, module, "CBaseEntity", "m_hPlayerPawn", value_type, &field);
    };
    if (resolve() != KEEL_RESULT_OK || field.value_type != KEELS2_SCHEMA_ENTITY_HANDLE ||
        field.value_size != sizeof(uint32) || field.value_alignment != alignof(uint32))
        return 100;
    KeelCs2EntityIdentity entity{};
    uint32 value{};
    const uint32 stored = static_cast<uint32>(CEntityHandle(75,17).ToInt());
    std::memcpy(g_entity_storage.data() + kHealthOffset, &stored, sizeof(stored));
    if (KeelCs2_FindEntityByIndex(EntitySystem(), kEntityIndex, &entity) != KEEL_RESULT_OK ||
        KeelCs2_ReadEntityField(EntitySystem(), &entity, &field, &value, sizeof(value)) != KEEL_RESULT_OK || value != stored)
        return 101;
    if (resolve(KEELS2_SCHEMA_UINT32) != KEEL_RESULT_INCOMPATIBLE)
        return 102;
    type->m_sTypeName = "CUtlVector< CCSPlayerPawn >";
    if (resolve() != KEEL_RESULT_INCOMPATIBLE)
        return 103;
    type->m_sTypeName = "CHandle< CCSPlayerPawn >";
    type->m_nSize = 8;
    if (resolve() != KEEL_RESULT_INCOMPATIBLE)
        return 104;
    type->m_nSize = 4;
    type->m_nAlignment = 8;
    if (resolve() != KEEL_RESULT_INCOMPATIBLE)
        return 105;
    type->m_nAlignment = 4;
    parameter->m_eTypeCategory = SCHEMA_TYPE_BUILTIN;
    if (resolve() != KEEL_RESULT_INCOMPATIBLE)
        return 106;
    parameter->m_eTypeCategory = SCHEMA_TYPE_DECLARED_CLASS;
    type->m_eAtomicCategory = SCHEMA_ATOMIC_COLLECTION_OF_T;
    if (resolve() != KEEL_RESULT_INCOMPATIBLE)
        return 107;
    type->m_eAtomicCategory = SCHEMA_ATOMIC_T;
    type->m_pTemplateType = nullptr;
    if (resolve() != KEEL_RESULT_INCOMPATIBLE)
        return 108;
    type->m_sTypeName.Purge();
    Reset();
    return 0;
}


std::vector<int> g_actions;
float g_action_damage{};
bool g_action_arguments{};
void TestTeleport(void* instance, const Vector* position, const QAngle* angles, const Vector* velocity)
{
    g_action_arguments = instance == EntityInstance() && !position && !angles && velocity;
    if (velocity)
        std::memcpy(g_entity_storage.data() + kHealthOffset + 8, velocity, sizeof(Vector));
    g_actions.push_back(1);
}
void TestSuicide(void* instance, bool explode, bool force)
{
    g_action_arguments = instance == EntityInstance() && !explode && force;
    g_entity_storage[kHealthOffset + 4] = std::byte{2};
    g_actions.push_back(2);
}
void TestDamageConstruct(void*, void* inflictor, void* attacker, void* ability, float damage, std::uint32_t type, std::int32_t custom)
{
    g_action_arguments = g_action_arguments && !inflictor && !attacker && !ability && type == 0 && custom == 0;
    g_action_damage = damage;
    g_actions.push_back(3);
}
void TestDamageApply(void* instance, void* info, void* result)
{
    g_action_arguments = g_action_arguments && instance == EntityInstance() && info && !result;
    g_actions.push_back(4);
}
void TestDamageDestroy(void* info)
{
    g_action_arguments = g_action_arguments && info;
    g_actions.push_back(5);
}
int RunPlayerActionChecks()
{
    Reset();
#if defined(_WIN32)
    constexpr const char* module = "server.dll";
#else
    constexpr const char* module = "libserver.so";
#endif
    static_assert(keels2::schema::detail::ValueType<Vector>() == KEELS2_SCHEMA_VECTOR3);
    alignas(CSchemaType_Builtin) std::array<std::byte, sizeof(CSchemaType_Builtin)> life_storage{};
    alignas(CSchemaType_Atomic) std::array<std::byte, sizeof(CSchemaType_Atomic)> vector_storage{};
    auto* life = reinterpret_cast<CSchemaType_Builtin*>(life_storage.data());
    life->m_eTypeCategory = SCHEMA_TYPE_BUILTIN;
    life->m_eAtomicCategory = SCHEMA_ATOMIC_INVALID;
    life->m_eBuiltinType = SCHEMA_BUILTIN_TYPE_UINT8;
    life->m_nSize = 1;
    auto* vector = reinterpret_cast<CSchemaType_Atomic*>(vector_storage.data());
    vector->m_eTypeCategory = SCHEMA_TYPE_ATOMIC;
    vector->m_eAtomicCategory = SCHEMA_ATOMIC_PLAIN;
    vector->m_sTypeName = "Vector";
    vector->m_nSize = sizeof(Vector);
    vector->m_nAlignment = alignof(Vector);
    g_base_fields[1] = {"m_lifeState", life, static_cast<int>(kHealthOffset + 4), 0, nullptr};
    g_base_fields[2] = {"m_vecAbsVelocity", vector, static_cast<int>(kHealthOffset + 8), 0, nullptr};
    g_base_class.m_nFieldCount = 3;
    g_damage_class = {};
    g_damage_class.m_pszName = "CTakeDamageInfo";
    g_damage_class.m_nSize = 0x118;
    g_damage_class.m_nAlignment = 8;
    std::array<void*, 385> table{};
    table[162] = FunctionAddress(&TestTeleport);
    table[384] = FunctionAddress(&TestSuicide);
    StorePointer(g_entity_storage.data(), table.data());
    KeelCs2EntityIdentity entity{};
    if (KeelCs2_FindEntityByIndex(EntitySystem(), kEntityIndex, &entity) != KEEL_RESULT_OK)
        return 200;
    KeelCs2PlayerActionBindings bindings{162,384,FunctionAddress(&TestDamageConstruct),
        FunctionAddress(&TestDamageApply),FunctionAddress(&TestDamageDestroy),0x118};
    KeelPlayerAction action{sizeof(KeelPlayerAction),KEELS2_PLAYER_ACTION_IMPULSE,{200,-200,300},5};
    auto apply = [&] { return KeelCs2_PlayerAction(EntitySystem(), &g_schema_system, module, &entity, &action, &bindings); };
    g_actions.clear();
    if (apply() != KEEL_RESULT_OK || !g_action_arguments || g_action_damage != 5 || g_actions != std::vector<int>({1,3,4,5}))
        return 201;
    Vector velocity;
    std::memcpy(velocity.Base(), g_entity_storage.data() + kHealthOffset + 8, sizeof(velocity));
    if (velocity != Vector(200,-200,300))
        return 202;
    g_actions.clear();
    action.damage = 0;
    if (apply() != KEEL_RESULT_OK || g_actions != std::vector<int>({1}))
        return 203;
    std::memcpy(velocity.Base(), g_entity_storage.data() + kHealthOffset + 8, sizeof(velocity));
    if (velocity != Vector(400,-400,600))
        return 204;
    g_actions.clear();
    action.damage = -1;
    if (apply() != KEEL_RESULT_INVALID_ARGUMENT || !g_actions.empty())
        return 205;
    action.damage = 0;
    action.impulse[0] = std::numeric_limits<float>::quiet_NaN();
    if (apply() != KEEL_RESULT_INVALID_ARGUMENT)
        return 206;
    action.impulse[0] = 200;
    vector->m_nSize = 16;
    if (apply() != KEEL_RESULT_INCOMPATIBLE || !g_actions.empty())
        return 207;
    vector->m_nSize = sizeof(Vector);
    vector->m_sTypeName = "VectorWS";
    if (apply() != KEEL_RESULT_INCOMPATIBLE)
        return 208;
    vector->m_sTypeName = "Vector";
    vector->m_nAlignment = 16;
    if (apply() != KEEL_RESULT_INCOMPATIBLE)
        return 209;
    vector->m_nAlignment = alignof(Vector);
    action.damage = 5;
    g_damage_class.m_nSize = 0x110;
    if (apply() != KEEL_RESULT_INCOMPATIBLE || !g_actions.empty())
        return 210;
    g_damage_class.m_nSize = 0x118;
    action = {sizeof(KeelPlayerAction),KEELS2_PLAYER_ACTION_KILL,{},0};
    if (apply() != KEEL_RESULT_OK || !g_action_arguments || g_actions != std::vector<int>({2}))
        return 211;
    if (apply() != KEEL_RESULT_NOT_READY || g_actions.size() != 1)
        return 212;
    g_entity_storage[kHealthOffset + 4] = std::byte{};
    g_derived_class.m_pszName = "CChicken";
    if (apply() != KEEL_RESULT_INCOMPATIBLE)
        return 213;
    g_derived_class.m_pszName = "CCSPlayerPawn";
    SetHandle(*Identity(), 13);
    if (apply() != KEEL_RESULT_NOT_FOUND)
        return 214;
    vector->m_sTypeName.Purge();
    Reset();
    return 0;
}

int RunNativeBridgeChecks()
{
#if defined(_WIN32)
    constexpr const char* module = "server.dll";
#else
    constexpr const char* module = "libserver.so";
#endif
    KeelCs2SchemaField field{};
    if (KeelCs2_ResolveSchemaField(
            nullptr,
            module,
            "CBaseEntity",
            "m_iHealth",
            KEELS2_SCHEMA_INT32,
            &field) != KEEL_RESULT_INVALID_ARGUMENT)
    {
        return 1;
    }
    if (KeelCs2_ResolveSchemaField(
            &g_schema_system,
            module,
            "CBaseEntity",
            "m_iHealth",
            0,
            &field) != KEEL_RESULT_INVALID_ARGUMENT)
    {
        return 2;
    }
    if (KeelCs2_ResolveSchemaField(
            &g_schema_system,
            module,
            "Missing",
            "m_iHealth",
            KEELS2_SCHEMA_INT32,
            &field) != KEEL_RESULT_NOT_FOUND)
    {
        return 3;
    }
    if (KeelCs2_ResolveSchemaField(
            &g_schema_system,
            module,
            "CBaseEntity",
            "missing",
            KEELS2_SCHEMA_INT32,
            &field) != KEEL_RESULT_NOT_FOUND)
    {
        return 4;
    }
    if (KeelCs2_ResolveSchemaField(
            &g_schema_system,
            module,
            "CBaseEntity",
            "m_iHealth",
            KEELS2_SCHEMA_FLOAT32,
            &field) != KEEL_RESULT_INCOMPATIBLE)
    {
        return 5;
    }
    Reset();
    if (KeelCs2_ResolveSchemaField(
            &g_schema_system,
            module,
            "CBaseEntity",
            "m_iHealth",
            KEELS2_SCHEMA_INT32,
            &field) != KEEL_RESULT_OK ||
        field.declaring_class != &g_base_class ||
        field.offset != static_cast<std::int32_t>(kHealthOffset) ||
        field.value_size != sizeof(int32) || field.value_alignment != alignof(int32) ||
        field.value_type != KEELS2_SCHEMA_INT32)
    {
        return 6;
    }

    Reset();
    g_base_class.m_nAlignment = 3;
    if (KeelCs2_ResolveSchemaField(
            &g_schema_system,
            module,
            "CBaseEntity",
            "m_iHealth",
            KEELS2_SCHEMA_INT32,
            &field) != KEEL_RESULT_INCOMPATIBLE)
    {
        return 7;
    }
    Reset();
    g_base_class.m_pFields = nullptr;
    if (KeelCs2_ResolveSchemaField(
            &g_schema_system,
            module,
            "CBaseEntity",
            "m_iHealth",
            KEELS2_SCHEMA_INT32,
            &field) != KEEL_RESULT_INCOMPATIBLE)
    {
        return 8;
    }
    Reset();
    g_base_fields[0].m_nSingleInheritanceOffset = -1;
    if (KeelCs2_ResolveSchemaField(
            &g_schema_system,
            module,
            "CBaseEntity",
            "m_iHealth",
            KEELS2_SCHEMA_INT32,
            &field) != KEEL_RESULT_INCOMPATIBLE)
    {
        return 9;
    }
    Reset();
    g_base_fields[0].m_nSingleInheritanceOffset = static_cast<int>(kEntitySize);
    if (KeelCs2_ResolveSchemaField(
            &g_schema_system,
            module,
            "CBaseEntity",
            "m_iHealth",
            KEELS2_SCHEMA_INT32,
            &field) != KEEL_RESULT_INCOMPATIBLE)
    {
        return 10;
    }
    Reset();
    auto* int32_type = reinterpret_cast<CSchemaType_Builtin*>(g_int32_type_storage.data());
    int32_type->m_nSize = 8;
    if (KeelCs2_ResolveSchemaField(
            &g_schema_system,
            module,
            "CBaseEntity",
            "m_iHealth",
            KEELS2_SCHEMA_INT32,
            &field) != KEEL_RESULT_INCOMPATIBLE)
    {
        return 11;
    }
    Reset();
    int32_type = reinterpret_cast<CSchemaType_Builtin*>(g_int32_type_storage.data());
    int32_type->m_eTypeCategory = SCHEMA_TYPE_POINTER;
    if (KeelCs2_ResolveSchemaField(
            &g_schema_system,
            module,
            "CBaseEntity",
            "m_iHealth",
            KEELS2_SCHEMA_INT32,
            &field) != KEEL_RESULT_INCOMPATIBLE)
    {
        return 12;
    }

    Reset();
    if (KeelCs2_ResolveSchemaField(
            &g_schema_system,
            module,
            "CBaseEntity",
            "m_iHealth",
            KEELS2_SCHEMA_INT32,
            &field) != KEEL_RESULT_OK)
    {
        return 13;
    }
    if (KeelCs2_ReadGameEntitySystem(g_game_resource_storage.data(), 1) != nullptr ||
        KeelCs2_ReadGameEntitySystem(
            g_game_resource_storage.data(),
            static_cast<std::uint32_t>(kEntitySystemOffset)) != nullptr)
    {
        return 14;
    }
    SetGameEntitySystem(true);
    void* system = KeelCs2_ReadGameEntitySystem(
        g_game_resource_storage.data(),
        static_cast<std::uint32_t>(kEntitySystemOffset));
    if (system != EntitySystem())
    {
        return 15;
    }

    KeelCs2EntityIdentity entity{};
    if (KeelCs2_FindEntityByIndex(system, -1, &entity) != KEEL_RESULT_INVALID_ARGUMENT ||
        KeelCs2_FindEntityByIndex(system, MAX_TOTAL_ENTITIES, &entity) !=
            KEEL_RESULT_INVALID_ARGUMENT ||
        KeelCs2_FindEntityByIndex(system, 8, &entity) != KEEL_RESULT_NOT_FOUND ||
        KeelCs2_FindEntityByIndex(system, kEntityIndex, &entity) != KEEL_RESULT_OK ||
        entity.index != kEntityIndex)
    {
        return 16;
    }
    const std::uint32_t original_handle = entity.source2_handle;
    KeelCs2EntityIdentity by_handle{};
    if (KeelCs2_FindEntityBySource2Handle(
            system,
            INVALID_EHANDLE_INDEX,
            &by_handle) != KEEL_RESULT_INVALID_ARGUMENT ||
        KeelCs2_FindEntityBySource2Handle(system, original_handle, &by_handle) !=
            KEEL_RESULT_OK ||
        by_handle.index != entity.index || by_handle.source2_handle != entity.source2_handle ||
        KeelCs2_ValidateEntity(system, &entity) != KEEL_RESULT_OK)
    {
        return 17;
    }
    int32 health{};
    if (KeelCs2_ReadEntityField(
            system,
            &entity,
            &field,
            &health,
            sizeof(health)) != KEEL_RESULT_OK ||
        health != 42)
    {
        return 18;
    }

    KeelCs2SchemaField malformed = field;
    malformed.offset = -1;
    if (KeelCs2_ReadEntityField(
            system,
            &entity,
            &malformed,
            &health,
            sizeof(health)) != KEEL_RESULT_INVALID_ARGUMENT)
    {
        return 19;
    }
    malformed = field;
    malformed.value_alignment = 3;
    if (KeelCs2_ReadEntityField(
            system,
            &entity,
            &malformed,
            &health,
            sizeof(health)) != KEEL_RESULT_INVALID_ARGUMENT)
    {
        return 20;
    }
    if (KeelCs2_ReadEntityField(
            system,
            &entity,
            &field,
            &health,
            sizeof(health) - 1) != KEEL_RESULT_INVALID_ARGUMENT)
    {
        return 21;
    }
    malformed = field;
    malformed.offset = static_cast<std::int32_t>(kEntitySize);
    if (KeelCs2_ReadEntityField(
            system,
            &entity,
            &malformed,
            &health,
            sizeof(health)) != KEEL_RESULT_INCOMPATIBLE)
    {
        return 22;
    }
    malformed = field;
    malformed.offset = 1;
    if (KeelCs2_ReadEntityField(
            system,
            &entity,
            &malformed,
            &health,
            sizeof(health)) != KEEL_RESULT_INCOMPATIBLE)
    {
        return 23;
    }

    CSchemaClassInfo unrelated{};
    unrelated.m_nSize = static_cast<int>(kEntitySize);
    unrelated.m_nAlignment = 16;
    g_entity_class_info.m_pSchemaBinding = &unrelated;
    if (KeelCs2_ReadEntityField(
            system,
            &entity,
            &field,
            &health,
            sizeof(health)) != KEEL_RESULT_INCOMPATIBLE)
    {
        return 24;
    }
    g_entity_class_info.m_pSchemaBinding = &g_derived_class;
    g_derived_bases[0].m_pClass = &g_derived_class;
    if (KeelCs2_ReadEntityField(
            system,
            &entity,
            &field,
            &health,
            sizeof(health)) != KEEL_RESULT_INCOMPATIBLE)
    {
        return 25;
    }

    Reset();
    SetGameEntitySystem(true);
    system = EntitySystem();
    if (KeelCs2_ResolveSchemaField(
            &g_schema_system,
            module,
            "CBaseEntity",
            "m_iHealth",
            KEELS2_SCHEMA_INT32,
            &field) != KEEL_RESULT_OK ||
        KeelCs2_FindEntityByIndex(system, kEntityIndex, &entity) != KEEL_RESULT_OK)
    {
        return 26;
    }
    SchemaBaseClassInfoData_t left_bases[1]{{0, &g_base_class}};
    SchemaBaseClassInfoData_t right_bases[1]{{0, &g_base_class}};
    CSchemaClassInfo left{};
    left.m_nSize = static_cast<int>(kEntitySize);
    left.m_nAlignment = 16;
    left.m_nBaseClassCount = 1;
    left.m_pBaseClasses = left_bases;
    CSchemaClassInfo right = left;
    right.m_pBaseClasses = right_bases;
    SchemaBaseClassInfoData_t root_bases[2]{
        {0, &left},
        {16, &right}
    };
    CSchemaClassInfo root{};
    root.m_nSize = static_cast<int>(kEntitySize + 16);
    root.m_nAlignment = 16;
    root.m_nBaseClassCount = 2;
    root.m_pBaseClasses = root_bases;
    g_entity_class_info.m_pSchemaBinding = &root;
    if (KeelCs2_ReadEntityField(
            system,
            &entity,
            &field,
            &health,
            sizeof(health)) != KEEL_RESULT_INCOMPATIBLE)
    {
        return 27;
    }

    Reset();
    system = EntitySystem();
    if (KeelCs2_FindEntityByIndex(system, kEntityIndex, &entity) != KEEL_RESULT_OK)
    {
        return 28;
    }
    EntityInstance()->m_pEntity = nullptr;
    if (KeelCs2_ValidateEntity(system, &entity) != KEEL_RESULT_NOT_FOUND)
    {
        return 29;
    }
    Reset();
    system = EntitySystem();
    if (KeelCs2_FindEntityByIndex(system, kEntityIndex, &entity) != KEEL_RESULT_OK)
    {
        return 30;
    }
    CEntityIdentity* identity = Identity();
    identity->m_flags = EF_DELETE_IN_PROGRESS;
    identity->m_pInstance = nullptr;
    if (KeelCs2_ValidateEntity(system, &entity) != KEEL_RESULT_NOT_FOUND)
    {
        return 31;
    }
    identity->m_pInstance = EntityInstance();
    identity->m_pClass = EntityClass();
    SetHandle(*identity, 13);
    identity->m_flags = static_cast<EntityFlags_t>(0);
    EntityInstance()->m_pEntity = identity;
    SetHealth(84);
    if (KeelCs2_ValidateEntity(system, &entity) != KEEL_RESULT_NOT_FOUND ||
        KeelCs2_FindEntityByIndex(system, kEntityIndex, &by_handle) != KEEL_RESULT_OK ||
        by_handle.source2_handle == original_handle)
    {
        return 32;
    }
    return 0;
}
#endif

}

extern "C" KEELS2_SCHEMA_FIXTURE_EXPORT void* KeelTest_SchemaSystem()
{
    return &g_schema_system;
}

extern "C" KEELS2_SCHEMA_FIXTURE_EXPORT void* KeelTest_GameResourceService()
{
    return g_game_resource_storage.data();
}

extern "C" KEELS2_SCHEMA_FIXTURE_EXPORT void KeelTest_ResetSchemaEntities()
{
    Reset();
}

extern "C" KEELS2_SCHEMA_FIXTURE_EXPORT void KeelTest_SetEntitySystemReady(bool ready)
{
    SetGameEntitySystem(ready);
}

extern "C" KEELS2_SCHEMA_FIXTURE_EXPORT void KeelTest_DestroyEntity()
{
    CEntityIdentity* identity = Identity();
    identity->m_flags = EF_DELETE_IN_PROGRESS;
    identity->m_pInstance = nullptr;
}

extern "C" KEELS2_SCHEMA_FIXTURE_EXPORT void KeelTest_ReuseEntity()
{
    CEntityIdentity* identity = Identity();
    identity->m_pInstance = EntityInstance();
    identity->m_pClass = EntityClass();
    SetHandle(*identity, 13);
    identity->m_flags = static_cast<EntityFlags_t>(0);
    EntityInstance()->m_pEntity = identity;
    SetHealth(84);
}

extern "C" KEELS2_SCHEMA_FIXTURE_EXPORT std::uint32_t KeelTest_SchemaLookupCount()
{
    return g_schema_lookup_count;
}

#if defined(KEELS2_SCHEMA_FIXTURE_NATIVE_TEST)
int main()
{
    const int handles = RunHandleChecks();
    if (handles) return handles;
    const int actions = RunPlayerActionChecks();
    return actions ? actions : RunNativeBridgeChecks();
}
#endif

namespace
{
std::mutex g_console_mutex;
std::vector<std::pair<int, std::string>> g_console_lines;
std::array<void*, 256> g_console_vtable{};
RawInterface g_console_engine{g_console_vtable.data()};
int g_player_slot{-1};
int g_player_user{-1};
CSteamID g_player_identity;
bool g_player_throw{};

bool PlayerInfo(void*, CPlayerSlot slot, google::protobuf::Message& info)
{
    if (g_player_throw)
    {
        throw std::runtime_error("player fixture failure");
    }
    if (slot.Get() != g_player_slot)
    {
        return false;
    }
    const auto* descriptor = info.GetDescriptor();
    const auto* reflection = info.GetReflection();
    reflection->SetString(&info, descriptor->FindFieldByName("name"), "Late player");
    reflection->SetBool(&info, descriptor->FindFieldByName("fakeplayer"), false);
    reflection->SetBool(&info, descriptor->FindFieldByName("ishltv"), false);
    return true;
}

CPlayerUserId PlayerUser(void*, CPlayerSlot slot)
{
    return CPlayerUserId(slot.Get() == g_player_slot ? g_player_user : -1);
}

bool PlayerAuthenticated(void*, CPlayerSlot slot)
{
    return slot.Get() == g_player_slot && g_player_identity.ConvertToUint64() != 0;
}

const CSteamID* PlayerIdentity(void*, CPlayerSlot slot)
{
    return slot.Get() == g_player_slot ? &g_player_identity : nullptr;
}

void ConsolePrint(void*, CPlayerSlot slot, const char* message)
{
    std::scoped_lock lock(g_console_mutex);
    g_console_lines.emplace_back(slot.Get(), message ? message : "");
}
}

extern "C" KEELS2_SCHEMA_FIXTURE_EXPORT void* KeelTest_ConsoleEngine()
{
    const auto index = keels2::kh::VirtualIndex<&IVEngineServer2::ClientPrintf>();
    const auto info = keels2::kh::VirtualIndex<&IVEngineServer2::GetPlayerInfo>();
    const auto user = keels2::kh::VirtualIndex<&IVEngineServer2::GetPlayerUserId>();
    const auto auth = keels2::kh::VirtualIndex<&IVEngineServer2::IsClientFullyAuthenticated>();
    const auto identity = keels2::kh::VirtualIndex<&IVEngineServer2::GetClientSteamID>();
    if (!index || !info || !user || !auth || !identity || *index >= g_console_vtable.size() ||
        *info >= g_console_vtable.size() || *user >= g_console_vtable.size() ||
        *auth >= g_console_vtable.size() || *identity >= g_console_vtable.size())
    {
        return nullptr;
    }
    g_console_vtable[0] = FunctionAddress(&ValidationSlot);
    g_console_vtable[*index] = FunctionAddress(&ConsolePrint);
    g_console_vtable[*info] = FunctionAddress(&PlayerInfo);
    g_console_vtable[*user] = FunctionAddress(&PlayerUser);
    g_console_vtable[*auth] = FunctionAddress(&PlayerAuthenticated);
    g_console_vtable[*identity] = FunctionAddress(&PlayerIdentity);
    return &g_console_engine;
}

extern "C" KEELS2_SCHEMA_FIXTURE_EXPORT void KeelTest_SetPlayer(int slot, int user,
    std::uint64_t identity, bool fail)
{
    g_player_slot = slot;
    g_player_user = user;
    g_player_identity.SetFromUint64(identity);
    g_player_throw = fail;
}

extern "C" KEELS2_SCHEMA_FIXTURE_EXPORT const char* KeelTest_ConsoleOutput(int slot)
{
    std::scoped_lock lock(g_console_mutex);
    thread_local std::string output;
    output.clear();
    for (const auto& [caller, line] : g_console_lines)
    {
        if (caller == slot)
        {
            output += line;
        }
    }
    return output.c_str();
}

extern "C" KEELS2_SCHEMA_FIXTURE_EXPORT void KeelTest_ResetConsoleOutput()
{
    std::scoped_lock lock(g_console_mutex);
    g_console_lines.clear();
}
