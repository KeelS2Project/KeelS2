#include <keels2/player_management.h>
#include <keels2/entity_writes.h>
#include <keels2/entity_tools.h>
#include <keels2/entity_access.h>
#include <keels2/entity_hook_data.h>
#include "entity_hook_data_fixture.h"
#include <keels2/round_control.h>
#include <keels2/player_statistics.h>
#include <keels2/keels2.hpp>

#include <string.h>
#include <limits>

namespace
{
const KeelHostApi* g_api{};
KeelPluginHandle g_owner{};
void (*g_deferred_dispatch)(){};
void* g_hook_record{};
void* g_hook_component{};
void (*g_hook_set_callback)(void (*)()){};
void (*g_hook_change_map)(){};
const KeelEntityHookDataApi* g_hook_data{};
unsigned g_hook_nested{};
bool g_hook_valid{};
void HookDataReentry()
{
    ++g_hook_nested;
    g_hook_set_callback(&HookDataReentry);
    KeelDamageInfo info{}; info.size = sizeof(info);
    const auto expected = g_hook_nested < 8 ? KEEL_RESULT_OK : KEEL_RESULT_BUSY;
    g_hook_valid = g_hook_data->read_damage(g_owner,g_hook_record,&info) == expected && g_hook_valid;
}

class DeferredDispatch
{
public:
    virtual void Run() { if (g_deferred_dispatch) g_deferred_dispatch(); }
};
DeferredDispatch g_deferred_target;

class SchemaEntityPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "Schema Entity Test",
        "KeelS2 Project",
        "0.6",
        "Schema and entity service integration fixture"
    };

    bool Load() override
    {
        keels2::SchemaField<int32> repeated;
        keels2::SchemaField<float32> wrong_type;
        keels2::SchemaField<int32> missing;
        keels2::SchemaField<int32> malformed;
        keels2::Entity unavailable;
        const bool resolved = FindSchemaField(
                "CBaseEntity",
                "m_iHealth",
                health_) &&
            FindSchemaField("CBaseEntity", "m_iHealth", repeated) &&
            health_.Offset() == repeated.Offset() &&
            !FindSchemaField("CBaseEntity", "m_iHealth", wrong_type) &&
            !FindSchemaField("CBaseEntity", "m_missing", missing) &&
            !FindSchemaField("CBaseEntity!", "m_iHealth", malformed) &&
            !FindEntity(7, unavailable);
        if (!resolved || !HookPre(&g_deferred_target, &DeferredDispatch::Run, &SchemaEntityPlugin::BeforeDispatch) || !CreateCommand(
                "keel_schema_entity_check",
                "Checks schema fields and validated entity handles",
                &SchemaEntityPlugin::Check))
        {
            LogError("schema and entity load validation failed");
            return false;
        }
        LogMessage("schema field resolution passed");
        return true;
    }

    void Unload() override
    {
        if (captured_ && captured_api_) { captured_api_->release(g_owner,captured_); captured_ = 0; }
        LogMessage(!health_ && !retained_
            ? "schema and entity views invalidated before unload"
            : "schema or entity view remained active during unload");
    }

    void OnLevelShutdown() override
    {
        int32 health;
        LogMessage(CapturedStale() && !retained_.Valid() && !retained_.Read(health_, health)
            ? "map epoch invalidation passed"
            : "map epoch invalidation failed");
    }

    void OnGameFrame(bool, bool, bool) override {}

private:
    keels2::kh::Action BeforeDispatch() { return keels2::kh::Action::Continue; }
    void Check(const CCommandContext&, const CCommand& command)
    {
        if (command.ArgC() != 2)
        {
            LogError("usage: keel_schema_entity_check [initial|stale|reuse|offthread]");
            return;
        }
        if (strcmp(command[1], "hookdata") == 0)
        {
            LogMessage(HookData() ? "typed hook data passed" : "typed hook data failed");
            return;
        }
        if (strcmp(command[1], "initial") == 0)
        {
            Initial();
            return;
        }
        if (strcmp(command[1], "stale") == 0)
        {
            Stale();
            return;
        }
        if (strcmp(command[1], "reuse") == 0)
        {
            Reuse();
            return;
        }
        if (strcmp(command[1], "offthread") == 0)
        {
            OffThread();
            return;
        }
        LogError("unknown schema and entity check");
    }

    bool HookData()
    {
        if (!g_hook_record || !g_hook_component || !g_hook_set_callback || !g_hook_change_map) return false;
        const void* raw{};
        if (g_api->query_service(g_owner,KEELS2_ENTITY_HOOK_DATA_SERVICE_NAME,2,&raw) != KEEL_RESULT_INCOMPATIBLE ||
            g_api->query_service(g_owner,KEELS2_ENTITY_HOOK_DATA_SERVICE_NAME,1,&raw) != KEEL_RESULT_OK || !raw) return false;
        g_hook_data = static_cast<const KeelEntityHookDataApi*>(raw);
        if (g_hook_data->size != sizeof(*g_hook_data) || g_hook_data->api_version != 1 ||
            !g_hook_data->read_damage || !g_hook_data->write_damage || !g_hook_data->weapon_matches) return false;
        if (g_api->query_service(g_owner,KEELS2_ENTITIES_SERVICE_NAME,1,&raw) != KEEL_RESULT_OK || !raw) return false;
        const auto* entities = static_cast<const KeelEntitiesApi*>(raw);
        KeelEntityHandle pawn{};
        if (entities->find_by_index(g_owner,7,&pawn) != KEEL_RESULT_OK) return false;
        struct Hold { const KeelEntitiesApi* api; KeelEntityHandle handle; ~Hold() { api->release(g_owner,handle); } } hold{entities,pawn};
        KeelDamageInfo info{}; info.size = sizeof(info);
        if (g_hook_data->read_damage(g_owner,g_hook_record,&info) != KEEL_RESULT_OK || info.damage != 42.5f ||
            info.damage_type != 0x80000040 || info.damage_custom != -7 || info.inflictor != 0x6007 ||
            info.attacker != UINT32_MAX || info.ability != 0x7008 || info.force[1] != 2 || info.position[2] != 6) return false;
        KeelDamageEdit edit{sizeof(edit),0,9.5f,32,{7,8,9},{10,11,12}};
        auto expected = hook_data_fixture::MakeDamage();
        std::memcpy(expected.bytes.data()+hook_data_fixture::offsets[0],&edit.damage,4);
        std::memcpy(expected.bytes.data()+hook_data_fixture::offsets[1],&edit.damage_type,4);
        std::memcpy(expected.bytes.data()+hook_data_fixture::offsets[6],edit.force,12);
        std::memcpy(expected.bytes.data()+hook_data_fixture::offsets[7],edit.position,12);
        if (g_hook_data->write_damage(g_owner,g_hook_record,&edit) != KEEL_RESULT_OK ||
            std::memcmp(g_hook_record,expected.bytes.data(),expected.bytes.size()) ||
            g_hook_data->read_damage(g_owner,g_hook_record,&info) != KEEL_RESULT_OK || info.damage != 9.5f) return false;
        auto bad = edit; bad.force[2] = std::numeric_limits<float>::quiet_NaN();
        if (g_hook_data->write_damage(g_owner,g_hook_record,&bad) != KEEL_RESULT_INVALID_ARGUMENT ||
            std::memcmp(g_hook_record,expected.bytes.data(),expected.bytes.size())) return false;
        bad = edit; bad.reserved = 1;
        if (g_hook_data->write_damage(g_owner,g_hook_record,&bad) != KEEL_RESULT_INVALID_ARGUMENT ||
            g_hook_data->read_damage(g_owner+10000,g_hook_record,&info) != KEEL_RESULT_NOT_READY ||
            info.damage || info.inflictor != UINT32_MAX) return false;
        info.size = 1;
        if (g_hook_data->read_damage(g_owner,g_hook_record,&info) != KEEL_RESULT_INVALID_ARGUMENT || info.damage ||
            g_hook_data->read_damage(g_owner,nullptr,&info) != KEEL_RESULT_INVALID_ARGUMENT) return false;
        KeelBool matches = KEEL_FALSE;
        if (g_hook_data->weapon_matches(g_owner,pawn,g_hook_component,&matches) != KEEL_RESULT_OK || !matches ||
            g_hook_data->weapon_matches(g_owner,pawn,reinterpret_cast<void*>(1),&matches) != KEEL_RESULT_OK || matches ||
            g_hook_data->weapon_matches(g_owner,pawn+10000,g_hook_component,&matches) != KEEL_RESULT_NOT_FOUND || matches) return false;
        g_hook_nested = 0; g_hook_valid = true;
        g_hook_set_callback(&HookDataReentry);
        const auto nested_result = g_hook_data->read_damage(g_owner,g_hook_record,&info);
        g_hook_set_callback(nullptr);
        if (nested_result != KEEL_RESULT_OK || !g_hook_valid || g_hook_nested != 8) return false;
        edit.damage = 55;
        g_hook_set_callback(g_hook_change_map);
        const auto changed = g_hook_data->write_damage(g_owner,g_hook_record,&edit);
        g_hook_set_callback(nullptr);
        if (changed != KEEL_RESULT_NOT_FOUND || std::memcmp(g_hook_record,expected.bytes.data(),expected.bytes.size()) ||
            g_hook_data->weapon_matches(g_owner,pawn,g_hook_component,&matches) != KEEL_RESULT_NOT_FOUND || matches) return false;
        return true;
    }

    void Initial()
    {
        keels2::Entity same;
        int32 health;
        if (FindEntity(7, retained_) &&
            FindEntity(CEntityHandle(retained_.Source2Handle()), same) &&
            retained_.Same(same) && retained_.Read(health_, health) && health == 42 &&
            ReadDiagnostics() &&
            Actions() && Access())
        {
            original_handle_ = retained_.Source2Handle();
            LogMessage("entity lookup and typed read passed");
            return;
        }
        LogError("entity lookup or typed read failed");
    }

    struct AccessProbe
    {
        const KeelEntityAccessApi* api;
        KeelEntityAccessSpec spec;
        unsigned calls = 0;
        bool valid = true;
    };
    static KeelResult ProbeAccess(void* raw, void* const* pointers, std::uint32_t count)
    {
        auto& probe = *static_cast<AccessProbe*>(raw);
        ++probe.calls;
        probe.valid = probe.valid && count == 1 && pointers && pointers[0];
        if (probe.valid)
        {
            const auto expected = probe.calls < KEELS2_ENTITY_ACCESS_MAX_DEPTH ? KEEL_RESULT_OK : KEEL_RESULT_BUSY;
            probe.valid = probe.api->visit(g_owner, &probe.spec, 1, &ProbeAccess, &probe) == expected && probe.valid;
        }
        return KEEL_RESULT_OK;
    }
    bool Access()
    {
        const void* raw{};
        if (g_api->query_service(g_owner, KEELS2_ENTITY_ACCESS_SERVICE_NAME, 2, &raw) != KEEL_RESULT_INCOMPATIBLE ||
            g_api->query_service(g_owner, KEELS2_ENTITY_ACCESS_SERVICE_NAME, 1, &raw) != KEEL_RESULT_OK || !raw) return false;
        const auto* access = static_cast<const KeelEntityAccessApi*>(raw);
        if (access->size != sizeof(*access) || access->api_version != 1 || !access->visit) return false;
        if (g_api->query_service(g_owner, KEELS2_ENTITIES_SERVICE_NAME, 1, &raw) != KEEL_RESULT_OK || !raw) return false;
        const auto* entities = static_cast<const KeelEntitiesApi*>(raw);
        const void* capture_raw{};
        if (g_api->query_service(g_owner, KEELS2_ENTITY_CAPTURE_SERVICE_NAME, 2, &capture_raw) != KEEL_RESULT_INCOMPATIBLE ||
            g_api->query_service(g_owner, KEELS2_ENTITY_CAPTURE_SERVICE_NAME, 1, &capture_raw) != KEEL_RESULT_OK || !capture_raw) return false;
        const auto* capture = static_cast<const KeelEntityCaptureApi*>(capture_raw);
        if (capture->size != sizeof(*capture) || capture->api_version != 1 || !capture->capture) return false;
        KeelEntityHandle entity{};
        if (entities->find_by_index(g_owner, 7, &entity) != KEEL_RESULT_OK) return false;
        KeelEntityAccessSpec specs[2]{{sizeof(KeelEntityAccessSpec), 0, entity, "CCSPlayerPawn"},
            {sizeof(KeelEntityAccessSpec), 0, entity, "CBaseEntity"}};
        if (captured_ && captured_api_) { captured_api_->release(g_owner,captured_); captured_ = 0; }
        captured_api_ = entities;
        struct CaptureProbe { const KeelEntityCaptureApi* capture; const KeelEntitiesApi* entities; KeelEntityHandle source; KeelEntityHandle* retained; } capture_probe{capture,entities,entity,&captured_};
        const auto capture_pointer = [](void* data, void* const* pointers, std::uint32_t count) -> KeelResult {
            auto& probe = *static_cast<CaptureProbe*>(data);
            if (!pointers || count != 1 || !pointers[0]) return KEEL_RESULT_ENGINE_FAILURE;
            KeelEntityHandle handle = 99;
            if (probe.capture->capture(g_owner, nullptr, &handle) != KEEL_RESULT_INVALID_ARGUMENT || handle ||
                probe.capture->capture(g_owner, pointers[0], nullptr) != KEEL_RESULT_INVALID_ARGUMENT ||
                probe.capture->capture(g_owner + 10000, pointers[0], &handle) != KEEL_RESULT_NOT_READY || handle)
                return KEEL_RESULT_ENGINE_FAILURE;
            if (probe.capture->capture(g_owner, pointers[0], &handle) != KEEL_RESULT_OK || !handle || handle == probe.source)
                return KEEL_RESULT_ENGINE_FAILURE;
            KeelBool equal = KEEL_FALSE;
            KeelEntityInfo info{}; info.size = sizeof(info);
            const bool valid = probe.entities->equal(g_owner, handle, probe.source, &equal) == KEEL_RESULT_OK && equal &&
                probe.entities->describe(g_owner, handle, &info) == KEEL_RESULT_OK && info.index == 7 && info.epoch;
            if (probe.entities->release(g_owner, handle) != KEEL_RESULT_OK ||
                probe.entities->describe(g_owner, handle, &info) != KEEL_RESULT_NOT_FOUND) return KEEL_RESULT_ENGINE_FAILURE;
            return valid ? probe.capture->capture(g_owner,pointers[0],probe.retained) : KEEL_RESULT_ENGINE_FAILURE;
        };
        if (access->visit(g_owner, specs, 1, capture_pointer, &capture_probe) != KEEL_RESULT_OK) { entities->release(g_owner, entity); return false; }
        unsigned calls = 0;
        const auto count = [](void* data, void* const* pointers, std::uint32_t size) -> KeelResult {
            if (!pointers || !size || !pointers[0]) return KEEL_RESULT_ENGINE_FAILURE;
            ++*static_cast<unsigned*>(data); return KEEL_RESULT_OK;
        };
        bool valid = access->visit(g_owner, specs, 1, count, &calls) == KEEL_RESULT_OK && calls == 1 &&
            access->visit(g_owner, specs, 2, count, &calls) == KEEL_RESULT_INCOMPATIBLE && calls == 1 &&
            access->visit(g_owner, specs, 0, count, &calls) == KEEL_RESULT_INVALID_ARGUMENT &&
            access->visit(g_owner, specs, 33, count, &calls) == KEEL_RESULT_INVALID_ARGUMENT &&
            access->visit(g_owner, nullptr, 1, count, &calls) == KEEL_RESULT_INVALID_ARGUMENT &&
            access->visit(g_owner, specs, 1, nullptr, &calls) == KEEL_RESULT_INVALID_ARGUMENT &&
            access->visit(g_owner + 10000, specs, 1, count, &calls) == KEEL_RESULT_NOT_READY;
        specs[1] = specs[0]; specs[1].entity += 10000;
        valid = valid && access->visit(g_owner, specs, 2, count, &calls) == KEEL_RESULT_NOT_FOUND && calls == 1;
        specs[1] = specs[0]; specs[1].reserved = 1;
        valid = valid && access->visit(g_owner, specs, 2, count, &calls) == KEEL_RESULT_INVALID_ARGUMENT && calls == 1;
        AccessProbe probe{access, specs[0]};
        valid = valid && access->visit(g_owner, specs, 1, &ProbeAccess, &probe) == KEEL_RESULT_OK &&
            probe.valid && probe.calls == KEELS2_ENTITY_ACCESS_MAX_DEPTH;
        struct Closing { const KeelEntitiesApi* api; KeelEntityHandle entity; } closing{entities, entity};
        const auto close = [](void* data, void* const*, std::uint32_t) -> KeelResult {
            auto& value = *static_cast<Closing*>(data);
            return value.api->release(g_owner, value.entity);
        };
        valid = valid && access->visit(g_owner, specs, 1, close, &closing) == KEEL_RESULT_OK &&
            access->visit(g_owner, specs, 1, count, &calls) == KEEL_RESULT_NOT_FOUND && calls == 1;
        if (!valid) entities->release(g_owner, entity);
        LogMessage(valid ? "checked entity access passed" : "checked entity access failed");
        return valid;
    }

    bool CapturedStale()
    {
        KeelEntityInfo info{}; info.size = sizeof(info);
        return captured_ && captured_api_ && captured_api_->describe(g_owner,captured_,&info) != KEEL_RESULT_OK;
    }
    void Stale()
    {
        int32 health;
        LogMessage(CapturedStale() && !retained_.Valid() && !retained_.Read(health_, health) &&
                health == 0 && retained_.LastResult() == KEEL_RESULT_NOT_FOUND &&
                strcmp(retained_.LastError(), "not found") == 0 &&
                !retained_.Same(retained_) && retained_.Kill() == KEEL_RESULT_NOT_FOUND &&
                !retained_.TryKill() && retained_.LastResult() == KEEL_RESULT_NOT_FOUND
            ? "entity destruction invalidation passed"
            : "entity destruction invalidation failed");
    }

    void OffThread()
    {
        const void* raw{};
        if (g_api->query_service(g_owner, KEELS2_PLAYER_MANAGEMENT_SERVICE_NAME, 1, &raw) != KEEL_RESULT_OK || !raw)
        {
            LogError("player management wrong-thread service lookup failed");
            return;
        }
        const void* access_raw{};
        if (g_api->query_service(g_owner, KEELS2_ENTITY_ACCESS_SERVICE_NAME, 1, &access_raw) != KEEL_RESULT_OK || !access_raw)
        { LogError("entity access wrong-thread lookup failed"); return; }
        const auto* access = static_cast<const KeelEntityAccessApi*>(access_raw);
        KeelEntityAccessSpec access_spec{sizeof(access_spec), 0, 1, "CCSPlayerPawn"};
        if (access->visit(g_owner, &access_spec, 1, [](void*, void* const*, std::uint32_t) { return KEEL_RESULT_OK; }, nullptr) != KEEL_RESULT_WRONG_THREAD)
        { LogError("entity access wrong-thread rejection failed"); return; }
        const void* capture_raw{};
        if (g_api->query_service(g_owner, KEELS2_ENTITY_CAPTURE_SERVICE_NAME, 1, &capture_raw) != KEEL_RESULT_OK || !capture_raw)
        { LogError("entity capture wrong-thread lookup failed"); return; }
        const auto* capture = static_cast<const KeelEntityCaptureApi*>(capture_raw);
        KeelEntityHandle captured = 99;
        if (capture->capture(g_owner, &captured, &captured) != KEEL_RESULT_WRONG_THREAD || captured)
        { LogError("entity capture wrong-thread rejection failed"); return; }
        const void* hook_raw{};
        if (g_api->query_service(g_owner,KEELS2_ENTITY_HOOK_DATA_SERVICE_NAME,1,&hook_raw) != KEEL_RESULT_OK || !hook_raw)
        { LogError("hook data wrong-thread lookup failed"); return; }
        const auto* hook = static_cast<const KeelEntityHookDataApi*>(hook_raw);
        KeelDamageInfo damage{}; damage.size = sizeof(damage);
        KeelDamageEdit edit{sizeof(edit),0,1,2,{1,2,3},{4,5,6}};
        KeelBool matched = KEEL_TRUE;
        if (hook->read_damage(g_owner,&damage,&damage) != KEEL_RESULT_WRONG_THREAD || damage.damage ||
            hook->write_damage(g_owner,&damage,&edit) != KEEL_RESULT_WRONG_THREAD ||
            hook->weapon_matches(g_owner,1,&damage,&matched) != KEEL_RESULT_WRONG_THREAD || matched)
        { LogError("hook data wrong-thread rejection failed"); return; }
        const auto* management = static_cast<const KeelPlayerManagementApi*>(raw);
        std::uint32_t capabilities = UINT32_MAX;
        const KeelPlayerManagementAction request{sizeof(request), KEELS2_PLAYER_MANAGEMENT_RESPAWN, 0, 0};
        if (management->capabilities(g_owner, &capabilities) != KEEL_RESULT_WRONG_THREAD || capabilities ||
            management->apply(g_owner, 1, &request) != KEEL_RESULT_WRONG_THREAD)
        {
            LogError("player management wrong-thread rejection failed");
            return;
        }
        if (g_api->query_service(g_owner, KEELS2_ENTITY_WRITES_SERVICE_NAME, 1, &raw) != KEEL_RESULT_OK || !raw)
        { LogError("entity write wrong-thread service lookup failed"); return; }
        const void* tools_raw{};
        if (g_api->query_service(g_owner,KEELS2_ENTITY_TOOLS_SERVICE_NAME,1,&tools_raw) != KEEL_RESULT_OK || !tools_raw)
        { LogError("entity tools wrong-thread lookup failed"); return; }
        const auto* tools = static_cast<const KeelEntityToolsApi*>(tools_raw);
        KeelEntityTeleport teleport{sizeof(teleport),1,{1,2,3},{},{}};
        if (tools->capabilities(g_owner,&capabilities) != KEEL_RESULT_WRONG_THREAD || capabilities ||
            tools->teleport(g_owner,1,&teleport) != KEEL_RESULT_WRONG_THREAD ||
            tools->set_model(g_owner,1,"models/test.vmdl") != KEEL_RESULT_WRONG_THREAD || tools->remove(g_owner,1) != KEEL_RESULT_WRONG_THREAD)
        { LogError("entity tools wrong-thread rejection failed"); return; }
        const auto* writes = static_cast<const KeelEntityWritesApi*>(raw);
        const std::int32_t value = 1;
        capabilities = UINT32_MAX;
        if (writes->capabilities(g_owner, &capabilities) != KEEL_RESULT_WRONG_THREAD || capabilities ||
            writes->write_field(g_owner, 1, 1, &value, sizeof(value)) != KEEL_RESULT_WRONG_THREAD)
        { LogError("entity write wrong-thread rejection failed"); return; }
        if (g_api->query_service(g_owner, KEELS2_ROUND_CONTROL_SERVICE_NAME, 1, &raw) != KEEL_RESULT_OK || !raw)
        { LogError("round control wrong-thread service lookup failed"); return; }
        const auto* round = static_cast<const KeelRoundControlApi*>(raw);
        const KeelRoundTermination termination{sizeof(termination),8,1,0,0};
        capabilities = UINT32_MAX;
        if (round->capabilities(g_owner,&capabilities) != KEEL_RESULT_WRONG_THREAD || capabilities ||
            round->terminate(g_owner,&termination) != KEEL_RESULT_WRONG_THREAD)
        { LogError("round control wrong-thread rejection failed"); return; }
        if (g_api->query_service(g_owner,KEELS2_PLAYER_STATISTICS_SERVICE_NAME,1,&raw) != KEEL_RESULT_OK || !raw)
        { LogError("statistics wrong-thread lookup failed"); return; }
        const auto* stats = static_cast<const KeelPlayerStatisticsApi*>(raw);
        std::uint32_t reads = UINT32_MAX, writes_mask = UINT32_MAX;
        std::int32_t result_value = 99;
        if (stats->capabilities(g_owner,&reads,&writes_mask) != KEEL_RESULT_WRONG_THREAD || reads || writes_mask ||
            stats->read(g_owner,1,KEELS2_PLAYER_STAT_MONEY,&result_value) != KEEL_RESULT_WRONG_THREAD || result_value ||
            stats->write(g_owner,1,KEELS2_PLAYER_STAT_MONEY,1) != KEEL_RESULT_WRONG_THREAD)
        { LogError("statistics wrong-thread rejection failed"); return; }
        keels2::SchemaField<int32> field;
        keels2::Entity entity;
        int32 health;
        LogMessage(!FindSchemaField("CBaseEntity", "m_iHealth", field) &&
                !FindEntity(7, entity) && !retained_.Valid() &&
                !retained_.Read(health_, health) && health == 0 &&
                retained_.LastResult() == KEEL_RESULT_WRONG_THREAD &&
                strcmp(retained_.LastError(), "operation requires the game thread") == 0 &&
                retained_.Kill() == KEEL_RESULT_WRONG_THREAD && !retained_.TryKill() &&
                retained_.LastResult() == KEEL_RESULT_WRONG_THREAD
            ? "wrong-thread access rejected"
            : "wrong-thread access was accepted");
    }

    void Reuse()
    {
        int32 health;
        const bool old_stale = CapturedStale() && !retained_.Valid();
        if (old_stale && FindEntity(7, retained_) &&
            retained_.Source2Handle() != original_handle_ &&
            retained_.Read(health_, health) && health == 84 && Access())
        {
            LogMessage("entity serial reuse validation passed");
            return;
        }
        LogError("entity serial reuse validation failed");
    }

    bool ReadDiagnostics()
    {
        keels2::Entity empty;
        keels2::SchemaField<int32> empty_field;
        int32 health{99};
        if (empty.Read(health_, health) || health != 0 ||
            empty.LastResult() != KEEL_RESULT_NOT_READY || !empty.LastError()[0])
            return false;
        health = 99;
        if (retained_.Read(empty_field, health) || health != 0 ||
            retained_.LastResult() != KEEL_RESULT_NOT_READY || !retained_.LastError()[0])
            return false;
        return retained_.Read(health_, health) && health == 42 &&
            retained_.LastResult() == KEEL_RESULT_OK && !retained_.LastError()[0];
    }

    bool Actions()
    {
        const void* raw{};
        if (g_api->query_service(g_owner, KEELS2_PLAYER_ACTIONS_SERVICE_NAME, 2, &raw) != KEEL_RESULT_INCOMPATIBLE ||
            g_api->query_service(g_owner, KEELS2_PLAYER_ACTIONS_SERVICE_NAME, 1, &raw) != KEEL_RESULT_OK)
            return false;
        const auto* actions = static_cast<const KeelPlayerActionsApi*>(raw);
        if (!actions || actions->size != sizeof(*actions) || actions->api_version != 1 || !actions->apply)
            return false;
        if (g_api->query_service(g_owner, KEELS2_ENTITIES_SERVICE_NAME, 1, &raw) != KEEL_RESULT_OK)
            return false;
        const auto* entities = static_cast<const KeelEntitiesApi*>(raw);
        KeelEntityHandle entity{};
        if (entities->find_by_index(g_owner, 7, &entity) != KEEL_RESULT_OK)
            return false;
        KeelPlayerAction action{sizeof(KeelPlayerAction), KEELS2_PLAYER_ACTION_KILL, {}, 0};
        bool valid = actions->apply(g_owner, entity, &action) == KEEL_RESULT_UNSUPPORTED &&
            actions->apply(g_owner + 10000, entity, &action) == KEEL_RESULT_NOT_READY &&
            actions->apply(g_owner, entity + 10000, &action) == KEEL_RESULT_NOT_FOUND;
        const void* management_raw{};
        valid = valid && g_api->query_service(g_owner, KEELS2_PLAYER_MANAGEMENT_SERVICE_NAME, 2, &management_raw) == KEEL_RESULT_INCOMPATIBLE &&
            g_api->query_service(g_owner, KEELS2_PLAYER_MANAGEMENT_SERVICE_NAME, 1, &management_raw) == KEEL_RESULT_OK;
        const auto* management = static_cast<const KeelPlayerManagementApi*>(management_raw);
        if (!valid || !management || management->size != sizeof(*management) || management->api_version != 1 ||
            !management->capabilities || !management->apply) return false;
        std::uint32_t capabilities = UINT32_MAX;
        KeelPlayerManagementAction request{sizeof(request), KEELS2_PLAYER_MANAGEMENT_RESPAWN, 0, 0};
        valid = valid && management->capabilities(g_owner, &capabilities) == KEEL_RESULT_UNSUPPORTED && !capabilities &&
            management->apply(g_owner, entity, &request) == KEEL_RESULT_UNSUPPORTED &&
            management->apply(g_owner + 10000, entity, &request) == KEEL_RESULT_NOT_READY &&
            management->apply(g_owner, entity + 10000, &request) == KEEL_RESULT_NOT_FOUND;
        request.team = 1;
        valid = valid && management->apply(g_owner, entity, &request) == KEEL_RESULT_INVALID_ARGUMENT;
        request.team = 0; request.reserved = 1;
        valid = valid && management->apply(g_owner, entity, &request) == KEEL_RESULT_INVALID_ARGUMENT;
        request.reserved = 0; request.kind = 7;
        valid = valid && management->apply(g_owner, entity, &request) == KEEL_RESULT_INVALID_ARGUMENT;
        request.kind = KEELS2_PLAYER_MANAGEMENT_CHANGE_TEAM; request.team = 256;
        valid = valid && management->apply(g_owner, entity, &request) == KEEL_RESULT_INVALID_ARGUMENT;
        const void* stat_raw{};
        valid = valid && g_api->query_service(g_owner,KEELS2_PLAYER_STATISTICS_SERVICE_NAME,2,&stat_raw) == KEEL_RESULT_INCOMPATIBLE &&
            g_api->query_service(g_owner,KEELS2_PLAYER_STATISTICS_SERVICE_NAME,1,&stat_raw) == KEEL_RESULT_OK;
        const auto* stats = static_cast<const KeelPlayerStatisticsApi*>(stat_raw);
        if (!valid || !stats || stats->size != sizeof(*stats) || stats->api_version != 1 || !stats->capabilities || !stats->read || !stats->write) return false;
        std::uint32_t readable = UINT32_MAX, writable = UINT32_MAX;
        std::int32_t stat_value = 99;
        valid = valid && stats->capabilities(g_owner,&readable,&writable) == KEEL_RESULT_UNSUPPORTED && !readable && !writable &&
            stats->capabilities(g_owner,nullptr,&writable) == KEEL_RESULT_INVALID_ARGUMENT &&
            stats->read(g_owner,entity,KEELS2_PLAYER_STAT_MONEY,&stat_value) == KEEL_RESULT_UNSUPPORTED && !stat_value &&
            stats->write(g_owner,entity,KEELS2_PLAYER_STAT_MONEY,1) == KEEL_RESULT_UNSUPPORTED &&
            stats->write(g_owner+10000,entity,KEELS2_PLAYER_STAT_MONEY,1) == KEEL_RESULT_NOT_READY &&
            stats->write(g_owner,entity+10000,KEELS2_PLAYER_STAT_MONEY,1) == KEEL_RESULT_NOT_FOUND &&
            stats->read(g_owner,entity,KEELS2_PLAYER_STAT_MONEY,nullptr) == KEEL_RESULT_INVALID_ARGUMENT &&
            stats->write(g_owner,entity,3,1) == KEEL_RESULT_INVALID_ARGUMENT &&
            stats->write(g_owner,entity,KEELS2_PLAYER_STAT_MONEY,-1) == KEEL_RESULT_INVALID_ARGUMENT;
        const void* round_raw{};
        valid = valid && g_api->query_service(g_owner,KEELS2_ROUND_CONTROL_SERVICE_NAME,2,&round_raw) == KEEL_RESULT_INCOMPATIBLE &&
            g_api->query_service(g_owner,KEELS2_ROUND_CONTROL_SERVICE_NAME,1,&round_raw) == KEEL_RESULT_OK;
        const auto* round = static_cast<const KeelRoundControlApi*>(round_raw);
        if (!valid || !round || round->size != sizeof(*round) || round->api_version != 1 || !round->capabilities || !round->terminate) return false;
        KeelRoundTermination termination{sizeof(termination),8,1,0,0};
        capabilities = UINT32_MAX;
        valid = valid && round->capabilities(g_owner,&capabilities) == KEEL_RESULT_UNSUPPORTED && !capabilities &&
            round->capabilities(g_owner,nullptr) == KEEL_RESULT_INVALID_ARGUMENT &&
            round->terminate(g_owner,&termination) == KEEL_RESULT_UNSUPPORTED &&
            round->terminate(g_owner+10000,&termination) == KEEL_RESULT_NOT_READY &&
            round->terminate(g_owner,nullptr) == KEEL_RESULT_INVALID_ARGUMENT;
        termination.reserved = 1; valid = valid && round->terminate(g_owner,&termination) == KEEL_RESULT_INVALID_ARGUMENT;
        termination.reserved = 0; termination.delay = -1; valid = valid && round->terminate(g_owner,&termination) == KEEL_RESULT_INVALID_ARGUMENT;
        termination.delay = 1; --termination.size; valid = valid && round->terminate(g_owner,&termination) == KEEL_RESULT_INVALID_ARGUMENT;
        const void* tools_raw{};
        if (g_api->query_service(g_owner,KEELS2_ENTITY_TOOLS_SERVICE_NAME,2,&tools_raw) != KEEL_RESULT_INCOMPATIBLE ||
            g_api->query_service(g_owner,KEELS2_ENTITY_TOOLS_SERVICE_NAME,1,&tools_raw) != KEEL_RESULT_OK || !tools_raw) return false;
        const auto* tools = static_cast<const KeelEntityToolsApi*>(tools_raw);
        KeelEntityTeleport teleport{sizeof(teleport),1,{1,2,3},{},{}};
        if (tools->size != sizeof(*tools) || tools->api_version != 1 || !tools->capabilities || !tools->teleport || !tools->set_model || !tools->remove ||
            tools->capabilities(g_owner,&capabilities) != KEEL_RESULT_UNSUPPORTED || capabilities ||
            tools->capabilities(g_owner+10000,&capabilities) != KEEL_RESULT_NOT_READY || capabilities ||
            tools->capabilities(g_owner,nullptr) != KEEL_RESULT_INVALID_ARGUMENT ||
            tools->teleport(g_owner,entity,&teleport) != KEEL_RESULT_UNSUPPORTED ||
            tools->set_model(g_owner,entity,"models/test.vmdl") != KEEL_RESULT_UNSUPPORTED ||
            tools->remove(g_owner,entity) != KEEL_RESULT_UNSUPPORTED ||
            tools->remove(g_owner+10000,entity) != KEEL_RESULT_NOT_READY ||
            tools->remove(g_owner,UINT64_MAX) != KEEL_RESULT_NOT_FOUND || tools->remove(g_owner,0) != KEEL_RESULT_INVALID_ARGUMENT ||
            tools->teleport(g_owner,UINT64_MAX,&teleport) != KEEL_RESULT_NOT_FOUND ||
            tools->set_model(g_owner,UINT64_MAX,"models/test.vmdl") != KEEL_RESULT_NOT_FOUND ||
            tools->set_model(g_owner,1,"") != KEEL_RESULT_INVALID_ARGUMENT ||
            tools->set_model(g_owner,1,"bad\nasset") != KEEL_RESULT_INVALID_ARGUMENT) return false;
        teleport.flags = 0;
        if (tools->teleport(g_owner,1,&teleport) != KEEL_RESULT_INVALID_ARGUMENT) return false;
        const void* writes_raw{};
        valid = valid && g_api->query_service(g_owner, KEELS2_ENTITY_WRITES_SERVICE_NAME, 2, &writes_raw) == KEEL_RESULT_INCOMPATIBLE &&
            g_api->query_service(g_owner, KEELS2_ENTITY_WRITES_SERVICE_NAME, 1, &writes_raw) == KEEL_RESULT_OK;
        const auto* writes = static_cast<const KeelEntityWritesApi*>(writes_raw);
        if (!valid || !writes || writes->size != sizeof(*writes) || !writes->capabilities || !writes->write_field) return false;
        if (g_api->query_service(g_owner, KEELS2_SCHEMA_SERVICE_NAME, 1, &raw) != KEEL_RESULT_OK || !raw) return false;
        const auto* schema = static_cast<const KeelSchemaApi*>(raw);
        const KeelSchemaFieldSpec spec{sizeof(spec), KEELS2_SCHEMA_MODULE_SERVER, KEELS2_SCHEMA_INT32, 0, "CBaseEntity", "m_iHealth"};
        KeelSchemaFieldHandle field{};
        if (schema->resolve_field(g_owner, &spec, &field) != KEEL_RESULT_OK) return false;
        const std::int32_t value = 77;
        capabilities = UINT32_MAX;
        valid = valid && writes->capabilities(g_owner, &capabilities) == KEEL_RESULT_UNSUPPORTED && !capabilities &&
            writes->write_field(g_owner, entity, field, &value, sizeof(value)) == KEEL_RESULT_UNSUPPORTED &&
            writes->write_field(g_owner, entity, field, &value, 1) == KEEL_RESULT_INVALID_ARGUMENT &&
            writes->write_field(g_owner + 10000, entity, field, &value, sizeof(value)) == KEEL_RESULT_NOT_READY &&
            writes->write_field(g_owner, entity + 10000, field, &value, sizeof(value)) == KEEL_RESULT_NOT_FOUND;
        const auto released = schema->release_field(g_owner, field);
        valid = valid && released == KEEL_RESULT_OK &&
            writes->write_field(g_owner, entity, field, &value, sizeof(value)) == KEEL_RESULT_NOT_FOUND;
        action.damage = 1;
        valid = valid && actions->apply(g_owner, entity, &action) == KEEL_RESULT_INVALID_ARGUMENT;
        action.kind = KEELS2_PLAYER_ACTION_IMPULSE;
        action.impulse[0] = std::numeric_limits<float>::infinity();
        valid = valid && actions->apply(g_owner, entity, &action) == KEEL_RESULT_INVALID_ARGUMENT;
        action.impulse[0] = 0;
        action.damage = -1;
        valid = valid && actions->apply(g_owner, entity, &action) == KEEL_RESULT_INVALID_ARGUMENT;
        action.damage = 0;
        valid = valid && entities->release(g_owner, entity) == KEEL_RESULT_OK &&
            actions->apply(g_owner, entity, &action) == KEEL_RESULT_NOT_FOUND;
        LogMessage(valid ? "player action service rejection checks passed" : "player action service rejection checks failed");
        return valid;
    }

    keels2::SchemaField<int32> health_;
    keels2::Entity retained_;
    uint32 original_handle_ = 0;
    KeelEntityHandle captured_ = 0;
    const KeelEntitiesApi* captured_api_ = nullptr;
};

}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Query(const KeelHostQuery* query, KeelPluginInfo* info)
{
    return keels2::detail::AuthoringAdapter<SchemaEntityPlugin>::Query(query, info);
}
extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Manifest(const KeelHostQuery* query, KeelPluginManifest* manifest)
{
    return keels2::detail::AuthoringAdapter<SchemaEntityPlugin>::Manifest(query, manifest);
}
extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Load(const KeelHostApi* api, KeelPluginHandle plugin)
{
    g_api = api;
    g_owner = plugin;
    return keels2::detail::AuthoringAdapter<SchemaEntityPlugin>::Load(api, plugin);
}
extern "C" KEELS2_PLUGIN_EXPORT void KeelPlugin_Unload(KeelPluginHandle plugin)
{
    keels2::detail::AuthoringAdapter<SchemaEntityPlugin>::Unload(plugin);
}
extern "C" KEELS2_PLUGIN_EXPORT void* KeelTest_DeferredDispatch(void (*callback)())
{
    g_deferred_dispatch = callback;
    return &g_deferred_target;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_HookDataInputs(void* record, void* component,
    void (*set_callback)(void (*)()), void (*change_map)())
{
    g_hook_record = record; g_hook_component = component; g_hook_set_callback = set_callback; g_hook_change_map = change_map;
}
