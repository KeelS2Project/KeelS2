#include <entity2/entityclass.h>
#include <entity2/entityinstance.h>
#include <entity2/entitysystem.h>
#include <schemasystem/schemasystem.h>

#if defined(KEELS2_SCHEMA_FIXTURE_NATIVE_TEST)
#include <keels2/cs2/native_bridge.h>
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

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
SchemaClassFieldData_t g_base_fields[1]{};
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
constexpr std::size_t kEntitySize = Align(kHealthOffset + sizeof(int32), 16);
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
    return RunNativeBridgeChecks();
}
#endif
