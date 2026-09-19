#include <keels2/authoring.hpp>
#include <keels2/convar_access.h>
#include <keels2/convar_observe.h>

#include <cstring>
#include <string>
#include <string_view>

namespace docs
{
class ConVarAbi final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "Docs ConVar ABI", "KeelS2 documentation", "1.0.0",
        "ConVar values, callbacks, observations and protected native access"
    };

    bool Load() override
    {
        if (!Connect(KEELS2_CONVAR_SERVICE_NAME, KEELS2_CONVAR_API_VERSION, convars) ||
            !Connect(KEELS2_CONVAR_OBSERVE_SERVICE_NAME, KEELS2_CONVAR_OBSERVE_API_VERSION, observers) ||
            !Connect(KEELS2_CONVAR_ACCESS_SERVICE_NAME, KEELS2_CONVAR_ACCESS_API_VERSION, access) ||
            !Connect(KEELS2_SOURCE2_AUTHORING_SERVICE_NAME, KEELS2_SOURCE2_AUTHORING_API_VERSION, native) ||
            !convars->create || !convars->find || !convars->release || !convars->read ||
            !convars->queue_set || !convars->describe || !observers->observe ||
            !access->invoke || !native->create_convar) return false;

        g_pCVar = GetCVarSystem<ICvar>();

        if (!g_pCVar)
            return false;

        KeelConVarSpec count = Spec("keel_docs_abi_count", Integer(1));
        count.flags = KEELS2_CVAR_FLAG_NOTIFY;
        count.has_minimum = KEEL_TRUE;
        count.minimum_value = Integer(1);
        count.has_maximum = KEEL_TRUE;
        count.maximum_value = Integer(5);
        count.callback = [](const KeelConVarChange* change, void* state) {
            static_cast<ConVarAbi*>(state)->Changed(change);
        };
        count.user_data = this;

        if (!Check(convars->create(Owner(), &count, &count_handle), "Create count"))
            return false;

        const KeelConVarSpec text = Spec("keel_docs_abi_text", Text("Ready"));

        if (!Check(convars->create(Owner(), &text, &text_handle), "Create text"))
            return false;

        const KeelConVarSpec native_count = Spec("keel_docs_abi_native", Integer(0));
        void* borrowed_native{};

        if (!Check(native->create_convar(
                       Owner(),
                       &native_count,
                       [](void*, int32_t slot, const void* next, const void* previous, void* state)
                       {
                           if (!next || !previous)
                               return;

                           auto& self = *static_cast<ConVarAbi*>(state);
                           self.LogMessage("Native count [{}]: {} -> {}",
                                           slot,
                                           *static_cast<const int32_t*>(previous),
                                           *static_cast<const int32_t*>(next));
                       },
                       this,
                       &native_handle,
                       &borrowed_native),
                   "Create native count"))
            return false;

        if (!Check(convars->find(Owner(), count.name, KEELS2_CONVAR_INT32, &watched_handle), "Find count"))
            return false;

        if (!Check(observers->observe(
                       Owner(),
                       watched_handle,
                       [](const KeelConVarChange* change, void* state)
                       {
                           if (!change || change->size != sizeof(*change))
                               return;

                           auto& self = *static_cast<ConVarAbi*>(state);
                           self.LogMessage(
                               "Observed {} on handle {} at slot {}", change->name, change->convar, change->slot);
                       },
                       this),
                   "Observe count"))
            return false;

        return CreateCommand("keel_docs_convars", "Inspect and update the ConVar examples", &ConVarAbi::Command);
    }

    void Unload() override
    {
        count_handle = text_handle = native_handle = watched_handle = 0;
        convars = nullptr;
        observers = nullptr;
        access = nullptr;
        native = nullptr;
        nested = false;
    }

private:
    template <typename Api>
    bool Connect(const char* name, uint32_t version, const Api*& api)
    {
        const void* service{};

        if (!Check(HostContext().QueryService(name, version, &service), name))
            return false;

        api = static_cast<const Api*>(service);
        return api && api->size == sizeof(Api) && api->api_version == version;
    }

    KeelPluginHandle Owner() const
    {
        return HostContext().PluginHandle();
    }

    bool Check(KeelResult result, const char* operation)
    {
        if (result != KEEL_RESULT_OK)
            LogError("{}: result {}", operation, result);

        return result == KEEL_RESULT_OK;
    }

    static KeelConVarValue Integer(int32_t value)
    {
        return {sizeof(KeelConVarValue), KEELS2_CONVAR_INT32, {.int32_value = value}};
    }

    static KeelConVarValue Text(const char* value)
    {
        return {sizeof(KeelConVarValue), KEELS2_CONVAR_STRING, {.string_value = value}};
    }

    static KeelConVarSpec Spec(const char* name, KeelConVarValue value)
    {
        KeelConVarSpec spec{};
        spec.size = sizeof(spec);
        spec.type = value.type;
        spec.name = name;
        spec.description = "ConVar service example";
        spec.default_value = value;
        return spec;
    }

    void Set(KeelConVarHandle handle, const KeelConVarValue& value)
    {
        Check(convars->queue_set(Owner(), handle, KEELS2_CONVAR_GLOBAL_SLOT, &value), "Set value");
    }

    bool Read(KeelConVarHandle handle, KeelConVarValue& value)
    {
        value = {};
        value.size = sizeof(value);
        return Check(convars->read(Owner(), handle, KEELS2_CONVAR_GLOBAL_SLOT, &value), "Read value");
    }

    void Changed(const KeelConVarChange* change)
    {
        if (!change || change->size != sizeof(*change) ||
            change->old_value.type != KEELS2_CONVAR_INT32 || change->new_value.type != KEELS2_CONVAR_INT32)
            return;

        LogMessage("Count: {} -> {}", change->old_value.value.int32_value, change->new_value.value.int32_value);

        if (nested && change->new_value.value.int32_value == 2)
        {
            nested = false;
            LogMessage("Release during callback: {}", convars->release(Owner(), count_handle));
            Set(count_handle, Integer(3));
        }
    }

    void Show()
    {
        KeelConVarValue count{}, text{};

        if (!Read(count_handle, count) || count.type != KEELS2_CONVAR_INT32 ||
            !Read(text_handle, text) || text.type != KEELS2_CONVAR_STRING || !text.value.string_value) return;

        const std::string copied_text(text.value.string_value);
        KeelConVarInfo info{};
        info.size = sizeof(info);

        if (!Check(convars->describe(Owner(), count_handle, &info), "Describe count"))
            return;

        LogMessage("{} = {}; text = {}", info.name, count.value.int32_value, copied_text);

        if (info.has_minimum == KEEL_TRUE && info.has_maximum == KEEL_TRUE)
            LogMessage("Bounds: {}..{}; flags {}", info.minimum_value.value.int32_value,
                info.maximum_value.value.int32_value, info.flags);
    }

    void Inspect()
    {
        Check(access->invoke(
                  Owner(),
                  count_handle,
                  [](const void* reference, void* state) -> KeelResult
                  {
                      if (!reference || !g_pCVar)
                          return KEEL_RESULT_NOT_READY;

                      ConVarRef handle;
                      std::memcpy(&handle, reference, sizeof(handle));
                      CConVarRef<int32_t> value(handle);

                      if (!value.IsValidRef() || !value.IsConVarDataAvailable() ||
                          value.GetType() != TranslateConVarType<int32_t>())
                          return KEEL_RESULT_INCOMPATIBLE;

                      static_cast<ConVarAbi*>(state)->LogMessage("Native read: {}", value.Get());
                      return KEEL_RESULT_OK;
                  },
                  this),
              "Native access");
    }

    void Retire()
    {
        for (KeelConVarHandle* handle : {&watched_handle, &native_handle, &text_handle, &count_handle})
        {
            if (!*handle)
                continue;

            const KeelResult result = convars->release(Owner(), *handle);

            if (result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND)
                *handle = 0;
            else Check(result, "Release ConVar");
        }

        LogMessage("ConVar registrations remaining: {}",
            (count_handle != 0) + (text_handle != 0) + (native_handle != 0) + (watched_handle != 0));
    }

    void Command(const CCommandContext& context, const CCommand& command)
    {
        if (context.GetPlayerSlot().Get() != -1)
            return;

        if (command.ArgC() != 2)
        {
            LogMessage("Usage: keel_docs_convars <show|set|nested|inspect|retire>");
            return;
        }

        const std::string_view action(command[1]);

        if (action == "show")
            Show();

        else if (action == "set")
        {
            Set(count_handle, Integer(9));
            Set(text_handle, Text("Updated"));
            Set(native_handle, Integer(4));
        }
        else if (action == "nested")
        {
            nested = true;
            Set(count_handle, Integer(2));
            nested = false;
        }
        else if (action == "inspect") Inspect();
        else if (action == "retire") Retire();
    }

    const KeelConVarApi* convars{};
    const KeelConVarObserveApi* observers{};
    const KeelConVarAccessApi* access{};
    const KeelSource2AuthoringApi* native{};
    KeelConVarHandle count_handle{}, text_handle{}, native_handle{}, watched_handle{};
    bool nested{};
};
}

KEELS2_PLUGIN(docs::ConVarAbi)
