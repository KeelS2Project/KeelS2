#include <dyncall.h>
#include <dyncall_args.h>
#include <dyncall_callback.h>
#include <safetyhook/inline_hook.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
#include <pthread.h>
#endif

#if defined(_WIN32)
extern "C" __declspec(dllimport) std::int32_t KeelHookBackendTarget(std::int32_t value);
#else
extern "C" std::int32_t KeelHookBackendTarget(std::int32_t value);
#endif
extern "C" std::int32_t KeelHookBranchTarget(std::int32_t value);
extern "C" std::int32_t KeelHookLandingPadTarget(std::int32_t value);
extern "C" std::int32_t KeelHookUnsupportedBranchTarget(std::int32_t value);
extern "C" void KeelHookHazardLoop(std::atomic<bool>* entered, std::atomic<bool>* release);
extern "C" std::uint8_t KeelHookHazardLoopEnd;
extern "C" int KeelTest_DyncallbackBool();
#if defined(_WIN32)
extern "C" void dcCallback_x64_win64();
#else
extern "C" void dcCallback_x64_sysv();
#endif

namespace
{

using TargetFunction = std::int32_t (*)(std::int32_t);

std::atomic<void*> g_original{};
std::atomic<std::uint64_t> g_callbacks{};
constexpr std::array<std::uint8_t, 4> g_landing_pad{0xF3, 0x0F, 0x1E, 0xFA};

bool HasLandingPad(const void* address)
{
    const auto* bytes = static_cast<const std::uint8_t*>(address);
    return std::equal(g_landing_pad.begin(), g_landing_pad.end(), bytes);
}

DCsigchar Dispatch(DCCallback*, DCArgs* arguments, DCValue* result, void*)
{
    const std::int32_t value = dcbArgInt(arguments);
    DCCallVM* machine = dcNewCallVM(256);
    if (!machine)
    {
        result->i = 0;
        return DC_SIGCHAR_INT;
    }
    dcArgInt(machine, value);
    result->i = dcCallInt(machine, g_original.load(std::memory_order_acquire)) + 1000;
    dcFree(machine);
    g_callbacks.fetch_add(1, std::memory_order_relaxed);
    return DC_SIGCHAR_INT;
}

}

int main()
{
    if (KeelTest_DyncallbackBool() != 1)
    {
        std::fputs("dyncallback boolean argument decoding used indeterminate upper bytes\n", stderr);
        return 25;
    }
    DCCallback* closure = dcbNewCallback("i)i", &Dispatch, nullptr);
    if (!closure)
    {
        return 1;
    }
#if defined(_WIN32)
    const void* callback_entry = reinterpret_cast<const void*>(&dcCallback_x64_win64);
#else
    const void* callback_entry = reinterpret_cast<const void*>(&dcCallback_x64_sysv);
#endif
    if (!HasLandingPad(closure))
    {
        std::fputs("generated dyncall callback closure is missing ENDBR64\n", stderr);
        dcbFreeCallback(closure);
        return 18;
    }
    if (!HasLandingPad(callback_entry))
    {
        std::fputs("static dyncall callback entry is missing ENDBR64\n", stderr);
        dcbFreeCallback(closure);
        return 24;
    }
    auto created = safetyhook::InlineHook::create(
        reinterpret_cast<void*>(&KeelHookBackendTarget),
        static_cast<void*>(closure),
        safetyhook::InlineHook::StartDisabled);
    if (!created)
    {
        dcbFreeCallback(closure);
        return 2;
    }
    safetyhook::InlineHook hook = std::move(*created);
    g_original.store(hook.original<void*>(), std::memory_order_release);

    std::atomic<bool> stop{};
    std::atomic<bool> failed{};
    std::atomic<int> failure_code{};
    const std::size_t hardware_threads = std::max<std::size_t>(
        static_cast<std::size_t>(std::thread::hardware_concurrency()),
        2);
    const std::size_t worker_count = std::clamp<std::size_t>(
        hardware_threads / 2,
        2,
        6);
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    std::atomic<TargetFunction> target{&KeelHookBackendTarget};
    for (std::size_t index{}; index < worker_count; ++index)
    {
        workers.emplace_back([&, index] {
            std::int32_t value = static_cast<std::int32_t>(index);
            while (!stop.load(std::memory_order_acquire))
            {
                const std::int32_t base = value * 3 + 7;
                const std::int32_t actual = target.load(std::memory_order_acquire)(value);
                if (actual != base && actual != base + 1000)
                {
                    failure_code.store(4, std::memory_order_release);
                    failed.store(true, std::memory_order_release);
                    stop.store(true, std::memory_order_release);
                    break;
                }
                value = (value + 1) & 255;
            }
        });
    }

    constexpr std::size_t stress_iterations = 250;
    for (std::size_t iteration{};
         iteration < stress_iterations && !failed.load(std::memory_order_acquire);
         ++iteration)
    {
        if (!hook.enable())
        {
            failure_code.store(5, std::memory_order_release);
            failed.store(true, std::memory_order_release);
            break;
        }
        for (std::size_t call{}; call < 64; ++call)
        {
            const std::int32_t value = static_cast<std::int32_t>(call);
            if (target.load(std::memory_order_acquire)(value) != value * 3 + 1007)
            {
                failure_code.store(6, std::memory_order_release);
                failed.store(true, std::memory_order_release);
                break;
            }
        }
        if (!hook.disable())
        {
            failure_code.store(7, std::memory_order_release);
            failed.store(true, std::memory_order_release);
            break;
        }
        if ((iteration + 1) % 25 == 0)
        {
            std::printf(
                "KeelHook backend stress %zu/%zu (%zu workers)\n",
                iteration + 1,
                stress_iterations,
                worker_count);
            std::fflush(stdout);
        }
    }

    stop.store(true, std::memory_order_release);
    for (auto& worker : workers)
    {
        worker.join();
    }
    const bool result = !failed.load(std::memory_order_acquire) &&
        !hook.enabled() && g_callbacks.load(std::memory_order_acquire) != 0;
    hook.reset();
    if (!result)
    {
        dcbFreeCallback(closure);
        return failure_code.load(std::memory_order_acquire) == 0
            ? 3
            : failure_code.load(std::memory_order_acquire);
    }

    auto landing_created = safetyhook::InlineHook::create(
        reinterpret_cast<void*>(&KeelHookLandingPadTarget),
        static_cast<void*>(closure),
        safetyhook::InlineHook::StartDisabled);
    if (!landing_created)
    {
        dcbFreeCallback(closure);
        return 19;
    }
    safetyhook::InlineHook landing_hook = std::move(*landing_created);
    g_original.store(landing_hook.original<void*>(), std::memory_order_release);
    const void* landing_target = reinterpret_cast<const void*>(&KeelHookLandingPadTarget);
    if (landing_hook.target() != landing_target || !HasLandingPad(landing_target) ||
        !HasLandingPad(landing_hook.original<void*>()))
    {
        dcbFreeCallback(closure);
        return 20;
    }
    if (!landing_hook.enable() || !HasLandingPad(landing_target) ||
        KeelHookLandingPadTarget(42) != 1133)
    {
        dcbFreeCallback(closure);
        return 21;
    }
    if (!landing_hook.disable() || !HasLandingPad(landing_target) ||
        KeelHookLandingPadTarget(42) != 133)
    {
        dcbFreeCallback(closure);
        return 22;
    }
    landing_hook.reset();
    if (!HasLandingPad(landing_target) || KeelHookLandingPadTarget(42) != 133)
    {
        dcbFreeCallback(closure);
        return 23;
    }

    auto branch_created = safetyhook::InlineHook::create(
        reinterpret_cast<void*>(&KeelHookBranchTarget),
        static_cast<void*>(closure),
        safetyhook::InlineHook::StartDisabled);
    if (!branch_created)
    {
        dcbFreeCallback(closure);
        return 8;
    }
    safetyhook::InlineHook branch_hook = std::move(*branch_created);
    g_original.store(branch_hook.original<void*>(), std::memory_order_release);
    if (!branch_hook.enable() || KeelHookBranchTarget(42) != 1042 ||
        !branch_hook.disable() || KeelHookBranchTarget(42) != 42)
    {
        dcbFreeCallback(closure);
        return 9;
    }
    if (KeelHookUnsupportedBranchTarget(42) != 42)
    {
        dcbFreeCallback(closure);
        return 14;
    }
    auto unsupported = safetyhook::InlineHook::create(
        reinterpret_cast<void*>(&KeelHookUnsupportedBranchTarget),
        static_cast<void*>(closure),
        safetyhook::InlineHook::StartDisabled);
    if (unsupported || KeelHookUnsupportedBranchTarget(42) != 42)
    {
        dcbFreeCallback(closure);
        return 15;
    }

#if !defined(_WIN32)
    const auto blocked_worker = [](std::atomic<bool>& ready, std::atomic<bool>& release) {
        sigset_t signals;
        sigemptyset(&signals);
        for (int signal = SIGRTMIN; signal <= SIGRTMAX; ++signal)
        {
            sigaddset(&signals, signal);
        }
        if (pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0)
        {
            ready.store(true, std::memory_order_release);
            return;
        }
        ready.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        pthread_sigmask(SIG_UNBLOCK, &signals, nullptr);
    };
    std::atomic<bool> ready{};
    std::atomic<bool> release{};
    std::thread blocker(blocked_worker, std::ref(ready), std::ref(release));
    while (!ready.load(std::memory_order_acquire) && blocker.joinable())
    {
        std::this_thread::yield();
    }
    if (!ready.load(std::memory_order_acquire) || branch_hook.enable() ||
        branch_hook.enabled() || KeelHookBranchTarget(42) != 42)
    {
        release.store(true, std::memory_order_release);
        blocker.join();
        dcbFreeCallback(closure);
        return 10;
    }
    release.store(true, std::memory_order_release);
    blocker.join();
    if (!branch_hook.enable() || KeelHookBranchTarget(42) != 1042)
    {
        dcbFreeCallback(closure);
        return 11;
    }

    ready.store(false, std::memory_order_release);
    release.store(false, std::memory_order_release);
    blocker = std::thread(blocked_worker, std::ref(ready), std::ref(release));
    while (!ready.load(std::memory_order_acquire) && blocker.joinable())
    {
        std::this_thread::yield();
    }
    if (!ready.load(std::memory_order_acquire) || branch_hook.disable() ||
        !branch_hook.enabled() || KeelHookBranchTarget(42) != 1042)
    {
        release.store(true, std::memory_order_release);
        blocker.join();
        dcbFreeCallback(closure);
        return 12;
    }
    release.store(true, std::memory_order_release);
    blocker.join();
    if (!branch_hook.disable() || KeelHookBranchTarget(42) != 42)
    {
        dcbFreeCallback(closure);
        return 13;
    }
#endif
    std::atomic<bool> hazard_entered{};
    std::atomic<bool> hazard_release{};
    std::thread hazard_worker(&KeelHookHazardLoop, &hazard_entered, &hazard_release);
    while (!hazard_entered.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }
    auto* hazard_begin = reinterpret_cast<std::uint8_t*>(&KeelHookHazardLoop);
    const std::array hazard_ranges{safetyhook::IpRange{
        hazard_begin,
        static_cast<std::size_t>(&KeelHookHazardLoopEnd - hazard_begin)}};
    if (branch_hook.quiesce(hazard_ranges))
    {
        hazard_release.store(true, std::memory_order_release);
        hazard_worker.join();
        dcbFreeCallback(closure);
        return 16;
    }
    hazard_release.store(true, std::memory_order_release);
    hazard_worker.join();
    if (!branch_hook.quiesce(hazard_ranges))
    {
        dcbFreeCallback(closure);
        return 17;
    }
    branch_hook.reset();
    dcbFreeCallback(closure);
    return 0;
}
