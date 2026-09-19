#include "host.h"
#include "game_adapter_loader.h"
#include "schema_entity_service.h"
#include "convar_service.h"
#include "factory_service.h"
#include "keelhook_service.h"
#include "lifecycle_service.h"
#include "plugin_service.h"
#include "player_service.h"
#include "published_service_registry.h"
#include "source2_callbacks_service.h"
#include "source2_runtime_service.h"
#include "fixtures/adapter/synthetic_adapter.h"
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>

namespace keels2::host {
namespace {
void Check(bool value, int line)
{
    if (!value)
        throw std::runtime_error("host construction check " + std::to_string(line));
}
#define CHECK(value) Check((value),__LINE__)
class Adapter final : public test::SyntheticAdapter
{
public:
    Adapter() : SyntheticAdapter({}) {}

    const std::thread::id thread{std::this_thread::get_id()};

    struct Record
    {
        GameEntityIdentity identity;
        bool live{}, closed{}, busy{};
    };
    std::map<std::uint64_t,std::shared_ptr<Record>> records;
    std::function<void()> on_create, on_spawn, on_cancel, on_set, on_teleport;
    std::uint64_t next{1}, epoch{1};
    unsigned creates{}, cancels{}, sets{}, spawns{}, teleports{};
    bool dispatch{true};
    KeelResult spawn_result{KEEL_RESULT_OK};

    bool IsGameThread() const noexcept override
    {
        return std::this_thread::get_id() == thread;
    }

    KeelResult ValidateEntity(const GameEntityIdentity& identity, std::string&) override {
        const auto it = records.find(identity.source2_handle);
        return identity.epoch == epoch && it != records.end() && it->second->live ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }

    KeelResult FindEntityBySource2Handle(std::uint32_t source, GameEntityIdentity& out, std::string& error) override {
        out = {};
        const auto it = records.find(source);

        if (it == records.end() || ValidateEntity(it->second->identity, error) != KEEL_RESULT_OK)
            return KEEL_RESULT_NOT_FOUND;

        out = it->second->identity;
        return KEEL_RESULT_OK;
    }

    static Adapter& Get(GameAdapter* adapter)
    {
        return *static_cast<Adapter*>(adapter);
    }

    static void Call(const std::function<void()>& function)
    {
        const auto copy = function;

        if (copy)
            copy();
    }

    static GameAdapterEntityConstructionApi Api() {
        return {sizeof(GameAdapterEntityConstructionApi),
                1,
                [](GameAdapter*) noexcept -> KeelResult
                {
                    return KEEL_RESULT_OK;
                },
                [](GameAdapter* adapter, const char*, std::uint64_t* token, GameEntityIdentity* identity) noexcept
                -> KeelResult
                {
                    auto& a = Get(adapter);
                    ++a.creates;
                    *token = a.next++;
                    *identity = {static_cast<int>(*token), static_cast<std::uint32_t>(*token), a.epoch};
                    a.records.emplace(*token, std::make_shared<Record>(Record{*identity}));
                    Call(a.on_create);
                    return KEEL_RESULT_OK;
                },
                [](GameAdapter* adapter, std::uint64_t token, GameEntityIdentity* identity) noexcept -> KeelResult
                {
                    *identity = {};
                    auto& a = Get(adapter);
                    const auto it = a.records.find(token);

                    if (it == a.records.end() || it->second->closed || it->second->live ||
                        it->second->identity.epoch != a.epoch)
                        return KEEL_RESULT_NOT_FOUND;

                    *identity = it->second->identity;
                    return KEEL_RESULT_OK;
                },
                [](GameAdapter* adapter, std::uint64_t, const KeelEntityKeyValue* value) noexcept -> KeelResult
                {
                    auto& a = Get(adapter);
                    ++a.sets;
                    const auto copied_name = std::string(value->name);
                    const auto copied_text = std::string(value->string_value);
                    Call(a.on_set);
                    CHECK(copied_name == value->name && copied_text == value->string_value);
                    return KEEL_RESULT_OK;
                },
                [](GameAdapter* adapter, std::uint64_t, const KeelEntityTeleport* request) noexcept -> KeelResult
                {
                    auto& a = Get(adapter);
                    ++a.teleports;
                    const auto x = request->position[0];
                    Call(a.on_teleport);
                    CHECK(x == request->position[0]);
                    return KEEL_RESULT_OK;
                },
                [](GameAdapter* adapter, std::uint64_t token, KeelBool* invoked) noexcept -> KeelResult
                {
                    auto& a = Get(adapter);
                    *invoked = a.dispatch ? KEEL_TRUE : KEEL_FALSE;

                    if (!a.dispatch)
                        return a.spawn_result;

                    auto record = a.records.at(token);
                    record->busy = true;
                    ++a.spawns;
                    Call(a.on_spawn);
                    record->busy = false;
                    record->live = a.spawn_result == KEEL_RESULT_OK;

                    if (!record->live)
                        a.records.erase(token);

                    return a.spawn_result;
                },
                [](GameAdapter* adapter, std::uint64_t token) noexcept -> KeelResult
                {
                    auto& a = Get(adapter);
                    const auto it = a.records.find(token);

                    if (it == a.records.end())
                        return KEEL_RESULT_NOT_FOUND;

                    auto record = it->second;
                    record->closed = true;
                    ++a.cancels;

                    if (!record->busy && !record->live)
                        a.records.erase(it);

                    Call(a.on_cancel);
                    return KEEL_RESULT_OK;
                },
                [](GameAdapter* adapter,
                   std::uint64_t token,
                   const char* name,
                   KeelEntityAccessCallback callback,
                   void* data) noexcept -> KeelResult
                {
                    auto& a = Get(adapter);
                    const auto it = a.records.find(token);

                    if (it == a.records.end() || it->second->closed)
                        return KEEL_RESULT_NOT_FOUND;

                    if (std::string(name) != "CBaseEntity")
                        return KEEL_RESULT_INCOMPATIBLE;

                    auto hold = it->second;
                    void* pointer = hold.get();
                    return callback(data, &pointer, 1);
                }};
    }
};

KeelResult Visit(void* data, void* const* pointers, std::uint32_t count)
{
    CHECK(count == 1 && pointers[0]);
    (*static_cast<std::function<void()>*>(data))();
    return KEEL_RESULT_OK;
}
}

struct SchemaEntityServiceTest
{
    static void Run()
    {
        Adapter adapter;
        auto& host = Host::Instance();
        host.accepting_resources_ = true;
        host.adapter_ = &adapter;
        host.adapter_module_ = std::make_unique<GameAdapterModule>();
        host.adapter_module_->entity_construction_ = Adapter::Api();

        for (unsigned i : {1u,2u}) {
            auto record = std::make_unique<PluginRecord>();
            record->handle = i;
            record->state = PluginState::loaded;
            record->accepting_resources = true;
            host.plugins_.push_back(std::move(record));
        }

        const void* raw = reinterpret_cast<void*>(1);
        CHECK(host.QueryService(1,KEELS2_ENTITY_CONSTRUCTION_SERVICE_NAME,2,&raw) == KEEL_RESULT_INCOMPATIBLE && !raw);
        CHECK(host.QueryService(1,KEELS2_ENTITY_CONSTRUCTION_SERVICE_NAME,1,&raw) == KEEL_RESULT_OK && raw);
        const auto api = *static_cast<const KeelEntityConstructionApi*>(raw);
        auto& service = *host.schema_entities_;
        const auto entities = service.EntitiesApi();
        CHECK(api.size == sizeof(api) && api.api_version == 1 && api.ready(1) == KEEL_RESULT_OK);
        CHECK(api.ready(99) == KEEL_RESULT_NOT_READY);
        host.adapter_module_->entity_construction_ = {};
        CHECK(api.ready(1) == KEEL_RESULT_UNSUPPORTED);
        host.adapter_module_->entity_construction_ = Adapter::Api();
        KeelEntityHandle owner{}, observer{};
        KeelEntityInfo info{};
        info.size = sizeof(info);
        const auto create = [&]
        {
            CHECK(api.create(1, "prop_dynamic", &owner) == KEEL_RESULT_OK && owner);
        };
        const auto observe = [&]
        {
            CHECK(api.describe(1, owner, &info) == KEEL_RESULT_OK);
            CHECK(api.observe(2, info.source2_handle, &observer) == KEEL_RESULT_OK && observer);
        };

        CHECK(api.create(1,"bad/class",&owner) == KEEL_RESULT_INVALID_ARGUMENT && !owner && !adapter.creates);
        create();
        observe();
        const auto source = info.source2_handle;
        KeelEntityHandle found = 123;
        CHECK(entities.find_by_source2_handle(2,source,&found) == KEEL_RESULT_NOT_FOUND && !found);
        CHECK(entities.describe(1,owner,&info) == KEEL_RESULT_NOT_FOUND);
        CHECK(api.describe(2,owner,&info) == KEEL_RESULT_NOT_FOUND);
        KeelEntityKeyValue value{};
        value.size = sizeof(value);
        value.type = KEELS2_ENTITY_KEY_STRING;
        char name[] = "model", text[] = "models/test.vmdl";
        value.name = name;
        value.string_value = text;
        CHECK(api.set(2,observer,&value) == KEEL_RESULT_NOT_FOUND);
        CHECK(api.set(2,owner,&value) == KEEL_RESULT_NOT_FOUND);
        adapter.on_set = [&]
        {
            name[0] = text[0] = 'X';
        };
        CHECK(api.set(1, owner, &value) == KEEL_RESULT_OK && adapter.sets == 1);
        adapter.on_set = {};
        auto invalid = value;
        invalid.type = KEELS2_ENTITY_KEY_FLOAT;
        invalid.float_value = std::numeric_limits<float>::infinity();
        CHECK(api.set(1,owner,&invalid) == KEEL_RESULT_INVALID_ARGUMENT && adapter.sets == 1);
        KeelEntityTeleport teleport{sizeof(teleport), KEELS2_TELEPORT_POSITION, {1, 2, 3}, {}, {}};
        adapter.on_teleport = [&]
        {
            teleport.position[0] = 99;
        };
        CHECK(api.teleport(1, owner, &teleport) == KEEL_RESULT_OK);
        adapter.on_teleport = {};
        std::thread worker(
            [&]
            {
                CHECK(api.ready(1) == KEEL_RESULT_WRONG_THREAD);
                CHECK(entities.release(1, owner) == KEEL_RESULT_WRONG_THREAD);
                CHECK(entities.release(2, observer) == KEEL_RESULT_OK);
                CHECK(service.ReleasePlugin(1) == KEEL_RESULT_WRONG_THREAD);
                CHECK(!service.Shutdown());
            });

        worker.join();
        CHECK(!adapter.cancels);
        observe();
        adapter.on_cancel = [&] {
            CHECK(host.PluginByHandle(1)->active_native_operations);
            std::string reason;
            CHECK(host.HasRunningDependent(*host.PluginByHandle(1), reason, true));
            CHECK(api.describe(2,observer,&info) == KEEL_RESULT_NOT_FOUND);
            // This worker release must not wait on the host lock held by cancel.
            std::thread close(
                [&]
                {
                    CHECK(entities.release(2, observer) == KEEL_RESULT_OK);
                });

            close.join();
        };
        CHECK(entities.release(1,owner) == KEEL_RESULT_OK && adapter.cancels == 1);
        adapter.on_cancel = {};

        for (bool close_owner : {false,true}) {
            create();
            observe();
            KeelBool invoked{};
            adapter.on_spawn = [&] {
                CHECK(host.PluginByHandle(1)->active_native_operations);
                CHECK(api.describe(2,observer,&info) == KEEL_RESULT_OK);
                std::function<void()> callback = [&] {
                    CHECK(host.PluginByHandle(2)->active_native_operations);
                    KeelBool nested{};
                    CHECK(api.spawn(1, owner, &nested) == KEEL_RESULT_BUSY && !nested);
                    CHECK(api.set(1,owner,&value) == KEEL_RESULT_BUSY);

                    if (close_owner)
                        CHECK(entities.release(1, owner) == KEEL_RESULT_OK);
                };
                CHECK(api.visit(2,observer,"CBaseEntity",&Visit,&callback) == KEEL_RESULT_OK);
            };
            CHECK(api.spawn(1, owner, &invoked) == KEEL_RESULT_OK && invoked);
            adapter.on_spawn = {};
            CHECK(api.describe(2,observer,&info) == KEEL_RESULT_NOT_FOUND);
            CHECK(entities.describe(2,observer,&info) == (close_owner ? KEEL_RESULT_NOT_FOUND : KEEL_RESULT_OK));

            if (!close_owner) {
                std::thread close(
                    [&]
                    {
                        CHECK(entities.release(1, owner) == KEEL_RESULT_OK);
                    });

                close.join();
                CHECK(entities.describe(2,observer,&info) == KEEL_RESULT_OK);
            }

            CHECK(entities.release(2,observer) == KEEL_RESULT_OK);
        }

        create();
        adapter.dispatch = false;
        adapter.spawn_result = KEEL_RESULT_NOT_READY;
        KeelBool invoked = KEEL_TRUE;
        CHECK(api.spawn(1,owner,&invoked) == KEEL_RESULT_NOT_READY && !invoked);
        CHECK(api.describe(1,owner,&info) == KEEL_RESULT_OK);
        adapter.dispatch = true;
        adapter.spawn_result = KEEL_RESULT_ENGINE_FAILURE;
        CHECK(api.spawn(1,owner,&invoked) == KEEL_RESULT_ENGINE_FAILURE && invoked);
        CHECK(api.spawn(1,owner,&invoked) == KEEL_RESULT_NOT_FOUND && !invoked);
        CHECK(entities.release(1, owner) == KEEL_RESULT_OK);
        adapter.spawn_result = KEEL_RESULT_OK;
        // Factory callbacks can revoke the reserved host handle before return.
        create();
        const auto guessed = owner + 1;
        CHECK(entities.release(1, owner) == KEEL_RESULT_OK);
        adapter.on_create = [&]
        {
            CHECK(entities.release(1, guessed) == KEEL_RESULT_OK);
        };
        const auto cancels = adapter.cancels;
        CHECK(api.create(1,"prop_dynamic",&owner) == KEEL_RESULT_NOT_FOUND && !owner && adapter.cancels == cancels+1);
        adapter.on_create = {};
        // Recursion is global across both plugin owners and all construction calls.
        create();
        unsigned depth{}, visits{};

        std::function<void()> recursive = [&]
        {
            ++depth;
            ++visits;
            CHECK(api.visit(1,owner,"CBaseEntity",&Visit,&recursive) == (depth == 8 ? KEEL_RESULT_BUSY : KEEL_RESULT_OK));
            --depth;
        };
        CHECK(api.visit(1,owner,"CBaseEntity",&Visit,&recursive) == KEEL_RESULT_OK && visits == 8);
        CHECK(entities.release(1,owner) == KEEL_RESULT_OK);
        std::vector<KeelEntityHandle> pending;

        for (unsigned i = 0; i < 64; ++i)
        {
            create();
            pending.push_back(owner);
        }

        CHECK(api.create(1,"prop_dynamic",&owner) == KEEL_RESULT_BUSY && !owner);
        ++adapter.epoch;
        const auto before_prune = adapter.cancels;
        create();
        CHECK(adapter.cancels == before_prune + 64);

        for (const auto handle : pending)
            CHECK(entities.release(1, handle) == KEEL_RESULT_OK);

        std::vector<KeelEntityHandle> observers;

        for (unsigned i = 0; i < 256; ++i)
        {
            observe();
            observers.push_back(observer);
        }

        CHECK(api.observe(2,info.source2_handle,&observer) == KEEL_RESULT_BUSY && !observer);
        CHECK(service.ReleasePlugin(2) == KEEL_RESULT_OK);
        CHECK(api.describe(1, owner, &info) == KEEL_RESULT_OK);
        observe();
        const auto before_unload = adapter.cancels;
        CHECK(service.ReleasePlugin(1) == KEEL_RESULT_OK && adapter.cancels == before_unload+1);
        CHECK(api.describe(2,observer,&info) == KEEL_RESULT_NOT_FOUND);
        CHECK(entities.release(2,observer) == KEEL_RESULT_OK);
        // Host shutdown during an engine callback is retained by active native
        // operations; retry only after the callback has unwound.
        create();
        observe();
        const auto shutdown_observer = observer;
        create();
        host.state_ = HostState::running;
        host.dispatch_open_.store(true);
        CHECK(BeginGameCommandDispatch() == 1);
        unsigned shutdown_callbacks{};
        adapter.on_cancel = [&]
        {
            ++shutdown_callbacks;
            CHECK(!host.Stop());
        };
        CHECK(entities.release(1,owner) == KEEL_RESULT_OK);
        CHECK(host.state_ == HostState::running && host.schema_entities_);
        EndGameCommandDispatch();
        CHECK(host.Stop());
        adapter.on_cancel = {};
        CHECK(shutdown_callbacks == 2 && api.ready(1) == KEEL_RESULT_NOT_READY);
        CHECK(entities.release(2,shutdown_observer) == KEEL_RESULT_NOT_READY);
    }
};
}

int main()
{
    try
    {
        keels2::host::SchemaEntityServiceTest::Run();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
