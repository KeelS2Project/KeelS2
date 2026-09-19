#include "entity_authoring_fixture.h"
#include <stdexcept>
#include <vector>

namespace {
unsigned input_calls{};
std::uint32_t direct_types{511}, queued_types{383};
KeelBool input_marker{KEEL_TRUE};
KeelResult input_result{KEEL_RESULT_OK}, caps_result{KEEL_RESULT_OK};
std::function<void()> on_input, on_caps;
KeelEntityInputRequest observed_input{};

KeelResult InputCaps(KeelPluginHandle owner, std::uint32_t* direct, std::uint32_t* queued)
{
    if (Thread(owner) != KEEL_RESULT_OK)
        return KEEL_RESULT_WRONG_THREAD;

    Invoke(on_caps);
    *direct = direct_types;
    *queued = queued_types;
    return caps_result;
}

KeelResult
Input(KeelPluginHandle owner, KeelEntityHandle target, const KeelEntityInputRequest* request, KeelBool* invoked)
{
    *invoked = KEEL_FALSE;

    for (const auto handle : {target,request->activator,request->caller,request->value_entity}) if (handle) {
        KeelEntityInfo info{};
        const auto result = Describe(owner, handle, &info);

        if (result != KEEL_RESULT_OK)
            return result;
    }

    if (request->queued && request->value.type == KEELS2_INPUT_COLOR)
        return KEEL_RESULT_UNSUPPORTED;

    ++input_calls;
    *invoked = input_marker;
    Invoke(on_input);
    observed_input = *request;
    observed_name = request->input;
    observed_text = request->value.type == KEELS2_INPUT_STRING ? request->value.string_value : "";
    return input_result;
}

KeelEntityInputApi input_table{sizeof(input_table), 1, &InputCaps, &Input};

class ForeignProbe final : public Plugin {

public:
    static constexpr PluginInfo Info{"Foreign input owner","KeelS2 tests","1.0.0","Separate input context"};
    using Plugin::FindEntity;
    using Plugin::HostContext;
    static ForeignProbe* current;

    bool Load() override
    {
        current = this;
        return true;
    }
};
ForeignProbe* ForeignProbe::current{};
using ForeignAdapter = keels2::detail::AuthoringAdapter<ForeignProbe>;
}

int main()
{
    runtime.size = sizeof(runtime);
    runtime.api_version = 1;
    runtime.check_game_thread = &Thread;
    input_api_fixture = &input_table;
    KeelHostApi host{};
    host.size = sizeof(host);
    host.abi_version = KEELS2_PLUGIN_ABI_VERSION;
    host.query_service = &Query;

    host.log = [](KeelPluginHandle, KeelLogLevel, const char* text)
    {
        std::cerr << text << '\n';
    };

    host.register_command = [](KeelPluginHandle, const KeelCommandSpec*, KeelCommandHandle*) -> KeelResult
    {
        return KEEL_RESULT_UNSUPPORTED;
    };

    host.unregister_command = [](KeelPluginHandle, KeelCommandHandle) -> KeelResult
    {
        return KEEL_RESULT_UNSUPPORTED;
    };
    Check(Adapter::Load(&host, 77), "load native plugin");
    auto& plugin = *Probe::current;
    Entity target, participant;
    Check(plugin.FindEntity(7,target) && plugin.FindEntity(7,participant),"live input participants");
    bool invoked{};
    std::uint32_t direct{}, queued{};
    Check(plugin.EntityInputCapabilities(direct, queued) && direct == 511 && queued == 383,
          "separate direct and queue masks");

    missing = true;
    Check(!plugin.EntityInputCapabilities(direct,queued) && !direct && !queued,"optional input service absence");
    Check(!target.AcceptInput("Enable", invoked) && !invoked && target.LastResult() == KEEL_RESULT_NOT_FOUND,
          "missing dispatch service");

    missing = false;

    for (unsigned fault = 0; fault < 4; ++fault) {
        const auto original = input_table;

        if (fault == 0)
            --input_table.size;

        if (fault == 1)
            ++input_table.api_version;

        if (fault == 2)
            input_table.capabilities = nullptr;

        if (fault == 3)
            input_table.dispatch = nullptr;

        Check(!plugin.EntityInputCapabilities(direct, queued) && !direct && !queued &&
                  plugin.LastResult() == KEEL_RESULT_INCOMPATIBLE,
              "incompatible optional API");

        Check(!target.AcceptInput("Enable", invoked) && !invoked && target.LastResult() == KEEL_RESULT_INCOMPATIBLE,
              "incompatible dispatch API");

        input_table = original;
    }

    direct_types |= 1u << 9;
    Check(!plugin.EntityInputCapabilities(direct, queued) && !direct && !queued, "unknown capability bits refused");
    direct_types = 511;
    caps_result = KEEL_RESULT_ENGINE_FAILURE;
    Check(!plugin.EntityInputCapabilities(direct, queued) && !direct && !queued, "failed capabilities clear outputs");
    caps_result = KEEL_RESULT_OK;
    on_caps = []
    {
        throw std::runtime_error("capability callback");
    };
    Check(!plugin.EntityInputCapabilities(direct,queued) && !direct && !queued,"throwing capabilities clear outputs");
    std::vector<EntityInputValue> values;
    values.emplace_back();
    values.emplace_back("payload");
    values.emplace_back(true);
    values.emplace_back(std::int32_t{-17});
    values.emplace_back(1.25f);
    values.emplace_back(Vector(1, 2, 3));
    values.emplace_back(QAngle(4, 5, 6));
    values.emplace_back(Color(10, 20, 30, 255));
    values.emplace_back(participant);

    for (unsigned type = 0; type < values.size(); ++type) {
        Check(target.AcceptInput("Enable",invoked,values[type],&participant,&target) && invoked &&
            observed_input.value.type == type && observed_input.activator && observed_input.caller &&
            (type == KEELS2_INPUT_ENTITY) == (observed_input.value_entity != 0),"typed direct inputs");

        if (type == KEELS2_INPUT_COLOR) Check(!target.QueueInput("Enable",1.25f,invoked,values[type]) && !invoked &&
            target.LastResult() == KEEL_RESULT_UNSUPPORTED,"queued colors refused");
        else Check(target.QueueInput("Enable",1.25f,invoked,values[type]) && invoked && observed_input.queued &&
            observed_input.delay == 1.25f,"typed queue inputs");
    }

    Check(target.AcceptInput("Enable",invoked) && target.QueueInput("Enable",0,invoked) && invoked &&
        observed_input.value.type == KEELS2_INPUT_VOID,"void convenience overloads");

    const auto before_invalid = input_calls;
    Check(!target.AcceptInput("",invoked) && !invoked && !target.AcceptInput("bad\ninput",invoked) &&
        !target.AcceptInput(std::string(128,'n').c_str(),invoked),"bounded non-control input names");

    Check(!target.AcceptInput("Enable", invoked, EntityInputValue(static_cast<const char*>(nullptr))) &&
              !target.AcceptInput("Enable", invoked, EntityInputValue(std::string(4096, 'x').c_str())) &&
              !target.AcceptInput("Enable", invoked, EntityInputValue(std::numeric_limits<float>::infinity())) &&
              !target.AcceptInput(
                  "Enable", invoked, EntityInputValue(Vector(0, std::numeric_limits<float>::quiet_NaN(), 0))),
          "bounded finite values");

    Check(!target.QueueInput("Enable", -1, invoked) &&
              !target.QueueInput("Enable", std::numeric_limits<float>::infinity(), invoked) && !invoked &&
              input_calls == before_invalid,
          "invalid values never dispatch");

    Check(target.AcceptInput("Enable", invoked, EntityInputValue(std::string(4095, 's').c_str())) &&
              observed_text.size() == 4095,
          "max string owns temporary source");

    wrong_thread = true;
    Check(!plugin.EntityInputCapabilities(direct, queued) && !direct && !queued &&
              !target.AcceptInput("Enable", invoked) && !invoked && target.LastResult() == KEEL_RESULT_WRONG_THREAD,
          "wrong thread refused");

    wrong_thread = false;

    Entity pending;
    Check(plugin.CreateEntity("prop_dynamic", pending), "pending input setup");
    Check(!pending.AcceptInput("Enable", invoked) && !invoked && !target.AcceptInput("Enable", invoked, &pending) &&
              !target.AcceptInput("Enable", invoked, EntityInputValue(pending)) && !invoked,
          "all pending participants refused");

    pending.Reset();

    Check(ForeignAdapter::Load(&host, 88), "second plugin context");
    auto& foreign_plugin = *ForeignProbe::current;
    Entity foreign;
    Check(foreign_plugin.FindEntity(7, foreign), "foreign participant");
    Check(!target.AcceptInput("Enable", invoked, &foreign) && !invoked &&
              target.LastResult() == KEEL_RESULT_INVALID_ARGUMENT &&
              !target.AcceptInput("Enable", invoked, EntityInputValue(foreign)),
          "cross-context participants refused");

    foreign.Reset();

    keels2::entities::Service foreign_service;
    Check(foreign_service.Connect(foreign_plugin.HostContext()) == KEEL_RESULT_OK,"capture foreign service context");
    on_caps = []
    {
        ForeignAdapter::Unload(88);
    };
    Check(foreign_service.InputCapabilities(direct, queued) == KEEL_RESULT_NOT_READY && !direct && !queued,
          "context unload during capabilities clears outputs");

    ++epoch;
    Check(!target.AcceptInput("Enable", invoked) && !invoked, "stale map refused");
    --epoch;
    EntityInputValue saved_entity(participant);
    participant.Reset();
    Check(!target.AcceptInput("Enable",invoked,saved_entity) && !invoked,"entity payload snapshot expires on Reset");
    Check(plugin.FindEntity(7,participant),"replace participant");
    Check(!target.AcceptInput("Enable",invoked,saved_entity) && !invoked,"saved payload cannot retarget replacement");
    auto payload_owner = std::make_unique<Entity>();
    Check(plugin.FindEntity(7, *payload_owner), "heap payload owner");
    EntityInputValue destroyed_payload(*payload_owner);
    payload_owner.reset();
    Check(!target.AcceptInput("Enable",invoked,destroyed_payload) && !invoked,"payload owner destruction is safe");
    EntityInputValue movable(participant);
    Entity moved(std::move(participant));
    Check(target.AcceptInput("Enable", invoked, movable) && invoked, "moving entity retains payload identity");
    participant = std::move(moved);
    std::string source = "copied text";
    EntityInputValue copy_source(source.c_str());
    source.assign(10000, 'x');
    EntityInputValue copied(copy_source);
    EntityInputValue moved_value(std::move(copied));
    copy_source = EntityInputValue{};
    Check(target.AcceptInput("Enable", invoked, moved_value) && observed_text == "copied text",
          "copied/moved payload owns its string");

    std::string name = "Enable";
    auto destroyed_value = std::make_unique<EntityInputValue>("copied text");
    on_input_query = [&]
    {
        name.assign(1000, 'x');
        destroyed_value.reset();
    };
    Check(target.AcceptInput(name.c_str(),invoked,*destroyed_value) && invoked && observed_name == "Enable" &&
        observed_text == "copied text" && !destroyed_value,"source name/value destruction during query is safe");

    const auto before_closures = input_calls;
    on_describe = [&]
    {
        participant.Reset();
        Check(plugin.FindEntity(7, participant), "metadata callback replaces participant");
    };
    Check(!target.AcceptInput("Enable", invoked, &participant) && !invoked && participant.Valid() &&
              participant.LastResult() == KEEL_RESULT_OK && input_calls == before_closures,
          "metadata replacement is not silently retargeted");

    on_input_query = [&]
    {
        Check(plugin.FindEntity(7, target), "query replaces target");
    };
    Check(!target.AcceptInput("Enable",invoked) && !invoked && target.Valid() && target.LastResult() == KEEL_RESULT_OK &&
        input_calls == before_closures,"query replacement retains its own status");

    auto deleted = std::make_unique<Entity>();
    Check(plugin.FindEntity(7, *deleted), "delete during metadata setup");
    on_describe = [&]
    {
        deleted.reset();
    };
    Check(!deleted->AcceptInput("Enable",invoked) && !invoked && !deleted,"metadata may destroy target wrapper");
    deleted = std::make_unique<Entity>();
    Check(plugin.FindEntity(7, *deleted), "delete during input setup");
    on_input = [&]
    {
        deleted.reset();
        participant.Reset();
    };
    Check(deleted->AcceptInput("Enable", invoked, &participant) && invoked && !deleted,
          "input callback destroys all wrappers");

    on_input = [&]
    {
        Check(plugin.FindEntity(7, target), "input callback installs replacement");
    };
    input_result = KEEL_RESULT_ENGINE_FAILURE;
    Check(!target.AcceptInput("Enable", invoked) && invoked && target.Valid() && target.LastResult() == KEEL_RESULT_OK,
          "old engine error does not overwrite replacement status");

    Check(!target.AcceptInput("Enable", invoked) && invoked && target.LastResult() == KEEL_RESULT_ENGINE_FAILURE,
          "invoked engine failure");

    input_marker = KEEL_FALSE;
    Check(!target.AcceptInput("Enable", invoked) && !invoked, "noninvoked failure");
    input_marker = 7;
    input_result = KEEL_RESULT_OK;
    Check(!target.AcceptInput("Enable", invoked) && invoked && target.LastResult() == KEEL_RESULT_INCOMPATIBLE,
          "unknown nonzero invocation preserved conservatively");

    input_marker = KEEL_TRUE;
    on_input = []
    {
        throw std::runtime_error("engine callback");
    };
    Check(!target.AcceptInput("Enable", invoked) && invoked && target.LastResult() == KEEL_RESULT_ENGINE_FAILURE,
          "exception preserves invocation");

    target.Reset();
    participant.Reset();
    values.clear();
    Check(records.empty(),"all native resources released");
    Adapter::Unload(77);
    Check(!target.AcceptInput("Enable",invoked) && !invoked,"unloaded context refuses input");
    std::cout << "Native entity input authoring passed\n";
}
