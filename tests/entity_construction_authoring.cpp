#include <keels2/authoring.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>

namespace {
using namespace keels2::authoring;
void Check(bool value, const char* text) { if (!value) { std::cerr << text << '\n'; std::exit(1); } }
struct Record { KeelEntityInfo info; bool pending{}, dead{}; };
std::map<KeelEntityHandle,std::shared_ptr<Record>> records;
KeelEntityHandle next_handle = 100;
unsigned creates{}, cancels{}, spawns{}, keys{}, teleports{}, releases{};
std::uint64_t epoch = 1;
bool missing{}, wrong_thread{}, bad_info{}, noninvoked{}, failed_spawn{};
KeelResult release_result = KEEL_RESULT_OK;
std::function<void()> on_create, on_describe, on_release, on_set, on_spawn, on_teleport, on_action, on_find;
std::string observed_name, observed_text;
KeelEntityKeyValue observed_key{};
KeelEntityTeleport observed_teleport{};
void Invoke(std::function<void()>& event) { if (event) { auto call = std::move(event); event = {}; call(); } }
KeelResult Thread(KeelPluginHandle) { return wrong_thread ? KEEL_RESULT_WRONG_THREAD : KEEL_RESULT_OK; }
KeelResult Ready(KeelPluginHandle) { return Thread(77); }
KeelResult Release(KeelPluginHandle, KeelEntityHandle handle) {
    const auto found = records.find(handle); if (found == records.end()) return KEEL_RESULT_NOT_FOUND;
    if (wrong_thread && found->second->pending) return KEEL_RESULT_WRONG_THREAD;
    if (release_result == KEEL_RESULT_BUSY) return release_result;
    if (found->second->pending && !found->second->dead) ++cancels;
    ++releases; records.erase(found); Invoke(on_release); return release_result;
}
KeelResult Describe(KeelPluginHandle, KeelEntityHandle handle, KeelEntityInfo* info) {
    if (wrong_thread) return KEEL_RESULT_WRONG_THREAD;
    Invoke(on_describe);
    const auto found = records.find(handle);
    if (found == records.end() || found->second->pending || found->second->dead || found->second->info.epoch != epoch) return KEEL_RESULT_NOT_FOUND;
    *info = found->second->info; return KEEL_RESULT_OK;
}
KeelResult Pending(KeelPluginHandle, KeelEntityHandle handle, KeelEntityInfo* info) {
    if (wrong_thread) return KEEL_RESULT_WRONG_THREAD;
    Invoke(on_describe);
    const auto found = records.find(handle);
    if (found == records.end() || !found->second->pending || found->second->dead || found->second->info.epoch != epoch) return KEEL_RESULT_NOT_FOUND;
    *info = found->second->info; if (bad_info) info->reserved = 1; return KEEL_RESULT_OK;
}
KeelResult Find(KeelPluginHandle, int index, KeelEntityHandle* out) {
    Invoke(on_find); if (index != 7) return KEEL_RESULT_NOT_FOUND;
    *out = ++next_handle;
    records[*out] = std::make_shared<Record>(Record{{sizeof(KeelEntityInfo),7,0x12007,0,epoch},false,false});
    return KEEL_RESULT_OK;
}
KeelResult FindSource(KeelPluginHandle owner, uint32 source, KeelEntityHandle* out) {
    return Find(owner,source == 0x12007 ? 7 : -1,out);
}
KeelResult Equal(KeelPluginHandle, KeelEntityHandle left, KeelEntityHandle right, KeelBool* equal) {
    *equal = KEEL_FALSE;
    if (!records.contains(left) || !records.contains(right)) return KEEL_RESULT_NOT_FOUND;
    *equal = records.at(left)->info.source2_handle == records.at(right)->info.source2_handle ? KEEL_TRUE : KEEL_FALSE;
    return KEEL_RESULT_OK;
}
KeelResult Read(KeelPluginHandle, KeelEntityHandle, KeelSchemaFieldHandle, void*, uint32) { return KEEL_RESULT_UNSUPPORTED; }
KeelResult Create(KeelPluginHandle, const char* name, KeelEntityHandle* out) {
    if (wrong_thread) return KEEL_RESULT_WRONG_THREAD;
    Invoke(on_create); observed_name = name;
    *out = ++next_handle; ++creates;
    records[*out] = std::make_shared<Record>(Record{{sizeof(KeelEntityInfo),16,0x23010,0,epoch},true,false});
    return KEEL_RESULT_OK;
}
KeelResult Set(KeelPluginHandle, KeelEntityHandle, const KeelEntityKeyValue* value) {
    ++keys; Invoke(on_set); observed_name = value->name; observed_key = *value;
    if (value->type == KEELS2_ENTITY_KEY_STRING) observed_text = value->string_value;
    return KEEL_RESULT_OK;
}
KeelResult Teleport(KeelPluginHandle, KeelEntityHandle, const KeelEntityTeleport* value) {
    ++teleports; Invoke(on_teleport); observed_teleport = *value; return KEEL_RESULT_OK;
}
KeelResult Spawn(KeelPluginHandle, KeelEntityHandle handle, KeelBool* invoked) {
    *invoked = KEEL_FALSE;
    if (noninvoked) return KEEL_RESULT_NOT_READY;
    const auto record = records.at(handle); *invoked = KEEL_TRUE; ++spawns; Invoke(on_spawn);
    record->pending = false; record->dead = failed_spawn;
    return failed_spawn ? KEEL_RESULT_ENGINE_FAILURE : KEEL_RESULT_OK;
}
KeelResult Observe(KeelPluginHandle, uint32, KeelEntityHandle*) { return KEEL_RESULT_UNSUPPORTED; }
KeelResult Visit(KeelPluginHandle, KeelEntityHandle, const char*, KeelEntityAccessCallback, void*) { return KEEL_RESULT_UNSUPPORTED; }
KeelResult Action(KeelPluginHandle, KeelEntityHandle, const KeelPlayerAction*) { Invoke(on_action); return KEEL_RESULT_OK; }
KeelEntitiesApi entities{sizeof(entities),KEELS2_ENTITIES_API_VERSION,&Find,&FindSource,&Release,&Describe,&Equal,&Read};
KeelEntityConstructionApi construction_api_fixture{sizeof(construction_api_fixture),KEELS2_ENTITY_CONSTRUCTION_API_VERSION,&Ready,&Create,&Pending,&Set,&Teleport,&Spawn,&Observe,&Visit};
KeelEntityToolsApi tools{};
KeelNativeRuntimeApi runtime{};
KeelPlayerActionsApi actions{sizeof(actions),KEELS2_PLAYER_ACTIONS_API_VERSION,&Action};
KeelResult Query(KeelPluginHandle, const char* name, uint32, const void** out) {
    *out = nullptr; const std::string key(name);
    if (key == KEELS2_ENTITIES_SERVICE_NAME) *out = &entities;
    if (key == KEELS2_ENTITY_CONSTRUCTION_SERVICE_NAME && !missing) *out = &construction_api_fixture;
    if (key == KEELS2_ENTITY_TOOLS_SERVICE_NAME) *out = &tools;
    if (key == KEELS2_NATIVE_RUNTIME_SERVICE_NAME) *out = &runtime;
    if (key == KEELS2_PLAYER_ACTIONS_SERVICE_NAME) *out = &actions;
    return *out ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
}
class Probe final : public Plugin {
public:
    static constexpr PluginInfo Info{"Entity construction authoring","KeelS2 tests","1.0.0","Owned native construction"};
    using Plugin::CreateEntity;
    using Plugin::EntityConstructionAvailable;
    using Plugin::FindEntity;
    using Plugin::LastResult;
    using Plugin::LastError;
    static Probe* current;
    bool Load() override { current = this; return true; }
};
Probe* Probe::current{};
using Adapter = keels2::detail::AuthoringAdapter<Probe>;
}
int main() {
    runtime.size = sizeof(runtime); runtime.api_version = KEELS2_NATIVE_RUNTIME_API_VERSION; runtime.check_game_thread = &Thread;
    tools.size = sizeof(tools); tools.api_version = KEELS2_ENTITY_TOOLS_API_VERSION; tools.teleport = &Teleport;
    KeelHostApi host{}; host.size = sizeof(host); host.abi_version = KEELS2_PLUGIN_ABI_VERSION; host.query_service = &Query;
    host.log = [](KeelPluginHandle, KeelLogLevel, const char* text) { std::cerr << text << '\n'; };
    host.register_command = [](KeelPluginHandle, const KeelCommandSpec*, KeelCommandHandle*) -> KeelResult { return KEEL_RESULT_UNSUPPORTED; };
    host.unregister_command = [](KeelPluginHandle, KeelCommandHandle) -> KeelResult { return KEEL_RESULT_UNSUPPORTED; };
    Check(Adapter::Load(&host,77),"load native plugin"); auto& plugin = *Probe::current;
    Entity entity;
    missing = true;
    Check(!plugin.EntityConstructionAvailable() && plugin.LastResult() == KEEL_RESULT_NOT_FOUND && !plugin.CreateEntity("prop_dynamic",entity),"optional service absence");
    Check(plugin.FindEntity(7,entity) && entity.Valid() && !entity.Pending(),"legacy lookup remains usable"); entity.Reset();
    missing = false; Check(plugin.EntityConstructionAvailable(),"construction availability");
    Check(!plugin.CreateEntity("bad class",entity) && !creates,"invalid classname before factory");
    construction_api_fixture.size--;
    Check(!plugin.CreateEntity("prop_dynamic",entity) && plugin.LastResult() == KEEL_RESULT_INCOMPATIBLE && !creates,"incompatible table refused"); ++construction_api_fixture.size;
    Check(plugin.CreateEntity("prop_dynamic",entity) && entity.Valid() && entity.Pending() && entity.Same(entity) && entity.Index() == 16 && entity.Source2Handle() == 0x23010,"owned pending identity");
    Check(entity.SetKey("model","models/test.vmdl") && observed_key.type == KEELS2_ENTITY_KEY_STRING && observed_text == "models/test.vmdl","string key");
    Check(entity.SetKey("solid",true) && observed_key.type == KEELS2_ENTITY_KEY_BOOL && observed_key.int_value == 1,"bool key");
    Check(entity.SetKey("flags",std::int32_t{-7}) && observed_key.type == KEELS2_ENTITY_KEY_INT32 && observed_key.int_value == -7,"integer key");
    Check(entity.SetKey("scale",1.5f) && observed_key.type == KEELS2_ENTITY_KEY_FLOAT && observed_key.float_value == 1.5f,"float key");
    Check(entity.SetKey("origin",Vector(1,2,3)) && observed_key.type == KEELS2_ENTITY_KEY_VECTOR && observed_key.vector_value[2] == 3,"vector key");
    Check(entity.SetKey("angles",QAngle(4,5,6)) && observed_key.type == KEELS2_ENTITY_KEY_ANGLES && observed_key.vector_value[1] == 5,"angles key");
    Check(entity.SetKey("color",Color(10,20,30,255)) && observed_key.type == KEELS2_ENTITY_KEY_COLOR && observed_key.color_value[3] == 255,"color key");
    const auto before_keys = keys;
    Check(!entity.SetKey("",1) && !entity.SetKey("key",static_cast<const char*>(nullptr)) && !entity.SetKey("key",std::string(4096,'x').c_str()) &&
        !entity.SetKey("key",std::numeric_limits<float>::infinity()) && !entity.SetKey("key",Vector(1,2,std::numeric_limits<float>::quiet_NaN())) && keys == before_keys,"bounded finite keys");
    Vector position(10,20,30); QAngle angles(4,5,6);
    Check(!entity.Teleport() && entity.Teleport(&position,&angles) && observed_teleport.flags == 3 && observed_teleport.position[2] == 30 && observed_teleport.angles[1] == 5,"pending selected teleport");
    wrong_thread = true; const auto old_cancels = cancels;
    Check(entity.Reset() == KEEL_RESULT_WRONG_THREAD && !plugin.CreateEntity("prop_dynamic",entity),"wrong-thread ownership retained"); wrong_thread = false;
    Check(entity.Valid() && entity.Pending() && cancels == old_cancels,"wrong-thread refusal does not lose handle");
    release_result = KEEL_RESULT_BUSY;
    Check(entity.Reset() == KEEL_RESULT_BUSY && !plugin.FindEntity(7,entity),"busy release preserves target"); release_result = KEEL_RESULT_OK;
    Check(entity.Pending(),"failed replacement preserves pending target");
    bool invoked = true; noninvoked = true;
    Check(!entity.DispatchSpawn(invoked) && !invoked && entity.Pending(),"noninvoked spawn can retry"); noninvoked = false;
    Check(entity.DispatchSpawn(invoked) && invoked && entity.Valid() && !entity.Pending() && !entity.SetKey("flags",1),"same handle becomes live");
    Check(entity.Teleport(&position),"live teleport uses ordinary tools");
    Check(!entity.DispatchSpawn(invoked) && !invoked,"spawn cannot repeat");
    Entity moved(std::move(entity)); Check(!entity.Valid() && moved.Valid(),"move transfers ownership");
    moved.Reset(); Check(cancels == old_cancels,"live close only releases handle");
    Check(plugin.CreateEntity("prop_dynamic",entity),"failure setup"); failed_spawn = true;
    Check(!entity.DispatchSpawn(invoked) && invoked && !entity.Valid() && entity.LastResult() == KEEL_RESULT_ENGINE_FAILURE,"invoked failure preserves output"); failed_spawn = false; entity.Reset();
    bad_info = true; const auto before_bad = cancels;
    Check(!plugin.CreateEntity("prop_dynamic",entity) && cancels == before_bad+1 && !entity.Valid(),"invalid metadata cancels candidate"); bad_info = false;
    Check(plugin.CreateEntity("prop_dynamic",entity),"stale setup"); ++epoch;
    Check(!entity.Valid() && !entity.SetKey("flags",1) && !entity.DispatchSpawn(invoked) && !invoked,"epoch invalidates operations"); entity.Reset();
    const auto before_factory_close = cancels;
    on_create = [&] { entity.Reset(); };
    Check(!plugin.CreateEntity("prop_dynamic",entity) && cancels == before_factory_close+1 && !entity.Valid(),"reset during factory cancels returned candidate");
    auto deleted = std::make_unique<Entity>();
    on_create = [&] { deleted.reset(); };
    Check(!plugin.CreateEntity("prop_dynamic",*deleted) && !deleted,"destroy output during factory");
    deleted = std::make_unique<Entity>(); on_describe = [&] { deleted.reset(); };
    Check(!plugin.CreateEntity("prop_dynamic",*deleted) && !deleted,"destroy output during candidate metadata");
    Check(plugin.CreateEntity("prop_dynamic",entity),"reset reentry setup");
    on_release = [&] { Check(plugin.FindEntity(7,entity),"reentrant lookup while replacing pending owner"); };
    Check(!plugin.CreateEntity("prop_dynamic",entity) && entity.Valid() && !entity.Pending() && entity.Index() == 7,"nested replacement survives outer call without mutex deadlock"); entity.Reset();
    deleted = std::make_unique<Entity>(); Check(plugin.CreateEntity("prop_dynamic",*deleted),"delete on cancellation setup");
    on_release = [&] { deleted.reset(); };
    Check(deleted->Reset() == KEEL_RESULT_OK && !deleted,"cancel callback may destroy calling wrapper");
    std::string class_name = "prop_dynamic";
    on_create = [&] { class_name.assign(1000,'x'); };
    Check(plugin.CreateEntity(class_name.c_str(),entity) && observed_name == "prop_dynamic","classname copied across factory callbacks"); entity.Reset();
    deleted = std::make_unique<Entity>(); Check(plugin.CreateEntity("prop_dynamic",*deleted),"set deletion setup");
    std::string key_name = "model", model = "models/copied.vmdl";
    on_set = [&] { deleted.reset(); key_name.assign(1000,'x'); model.assign(10000,'y'); };
    Check(deleted->SetKey(key_name.c_str(),model.c_str()) && !deleted && observed_name == "model" && observed_text == "models/copied.vmdl","set retains copied inputs and status after wrapper destruction");
    deleted = std::make_unique<Entity>(); Check(plugin.CreateEntity("prop_dynamic",*deleted),"teleport deletion setup");
    on_teleport = [&] { deleted.reset(); position.Init(9,9,9); };
    Check(deleted->Teleport(&position) && !deleted && observed_teleport.position[0] == 10,"teleport copies values and survives destruction");
    deleted = std::make_unique<Entity>(); Check(plugin.CreateEntity("prop_dynamic",*deleted),"spawn deletion setup");
    on_spawn = [&] { deleted.reset(); };
    Check(deleted->DispatchSpawn(invoked) && invoked && !deleted,"spawn output survives wrapper destruction");
    deleted = std::make_unique<Entity>(); Check(plugin.FindEntity(7,*deleted),"action deletion setup");
    on_action = [&] { deleted.reset(); };
    Check(deleted->TryKill() && !deleted,"legacy action status survives wrapper destruction");
    on_find = [&] { entity.Reset(); };
    Check(!plugin.FindEntity(7,entity) && !entity.Valid(),"ordinary factory result also respects reset");
    Check(plugin.CreateEntity("prop_dynamic",entity),"move assignment target setup");
    Entity other; Check(plugin.FindEntity(7,other),"move assignment source setup");
    const auto before_move = cancels; entity = std::move(other);
    Check(entity.Valid() && !other.Valid() && entity.Index() == 7 && cancels == before_move+1,"move assignment cancels previous pending owner"); entity.Reset();
    Check(records.empty(),"all native handles released exactly once");
    Adapter::Unload(77);
    std::cout << "Native entity construction authoring passed\n";
}
