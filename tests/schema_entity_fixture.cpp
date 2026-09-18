#include <keels2/keelhook.hpp>
#include <keels2/schema.hpp>
#include "entity_hook_data_fixture.h"
#include <eiface.h>
#include <in_buttons.h>
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
#include <iostream>
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
void (*g_input_read_callback)(){};
CSchemaClassInfo g_input_pawn_class{}, g_movement_class{}, g_buttons_class{};
bool g_buttons_registered = true;
SchemaClassFieldData_t g_input_pointer{}, g_input_state{}, g_input_masks{};
SchemaBaseClassInfoData_t g_input_pawn_base{};
CSchemaClassInfo g_controller_base{}, g_controller_class{};
CSchemaClassInfo g_round_proxy_class{}, g_round_rules_class{};
CSchemaClassInfo g_stat_base{}, g_stat_money{}, g_stat_tracking{}, g_stat_match{}, g_stat_values{}, g_stat_chainer{};
SchemaClassFieldData_t g_controller_fields[2]{}, g_controller_pawn{};
SchemaBaseClassInfoData_t g_controller_inherit{};
template <typename T> struct SchemaStorage {
    alignas(T) std::array<std::byte, sizeof(T)> data{};
    T* Get() { return reinterpret_cast<T*>(data.data()); }
};
SchemaStorage<CSchemaType_DeclaredClass> g_input_movement_type, g_input_state_type, g_input_pawn_type;
SchemaStorage<CSchemaType_Ptr> g_input_pointer_type;
SchemaStorage<CSchemaType_FixedArray> g_input_array_type;
SchemaStorage<CSchemaType_Builtin> g_input_uint64;
SchemaStorage<CSchemaType_Atomic_T> g_input_handle;
alignas(16) std::array<std::byte, 64> g_movement_storage{}, g_other_movement{};

SchemaStorage<CSchemaType_Builtin> g_hook_float;
SchemaStorage<CSchemaType_DeclaredEnum> g_hook_enum;
SchemaStorage<CSchemaType_Atomic> g_hook_vector, g_hook_chainer;
SchemaStorage<CSchemaType_Atomic_T> g_hook_handle;
SchemaStorage<CSchemaType_DeclaredClass> g_hook_entity_type, g_hook_component_type;
SchemaStorage<CSchemaType_Ptr> g_hook_pointer;
CSchemaEnumInfo g_hook_damage_enum{};
SchemaClassFieldData_t g_hook_damage_fields[8]{}, g_hook_weapon_pointer{}, g_hook_chain{};
CSchemaClassInfo g_hook_component{}, g_hook_component_base{};
SchemaBaseClassInfoData_t g_hook_component_inherit{};
alignas(16) std::array<std::byte,128> g_hook_weapon{};
hook_data_fixture::DamageRecord g_hook_damage{};
void (*g_hook_schema_callback)(){};

CSchemaClassInfo* DeclaredClass(const char* name)
{
    ++g_schema_lookup_count;
    if (g_hook_schema_callback && name && std::strcmp(name,"CTakeDamageInfo") == 0) {
        const auto callback = g_hook_schema_callback; g_hook_schema_callback = nullptr; callback();
    }
    if (g_input_read_callback && name && std::strcmp(name, "CPlayer_MovementServices") == 0)
    {
        const auto callback = g_input_read_callback;
        g_input_read_callback = nullptr;
        callback();
    }
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
    if (!g_buttons_registered && name && std::strcmp(name, "CInButtonState") == 0) return nullptr;
    for (auto* type : {&g_input_pawn_class, &g_movement_class, &g_buttons_class, &g_controller_base, &g_controller_class, &g_round_proxy_class, &g_round_rules_class, &g_stat_base, &g_stat_money, &g_stat_tracking, &g_stat_match, &g_stat_values, &g_stat_chainer, &g_hook_component, &g_hook_component_base})
        if (name && type->m_pszName && std::strcmp(name, type->m_pszName) == 0) return type;
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
CEntityClassInfo g_entity_class_info{}, g_controller_info{};
alignas(CEntityInstance) std::array<std::byte, kEntitySize> g_controller_storage{};
alignas(CEntityClass) std::array<std::byte, sizeof(CEntityClass)> g_controller_class_storage{};

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
    g_input_read_callback = nullptr;
    g_hook_schema_callback = nullptr; g_hook_component = g_hook_component_base = {};
    g_input_pawn_class = g_movement_class = g_buttons_class = g_controller_base = g_controller_class = {};
    g_round_proxy_class = g_round_rules_class = {};
    g_stat_base = g_stat_money = g_stat_tracking = g_stat_match = g_stat_values = g_stat_chainer = {};
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

void InputFixture(std::uint64_t held)
{
    Reset();
    g_buttons_registered = true;
    const auto declared = [](CSchemaType_DeclaredClass* type, CSchemaClassInfo* info) {
        type->m_eTypeCategory = SCHEMA_TYPE_DECLARED_CLASS; type->m_eAtomicCategory = SCHEMA_ATOMIC_INVALID; type->m_pClassInfo = info;
    };
    const auto class_info = [](CSchemaClassInfo& info, const char* name, int size, std::uint8_t alignment, SchemaClassFieldData_t* field) {
        info = {}; info.m_pszName = name; info.m_nSize = size; info.m_nAlignment = alignment;
        info.m_nFieldCount = 1; info.m_pFields = field;
    };
    declared(g_input_movement_type.Get(), &g_movement_class);
    declared(g_input_state_type.Get(), &g_buttons_class);
    declared(g_input_pawn_type.Get(), &g_derived_class);
    auto* pointer = g_input_pointer_type.Get();
    pointer->m_eTypeCategory = SCHEMA_TYPE_POINTER; pointer->m_eAtomicCategory = SCHEMA_ATOMIC_INVALID;
    pointer->m_pObjectType = g_input_movement_type.Get();
    auto* integer = g_input_uint64.Get();
    integer->m_eTypeCategory = SCHEMA_TYPE_BUILTIN; integer->m_eAtomicCategory = SCHEMA_ATOMIC_INVALID;
    integer->m_eBuiltinType = SCHEMA_BUILTIN_TYPE_UINT64; integer->m_nSize = 8;
    auto* array = g_input_array_type.Get();
    array->m_eTypeCategory = SCHEMA_TYPE_FIXED_ARRAY; array->m_eAtomicCategory = SCHEMA_ATOMIC_INVALID;
    array->m_nElementCount = 3; array->m_nElementSize = 8; array->m_nElementAlignment = 8; array->m_pElementType = integer;
    constexpr auto pointer_offset = Align(kHealthOffset + 8, alignof(void*));
    g_input_pointer = {"m_pMovementServices", pointer, static_cast<int>(pointer_offset), 0, nullptr};
    g_input_state = {"m_nButtons", g_input_state_type.Get(), 16, 0, nullptr};
    g_input_masks = {"m_pButtonStates", array, 8, 0, nullptr};
    class_info(g_input_pawn_class, "CBasePlayerPawn", static_cast<int>(kEntitySize), 16, &g_input_pointer);
    class_info(g_movement_class, "CPlayer_MovementServices", 64, 16, &g_input_state);
    class_info(g_buttons_class, "CInButtonState", 32, 8, &g_input_masks);
    g_input_pawn_base = {0, &g_base_class};
    g_input_pawn_class.m_nBaseClassCount = 1; g_input_pawn_class.m_pBaseClasses = &g_input_pawn_base;
    g_derived_bases[0].m_pClass = &g_input_pawn_class;
    g_movement_storage.fill(std::byte{});
    std::memcpy(g_movement_storage.data() + 24, &held, sizeof(held));
    const std::uint64_t changed = IN_ATTACK;
    std::memcpy(g_movement_storage.data() + 32, &changed, sizeof(changed));
    g_other_movement = g_movement_storage;
    StorePointer(g_entity_storage.data() + pointer_offset, g_movement_storage.data());

    auto* handle = g_input_handle.Get();
    handle->m_eTypeCategory = SCHEMA_TYPE_ATOMIC; handle->m_eAtomicCategory = SCHEMA_ATOMIC_T;
    handle->m_sTypeName = "CHandle<CCSPlayerPawn>"; handle->m_nSize = 4; handle->m_nAlignment = 4;
    handle->m_pTemplateType = g_input_pawn_type.Get();
    g_controller_fields[0] = {"m_steamID", integer, static_cast<int>(kHealthOffset), 0, nullptr};
    g_controller_pawn = {"m_hPlayerPawn", handle, static_cast<int>(kHealthOffset + 8), 0, nullptr};
    g_controller_fields[1] = {"m_hPawn", handle, static_cast<int>(kHealthOffset + 16), 0, nullptr};
    class_info(g_controller_base, "CBasePlayerController", static_cast<int>(kEntitySize), 16, g_controller_fields);
    g_controller_base.m_nFieldCount = 2;
    class_info(g_controller_class, "CCSPlayerController", static_cast<int>(kEntitySize), 16, &g_controller_pawn);
    g_controller_inherit = {0, &g_controller_base};
    g_controller_class.m_nBaseClassCount = 1; g_controller_class.m_pBaseClasses = &g_controller_inherit;
    g_controller_storage.fill(std::byte{}); g_controller_class_storage.fill(std::byte{});
    auto* identity = &reinterpret_cast<CEntityIdentity*>(g_identity_storage.data())[4];
    auto* instance = reinterpret_cast<CEntityInstance*>(g_controller_storage.data());
    auto* cls = reinterpret_cast<CEntityClass*>(g_controller_class_storage.data());
    g_controller_info = {}; g_controller_info.m_pszClassname = "controller";
    g_controller_info.m_pszCPPClassname = "CCSPlayerController"; g_controller_info.m_pSchemaBinding = &g_controller_class;
    cls->m_pClassInfo = &g_controller_info;
    identity->m_EHandle = CEntityHandle(4, 22); identity->m_pInstance = instance; identity->m_pClass = cls;
    instance->m_pEntity = identity;
    const std::uint64_t steam = 76561198000000004ull;
    std::memcpy(g_controller_storage.data() + kHealthOffset, &steam, sizeof(steam));
    const auto pawn = Identity()->GetRefEHandle();
    std::memcpy(g_controller_storage.data() + kHealthOffset + 8, &pawn, sizeof(pawn));
    std::memcpy(g_controller_storage.data() + kHealthOffset + 16, &pawn, sizeof(pawn));
    SetGameEntitySystem(true);
}

void HookDataFixture()
{
    auto* number = g_hook_float.Get();
    number->m_eTypeCategory = SCHEMA_TYPE_BUILTIN; number->m_eAtomicCategory = SCHEMA_ATOMIC_INVALID;
    number->m_eBuiltinType = SCHEMA_BUILTIN_TYPE_FLOAT32; number->m_nSize = 4;
    g_hook_damage_enum = {}; g_hook_damage_enum.m_pszName = "DamageTypes_t";
    g_hook_damage_enum.m_nSize = g_hook_damage_enum.m_nAlignment = 4;
    auto* enumeration = g_hook_enum.Get();
    enumeration->m_eTypeCategory = SCHEMA_TYPE_DECLARED_ENUM; enumeration->m_eAtomicCategory = SCHEMA_ATOMIC_INVALID;
    enumeration->m_pEnumInfo = &g_hook_damage_enum;
    auto* vector = g_hook_vector.Get();
    vector->m_eTypeCategory = SCHEMA_TYPE_ATOMIC; vector->m_eAtomicCategory = SCHEMA_ATOMIC_PLAIN;
    vector->m_sTypeName = "Vector"; vector->m_nSize = 12; vector->m_nAlignment = 4;
    auto* entity_type = g_hook_entity_type.Get();
    entity_type->m_eTypeCategory = SCHEMA_TYPE_DECLARED_CLASS; entity_type->m_eAtomicCategory = SCHEMA_ATOMIC_INVALID;
    entity_type->m_pClassInfo = &g_base_class;
    auto* handle = g_hook_handle.Get();
    handle->m_eTypeCategory = SCHEMA_TYPE_ATOMIC; handle->m_eAtomicCategory = SCHEMA_ATOMIC_T;
    handle->m_sTypeName = "CHandle<CBaseEntity>"; handle->m_nSize = handle->m_nAlignment = 4; handle->m_pTemplateType = entity_type;
    const char* names[]{"m_flDamage","m_bitsDamageType","m_iDamageCustom","m_hInflictor","m_hAttacker","m_hAbility","m_vecDamageForce","m_vecDamagePosition"};
    CSchemaType* types[]{number,enumeration,reinterpret_cast<CSchemaType_Builtin*>(g_int32_type_storage.data()),handle,handle,handle,vector,vector};
    for (unsigned i = 0; i < 8; ++i) g_hook_damage_fields[i] = {names[i],types[i],hook_data_fixture::offsets[i],0,nullptr};
    g_damage_class = {}; g_damage_class.m_pszName = "CTakeDamageInfo"; g_damage_class.m_nSize = 0x118;
    g_damage_class.m_nAlignment = 8; g_damage_class.m_nFieldCount = 8; g_damage_class.m_pFields = g_hook_damage_fields;
    g_hook_damage = hook_data_fixture::MakeDamage();
    auto* chainer = g_hook_chainer.Get();
    chainer->m_eTypeCategory = SCHEMA_TYPE_ATOMIC; chainer->m_eAtomicCategory = SCHEMA_ATOMIC_PLAIN;
    chainer->m_sTypeName = "CNetworkVarChainer"; chainer->m_nSize = 40; chainer->m_nAlignment = 8;
    g_hook_chain = {"__m_pChainEntity",chainer,16,0,nullptr};
    g_hook_component_base = {}; g_hook_component_base.m_pszName = "CPlayerPawnComponent";
    g_hook_component_base.m_nSize = 72; g_hook_component_base.m_nAlignment = 8;
    g_hook_component_base.m_nFieldCount = 1; g_hook_component_base.m_pFields = &g_hook_chain;
    g_hook_component_inherit = {0,&g_hook_component_base};
    g_hook_component = {}; g_hook_component.m_pszName = "CPlayer_WeaponServices"; g_hook_component.m_nSize = 128;
    g_hook_component.m_nAlignment = 8; g_hook_component.m_nBaseClassCount = 1; g_hook_component.m_pBaseClasses = &g_hook_component_inherit;
    auto* component_type = g_hook_component_type.Get();
    component_type->m_eTypeCategory = SCHEMA_TYPE_DECLARED_CLASS; component_type->m_eAtomicCategory = SCHEMA_ATOMIC_INVALID;
    component_type->m_pClassInfo = &g_hook_component;
    auto* pointer = g_hook_pointer.Get();
    pointer->m_eTypeCategory = SCHEMA_TYPE_POINTER; pointer->m_eAtomicCategory = SCHEMA_ATOMIC_INVALID; pointer->m_pObjectType = component_type;
    constexpr auto pointer_offset = Align(kHealthOffset+16,alignof(void*));
    g_hook_weapon_pointer = {"m_pWeaponServices",pointer,static_cast<int>(pointer_offset),0,nullptr};
    g_input_pawn_class = {}; g_input_pawn_class.m_pszName = "CBasePlayerPawn"; g_input_pawn_class.m_nSize = static_cast<int>(kEntitySize);
    g_input_pawn_class.m_nAlignment = 16; g_input_pawn_class.m_nFieldCount = 1; g_input_pawn_class.m_pFields = &g_hook_weapon_pointer;
    g_input_pawn_base = {0,&g_base_class}; g_input_pawn_class.m_nBaseClassCount = 1; g_input_pawn_class.m_pBaseClasses = &g_input_pawn_base;
    g_derived_bases[0].m_pClass = &g_input_pawn_class;
    g_hook_weapon.fill(std::byte{});
    StorePointer(g_hook_weapon.data()+16,EntityInstance());
    StorePointer(g_entity_storage.data()+pointer_offset,g_hook_weapon.data());
}

struct Initialize
{
    Initialize()
    {
        Reset();
    }
} g_initialize;

#if defined(KEELS2_SCHEMA_FIXTURE_NATIVE_TEST)
int RunHookDataChecks()
{
    Reset(); HookDataFixture(); SetGameEntitySystem(true);
#if defined(_WIN32)
    constexpr const char* module = "server.dll";
#else
    constexpr const char* module = "libserver.so";
#endif
    KeelCs2DamageSchema schema{};
    if (KeelCs2_ResolveDamageSchema(&g_schema_system,module,&schema) != KEEL_RESULT_OK) return 1001;
    KeelDamageInfo info{}; info.size = sizeof(info);
    const auto read = [&] { return KeelCs2_ReadDamage(&schema,g_hook_damage.bytes.data(),&info); };
    if (read() != KEEL_RESULT_OK || info.damage != 42.5f || info.damage_type != 0x80000040 || info.damage_custom != -7 ||
        info.inflictor != 0x6007 || info.attacker != UINT32_MAX || info.ability != 0x7008 || info.force[1] != 2 || info.position[2] != 6) return 1002;
    KeelDamageEdit edit{sizeof(edit),0,12.25f,32,{7,8,9},{10,11,12}};
    auto expected = g_hook_damage;
    std::memcpy(expected.bytes.data()+hook_data_fixture::offsets[0],&edit.damage,4);
    std::memcpy(expected.bytes.data()+hook_data_fixture::offsets[1],&edit.damage_type,4);
    std::memcpy(expected.bytes.data()+hook_data_fixture::offsets[6],edit.force,12);
    std::memcpy(expected.bytes.data()+hook_data_fixture::offsets[7],edit.position,12);
    if (KeelCs2_WriteDamage(&schema,g_hook_damage.bytes.data(),&edit) != KEEL_RESULT_OK ||
        g_hook_damage.bytes != expected.bytes || read() != KEEL_RESULT_OK || info.damage_custom != -7 || info.damage != 12.25f) return 1003;
    edit.position[2] = std::numeric_limits<float>::infinity();
    if (KeelCs2_WriteDamage(&schema,g_hook_damage.bytes.data(),&edit) != KEEL_RESULT_INVALID_ARGUMENT ||
        g_hook_damage.bytes != expected.bytes) return 1004;
    edit.position[2] = 12; edit.damage = -1;
    if (KeelCs2_WriteDamage(&schema,g_hook_damage.bytes.data(),&edit) != KEEL_RESULT_INVALID_ARGUMENT ||
        g_hook_damage.bytes != expected.bytes) return 1005;
    edit.damage = 1; auto invalid = schema; invalid.offsets[7] = invalid.offsets[0];
    if (KeelCs2_WriteDamage(&invalid,g_hook_damage.bytes.data(),&edit) != KEEL_RESULT_INCOMPATIBLE ||
        g_hook_damage.bytes != expected.bytes) return 1006;
    if (KeelCs2_ReadDamage(&schema,g_hook_damage.bytes.data()+1,&info) != KEEL_RESULT_INCOMPATIBLE || info.damage ||
        info.inflictor != UINT32_MAX) return 1007;
    const auto old = g_hook_damage_fields[0]; g_hook_damage_fields[0].m_nSingleInheritanceOffset = 112;
    if (KeelCs2_ResolveDamageSchema(&g_schema_system,module,&invalid) != KEEL_RESULT_INCOMPATIBLE || invalid.class_info) return 1008;
    g_hook_damage_fields[0] = old; g_hook_damage_enum.m_nSize = 8;
    if (KeelCs2_ResolveDamageSchema(&g_schema_system,module,&invalid) != KEEL_RESULT_INCOMPATIBLE) return 1009;
    g_hook_damage_enum.m_nSize = 4; g_hook_damage_fields[2].m_pszName = "missing";
    if (KeelCs2_ResolveDamageSchema(&g_schema_system,module,&invalid) != KEEL_RESULT_NOT_FOUND) return 1010;
    HookDataFixture();
    const auto nonfinite = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(g_hook_damage.bytes.data()+hook_data_fixture::offsets[0],&nonfinite,4);
    if (read() != KEEL_RESULT_INCOMPATIBLE || info.damage || info.attacker != UINT32_MAX || info.force[0]) return 1018;
    HookDataFixture();
    info.size = 0;
    if (KeelCs2_ReadDamage(&schema,g_hook_damage.bytes.data(),&info) != KEEL_RESULT_INVALID_ARGUMENT || info.damage) return 1019;
    edit.size = 0;
    if (KeelCs2_WriteDamage(&schema,g_hook_damage.bytes.data(),&edit) != KEEL_RESULT_INVALID_ARGUMENT ||
        g_hook_damage.bytes != hook_data_fixture::MakeDamage().bytes) return 1020;
    KeelCs2WeaponSchema weapon{};
    KeelCs2EntityIdentity pawn{};
    if (KeelCs2_ResolveWeaponSchema(&g_schema_system,module,&weapon) != KEEL_RESULT_OK ||
        KeelCs2_FindEntityByIndex(EntitySystem(),7,&pawn) != KEEL_RESULT_OK) return 1011;
    KeelBool matches = KEEL_FALSE;
    if (KeelCs2_WeaponMatches(EntitySystem(),&pawn,&weapon,g_hook_weapon.data(),&matches) != KEEL_RESULT_OK || !matches) return 1012;
    if (KeelCs2_WeaponMatches(EntitySystem(),&pawn,&weapon,reinterpret_cast<void*>(1),&matches) != KEEL_RESULT_OK || matches) return 1013;
    StorePointer(g_entity_storage.data()+weapon.pointer_offset,nullptr);
    if (KeelCs2_WeaponMatches(EntitySystem(),&pawn,&weapon,g_hook_weapon.data(),&matches) != KEEL_RESULT_OK || matches) return 1021;
    StorePointer(g_entity_storage.data()+weapon.pointer_offset,g_hook_weapon.data());
    StorePointer(g_hook_weapon.data()+16,nullptr);
    if (KeelCs2_WeaponMatches(EntitySystem(),&pawn,&weapon,g_hook_weapon.data(),&matches) != KEEL_RESULT_NOT_FOUND || matches) return 1014;
    StorePointer(g_hook_weapon.data()+16,EntityInstance());
    auto stale = pawn; stale.source2_handle += 0x8000;
    if (KeelCs2_WeaponMatches(EntitySystem(),&stale,&weapon,g_hook_weapon.data(),&matches) != KEEL_RESULT_NOT_FOUND || matches) return 1015;
    g_hook_weapon_pointer.m_pType = g_hook_float.Get();
    if (KeelCs2_ResolveWeaponSchema(&g_schema_system,module,&weapon) != KEEL_RESULT_INCOMPATIBLE || weapon.pawn_class) return 1016;
    HookDataFixture(); g_hook_component_inherit.m_pClass = &g_hook_component;
    if (KeelCs2_ResolveWeaponSchema(&g_schema_system,module,&weapon) != KEEL_RESULT_INCOMPATIBLE) return 1017;
    return 0;
}

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
unsigned g_write_notifications{};
bool g_write_notification_valid{}, g_write_destroy{};
void TestEntityWriteNotify(void* instance, const NetworkStateChangedData& data)
{
    ++g_write_notifications;
    g_write_notification_valid = instance == EntityInstance() && data.m_unk001 == 0 &&
        data.m_LocalOffsets.Count() == 0 && data.m_nArrayIndex == -1;
    if (g_write_destroy) Identity()->m_pInstance = nullptr;
}
alignas(16) std::array<std::byte,128> g_round_rules_storage{};
unsigned g_round_calls{};
std::uint32_t g_round_reason{}, g_round_team{};
float g_round_delay{};
bool g_round_arguments{}, g_round_destroy{};
#if defined(_WIN32)
void TestTerminateRound(void* rules, float delay, std::uint32_t reason, const std::uint32_t* team)
#else
void TestTerminateRound(void* rules, std::uint32_t reason, const std::uint32_t* team, float delay)
#endif
{
    ++g_round_calls; g_round_reason = reason; g_round_delay = delay; g_round_team = team ? *team : 0;
    g_round_arguments = rules == g_round_rules_storage.data();
    if (g_round_destroy) { Identity()->m_pInstance = nullptr; g_round_rules_storage.fill(std::byte{}); }
}
int RunRoundChecks()
{
    SchemaClassFieldData_t g_round_field{};
    Reset();
#if defined(_WIN32)
    constexpr const char* module = "server.dll";
#else
    constexpr const char* module = "libserver.so";
#endif
    std::array<void*,3> proxy_table{}, rules_table{}, other_table{};
    SchemaStorage<CSchemaType_Ptr> pointer;
    SchemaStorage<CSchemaType_DeclaredClass> declared;
    auto* object = declared.Get(); object->m_eTypeCategory = SCHEMA_TYPE_DECLARED_CLASS;
    object->m_eAtomicCategory = SCHEMA_ATOMIC_INVALID; object->m_pClassInfo = &g_round_rules_class;
    auto* ptr = pointer.Get(); ptr->m_eTypeCategory = SCHEMA_TYPE_POINTER;
    ptr->m_eAtomicCategory = SCHEMA_ATOMIC_INVALID; ptr->m_pObjectType = object;
    constexpr auto offset = static_cast<std::int32_t>(Align(kHealthOffset+8,alignof(void*)));
    g_round_field = {"m_pGameRules",ptr,offset,0,nullptr};
    g_round_proxy_class.m_pszName = "CCSGameRulesProxy";
    g_round_proxy_class.m_nSize = static_cast<int>(kEntitySize); g_round_proxy_class.m_nAlignment = 16;
    g_round_proxy_class.m_pFields = &g_round_field; g_round_proxy_class.m_nFieldCount = 1;
    g_round_rules_class.m_pszName = "CCSGameRules";
    g_round_rules_class.m_nSize = static_cast<int>(g_round_rules_storage.size()); g_round_rules_class.m_nAlignment = 16;
    g_entity_class_info.m_pSchemaBinding = &g_round_proxy_class;
    StorePointer(g_entity_storage.data(),proxy_table.data());
    StorePointer(g_entity_storage.data()+offset,g_round_rules_storage.data());
    StorePointer(g_round_rules_storage.data(),rules_table.data());
    const KeelCs2RoundBindings bindings{rules_table.data(),proxy_table.data(),FunctionAddress(&TestTerminateRound)};
    KeelCs2RoundContext context{};
    const auto resolve = [&] { return KeelCs2_ResolveRoundSchema(&g_schema_system,module,&context); };
    const auto find = [&] { return KeelCs2_FindRoundContext(EntitySystem(),&bindings,&context); };
    KeelRoundTermination request{sizeof(request),8,2.5f,3,0};
    const auto apply = [&] { return KeelCs2_TerminateRound(EntitySystem(),&context,&request,&bindings); };
    g_round_calls = 0; g_round_destroy = false;
    if (resolve() != KEEL_RESULT_OK || find() != KEEL_RESULT_OK || context.rules != g_round_rules_storage.data() ||
        context.proxy.index != kEntityIndex || apply() != KEEL_RESULT_OK || g_round_calls != 1 || !g_round_arguments ||
        g_round_reason != 8 || g_round_delay != 2.5f || g_round_team != 3) return 700;
    request.team = 0; request.delay = 0;
    for (unsigned reason : {1u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,14u,16u,17u,18u,19u,20u,21u,22u}) {
        request.reason = reason;
        if (apply() != KEEL_RESULT_OK || g_round_reason != reason || g_round_team || g_round_delay) return 701;
    }
    const auto valid_calls = g_round_calls;
    for (unsigned reason : {0u,2u,3u,15u,23u,UINT32_MAX}) { request.reason = reason; if (apply() != KEEL_RESULT_INVALID_ARGUMENT) return 702; }
    request.reason = 8;
    for (float delay : {-1.0f,3601.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        request.delay = delay; if (apply() != KEEL_RESULT_INVALID_ARGUMENT) return 703;
    }
    request.delay = 1;
    for (int team : {-1,1,4,999}) { request.team = team; if (apply() != KEEL_RESULT_INVALID_ARGUMENT) return 704; }
    request.team = 2; request.reserved = 1; if (apply() != KEEL_RESULT_INVALID_ARGUMENT) return 705;
    request.reserved = 0; --request.size; if (apply() != KEEL_RESULT_INVALID_ARGUMENT) return 706; ++request.size;
    SetHandle(*Identity(),13); if (apply() != KEEL_RESULT_NOT_FOUND) return 707; SetHandle(*Identity(),12);
    StorePointer(g_entity_storage.data(),other_table.data()); if (apply() != KEEL_RESULT_INCOMPATIBLE) return 708;
    StorePointer(g_entity_storage.data(),proxy_table.data());
    StorePointer(g_round_rules_storage.data(),other_table.data()); if (apply() != KEEL_RESULT_INCOMPATIBLE) return 709;
    StorePointer(g_round_rules_storage.data(),rules_table.data());
    alignas(16) std::array<std::byte,128> other_rules{}; StorePointer(other_rules.data(),rules_table.data());
    StorePointer(g_entity_storage.data()+offset,other_rules.data()); if (apply() != KEEL_RESULT_NOT_FOUND) return 710;
    StorePointer(g_entity_storage.data()+offset,nullptr); if (find() != KEEL_RESULT_NOT_READY) return 711;
    StorePointer(g_entity_storage.data()+offset,g_round_rules_storage.data());
    for (int invalid : {-1,offset+1,static_cast<int>(kEntitySize)}) {
        g_round_field.m_nSingleInheritanceOffset = invalid;
        if (resolve() != KEEL_RESULT_INCOMPATIBLE) return 712;
    }
    g_round_field.m_nSingleInheritanceOffset = offset;
    object->m_pClassInfo = &g_base_class; if (resolve() != KEEL_RESULT_INCOMPATIBLE) return 713;
    object->m_pClassInfo = &g_round_rules_class;
    ptr->m_eTypeCategory = SCHEMA_TYPE_BUILTIN; if (resolve() != KEEL_RESULT_INCOMPATIBLE) return 714;
    ptr->m_eTypeCategory = SCHEMA_TYPE_POINTER;
    SchemaClassFieldData_t duplicate_fields[]{g_round_field,g_round_field};
    g_round_proxy_class.m_pFields = duplicate_fields; g_round_proxy_class.m_nFieldCount = 2;
    if (resolve() != KEEL_RESULT_INCOMPATIBLE) return 715;
    g_round_proxy_class.m_pFields = &g_round_field; g_round_proxy_class.m_nFieldCount = 1;
    if (resolve() != KEEL_RESULT_OK || find() != KEEL_RESULT_OK) return 716;
    auto* second = Identity()+1; second->m_EHandle = CEntityHandle(kEntityIndex+1,1);
    second->m_pClass = EntityClass(); second->m_pInstance = reinterpret_cast<CEntityInstance*>(g_controller_storage.data());
    second->m_pInstance->m_pEntity = second; second->m_flags = static_cast<EntityFlags_t>(0);
    if (find() != KEEL_RESULT_INCOMPATIBLE) return 717;
    second->m_pInstance = nullptr;
    Identity()->m_flags = EF_IS_PRE_SPAWN;
    if (find() != KEEL_RESULT_NOT_FOUND) return 718;
    Identity()->m_flags = static_cast<EntityFlags_t>(0);
    if (find() != KEEL_RESULT_OK || g_round_calls != valid_calls) return 719;
    g_round_destroy = true;
    if (apply() != KEEL_RESULT_OK || g_round_calls != valid_calls+1 || Identity()->m_pInstance) return 720;
    Reset();
    return 0;
}

unsigned g_stat_notifications{};
bool g_stat_notification_valid{}, g_stat_destroy{}, g_stat_throw{};
void* g_stat_notify_address{};
std::int32_t g_stat_observed{};
void TestStatNotify(void* controller, const NetworkStateChangedData& changed)
{
    ++g_stat_notifications;
    g_stat_notification_valid = controller == EntityInstance() && changed.m_unk001 == 0;
    if (g_stat_notify_address) std::memcpy(&g_stat_observed,g_stat_notify_address,sizeof(g_stat_observed));
    if (g_stat_destroy) Identity()->m_pInstance = nullptr;
    if (g_stat_throw) throw std::runtime_error("fixture notification failed after mutation");
}
int RunPlayerStatChecks()
{
    Reset();
#if defined(_WIN32)
    constexpr const char* module = "server.dll";
#else
    constexpr const char* module = "libserver.so";
#endif
    std::array<void*,30> controller_table{};
    std::array<void*,3> money_table{}, tracking_table{}, wrong_table{};
    controller_table[29] = FunctionAddress(&TestStatNotify);
    alignas(16) std::array<std::byte,128> money{};
    alignas(16) std::array<std::byte,256> tracking{};
    const auto info = [](CSchemaClassInfo& type, const char* name, int size, std::uint8_t alignment) {
        type = {}; type.m_pszName = name; type.m_nSize = size; type.m_nAlignment = alignment;
    };
    info(g_controller_class,"CCSPlayerController",static_cast<int>(kEntitySize),16);
    info(g_stat_base,"CPlayerControllerComponent",64,16);
    info(g_stat_money,"CCSPlayerController_InGameMoneyServices",128,16);
    info(g_stat_tracking,"CCSPlayerController_ActionTrackingServices",256,16);
    info(g_stat_match,"CSMatchStats_t",32,4);
    info(g_stat_values,"CSPerRoundStats_t",16,4);
    info(g_stat_chainer,"CNetworkVarChainer",40,8);
    SchemaBaseClassInfoData_t root_base{0,&g_base_class}, component_base{0,&g_stat_base}, match_base{0,&g_stat_values};
    g_controller_class.m_nBaseClassCount = 1; g_controller_class.m_pBaseClasses = &root_base;
    g_stat_money.m_nBaseClassCount = g_stat_tracking.m_nBaseClassCount = 1;
    g_stat_money.m_pBaseClasses = g_stat_tracking.m_pBaseClasses = &component_base;
    g_stat_match.m_nBaseClassCount = 1; g_stat_match.m_pBaseClasses = &match_base;
    SchemaStorage<CSchemaType_DeclaredClass> money_type, tracking_type, match_type, chain_type;
    const auto declared = [](CSchemaType_DeclaredClass* type, CSchemaClassInfo* target) {
        type->m_eTypeCategory = SCHEMA_TYPE_DECLARED_CLASS; type->m_eAtomicCategory = SCHEMA_ATOMIC_INVALID; type->m_pClassInfo = target;
    };
    declared(money_type.Get(),&g_stat_money); declared(tracking_type.Get(),&g_stat_tracking);
    declared(match_type.Get(),&g_stat_match); declared(chain_type.Get(),&g_stat_chainer);
    SchemaStorage<CSchemaType_Ptr> money_pointer, tracking_pointer;
    const auto pointer = [](CSchemaType_Ptr* type, CSchemaType* target) {
        type->m_eTypeCategory = SCHEMA_TYPE_POINTER; type->m_eAtomicCategory = SCHEMA_ATOMIC_INVALID; type->m_pObjectType = target;
    };
    pointer(money_pointer.Get(),money_type.Get()); pointer(tracking_pointer.Get(),tracking_type.Get());
    constexpr auto money_pointer_offset = static_cast<std::int32_t>(Align(kHealthOffset+8,8));
    constexpr auto tracking_pointer_offset = money_pointer_offset+8;
    g_controller_fields[0] = {"m_pInGameMoneyServices",money_pointer.Get(),money_pointer_offset,0,nullptr};
    g_controller_fields[1] = {"m_pActionTrackingServices",tracking_pointer.Get(),tracking_pointer_offset,0,nullptr};
    g_controller_class.m_nFieldCount = 2; g_controller_class.m_pFields = g_controller_fields;
    SchemaClassFieldData_t chain_field{"__m_pChainEntity",chain_type.Get(),8,0,nullptr};
    g_stat_base.m_nFieldCount = 1; g_stat_base.m_pFields = &chain_field;
    auto* integer = reinterpret_cast<CSchemaType_Builtin*>(g_int32_type_storage.data());
    SchemaClassFieldData_t money_field{"m_iAccount",integer,80,0,nullptr};
    SchemaClassFieldData_t tracking_field{"m_matchStats",match_type.Get(),80,0,nullptr};
    SchemaClassFieldData_t values[]{{"m_iKills",integer,0,0,nullptr},{"m_iDeaths",integer,4,0,nullptr},{"m_iAssists",integer,8,0,nullptr}};
    g_stat_money.m_nFieldCount = g_stat_tracking.m_nFieldCount = 1;
    g_stat_money.m_pFields = &money_field; g_stat_tracking.m_pFields = &tracking_field;
    g_stat_values.m_nFieldCount = 3; g_stat_values.m_pFields = values;
    g_entity_class_info.m_pSchemaBinding = &g_controller_class;
    StorePointer(g_entity_storage.data(),controller_table.data());
    StorePointer(g_entity_storage.data()+money_pointer_offset,money.data());
    StorePointer(g_entity_storage.data()+tracking_pointer_offset,tracking.data());
    StorePointer(money.data(),money_table.data()); StorePointer(tracking.data(),tracking_table.data());
    StorePointer(money.data()+8,EntityInstance()); StorePointer(tracking.data()+8,EntityInstance());
    const KeelCs2PlayerStatisticsBindings bindings{controller_table.data(),money_table.data(),tracking_table.data(),controller_table[29]};
    KeelCs2EntityIdentity entity{};
    if (KeelCs2_FindEntityByIndex(EntitySystem(),kEntityIndex,&entity) != KEEL_RESULT_OK) return 801;
    KeelCs2PlayerStatSchema schema{};
    const auto resolve = [&](unsigned key) { return KeelCs2_ResolvePlayerStatSchema(&g_schema_system,module,key,&schema); };
    std::int32_t value{};
    const auto read = [&] { value = 99; return KeelCs2_ReadPlayerStat(EntitySystem(),&entity,&schema,&bindings,&value); };
    const auto write = [&](std::int32_t next) { return KeelCs2_WritePlayerStat(EntitySystem(),&entity,&schema,&bindings,next); };
    g_stat_notifications = 0; g_stat_destroy = g_stat_throw = false;
    for (unsigned key : {1u,2u,4u,8u})
    {
        if (resolve(key) != KEEL_RESULT_OK || read() != KEEL_RESULT_OK || value) return 802;
        g_stat_notify_address = key == 1 ? money.data()+80 : tracking.data()+80+(key == 2 ? 0 : key == 4 ? 4 : 8);
        const auto expected = static_cast<std::int32_t>(20+key);
        if (write(expected) != KEEL_RESULT_OK || !g_stat_notification_valid || g_stat_observed != expected ||
            read() != KEEL_RESULT_OK || value != expected) return 803;
        const auto notifications = g_stat_notifications;
        if (write(expected) != KEEL_RESULT_OK || g_stat_notifications != notifications) return 804;
    }
    if (g_stat_notifications != 4 || resolve(1) != KEEL_RESULT_OK) return 805;
    g_stat_notify_address = money.data()+80;
    if (write(INT32_MAX) != KEEL_RESULT_OK || read() != KEEL_RESULT_OK || value != INT32_MAX) return 806;
    const auto valid_notifications = g_stat_notifications;
    if (write(-1) != KEEL_RESULT_INVALID_ARGUMENT) return 807;
    SetHandle(*Identity(),13); if (read() != KEEL_RESULT_NOT_FOUND || value || write(1) != KEEL_RESULT_NOT_FOUND) return 808;
    SetHandle(*Identity(),12);
    StorePointer(money.data()+8,tracking.data()); if (read() != KEEL_RESULT_INCOMPATIBLE || value || write(1) != KEEL_RESULT_INCOMPATIBLE) return 809;
    StorePointer(money.data()+8,EntityInstance());
    StorePointer(g_entity_storage.data()+money_pointer_offset,nullptr); if (read() != KEEL_RESULT_NOT_READY || value) return 810;
    StorePointer(g_entity_storage.data()+money_pointer_offset,money.data());
    StorePointer(money.data(),wrong_table.data()); if (read() != KEEL_RESULT_INCOMPATIBLE || value) return 811;
    StorePointer(money.data(),money_table.data());
    const auto original = schema;
    schema.value_offset = 8; if (write(1) != KEEL_RESULT_INCOMPATIBLE) return 812;
    schema = original; schema.value_offset = INT32_MAX; if (write(1) != KEEL_RESULT_INCOMPATIBLE) return 813;
    schema = original;
    for (int bad : {-1,81,128,8}) { money_field.m_nSingleInheritanceOffset = bad; if (resolve(1) != KEEL_RESULT_INCOMPATIBLE) return 814; }
    money_field.m_nSingleInheritanceOffset = 80;
    integer->m_eBuiltinType = SCHEMA_BUILTIN_TYPE_UINT32; if (resolve(1) != KEEL_RESULT_INCOMPATIBLE) return 815;
    integer->m_eBuiltinType = SCHEMA_BUILTIN_TYPE_INT32;
    money_pointer.Get()->m_pObjectType = tracking_type.Get(); if (resolve(1) != KEEL_RESULT_INCOMPATIBLE) return 816;
    money_pointer.Get()->m_pObjectType = money_type.Get();
    chain_field.m_nSingleInheritanceOffset = 0; if (resolve(1) != KEEL_RESULT_INCOMPATIBLE) return 817; chain_field.m_nSingleInheritanceOffset = 8;
    g_stat_chainer.m_nSize = 32; if (resolve(1) != KEEL_RESULT_INCOMPATIBLE) return 818; g_stat_chainer.m_nSize = 40;
    SchemaClassFieldData_t duplicates[]{money_field,money_field}; g_stat_money.m_pFields = duplicates; g_stat_money.m_nFieldCount = 2;
    if (resolve(1) != KEEL_RESULT_INCOMPATIBLE) return 819;
    g_stat_money.m_pFields = &money_field; g_stat_money.m_nFieldCount = 1;
    tracking_field.m_nSingleInheritanceOffset = 250; if (resolve(2) != KEEL_RESULT_INCOMPATIBLE) return 820; tracking_field.m_nSingleInheritanceOffset = 80;
    SchemaStorage<CSchemaType_Atomic> atomic_storage;
    auto* atomic = atomic_storage.Get(); atomic->m_eTypeCategory = SCHEMA_TYPE_ATOMIC; atomic->m_eAtomicCategory = SCHEMA_ATOMIC_PLAIN;
    atomic->m_sTypeName = "CNetworkVarChainer"; atomic->m_nSize = 40; atomic->m_nAlignment = 8;
    struct NameHold { CSchemaType_Atomic* type; ~NameHold() { type->m_sTypeName.Purge(); } } name_hold{atomic};
    chain_field.m_pType = atomic;
    if (resolve(1) != KEEL_RESULT_OK || read() != KEEL_RESULT_OK) return 821;
    atomic->m_nSize = 32; if (resolve(1) != KEEL_RESULT_INCOMPATIBLE) return 822; atomic->m_nSize = 40;
    for (unsigned invalid : {0u,3u,16u,UINT32_MAX}) if (resolve(invalid) != KEEL_RESULT_INVALID_ARGUMENT || schema.controller_class) return 823;
    if (g_stat_notifications != valid_notifications || resolve(1) != KEEL_RESULT_OK) return 824;
    g_stat_throw = true;
    if (write(10) != KEEL_RESULT_ENGINE_FAILURE || g_stat_observed != 10 || read() != KEEL_RESULT_OK || value != 10) return 825;
    g_stat_throw = false; g_stat_destroy = true;
    if (write(11) != KEEL_RESULT_OK || Identity()->m_pInstance || g_stat_observed != 11) return 826;
    g_stat_destroy = false; g_stat_notify_address = nullptr;
    Reset();
    return 0;
}

int RunEntityWriteChecks()
{
    Reset();
#if defined(_WIN32)
    constexpr const char* module = "server.dll";
#else
    constexpr const char* module = "libserver.so";
#endif
    std::array<void*,30> table{}; auto* notify = FunctionAddress(&TestEntityWriteNotify);
    table[29] = notify; StorePointer(g_entity_storage.data(), table.data());
    g_write_notifications = 0; g_write_destroy = false;
    KeelCs2EntityIdentity entity{};
    if (KeelCs2_FindEntityByIndex(EntitySystem(), kEntityIndex, &entity) != KEEL_RESULT_OK) return 450;
    KeelCs2SchemaField field{};
    if (KeelCs2_ResolveSchemaField(&g_schema_system,module,"CBaseEntity","m_iHealth",KEELS2_SCHEMA_INT32,&field) != KEEL_RESULT_OK) return 451;
    std::int32_t value = 84;
    const auto write = [&](const void* input, unsigned size) { return KeelCs2_WriteEntityField(EntitySystem(),&g_schema_system,module,&entity,&field,input,size,notify); };
    const auto health = [&] { std::int32_t result{}; std::memcpy(&result,g_entity_storage.data()+kHealthOffset,sizeof(result)); return result; };
    if (write(&value,sizeof(value)) != KEEL_RESULT_OK || health() != value || g_write_notifications != 1 || !g_write_notification_valid) return 452;
    if (write(&value,sizeof(value)) != KEEL_RESULT_OK || g_write_notifications != 1) return 453;
    auto original = entity; ++entity.source2_handle;
    if (write(&value,sizeof(value)) != KEEL_RESULT_NOT_FOUND || g_write_notifications != 1) return 454;
    entity = original; table[29] = nullptr; value = 12;
    if (write(&value,sizeof(value)) != KEEL_RESULT_INCOMPATIBLE || health() != 84) return 455;
    table[29] = notify; const auto original_field = field;
    field.offset = static_cast<int>(kEntitySize);
    if (write(&value,sizeof(value)) != KEEL_RESULT_INCOMPATIBLE || g_write_notifications != 1) return 456;
    field = original_field; ++field.offset;
    if (write(&value,sizeof(value)) != KEEL_RESULT_INCOMPATIBLE || g_write_notifications != 1) return 457;
    field = original_field; field.value_type = KEELS2_SCHEMA_ENTITY_HANDLE;
    if (write(&value,sizeof(value)) != KEEL_RESULT_INVALID_ARGUMENT) return 458;
    field = {&g_base_class,static_cast<int>(kHealthOffset),1,1,KEELS2_SCHEMA_BOOL};
    const std::uint8_t invalid_bool = 2;
    if (write(&invalid_bool,1) != KEEL_RESULT_INVALID_ARGUMENT || g_write_notifications != 1) return 459;
    field = {&g_base_class,static_cast<int>(kHealthOffset),4,4,KEELS2_SCHEMA_FLOAT32};
    const float nonfinite = std::numeric_limits<float>::infinity();
    if (write(&nonfinite,4) != KEEL_RESULT_INVALID_ARGUMENT || g_write_notifications != 1) return 460;
    const float finite = 2.5f;
    if (write(&finite,4) != KEEL_RESULT_OK || g_write_notifications != 2) return 461;
    field = {&g_base_class,static_cast<int>(kHealthOffset),12,4,KEELS2_SCHEMA_VECTOR3};
    const float vector[]{1,2,3};
    if (write(vector,sizeof(vector)) != KEEL_RESULT_OK || g_write_notifications != 3) return 462;
    field = original_field; g_write_destroy = true;
    if (write(&value,sizeof(value)) != KEEL_RESULT_OK || g_write_notifications != 4 ||
        KeelCs2_ValidateEntity(EntitySystem(),&entity) != KEEL_RESULT_NOT_FOUND) return 463;
    g_write_destroy = false;
    Reset();
    return 0;
}

int g_management_calls{};
int g_management_team{};
bool g_management_arguments{};
void ManagementChangeTeam(void* controller, std::int32_t team)
{
    g_management_arguments = controller == g_controller_storage.data();
    g_management_team = team; g_management_calls += 1;
}
void ManagementSwitchTeam(void* controller, std::int32_t team)
{
    g_management_arguments = controller == g_controller_storage.data();
    g_management_team = team; g_management_calls += 10;
}
void ManagementRespawn(void* controller)
{
    g_management_arguments = controller == g_controller_storage.data();
    g_management_calls += 100;
}
void ManagementSetPawn(void* controller, void* pawn, bool one, bool two, bool three, bool four)
{
    g_management_arguments = controller == g_controller_storage.data() && pawn == EntityInstance() && one && !two && !three && !four;
    const auto handle = Identity()->GetRefEHandle();
    std::memcpy(g_controller_storage.data() + kHealthOffset + 16, &handle, sizeof(handle));
    g_management_calls += 1000;
}
int RunPlayerManagementChecks()
{
    InputFixture(0);
#if defined(_WIN32)
    constexpr const char* module = "server.dll";
#else
    constexpr const char* module = "libserver.so";
#endif
    SchemaStorage<CSchemaType_Builtin> uint8_storage;
    auto* byte = uint8_storage.Get();
    byte->m_eTypeCategory = SCHEMA_TYPE_BUILTIN; byte->m_eAtomicCategory = SCHEMA_ATOMIC_INVALID;
    byte->m_eBuiltinType = SCHEMA_BUILTIN_TYPE_UINT8; byte->m_nSize = 1;
    g_base_fields[0] = {"m_iTeamNum", byte, static_cast<int>(kHealthOffset + 24), 0, nullptr};
    SchemaBaseClassInfoData_t base{0, &g_base_class};
    g_controller_base.m_nBaseClassCount = 1; g_controller_base.m_pBaseClasses = &base;
    g_controller_storage[kHealthOffset + 24] = std::byte{2};
    std::array<void*, 273> table{};
    table[102] = FunctionAddress(&ManagementChangeTeam); table[272] = FunctionAddress(&ManagementRespawn);
    StorePointer(g_controller_storage.data(), table.data());
    KeelCs2PlayerManagementBindings bindings{table.data(), table[102], FunctionAddress(&ManagementSwitchTeam),
        table[272], FunctionAddress(&ManagementSetPawn)};
    KeelCs2EntityIdentity controller{}, pawn{};
    if (KeelCs2_FindEntityByIndex(EntitySystem(), 4, &controller) != KEEL_RESULT_OK) return 400;
    KeelPlayerManagementAction action{sizeof(action), KEELS2_PLAYER_MANAGEMENT_CHANGE_TEAM, 3, 0};
    const auto apply = [&] { return KeelCs2_ManagePlayer(EntitySystem(), &g_schema_system, module, &controller, &pawn, &action, &bindings); };
    const auto prepare = [&] { return KeelCs2_PrepareRespawn(EntitySystem(), &g_schema_system, module, &controller, &bindings, &pawn); };
    g_management_calls = 0;
    if (apply() != KEEL_RESULT_OK || g_management_calls != 1 || g_management_team != 3 || !g_management_arguments) return 401;
    action.kind = KEELS2_PLAYER_MANAGEMENT_SWITCH_TEAM; action.team = 2;
    if (apply() != KEEL_RESULT_OK || g_management_calls != 11 || g_management_team != 2 || !g_management_arguments) return 402;
    for (const auto team : {-1,0,1,4,256})
    {
        action.team = team;
        if (apply() != KEEL_RESULT_INVALID_ARGUMENT || g_management_calls != 11) return 403;
    }
    action = {sizeof(action), KEELS2_PLAYER_MANAGEMENT_RESPAWN, 0, 0};
    if (prepare() != KEEL_RESULT_OK || g_management_calls != 1011 || !g_management_arguments || pawn.index != kEntityIndex) return 404;
    if (apply() != KEEL_RESULT_OK || g_management_calls != 1111 || !g_management_arguments) return 405;
    auto saved = pawn; ++pawn.source2_handle;
    if (apply() != KEEL_RESULT_NOT_FOUND || g_management_calls != 1111) return 406;
    pawn = saved;
    const std::uint32_t invalid = UINT32_MAX;
    std::memcpy(g_controller_storage.data() + kHealthOffset + 16, &invalid, sizeof(invalid));
    if (apply() != KEEL_RESULT_NOT_READY || g_management_calls != 1111) return 407;
    g_controller_storage[kHealthOffset + 24] = std::byte{1};
    if (prepare() != KEEL_RESULT_NOT_READY || g_management_calls != 1111) return 408;
    g_controller_storage[kHealthOffset + 24] = std::byte{2};
    auto source = controller.source2_handle; ++controller.source2_handle;
    if (prepare() != KEEL_RESULT_NOT_FOUND || g_management_calls != 1111) return 409;
    controller.source2_handle = source;
    g_controller_class.m_pszName = "NotAPlayerController";
    if (prepare() != KEEL_RESULT_INCOMPATIBLE || g_management_calls != 1111) return 410;
    g_controller_class.m_pszName = "CCSPlayerController";
    table[102] = nullptr;
    if (prepare() != KEEL_RESULT_INCOMPATIBLE || g_management_calls != 1111) return 411;
    table[102] = bindings.change_team;
    std::memcpy(g_controller_storage.data() + kHealthOffset + 8, &invalid, sizeof(invalid));
    if (prepare() != KEEL_RESULT_NOT_READY || g_management_calls != 1111) return 412;
    const CEntityHandle stale(kEntityIndex, 13);
    std::memcpy(g_controller_storage.data() + kHealthOffset + 8, &stale, sizeof(stale));
    if (prepare() != KEEL_RESULT_NOT_FOUND || g_management_calls != 1111) return 413;
    Reset();
    return 0;
}

int RunPlayerActionChecks()
{
    Reset();
#if defined(_WIN32)
    constexpr const char* module = "server.dll";
    constexpr std::uint32_t teleport_slot = 163;
#else
    constexpr const char* module = "libserver.so";
    constexpr std::uint32_t teleport_slot = 162;
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
    table[teleport_slot] = FunctionAddress(&TestTeleport);
    table[384] = FunctionAddress(&TestSuicide);
    StorePointer(g_entity_storage.data(), table.data());
    KeelCs2EntityIdentity entity{};
    if (KeelCs2_FindEntityByIndex(EntitySystem(), kEntityIndex, &entity) != KEEL_RESULT_OK)
        return 200;
    KeelCs2PlayerActionBindings bindings{teleport_slot,384,FunctionAddress(&TestDamageConstruct),
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
    bindings.teleport_slot = teleport_slot == 163 ? 162 : 163;
    if (apply() != KEEL_RESULT_INVALID_ARGUMENT || !g_actions.empty())
        return 215;
    bindings.teleport_slot = teleport_slot;
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

int RunPlayerInputChecks()
{
#if defined(_WIN32)
    constexpr const char* module = "server.dll";
#else
    constexpr const char* module = "libserver.so";
#endif
    InputFixture(static_cast<std::uint64_t>(IN_FORWARD) | static_cast<std::uint64_t>(IN_SCORE) | (UINT64_C(1) << 63));
    const KeelCs2EntityIdentity pawn{kEntityIndex, static_cast<std::uint32_t>(Identity()->GetRefEHandle().ToInt())};
    std::uint64_t buttons{}; void* component{};
    const auto read = [&] { return KeelCs2_ReadPlayerButtons(EntitySystem(), &g_schema_system, module, &pawn, &buttons, &component); };
    const auto original = g_movement_storage;
    if (read() != KEEL_RESULT_OK || buttons != (KEELS2_BUTTON_FORWARD | KEELS2_BUTTON_SCORE) ||
        component != g_movement_storage.data() || original != g_movement_storage) return 220;
    auto* controller_identity = &reinterpret_cast<CEntityIdentity*>(g_identity_storage.data())[4];
    KeelCs2EntityIdentity controller{4, static_cast<std::uint32_t>(controller_identity->GetRefEHandle().ToInt())};
    std::uint32_t active{};
    const auto controlled = [&] {
        buttons = UINT64_MAX; component = reinterpret_cast<void*>(1); active = 0;
        return KeelCs2_ReadControllerInput(EntitySystem(), &g_schema_system, module, &controller, &buttons, &component, &active);
    };
    if (controlled() != KEEL_RESULT_OK || active != pawn.source2_handle ||
        buttons != (KEELS2_BUTTON_FORWARD | KEELS2_BUTTON_SCORE)) return 231;
    g_buttons_registered = false;
    if (DeclaredClass("CInButtonState") || controlled() != KEEL_RESULT_OK || active != pawn.source2_handle ||
        buttons != (KEELS2_BUTTON_FORWARD | KEELS2_BUTTON_SCORE) || component != g_movement_storage.data()) return 234;
    g_buttons_registered = true;
    g_movement_class.m_nAlignment = UINT8_MAX;
    g_buttons_class.m_nAlignment = UINT8_MAX;
    if (controlled() != KEEL_RESULT_OK || active != pawn.source2_handle ||
        buttons != (KEELS2_BUTTON_FORWARD | KEELS2_BUTTON_SCORE) ||
        component != g_movement_storage.data() || original != g_movement_storage) return 238;
    const auto reject_alignment = [&] {
        return controlled() == KEEL_RESULT_INCOMPATIBLE && !buttons && !component && active == UINT32_MAX;
    };
    const auto pointer_offset = static_cast<std::size_t>(g_input_pointer.m_nSingleInheritanceOffset);
    StorePointer(g_entity_storage.data() + pointer_offset, g_movement_storage.data() + 1);
    if (!reject_alignment()) return 239;
    StorePointer(g_entity_storage.data() + pointer_offset, g_movement_storage.data());
    g_input_state.m_nSingleInheritanceOffset = 17;
    if (!reject_alignment()) return 240;
    g_input_state.m_nSingleInheritanceOffset = 40;
    if (!reject_alignment()) return 241;
    g_input_state.m_nSingleInheritanceOffset = 16;
    g_input_masks.m_nSingleInheritanceOffset = 1;
    if (!reject_alignment()) return 242;
    g_input_masks.m_nSingleInheritanceOffset = 8;
    for (const auto alignment : {0, 3, 7, 129, 254}) {
        g_movement_class.m_nAlignment = static_cast<std::uint8_t>(alignment);
        if (!reject_alignment()) return 243;
        g_movement_class.m_nAlignment = UINT8_MAX;
        g_buttons_class.m_nAlignment = static_cast<std::uint8_t>(alignment);
        if (!reject_alignment()) return 244;
        g_buttons_class.m_nAlignment = UINT8_MAX;
    }
    g_input_array_type.Get()->m_nElementAlignment = UINT8_MAX;
    if (!reject_alignment()) return 245;
    g_input_array_type.Get()->m_nElementAlignment = 8;
    g_input_pawn_class.m_nAlignment = UINT8_MAX;
    if (!reject_alignment()) return 246;
    g_input_pawn_class.m_nAlignment = 16;
    g_movement_class.m_nAlignment = 16;
    g_buttons_class.m_nAlignment = 8;
    g_input_state_type.Get()->m_pClassInfo = nullptr;
    if (controlled() != KEEL_RESULT_INCOMPATIBLE || buttons || component || active != UINT32_MAX) return 235;
    g_input_state_type.Get()->m_pClassInfo = &g_movement_class;
    if (controlled() != KEEL_RESULT_INCOMPATIBLE || buttons || component || active != UINT32_MAX) return 236;
    g_input_state_type.Get()->m_pClassInfo = &g_buttons_class;
    g_input_state_type.Get()->m_eTypeCategory = SCHEMA_TYPE_POINTER;
    if (controlled() != KEEL_RESULT_INCOMPATIBLE || buttons || component || active != UINT32_MAX) return 237;
    g_input_state_type.Get()->m_eTypeCategory = SCHEMA_TYPE_DECLARED_CLASS;
    const CEntityHandle invalid;
    std::memcpy(g_controller_storage.data() + kHealthOffset + 16, &invalid, sizeof(invalid));
    if (controlled() != KEEL_RESULT_NOT_READY || buttons || component || active != UINT32_MAX) return 232;
    const auto active_handle = Identity()->GetRefEHandle();
    std::memcpy(g_controller_storage.data() + kHealthOffset + 16, &active_handle, sizeof(active_handle));
    controller.source2_handle = static_cast<std::uint32_t>(CEntityHandle(4, 23).ToInt());
    if (controlled() != KEEL_RESULT_NOT_FOUND || buttons || component || active != UINT32_MAX) return 233;
    const auto reject = [&](KeelResult expected) { buttons = UINT64_MAX; component = reinterpret_cast<void*>(1); return read() == expected && !buttons && !component; };
    const auto offset = static_cast<std::size_t>(g_input_pointer.m_nSingleInheritanceOffset);
    StorePointer(g_entity_storage.data() + offset, nullptr);
    if (!reject(KEEL_RESULT_NOT_READY)) return 221;
    StorePointer(g_entity_storage.data() + offset, reinterpret_cast<void*>(1));
    g_input_array_type.Get()->m_nElementCount = 2;
    if (!reject(KEEL_RESULT_INCOMPATIBLE)) return 222;
    g_input_array_type.Get()->m_nElementCount = 3;
    if (!reject(KEEL_RESULT_INCOMPATIBLE)) return 223;
    StorePointer(g_entity_storage.data() + offset, g_movement_storage.data());
    g_input_uint64.Get()->m_eBuiltinType = SCHEMA_BUILTIN_TYPE_INT64;
    if (!reject(KEEL_RESULT_INCOMPATIBLE)) return 224;
    g_input_uint64.Get()->m_eBuiltinType = SCHEMA_BUILTIN_TYPE_UINT64;
    g_input_state.m_nSingleInheritanceOffset = 40;
    if (!reject(KEEL_RESULT_INCOMPATIBLE)) return 225;
    g_input_state.m_nSingleInheritanceOffset = 16;
    g_input_masks.m_nSingleInheritanceOffset = -8;
    if (!reject(KEEL_RESULT_INCOMPATIBLE)) return 226;
    g_input_masks.m_nSingleInheritanceOffset = 8;
    g_input_pointer_type.Get()->m_pObjectType = g_input_state_type.Get();
    if (!reject(KEEL_RESULT_INCOMPATIBLE)) return 227;
    g_input_pointer_type.Get()->m_pObjectType = g_input_movement_type.Get();
    g_input_pointer.m_pszName = "unavailable";
    if (!reject(KEEL_RESULT_NOT_FOUND)) return 228;
    g_input_pointer.m_pszName = "m_pMovementServices";
    g_derived_bases[0].m_pClass = &g_base_class;
    if (!reject(KEEL_RESULT_INCOMPATIBLE)) return 229;
    g_derived_bases[0].m_pClass = &g_input_pawn_class;
    SetHandle(*Identity(), 13);
    if (!reject(KEEL_RESULT_NOT_FOUND)) return 230;
    g_input_handle.Get()->m_sTypeName.Purge();
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
    void* pointer = reinterpret_cast<void*>(1);
    if (KeelCs2_ResolveEntityPointer(system, &entity, "CCSPlayerPawn", &pointer) != KEEL_RESULT_OK ||
        pointer != EntityInstance()) return 901;
    if (KeelCs2_ResolveEntityPointer(system, &entity, "CBaseEntity", &pointer) != KEEL_RESULT_INCOMPATIBLE || pointer)
        return 902;
    if (KeelCs2_ResolveEntityPointer(system, &entity, nullptr, &pointer) != KEEL_RESULT_INVALID_ARGUMENT || pointer ||
        KeelCs2_ResolveEntityPointer(system, &entity, "CCSPlayerPawn", nullptr) != KEEL_RESULT_INVALID_ARGUMENT) return 903;
    auto stale_pointer_entity = entity; stale_pointer_entity.source2_handle += 0x8000;
    pointer = reinterpret_cast<void*>(1);
    if (KeelCs2_ResolveEntityPointer(system, &stale_pointer_entity, "CCSPlayerPawn", &pointer) != KEEL_RESULT_NOT_FOUND || pointer)
        return 904;
    const auto saved_alignment = g_derived_class.m_nAlignment; g_derived_class.m_nAlignment = 3;
    const auto invalid_alignment = KeelCs2_ResolveEntityPointer(system, &entity, "CCSPlayerPawn", &pointer);
    g_derived_class.m_nAlignment = saved_alignment;
    if (invalid_alignment != KEEL_RESULT_INCOMPATIBLE || pointer) return 905;
    KeelCs2EntityIdentity captured{};
    if (KeelCs2_CaptureEntity(system, EntityInstance(), &captured) != KEEL_RESULT_OK ||
        captured.index != entity.index || captured.source2_handle != entity.source2_handle) return 906;
    if (KeelCs2_CaptureEntity(system, nullptr, &captured) != KEEL_RESULT_INVALID_ARGUMENT ||
        captured.index || captured.source2_handle ||
        KeelCs2_CaptureEntity(system, EntityInstance(), nullptr) != KEEL_RESULT_INVALID_ARGUMENT ||
        KeelCs2_CaptureEntity(nullptr, EntityInstance(), &captured) != KEEL_RESULT_INVALID_ARGUMENT) return 907;
    if (KeelCs2_CaptureEntity(system, reinterpret_cast<std::byte*>(EntityInstance()) + 1, &captured) != KEEL_RESULT_INVALID_ARGUMENT)
        return 908;
    auto* saved_backlink = EntityInstance()->m_pEntity;
    for (auto* invalid : {static_cast<CEntityIdentity*>(nullptr), reinterpret_cast<CEntityIdentity*>(1),
        reinterpret_cast<CEntityIdentity*>(reinterpret_cast<std::byte*>(Identity()) + 1)})
    {
        EntityInstance()->m_pEntity = invalid;
        const auto status = KeelCs2_CaptureEntity(system, EntityInstance(), &captured);
        EntityInstance()->m_pEntity = saved_backlink;
        if (status != KEEL_RESULT_NOT_FOUND || captured.index || captured.source2_handle) return 909;
    }
    const auto saved_flags = Identity()->m_flags;
    Identity()->m_flags = EF_DELETE_IN_PROGRESS;
    const auto deleted = KeelCs2_CaptureEntity(system, EntityInstance(), &captured);
    Identity()->m_flags = saved_flags;
    if (deleted != KEEL_RESULT_NOT_FOUND || captured.index || captured.source2_handle) return 910;
    alignas(CEntityInstance) std::array<std::byte, sizeof(CEntityInstance)> copy{};
    std::memcpy(copy.data(), EntityInstance(), copy.size());
    if (KeelCs2_CaptureEntity(system, copy.data(), &captured) != KEEL_RESULT_NOT_FOUND) return 911;
    g_derived_class.m_nAlignment = 3;
    const auto capture_alignment = KeelCs2_CaptureEntity(system, EntityInstance(), &captured);
    g_derived_class.m_nAlignment = saved_alignment;
    if (capture_alignment != KEEL_RESULT_INCOMPATIBLE || captured.index || captured.source2_handle) return 912;
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

extern "C" KEELS2_SCHEMA_FIXTURE_EXPORT void KeelTest_HookDataFixture(void** damage, void** component)
{
    HookDataFixture();
    if (damage) *damage = g_hook_damage.bytes.data();
    if (component) *component = g_hook_weapon.data();
}
extern "C" KEELS2_SCHEMA_FIXTURE_EXPORT void KeelTest_HookDataSchemaCallback(void (*callback)())
{
    g_hook_schema_callback = callback;
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
    for (const auto check : {RunHandleChecks, RunPlayerInputChecks, RunEntityWriteChecks,
                            RunPlayerStatChecks, RunRoundChecks, RunPlayerManagementChecks,
                            RunPlayerActionChecks, RunNativeBridgeChecks, RunHookDataChecks})
        if (const int result = check()) {
            std::cerr << "schema/entity native bridge check " << result << " failed\n";
            return result;
        }
    return 0;
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

class UserIdFixture final
{
public:
    virtual CPlayerUserId GetUser(CPlayerSlot slot)
    {
        return CPlayerUserId(slot.Get() == g_player_slot ? g_player_user : -1);
    }
};

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
    UserIdFixture user_id;
    void** user_id_table{};
    std::memcpy(&user_id_table, &user_id, sizeof(user_id_table));
    g_console_vtable[*user] = user_id_table[0];
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

extern "C" KEELS2_SCHEMA_FIXTURE_EXPORT void KeelTest_PlayerInputFixture(unsigned mode)
{
    if (mode == 0) InputFixture(static_cast<std::uint64_t>(IN_FORWARD) | static_cast<std::uint64_t>(IN_USE));
    if (mode == 1) StorePointer(g_entity_storage.data() + g_input_pointer.m_nSingleInheritanceOffset, g_other_movement.data());
    if (mode == 2)
    {
        SetHandle(*Identity(), 13);
        const auto pawn = Identity()->GetRefEHandle();
        std::memcpy(g_controller_storage.data() + kHealthOffset + 16, &pawn, sizeof(pawn));
    }
    if (mode == 3) StorePointer(g_entity_storage.data() + g_input_pointer.m_nSingleInheritanceOffset, nullptr);
    if (mode == 4)
    {
        g_input_handle.Get()->m_sTypeName.Purge();
        Reset();
    }
    if (mode == 5) g_input_read_callback = [] { ++g_player_user; };
}
