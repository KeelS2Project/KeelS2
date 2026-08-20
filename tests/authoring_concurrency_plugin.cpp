#include <keels2/keels2.hpp>

#include <atomic>
#include <cstdint>

namespace
{

std::atomic<bool> g_block_armed{};
std::atomic<bool> g_block_entered{};
std::atomic<bool> g_block_release{};
std::atomic<std::uint32_t> g_callback_count{};
std::atomic<std::uint32_t> g_unload_count{};

class AuthoringConcurrencyPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "Authoring Concurrency Test",
        "KeelS2",
        "0.5C",
        "Exercises facade lifecycle quiescence"
    };

    bool Load() override
    {
        g_block_armed.store(false, std::memory_order_release);
        g_block_entered.store(false, std::memory_order_release);
        g_block_release.store(false, std::memory_order_release);
        g_callback_count.store(0, std::memory_order_release);
        g_unload_count.store(0, std::memory_order_release);
        LogMessage("[Authoring Concurrency] loaded");
        return true;
    }

    void Unload() override
    {
        g_unload_count.fetch_add(1, std::memory_order_acq_rel);
        LogMessage("[Authoring Concurrency] unloaded after callback drain");
    }

    void OnGameFrame(bool, bool, bool) override
    {
        g_callback_count.fetch_add(1, std::memory_order_acq_rel);
        if (!g_block_armed.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }
        g_block_entered.store(true, std::memory_order_release);
        g_block_entered.notify_all();
        while (!g_block_release.load(std::memory_order_acquire))
        {
            g_block_release.wait(false, std::memory_order_acquire);
        }
    }
};

}

KEELS2_PLUGIN(AuthoringConcurrencyPlugin)

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_AuthoringArmBlock()
{
    g_block_release.store(false, std::memory_order_release);
    g_block_entered.store(false, std::memory_order_release);
    g_block_armed.store(true, std::memory_order_release);
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelTest_AuthoringBlockEntered()
{
    return g_block_entered.load(std::memory_order_acquire) ? KEEL_TRUE : KEEL_FALSE;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_AuthoringReleaseBlock()
{
    g_block_release.store(true, std::memory_order_release);
    g_block_release.notify_all();
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_AuthoringCallbackCount()
{
    return g_callback_count.load(std::memory_order_acquire);
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_AuthoringUnloadCount()
{
    return g_unload_count.load(std::memory_order_acquire);
}
