#include <keels2/authoring.hpp>

namespace
{

class NativeConVarPlugin;
NativeConVarPlugin* plugin{};
keels2::ConVar<int32> value;
uint32 unload_count{};

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
        value = FindConVar<int32>("keels2_native_access");
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
        value = FindConVar<int32>("keels2_native_access");
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
