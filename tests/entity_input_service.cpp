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
#include <stdexcept>
#include <thread>

namespace keels2::host {
namespace {
void Check(bool value, int line)
{
    if (!value)
        throw std::runtime_error("host input check " + std::to_string(line));
}
#define CHECK(value) Check((value),__LINE__)
class Adapter final : public test::SyntheticAdapter
{
public:
    Adapter() : SyntheticAdapter({}) {}

    const std::thread::id thread{std::this_thread::get_id()};
    std::uint64_t epoch{1};
    std::uint32_t direct{511}, queued{383};
    unsigned dispatches{}, discoveries{};
    bool live{true}, enter{true};
    KeelResult result{KEEL_RESULT_OK};
    std::function<void(const GameEntityInputRequest&)> on_dispatch;
    std::function<void()> on_capabilities;

    bool IsGameThread() const noexcept override
    {
        return std::this_thread::get_id() == thread;
    }

    KeelResult ValidateEntity(const GameEntityIdentity& identity, std::string&) override {
        return live && identity.epoch == epoch ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }

    KeelResult FindEntityBySource2Handle(std::uint32_t source, GameEntityIdentity& out, std::string&) override {
        out = {static_cast<int>(source), source, epoch};
        return KEEL_RESULT_OK;
    }

    static GameAdapterEntityInputApi Api() {
        return {
            sizeof(GameAdapterEntityInputApi),
            1,
            [](GameAdapter* adapter, std::uint32_t* direct_types, std::uint32_t* queued_types) noexcept -> KeelResult
            {
                auto& a = *static_cast<Adapter*>(adapter);
                ++a.discoveries;
                const auto callback = a.on_capabilities;

                if (callback)
                    callback();

                *direct_types = a.direct;
                *queued_types = a.queued;
                return a.result;
            },
            [](GameAdapter* adapter, const GameEntityInputRequest* request, KeelBool* invoked) noexcept -> KeelResult
            {
                auto& a = *static_cast<Adapter*>(adapter);
                ++a.dispatches;
                CHECK(request->target.index == static_cast<int>(request->target.source2_handle));

                for (const auto& identity : {request->target,request->activator,request->caller,request->value_entity})
                    if (identity.epoch && (!a.live || identity.epoch != a.epoch))
                        return KEEL_RESULT_NOT_FOUND;

                if (a.enter)
                    *invoked = KEEL_TRUE;

                const auto callback = a.on_dispatch;

                if (callback)
                    callback(*request);

                return a.result;
            }};
    }
};
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
        host.adapter_module_->entity_input_ = Adapter::Api();

        for (unsigned i : {1u,2u}) {
            auto record = std::make_unique<PluginRecord>();
            record->handle = i;
            record->state = PluginState::loaded;
            record->accepting_resources = true;
            host.plugins_.push_back(std::move(record));
        }

        const void* raw = reinterpret_cast<void*>(1);
        CHECK(host.QueryService(1,KEELS2_ENTITY_INPUT_SERVICE_NAME,2,&raw) == KEEL_RESULT_INCOMPATIBLE && !raw);
        CHECK(host.QueryService(1,KEELS2_ENTITY_INPUT_SERVICE_NAME,1,&raw) == KEEL_RESULT_OK && raw);
        const auto api = *static_cast<const KeelEntityInputApi*>(raw);
        auto& service = *host.schema_entities_;
        const auto entities = service.EntitiesApi();
        std::uint32_t direct{}, queued{};
        CHECK(api.size == sizeof(api) && api.api_version == 1);
        CHECK(api.capabilities(1,&direct,&queued) == KEEL_RESULT_OK && direct == 511 && queued == 383);
        CHECK(api.capabilities(1,nullptr,&queued) == KEEL_RESULT_INVALID_ARGUMENT && !queued);
        CHECK(api.capabilities(99,&direct,&queued) == KEEL_RESULT_NOT_READY && !direct && !queued);
        adapter.result = KEEL_RESULT_NOT_READY;
        CHECK(api.capabilities(1,&direct,&queued) == KEEL_RESULT_NOT_READY && !direct && !queued);
        adapter.result = KEEL_RESULT_OK;
        adapter.direct |= 1u << 9;
        CHECK(api.capabilities(1, &direct, &queued) == KEEL_RESULT_INCOMPATIBLE && !direct && !queued);
        adapter.direct = 511;
        host.adapter_module_->entity_input_ = {};
        CHECK(api.capabilities(1,&direct,&queued) == KEEL_RESULT_UNSUPPORTED && !direct && !queued);
        host.adapter_module_->entity_input_ = Adapter::Api();
        KeelEntityHandle target{}, participant{}, foreign{};
        const auto acquire = [&](KeelPluginHandle owner, std::uint32_t source, KeelEntityHandle& handle) {
            CHECK(entities.find_by_source2_handle(owner,source,&handle) == KEEL_RESULT_OK && handle);
        };
        acquire(1, 7, target);
        acquire(1, 4, participant);
        acquire(2, 7, foreign);
        char name[] = "Enable", text[] = "original";
        KeelEntityInputRequest request{};
        request.size = sizeof(request);
        request.input = name;
        request.value.size = sizeof(request.value);
        request.value.type = KEELS2_INPUT_STRING;
        request.value.string_value = text;
        request.activator = participant;
        request.caller = target;
        KeelBool invoked = KEEL_TRUE;
        const auto call = [&]
        {
            invoked = KEEL_TRUE;
            return api.dispatch(1, target, &request, &invoked);
        };
        adapter.on_dispatch = [&](const GameEntityInputRequest& copied) {
            CHECK(copied.activator.source2_handle == 4 && copied.caller.source2_handle == 7);
            name[0] = text[0] = 'X';
            request.value.type = KEELS2_INPUT_VOID;
            CHECK(std::string(copied.input) == "Enable" && copied.value.type == KEELS2_INPUT_STRING &&
                std::string(copied.value.string_value) == "original");

            CHECK(host.PluginByHandle(1)->active_native_operations);
            std::string reason;
            CHECK(host.HasRunningDependent(*host.PluginByHandle(1), reason, true));
            // Registry mutex is not retained across the callback.
            std::thread close(
                [&]
                {
                    CHECK(entities.release(1, participant) == KEEL_RESULT_OK);
                });

            close.join();
        };
        CHECK(call() == KEEL_RESULT_OK && invoked);
        adapter.on_dispatch = {};
        request.input = "Enable";
        request.value.type = KEELS2_INPUT_VOID;
        CHECK(call() == KEEL_RESULT_NOT_FOUND && !invoked);
        acquire(1, 4, participant);
        request.activator = participant;
        request.caller = foreign;
        const auto before = adapter.dispatches;
        CHECK(call() == KEEL_RESULT_NOT_FOUND && !invoked && adapter.dispatches == before);
        request.caller = target;
        CHECK(api.dispatch(2,target,&request,&invoked) == KEEL_RESULT_NOT_FOUND && !invoked);
        CHECK(api.dispatch(1,0,&request,&invoked) == KEEL_RESULT_INVALID_ARGUMENT && !invoked);
        CHECK(api.dispatch(1,target,nullptr,&invoked) == KEEL_RESULT_INVALID_ARGUMENT && !invoked);
        CHECK(api.dispatch(1,target,&request,nullptr) == KEEL_RESULT_INVALID_ARGUMENT);
        request.value.type = KEELS2_INPUT_ENTITY;
        CHECK(call() == KEEL_RESULT_INVALID_ARGUMENT && !invoked);
        request.value_entity = participant;
        CHECK(call() == KEEL_RESULT_OK && invoked);
        request.value.type = KEELS2_INPUT_VOID;
        CHECK(call() == KEEL_RESULT_INVALID_ARGUMENT && !invoked);
        request.value_entity = 0;

        for (const auto type : {KEELS2_INPUT_FLOAT,KEELS2_INPUT_VECTOR,KEELS2_INPUT_ANGLES}) {
            request.value.type = type;
            request.value.float_value = std::numeric_limits<float>::infinity();
            request.value.vector_value[2] = std::numeric_limits<float>::quiet_NaN();
            CHECK(call() == KEEL_RESULT_INVALID_ARGUMENT && !invoked);
        }

        request.value.type = KEELS2_INPUT_BOOL;
        request.value.int_value = 2;
        CHECK(call() == KEEL_RESULT_INVALID_ARGUMENT && !invoked);
        request.value.type = KEELS2_INPUT_VOID;
        request.queued = 2;
        CHECK(call() == KEEL_RESULT_INVALID_ARGUMENT && !invoked);
        request.queued = KEEL_FALSE;
        request.delay = 1;
        CHECK(call() == KEEL_RESULT_INVALID_ARGUMENT && !invoked);
        request.delay = 0;
        request.value.type = KEELS2_INPUT_STRING;
        const std::string oversized(4096, 's');
        request.value.string_value = oversized.c_str();
        CHECK(call() == KEEL_RESULT_INVALID_ARGUMENT && !invoked);
        request.value.type = KEELS2_INPUT_VOID;
        std::thread worker(
            [&]
            {
                CHECK(api.capabilities(1, &direct, &queued) == KEEL_RESULT_WRONG_THREAD && !direct && !queued);
                CHECK(call() == KEEL_RESULT_WRONG_THREAD && !invoked);
            });

        worker.join();
        host.PluginByHandle(1)->transitioning = true;
        CHECK(call() == KEEL_RESULT_BUSY && !invoked);
        host.PluginByHandle(1)->transitioning = false;
        ++adapter.epoch;
        CHECK(call() == KEEL_RESULT_NOT_FOUND && !invoked);
        KeelEntityHandle new_map{};
        acquire(1, 7, new_map);
        request.activator = new_map;
        CHECK(call() == KEEL_RESULT_NOT_FOUND && !invoked);
        CHECK(entities.release(1,new_map) == KEEL_RESULT_OK);
        --adapter.epoch;
        request.activator = participant;
        adapter.enter = false;
        adapter.result = KEEL_RESULT_NOT_READY;
        CHECK(call() == KEEL_RESULT_NOT_READY && !invoked);
        adapter.enter = true;
        adapter.result = KEEL_RESULT_ENGINE_FAILURE;
        CHECK(call() == KEEL_RESULT_ENGINE_FAILURE && invoked);
        adapter.result = KEEL_RESULT_OK;
        // Shared recursion bound covers capabilities and dispatch across owners.
        unsigned depth{}, visits{};
        adapter.on_capabilities = [&]
        {
            ++depth;
            ++visits;
            std::uint32_t a{}, b{};
            CHECK(api.capabilities(depth % 2 ? 2 : 1, &a, &b) == (depth == 8 ? KEEL_RESULT_BUSY : KEEL_RESULT_OK));
            --depth;
        };
        CHECK(api.capabilities(1, &direct, &queued) == KEEL_RESULT_OK && visits == 8);
        adapter.on_capabilities = {};

        adapter.on_dispatch = [&](const GameEntityInputRequest&)
        {
            ++depth;
            ++visits;
            KeelBool nested = KEEL_TRUE;
            CHECK(api.dispatch(1,target,&request,&nested) == (depth == 8 ? KEEL_RESULT_BUSY : KEEL_RESULT_OK));
            CHECK(nested == (depth == 8 ? KEEL_FALSE : KEEL_TRUE));
            --depth;
        };
        visits = 0;
        CHECK(call() == KEEL_RESULT_OK && invoked && visits == 8);
        adapter.on_dispatch = {};
        CHECK(!host.PluginByHandle(1)->active_native_operations && !host.PluginByHandle(2)->active_native_operations);
        host.state_ = HostState::running;
        host.dispatch_open_.store(true);
        CHECK(BeginGameCommandDispatch() == 1);

        adapter.on_dispatch = [&](const GameEntityInputRequest&)
        {
            CHECK(!host.Stop());
            CHECK(entities.release(1, target) == KEEL_RESULT_OK);
            adapter.live = false;
        };
        CHECK(call() == KEEL_RESULT_OK && invoked);
        adapter.on_dispatch = {};
        CHECK(host.state_ == HostState::running && host.schema_entities_);
        CHECK(call() == KEEL_RESULT_NOT_FOUND && !invoked);
        EndGameCommandDispatch();
        CHECK(host.Stop());
        CHECK(api.capabilities(1,&direct,&queued) == KEEL_RESULT_NOT_READY && !direct && !queued);
        CHECK(call() == KEEL_RESULT_NOT_READY && !invoked);
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
