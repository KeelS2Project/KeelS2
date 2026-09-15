#include <keels2/authoring.hpp>

namespace
{

class NativeConVarPlugin;
NativeConVarPlugin* g_nativePlugin{};
keels2::ConVar<int32> g_nativeValue;
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
        g_nativePlugin = this;
#if defined(KEELS2_TEST_CONVAR_PROVIDER)
        g_nativeValue = CreateConVar<int32>("keels2_native_access", 7, "Native access contract");
#else
        g_nativeValue = FindConVar<int32>("keels2_native_access", &NativeConVarPlugin::Changed);
#endif
        int32 observed{};
        return g_nativeValue.WithNative([&observed](CConVarRef<int32>& native) {
            observed = native.Get();
        }) == KEEL_RESULT_OK && observed == 7;
    }

    void Unload() override
    {
        ++unload_count;
        g_nativePlugin = nullptr;
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
        return RemoveConVar(g_nativeValue);
    }

    bool Refresh()
    {
        if (g_nativeValue && !RemoveConVar(g_nativeValue))
        {
            return false;
        }
        g_nativeValue = FindConVar<int32>("keels2_native_access", &NativeConVarPlugin::Changed);
        return static_cast<bool>(g_nativeValue);
    }
};

}

KEELS2_PLUGIN(NativeConVarPlugin)

extern "C" KEELS2_PLUGIN_EXPORT KeelResult KeelTest_NativeConVarAccess(
    void (*during)(void*), void* context, int32* observed)
{
    return g_nativeValue.WithNative([&](CConVarRef<int32>& native) {
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
    return g_nativeValue.WithNative(read);
}

extern "C" KEELS2_PLUGIN_EXPORT KeelResult KeelTest_NativeConVarThrow()
{
    return g_nativeValue.WithNative([](CConVarRef<int32>&) { throw 42; });
}

extern "C" KEELS2_PLUGIN_EXPORT bool KeelTest_NativeConVarRemove()
{
    return g_nativePlugin && g_nativePlugin->Remove();
}

extern "C" KEELS2_PLUGIN_EXPORT bool KeelTest_NativeConVarRefresh()
{
    return g_nativePlugin && g_nativePlugin->Refresh();
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
    const bool changed = g_nativeValue.Set(23);
    *observed = g_nativeValue.Get();
    observer_action = nullptr;
    observer_context = nullptr;
    const bool restored = g_nativeValue.Set(7);
    return changed && restored && observer_valid && observer_count == before + 2
        ? KEEL_RESULT_OK : KEEL_RESULT_ENGINE_FAILURE;
}
