#pragma once
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

void Check(bool value, const char* text)
{
    if (!value)
    {
        std::cerr << text << '\n';
        std::exit(1);
    }
}

struct Record
{
    KeelEntityInfo info;
    bool pending{}, dead{};
};
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

void Invoke(std::function<void()>& event)
{
    if (event)
    {
        auto call = std::move(event);
        event = {};
        call();
    }
}

KeelResult Thread(KeelPluginHandle)
{
    return wrong_thread ? KEEL_RESULT_WRONG_THREAD : KEEL_RESULT_OK;
}

KeelResult Ready(KeelPluginHandle)
{
    return Thread(77);
}

KeelResult Release(KeelPluginHandle, KeelEntityHandle handle) {
    const auto found = records.find(handle);

    if (found == records.end())
        return KEEL_RESULT_NOT_FOUND;

    if (wrong_thread && found->second->pending)
        return KEEL_RESULT_WRONG_THREAD;

    if (release_result == KEEL_RESULT_BUSY)
        return release_result;

    if (found->second->pending && !found->second->dead)
        ++cancels;

    ++releases;
    records.erase(found);
    Invoke(on_release);
    return release_result;
}

KeelResult Describe(KeelPluginHandle, KeelEntityHandle handle, KeelEntityInfo* info) {
    if (wrong_thread)
        return KEEL_RESULT_WRONG_THREAD;

    Invoke(on_describe);
    const auto found = records.find(handle);

    if (found == records.end() || found->second->pending || found->second->dead || found->second->info.epoch != epoch)
        return KEEL_RESULT_NOT_FOUND;

    *info = found->second->info;
    return KEEL_RESULT_OK;
}

KeelResult Pending(KeelPluginHandle, KeelEntityHandle handle, KeelEntityInfo* info) {
    if (wrong_thread)
        return KEEL_RESULT_WRONG_THREAD;

    Invoke(on_describe);
    const auto found = records.find(handle);

    if (found == records.end() || !found->second->pending || found->second->dead || found->second->info.epoch != epoch)
        return KEEL_RESULT_NOT_FOUND;

    *info = found->second->info;

    if (bad_info)
        info->reserved = 1;

    return KEEL_RESULT_OK;
}

KeelResult Find(KeelPluginHandle, int index, KeelEntityHandle* out) {
    Invoke(on_find);

    if (index != 7)
        return KEEL_RESULT_NOT_FOUND;

    *out = ++next_handle;
    records[*out] = std::make_shared<Record>(Record{{sizeof(KeelEntityInfo), 7, 0x12007, 0, epoch}, false, false});
    return KEEL_RESULT_OK;
}

KeelResult FindSource(KeelPluginHandle owner, uint32 source, KeelEntityHandle* out) {
    return Find(owner,source == 0x12007 ? 7 : -1,out);
}

KeelResult Equal(KeelPluginHandle, KeelEntityHandle left, KeelEntityHandle right, KeelBool* equal) {
    *equal = KEEL_FALSE;

    if (!records.contains(left) || !records.contains(right))
        return KEEL_RESULT_NOT_FOUND;

    *equal = records.at(left)->info.source2_handle == records.at(right)->info.source2_handle ? KEEL_TRUE : KEEL_FALSE;
    return KEEL_RESULT_OK;
}

KeelResult Read(KeelPluginHandle, KeelEntityHandle, KeelSchemaFieldHandle, void*, uint32)
{
    return KEEL_RESULT_UNSUPPORTED;
}

KeelResult Create(KeelPluginHandle, const char* name, KeelEntityHandle* out) {
    if (wrong_thread)
        return KEEL_RESULT_WRONG_THREAD;

    Invoke(on_create);
    observed_name = name;
    *out = ++next_handle;
    ++creates;
    records[*out] = std::make_shared<Record>(Record{{sizeof(KeelEntityInfo), 16, 0x23010, 0, epoch}, true, false});
    return KEEL_RESULT_OK;
}

KeelResult Set(KeelPluginHandle, KeelEntityHandle, const KeelEntityKeyValue* value) {
    ++keys;
    Invoke(on_set);
    observed_name = value->name;
    observed_key = *value;

    if (value->type == KEELS2_ENTITY_KEY_STRING)
        observed_text = value->string_value;

    return KEEL_RESULT_OK;
}

KeelResult Teleport(KeelPluginHandle, KeelEntityHandle, const KeelEntityTeleport* value) {
    ++teleports;
    Invoke(on_teleport);
    observed_teleport = *value;
    return KEEL_RESULT_OK;
}

KeelResult Spawn(KeelPluginHandle, KeelEntityHandle handle, KeelBool* invoked) {
    *invoked = KEEL_FALSE;

    if (noninvoked)
        return KEEL_RESULT_NOT_READY;

    const auto record = records.at(handle);
    *invoked = KEEL_TRUE;
    ++spawns;
    Invoke(on_spawn);
    record->pending = false;
    record->dead = failed_spawn;
    return failed_spawn ? KEEL_RESULT_ENGINE_FAILURE : KEEL_RESULT_OK;
}

KeelResult Observe(KeelPluginHandle, uint32, KeelEntityHandle*)
{
    return KEEL_RESULT_UNSUPPORTED;
}

KeelResult Visit(KeelPluginHandle, KeelEntityHandle, const char*, KeelEntityAccessCallback, void*)
{
    return KEEL_RESULT_UNSUPPORTED;
}

KeelResult Action(KeelPluginHandle, KeelEntityHandle, const KeelPlayerAction*)
{
    Invoke(on_action);
    return KEEL_RESULT_OK;
}

KeelEntitiesApi entities{
    sizeof(entities), KEELS2_ENTITIES_API_VERSION, &Find, &FindSource, &Release, &Describe, &Equal, &Read};

KeelEntityConstructionApi construction_api_fixture{sizeof(construction_api_fixture),
                                                   KEELS2_ENTITY_CONSTRUCTION_API_VERSION,
                                                   &Ready,
                                                   &Create,
                                                   &Pending,
                                                   &Set,
                                                   &Teleport,
                                                   &Spawn,
                                                   &Observe,
                                                   &Visit};

const KeelEntityInputApi* input_api_fixture{};
std::function<void()> on_input_query;
KeelEntityToolsApi tools{};
KeelNativeRuntimeApi runtime{};
KeelPlayerActionsApi actions{sizeof(actions), KEELS2_PLAYER_ACTIONS_API_VERSION, &Action};
KeelResult Query(KeelPluginHandle, const char* name, uint32, const void** out) {
    *out = nullptr;
    const std::string key(name);

    if (key == KEELS2_ENTITIES_SERVICE_NAME)
        *out = &entities;

    if (key == KEELS2_ENTITY_CONSTRUCTION_SERVICE_NAME && !missing)
        *out = &construction_api_fixture;

    if (key == KEELS2_ENTITY_INPUT_SERVICE_NAME)
    {
        Invoke(on_input_query);

        if (!missing)
            *out = input_api_fixture;
    }

    if (key == KEELS2_ENTITY_TOOLS_SERVICE_NAME)
        *out = &tools;

    if (key == KEELS2_NATIVE_RUNTIME_SERVICE_NAME)
        *out = &runtime;

    if (key == KEELS2_PLAYER_ACTIONS_SERVICE_NAME)
        *out = &actions;

    return *out ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
}

class Probe final : public Plugin {

public:
    static constexpr PluginInfo Info{"Entity construction authoring","KeelS2 tests","1.0.0","Owned native construction"};
    using Plugin::CreateEntity;
    using Plugin::EntityConstructionAvailable;
    using Plugin::EntityInputCapabilities;
    using Plugin::FindEntity;
    using Plugin::LastResult;
    using Plugin::LastError;
    static Probe* current;

    bool Load() override
    {
        current = this;
        return true;
    }
};
Probe* Probe::current{};
using Adapter = keels2::detail::AuthoringAdapter<Probe>;
}
