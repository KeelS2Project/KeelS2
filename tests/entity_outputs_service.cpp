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
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace keels2::host {
namespace {
void Check(bool value, int line)
{
    if (!value)
        throw std::runtime_error("host output check " + std::to_string(line));
}
#define CHECK(value) Check((value),__LINE__)
bool callback_failed{};

class Adapter final : public test::SyntheticAdapter {

public:
    Adapter() : SyntheticAdapter({}) {}

    const std::thread::id thread{std::this_thread::get_id()};
    std::uint64_t epoch{1}, next_token{1};
    unsigned starts{}, stops{}, active{}, original{}, validations{};
    bool live{true}, refuse_stop{};
    KeelResult start_result{KEEL_RESULT_OK};
    GameEntityOutputCallback callback{};
    void* callback_data{};
    std::function<void()> on_start, on_validate, on_original, on_completion;

    bool IsGameThread() const noexcept override
    {
        return std::this_thread::get_id() == thread;
    }

    GameEntityIdentity Identity() const
    {
        return {7, 0x12340007, epoch};
    }

    KeelResult ValidateEntity(const GameEntityIdentity& identity, std::string&) override {
        ++validations;
        const auto function = on_validate;

        if (function)
            function();

        return live && identity.epoch == epoch && identity.source2_handle == 0x12340007 ? KEEL_RESULT_OK
                                                                                        : KEEL_RESULT_NOT_FOUND;
    }

    KeelResult FindEntityBySource2Handle(std::uint32_t source, GameEntityIdentity& out, std::string&) override {
        if (!live || source != 0x12340007)
            return KEEL_RESULT_NOT_FOUND;

        out = Identity();
        return KEEL_RESULT_OK;
    }

    static KeelResult Start(GameAdapter* raw,
                            const KeelHookApi* hooks,
                            GameHookDefer defer,
                            GameEntityOutputCallback fn,
                            void* data) noexcept
    {
        auto& a = *static_cast<Adapter*>(raw);
        ++a.starts;

        if (!hooks || !defer || !fn)
            return KEEL_RESULT_INVALID_ARGUMENT;

        try
        {
            const auto function = a.on_start;

            if (function)
                function();
        }
        catch (...)
        {
            callback_failed = true;
            return KEEL_RESULT_ENGINE_FAILURE;
        }

        if (a.start_result != KEEL_RESULT_OK)
            return a.start_result;

        a.callback = fn;
        a.callback_data = data;
        return KEEL_RESULT_OK;
    }

    static KeelResult Stop(GameAdapter* raw) noexcept {
        auto& a = *static_cast<Adapter*>(raw);
        ++a.stops;

        if (a.active || a.refuse_stop)
            return KEEL_RESULT_BUSY;

        a.callback = nullptr;
        a.callback_data = nullptr;
        return KEEL_RESULT_OK;
    }

    static GameAdapterEntityOutputsApi Api()
    {
        return {sizeof(GameAdapterEntityOutputsApi), 1, &Start, &Stop};
    }

    bool Fire(bool skip_post = false) {
        CHECK(callback);
        ++active;

        struct Active
        {
            unsigned& value;

            ~Active()
            {
                --value;
            }
        } guard{active};
        const auto token = next_token++;
        const auto fn = callback;
        auto* data = callback_data;
        KeelEntityOutputEvent event{};
        event.size = sizeof(event);
        event.phase = KEELS2_OUTPUT_PRE;
        event.entity = {sizeof(KeelEntityInfo), 7, 0x12340007, 0, epoch};
        event.activator = event.caller = {sizeof(KeelEntityInfo), -1, UINT32_MAX, 0, 0};
        std::memcpy(event.class_name, "trigger_multiple", sizeof("trigger_multiple"));
        std::memcpy(event.schema_name, "CTriggerMultiple", sizeof("CTriggerMultiple"));
        std::memcpy(event.output_name, "OnTrigger", sizeof("OnTrigger"));
        event.delay = 1.25f;
        event.value_status = KEEL_RESULT_OK;
        event.value.size = sizeof(event.value);
        event.value.type = KEELS2_INPUT_STRING;
        std::memcpy(event.value.string_value,"copied payload",sizeof("copied payload"));
        const auto action = fn(&event, token, data);
        CHECK(action == KEELS2_OUTPUT_CONTINUE || action == KEELS2_OUTPUT_BLOCK);

        if (action == KEELS2_OUTPUT_CONTINUE)
        {
            ++original;

            if (on_original)
                on_original();
        }

        event.phase = KEELS2_OUTPUT_POST;
        event.flags = action == KEELS2_OUTPUT_CONTINUE ? KEELS2_OUTPUT_ORIGINAL_CALLED : 0;

        if (!skip_post)
            CHECK(fn(&event, token, data) == KEELS2_OUTPUT_CONTINUE);

        CHECK(fn(nullptr,token,data) == KEELS2_OUTPUT_CONTINUE);

        if (on_completion)
            on_completion();

        return action == KEELS2_OUTPUT_CONTINUE;
    }
};

struct Listener {
    unsigned pre{}, post{};
    bool expected_throw{};
    KeelEntityOutputEvent last{};
    std::function<std::uint32_t(const KeelEntityOutputEvent&)> function;
    static std::uint32_t Call(const KeelEntityOutputEvent* event, void* data) {
        auto& self = *static_cast<Listener*>(data);
        self.last = *event;

        if (event->phase == KEELS2_OUTPUT_PRE)
            ++self.pre;
        else
            ++self.post;

        try
        {
            return self.function ? self.function(*event) : KEELS2_OUTPUT_CONTINUE;
        }
        catch (...)
        {
            if (!self.expected_throw)
                callback_failed = true;

            throw;
        }
    }
};
}

struct SchemaEntityServiceTest {
    static void Run() {
        Adapter adapter;
        auto& host = Host::Instance();
        host.accepting_resources_ = true;
        host.adapter_ = &adapter;
        host.state_ = HostState::running;
        host.adapter_module_ = std::make_unique<GameAdapterModule>();
        host.adapter_module_->entity_outputs_ = Adapter::Api();

        for (unsigned i : {1u,2u,3u,4u,5u}) {
            auto record = std::make_unique<PluginRecord>();
            record->handle = i;
            record->state = PluginState::loaded;
            record->accepting_resources = true;
            host.plugins_.push_back(std::move(record));
        }

        const void* raw = reinterpret_cast<void*>(1);
        CHECK(host.QueryService(1,KEELS2_ENTITY_OUTPUTS_SERVICE_NAME,2,&raw) == KEEL_RESULT_INCOMPATIBLE && !raw);
        CHECK(host.QueryService(1,KEELS2_ENTITY_OUTPUTS_SERVICE_NAME,1,&raw) == KEEL_RESULT_OK && raw);
        const auto api = *static_cast<const KeelEntityOutputsApi*>(raw);
        auto& service = *host.schema_entities_;
        const auto entities = service.EntitiesApi();
        CHECK(api.size == sizeof(api) && api.api_version == 1);
        CHECK(api.ready(99) == KEEL_RESULT_NOT_READY);
        host.adapter_module_->entity_outputs_ = {};
        CHECK(api.ready(1) == KEEL_RESULT_UNSUPPORTED);
        host.adapter_module_->entity_outputs_ = Adapter::Api();
        adapter.start_result = KEEL_RESULT_NOT_READY;
        adapter.refuse_stop = true;
        CHECK(api.ready(1) == KEEL_RESULT_NOT_READY && service.outputs_retained_);
        CHECK(api.ready(1) == KEEL_RESULT_BUSY);
        adapter.refuse_stop = false;
        adapter.start_result = KEEL_RESULT_OK;
        adapter.on_start = [&]
        {
            CHECK(!host.Stop() && host.PluginByHandle(1)->active_native_operations);
            CHECK(api.ready(1) == KEEL_RESULT_BUSY);
        };
        CHECK(api.ready(1) == KEEL_RESULT_OK);
        adapter.on_start = {};
        Listener first, second, third;
        KeelEntityOutputSpec spec{
            sizeof(spec), KEELS2_OUTPUT_BOTH, 10, 0, 0, "trigger_multiple", "OnTrigger", &Listener::Call, &first};

        KeelEntityOutputHandle a{}, b{}, c{};
        CHECK(api.subscribe(1,&spec,&a) == KEEL_RESULT_OK);
        spec.priority = 0;
        spec.user_data = &second;
        CHECK(api.subscribe(2, &spec, &b) == KEEL_RESULT_OK);
        std::vector<int> order;
        first.function = [&](const auto& event) {
            CHECK(host.PluginByHandle(1)->active_native_operations && !host.Stop());
            std::string dependent;
            CHECK(host.HasRunningDependent(*host.PluginByHandle(1), dependent, true));
            order.push_back(event.phase == KEELS2_OUTPUT_PRE ? 1 : 4);
            CHECK(!std::strcmp(event.value.string_value,"copied payload"));
            const_cast<KeelEntityOutputEvent&>(event).value.string_value[0] = 'X';
            return KEELS2_OUTPUT_CONTINUE;
        };
        second.function = [&](const auto& event) {
            order.push_back(event.phase == KEELS2_OUTPUT_PRE ? 2 : 3);
            CHECK(!std::strcmp(event.value.string_value,"copied payload"));
            return KEELS2_OUTPUT_CONTINUE;
        };
        CHECK(adapter.Fire() && order == std::vector<int>({1, 2, 3, 4}) && service.output_invocations_.empty());
        CHECK(api.unsubscribe(2,a) == KEEL_RESULT_NOT_FOUND);
        CHECK(api.unsubscribe(1,0) == KEEL_RESULT_INVALID_ARGUMENT);
        CHECK(api.unsubscribe(1, a) == KEEL_RESULT_OK);
        CHECK(api.unsubscribe(2, b) == KEEL_RESULT_OK);
        first.function = {};
        second.function = {};
        KeelEntityHandle entity{}, foreign{};
        CHECK(entities.find_by_source2_handle(1,0x12340007,&entity) == KEEL_RESULT_OK);
        CHECK(entities.find_by_source2_handle(2,0x12340007,&foreign) == KEEL_RESULT_OK);
        spec.entity = foreign;
        spec.user_data = &first;
        CHECK(api.subscribe(1,&spec,&a) == KEEL_RESULT_NOT_FOUND);
        spec.entity = entity;
        char name[] = "OnTrigger", classname[] = "trigger_multiple";
        spec.output_name = name;
        spec.class_name = classname;
        adapter.on_start = [&]
        {
            name[0] = classname[0] = 'X';
            spec.phases = 0;
        };
        CHECK(api.subscribe(1, &spec, &a) == KEEL_RESULT_OK);
        adapter.on_start = {};
        CHECK(entities.release(1,entity) == KEEL_RESULT_OK);
        const auto before = first.pre;
        CHECK(adapter.Fire() && first.pre == before + 1);
        ++adapter.epoch;
        CHECK(adapter.Fire() && first.pre == before + 1 && api.unsubscribe(1, a) == KEEL_RESULT_NOT_FOUND);
        spec = {sizeof(spec), KEELS2_OUTPUT_BOTH, 0, 0, 0, nullptr, nullptr, &Listener::Call, &first};
        spec.output_name = "ontrigger";
        CHECK(api.subscribe(1, &spec, &a) == KEEL_RESULT_OK);
        const auto filtered = first.pre;
        CHECK(adapter.Fire() && first.pre == filtered);
        CHECK(api.unsubscribe(1, a) == KEEL_RESULT_OK);
        spec.output_name = nullptr;
        spec.class_name = "CTriggerMultiple";
        CHECK(api.subscribe(1, &spec, &a) == KEEL_RESULT_OK);
        CHECK(adapter.Fire() && first.pre == filtered);
        CHECK(api.unsubscribe(1, a) == KEEL_RESULT_OK);
        spec.class_name = nullptr;
        CHECK(entities.find_by_source2_handle(1,0x12340007,&entity) == KEEL_RESULT_OK);
        spec.entity = entity;
        adapter.on_start = [&]
        {
            CHECK(entities.release(1, entity) == KEEL_RESULT_OK);
        };
        CHECK(api.subscribe(1, &spec, &a) == KEEL_RESULT_NOT_FOUND && !a);
        adapter.on_start = {};
        spec.entity = 0;
        CHECK(api.subscribe(1,&spec,&a) == KEEL_RESULT_OK);
        first.function = [&](const auto& event) {
            if (event.phase == KEELS2_OUTPUT_PRE) {
                CHECK(api.unsubscribe(1,a) == KEEL_RESULT_OK);
                auto added = spec;
                added.user_data = &third;
                CHECK(api.subscribe(1,&added,&c) == KEEL_RESULT_OK);
            }

            return KEELS2_OUTPUT_CONTINUE;
        };
        const auto post_before = first.post;
        CHECK(adapter.Fire() && first.post == post_before && !third.post);
        CHECK(adapter.Fire() && third.pre == 1 && third.post == 1);
        CHECK(api.unsubscribe(1, c) == KEEL_RESULT_OK);
        first.function = {};
        CHECK(api.subscribe(1,&spec,&a) == KEEL_RESULT_OK);
        host.PluginByHandle(1)->state = PluginState::paused;
        const auto paused = first.pre;
        CHECK(adapter.Fire() && first.pre == paused);
        host.PluginByHandle(1)->state = PluginState::loaded;

        first.function = [](const auto& event)
        {
            return event.phase == KEELS2_OUTPUT_PRE ? KEELS2_OUTPUT_BLOCK : KEELS2_OUTPUT_CONTINUE;
        };
        CHECK(!adapter.Fire() && !first.last.flags && service.output_invocations_.empty());
        first.function = {};
        const auto skipped = first.post;
        CHECK(adapter.Fire(true) && first.post == skipped && service.output_invocations_.empty());

        first.function = [](const auto&) -> std::uint32_t
        {
            return 99;
        };
        CHECK(!adapter.Fire() && api.unsubscribe(1,a) == KEEL_RESULT_NOT_FOUND);
        first.expected_throw = true;

        first.function = [](const auto&) -> std::uint32_t
        {
            throw std::runtime_error("expected observer failure");
        };
        CHECK(api.subscribe(1, &spec, &a) == KEEL_RESULT_OK);
        CHECK(!adapter.Fire() && api.unsubscribe(1, a) == KEEL_RESULT_NOT_FOUND);
        first.expected_throw = false;

        first.function = [](const auto& event)
        {
            return event.phase == KEELS2_OUTPUT_POST ? KEELS2_OUTPUT_BLOCK : KEELS2_OUTPUT_CONTINUE;
        };
        CHECK(api.subscribe(1, &spec, &a) == KEEL_RESULT_OK);
        CHECK(adapter.Fire() && api.unsubscribe(1, a) == KEEL_RESULT_NOT_FOUND);
        unsigned depth{}, visits{};

        first.function = [&](const auto& event)
        {
            if (event.phase == KEELS2_OUTPUT_PRE)
            {
                ++depth;
                ++visits;
                CHECK(adapter.Fire() == (depth < 8));
                --depth;
            }

            return KEELS2_OUTPUT_CONTINUE;
        };
        CHECK(api.subscribe(1, &spec, &a) == KEEL_RESULT_OK);
        CHECK(adapter.Fire() && visits == 8 && service.output_invocations_.empty());
        CHECK(api.unsubscribe(1, a) == KEEL_RESULT_OK);
        first.function = {};
        auto pending = std::make_shared<SchemaEntityService::Construction>();
        pending->token = 123;
        pending->initializing = false;
        pending->thread = std::this_thread::get_id();
        pending->owner = 2;
        const auto pending_handle = service.next_entity_++;
        service.entities_.emplace(pending_handle,
                                  SchemaEntityService::EntityRecord{1, adapter.Identity(), pending, false});

        host.adapter_module_->entity_construction_.describe =
            [](GameAdapter* raw_adapter, std::uint64_t token, GameEntityIdentity* out) noexcept -> KeelResult
        {
            if (token != 123)
                return KEEL_RESULT_NOT_FOUND;

            *out = static_cast<Adapter*>(raw_adapter)->Identity();
            return KEEL_RESULT_OK;
        };
        adapter.live = false;
        spec.entity = pending_handle;
        CHECK(api.subscribe(1, &spec, &a) == KEEL_RESULT_OK);
        CHECK(adapter.Fire());
        pending->closed = true;
        CHECK(api.ready(1) == KEEL_RESULT_OK && api.unsubscribe(1, a) == KEEL_RESULT_NOT_FOUND);
        CHECK(entities.release(1, pending_handle) == KEEL_RESULT_OK);
        adapter.live = true;
        spec.entity = 0;
        std::thread worker(
            [&]
            {
                CHECK(api.ready(1) == KEEL_RESULT_WRONG_THREAD);
                CHECK(api.subscribe(1, &spec, &a) == KEEL_RESULT_WRONG_THREAD && !a);
                CHECK(api.unsubscribe(1, 999) == KEEL_RESULT_WRONG_THREAD);
            });

        worker.join();

        for (const auto invalid : {0u, 4u})
        {
            auto bad = spec;
            bad.phases = invalid;
            CHECK(api.subscribe(1, &bad, &a) == KEEL_RESULT_INVALID_ARGUMENT && !a);
        }

        auto bad = spec;
        bad.reserved = 1;
        CHECK(api.subscribe(1, &bad, &a) == KEEL_RESULT_INVALID_ARGUMENT);
        bad = spec;
        bad.callback = nullptr;
        CHECK(api.subscribe(1, &bad, &a) == KEEL_RESULT_INVALID_ARGUMENT);

        for (const std::string& text : {std::string(128, 'n'), std::string("bad\nname")})
        {
            bad = spec;
            bad.output_name = text.c_str();
            CHECK(api.subscribe(1, &bad, &a) == KEEL_RESULT_INVALID_ARGUMENT);
        }

        CHECK(api.subscribe(1, nullptr, &a) == KEEL_RESULT_INVALID_ARGUMENT && !a);
        CHECK(api.subscribe(1, &spec, nullptr) == KEEL_RESULT_INVALID_ARGUMENT);
        // A map switch must free expired entity filters even before the next
        // output arrives, so old-map registrations cannot exhaust the quota.
        CHECK(entities.find_by_source2_handle(1,0x12340007,&entity) == KEEL_RESULT_OK);
        spec.entity = entity;

        for (unsigned i = 0; i < 64; ++i)
            CHECK(api.subscribe(1, &spec, &a) == KEEL_RESULT_OK);

        CHECK(api.subscribe(1,&spec,&a) == KEEL_RESULT_BUSY);
        ++adapter.epoch;
        CHECK(api.ready(1) == KEEL_RESULT_OK && service.outputs_.empty());
        CHECK(entities.release(1, entity) == KEEL_RESULT_OK);
        spec.entity = 0;

        for (unsigned owner : {1u, 2u, 3u, 4u})
            for (unsigned i = 0; i < 64; ++i)
                CHECK(api.subscribe(owner, &spec, &a) == KEEL_RESULT_OK);

        CHECK(api.subscribe(1, &spec, &a) == KEEL_RESULT_BUSY && !a);
        CHECK(api.subscribe(5, &spec, &a) == KEEL_RESULT_BUSY && !a);

        for (unsigned owner : {1u, 2u, 3u, 4u})
            CHECK(service.ReleasePlugin(owner) == KEEL_RESULT_OK);

        CHECK(service.outputs_.empty());
        CHECK(api.subscribe(1,&spec,&a) == KEEL_RESULT_OK);

        first.function = [&](const auto&)
        {
            CHECK(service.ReleasePlugin(1) == KEEL_RESULT_OK);
            return KEELS2_OUTPUT_CONTINUE;
        };
        const auto unload_post = first.post;
        CHECK(adapter.Fire() && first.post == unload_post && service.outputs_.empty());
        first.function = {};
        CHECK(api.subscribe(1,&spec,&a) == KEEL_RESULT_OK);
        adapter.start_result = KEEL_RESULT_NOT_READY;
        CHECK(api.ready(1) == KEEL_RESULT_NOT_READY && adapter.callback);
        adapter.start_result = KEEL_RESULT_OK;
        // Shutdown after native POST and its completion notification must still
        // retain the adapter until the native frame's active slot is released.
        adapter.on_completion = [&]
        {
            CHECK(!host.Stop() && host.schema_entities_ && adapter.callback);
        };
        CHECK(adapter.Fire());
        adapter.on_completion = {};
        CHECK(!callback_failed && service.output_invocations_.empty());
        CHECK(host.Stop() && !adapter.callback && !host.schema_entities_);
        CHECK(api.ready(1) == KEEL_RESULT_NOT_READY);
    }
};
}

int main() {
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
