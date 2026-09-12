#include <keels2/authoring.hpp>

namespace
{

class NativeConVarPlugin;
NativeConVarPlugin* plugin{};
keels2::ConVar<int32> value;
uint32 unload_count{};
void (*observer_action)(void*){};
void* observer_context{};
bool observer_valid{true};
uint32 observer_count{};

class NativeConVarPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
#if defined(KEELS2_TEST_CONVAR_PROVIDER)
        "Native ConVar Provider",
#else
        "Native ConVar Consumer",
#endif
        "KeelS2 Tests", "1", "Scoped native ConVar lifetime contract"
    };

    bool Load() override
    {
        plugin = this;
#if defined(KEELS2_TEST_CONVAR_PROVIDER)
        value = CreateConVar<int32>("keels2_native_access", 7, "Native access contract");
#else
        value = FindConVar<int32>("keels2_native_access", &NativeConVarPlugin::Changed);
#endif
        int32 observed{};
        return value.WithNative([&observed](CConVarRef<int32>& native) {
            observed = native.Get();
        }) == KEEL_RESULT_OK && observed == 7;
    }

    void Unload() override
    {
        ++unload_count;
        plugin = nullptr;
    }

    void Changed(ConVar<int32>& convar, CSplitScreenSlot, int32 current, int32 previous)
    {
        ++observer_count;
        observer_valid = observer_valid && current != previous && convar.Get() == current;
        if (current == 23 && observer_action)
        {
            observer_action(observer_context);
        }
    }

    bool Remove()
    {
        return RemoveConVar(value);
    }

    bool Refresh()
    {
        if (value && !RemoveConVar(value))
        {
            return false;
        }
        value = FindConVar<int32>("keels2_native_access", &NativeConVarPlugin::Changed);
        return static_cast<bool>(value);
    }
};

}

KEELS2_PLUGIN(NativeConVarPlugin)

extern "C" KEELS2_PLUGIN_EXPORT KeelResult KeelTest_NativeConVarAccess(
    void (*during)(void*), void* context, int32* observed)
{
    return value.WithNative([&](CConVarRef<int32>& native) {
        const int32 original = native.Get();
        native.Set(23);
        if (during)
        {
            during(context);
        }
        *observed = native.Get();
        native.Set(original);
    });
}

extern "C" KEELS2_PLUGIN_EXPORT KeelResult KeelTest_NativeConVarAbstract(int32* observed)
{
    const auto read = [observed](ConVarRefAbstract& native) {
        *observed = native.GetAs<int32>();
    };
    return value.WithNative(read);
}

extern "C" KEELS2_PLUGIN_EXPORT KeelResult KeelTest_NativeConVarThrow()
{
    return value.WithNative([](CConVarRef<int32>&) { throw 42; });
}

extern "C" KEELS2_PLUGIN_EXPORT bool KeelTest_NativeConVarRemove()
{
    return plugin && plugin->Remove();
}

extern "C" KEELS2_PLUGIN_EXPORT bool KeelTest_NativeConVarRefresh()
{
    return plugin && plugin->Refresh();
}

extern "C" KEELS2_PLUGIN_EXPORT uint32 KeelTest_NativeConVarUnloads()
{
    return unload_count;
}

extern "C" KEELS2_PLUGIN_EXPORT KeelResult KeelTest_ConVarObservedChange(
    void (*during)(void*), void* context, int32* observed)
{
    observer_action = during;
    observer_context = context;
    const uint32 before = observer_count;
    const bool changed = value.Set(23);
    *observed = value.Get();
    observer_action = nullptr;
    observer_context = nullptr;
    const bool restored = value.Set(7);
    return changed && restored && observer_valid && observer_count == before + 2
        ? KEEL_RESULT_OK : KEEL_RESULT_ENGINE_FAILURE;
}
