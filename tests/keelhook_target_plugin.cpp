#include <keels2/keelhook.hpp>
#include <keels2/plugin.h>

#include "keelhook_virtual_fixture.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
#include <pthread.h>
#endif

#if defined(_MSC_VER)
#define KEELHOOK_NOINLINE __declspec(noinline)
#else
#define KEELHOOK_NOINLINE __attribute__((noinline))
#endif

extern "C" KEELS2_PLUGIN_EXPORT std::int32_t KeelHookFixtureTarget(
    std::int32_t left,
    std::int32_t right);
extern "C" KEELS2_PLUGIN_EXPORT std::int32_t KeelHookPauseFixtureTarget(
    std::int32_t value);

template <>
struct keels2::kh::AggregateTraits<KeelHookFixtureCoordinates>
{
    static consteval auto Fields()
    {
        return keels2::kh::Fields(
            keels2::kh::Field<std::int32_t>(offsetof(KeelHookFixtureCoordinates, integer)),
            keels2::kh::Field<float>(offsetof(KeelHookFixtureCoordinates, fractional)));
    }
};

template <>
struct keels2::kh::AggregateTraits<KeelHookFixtureAggregate>
{
    static consteval auto Fields()
    {
        return keels2::kh::Fields(
            keels2::kh::Field<KeelHookFixtureCoordinates>(
                offsetof(KeelHookFixtureAggregate, coordinates)),
            keels2::kh::Field<std::uint64_t>(offsetof(KeelHookFixtureAggregate, marker)));
    }
};

namespace
{

const KeelHostApi* g_host{};
const KeelHookApi* g_hook{};
KeelPluginHandle g_plugin{};
KeelHookTargetHandle g_target{};
KeelHookCallbackHandle g_high{};
KeelHookCallbackHandle g_override{};
KeelHookCallbackHandle g_low{};
KeelHookCallbackHandle g_self{};
KeelHookCallbackHandle g_cleanup{};
KeelHookTargetHandle g_pause_target{};
KeelHookCallbackHandle g_pause_callback{};
KeelHookTargetHandle g_virtual_shared_target{};
KeelHookCallbackHandle g_virtual_shared_callback{};
std::atomic<std::uint64_t> g_original_calls{};
std::atomic<std::int32_t> g_last_left{};
std::atomic<std::int32_t> g_last_right{};
std::atomic<std::uint32_t> g_self_calls{};
std::atomic<std::uint32_t> g_pause_calls{};
std::atomic<bool> g_capture{};
std::atomic<bool> g_run{};
std::atomic<bool> g_after_peer{};
std::atomic<bool> g_load_complete{};
std::atomic<bool> g_recursive_entry{};
std::atomic<std::int32_t> g_recursive_result{};
std::atomic<bool> g_cleanup_delay{};
std::atomic<bool> g_cleanup_entered{};
std::atomic<bool> g_shutdown_retry_armed{};
std::thread g_unload_worker;
std::thread g_shutdown_worker;
std::mutex g_order_mutex;
std::vector<std::uint32_t> g_order;

void Log(KeelLogLevel level, const char* message)
{
    if (g_host && g_host->log)
    {
        g_host->log(g_plugin, level, message);
    }
}

void Record(std::uint32_t value)
{
    if (!g_capture.load(std::memory_order_acquire))
    {
        return;
    }
    std::scoped_lock lock(g_order_mutex);
    g_order.push_back(value);
}

void* FunctionAddress(auto function)
{
    static_assert(sizeof(function) == sizeof(void*));
    void* address{};
    std::memcpy(&address, &function, sizeof(address));
    return address;
}

bool AddCallback(
    KeelHookTargetHandle target,
    KeelHookCallback callback,
    std::uint32_t phases,
    std::int32_t priority,
    KeelHookCallbackHandle& handle)
{
    const KeelHookCallbackSpec spec{
        sizeof(KeelHookCallbackSpec),
        phases,
        priority,
        0,
        callback,
        nullptr
    };
    return g_hook->add_callback(g_plugin, target, &spec, &handle) == KEEL_RESULT_OK;
}

bool AddCallback(
    KeelHookCallback callback,
    std::uint32_t phases,
    std::int32_t priority,
    KeelHookCallbackHandle& handle)
{
    return AddCallback(g_target, callback, phases, priority, handle);
}

KeelHookAction HighCallback(KeelHookFrame* frame, void*)
{
    Record(2);
    if (!frame || frame->phase != KH_PHASE_PRE || frame->argument_count != 2)
    {
        return KH_ACTION_CONTINUE;
    }
    const auto value = keels2::kh::Read<std::int32_t>(frame->arguments[0]);
    keels2::kh::Write(frame->arguments[0], value + 1);
    return KH_ACTION_CONTINUE;
}

KeelHookAction OverrideCallback(KeelHookFrame* frame, void*)
{
    if (!frame)
    {
        return KH_ACTION_CONTINUE;
    }
    if (frame->phase == KH_PHASE_PRE)
    {
        Record(3);
        const auto left = keels2::kh::Read<std::int32_t>(frame->arguments[0]);
        if (left == 9001 || left == 12001)
        {
            return KH_ACTION_CONTINUE;
        }
        keels2::kh::Write(frame->result, std::int32_t{500});
        return KH_ACTION_OVERRIDE;
    }
    Record(6);
    const auto left = keels2::kh::Read<std::int32_t>(frame->arguments[0]);
    if (left == 7001 || left == 9001 || left == 10001 || left == 12001)
    {
        return KH_ACTION_CONTINUE;
    }
    keels2::kh::Write(frame->result, std::int32_t{700});
    return KH_ACTION_OVERRIDE;
}

KeelHookAction LowCallback(KeelHookFrame* frame, void*)
{
    Record(frame && frame->phase == KH_PHASE_POST ? 5u : 4u);
    return KH_ACTION_CONTINUE;
}

KeelHookAction SelfRemovingCallback(KeelHookFrame*, void*)
{
    Record(1);
    g_self_calls.fetch_add(1, std::memory_order_relaxed);
    if (g_hook->remove_callback(g_plugin, g_self) != KEEL_RESULT_OK)
    {
        Log(KEEL_LOG_ERROR, "self-removing callback failed");
    }
    return KH_ACTION_CONTINUE;
}

KeelHookAction RecursiveCallback(KeelHookFrame* frame, void*)
{
    if (!frame || frame->phase != KH_PHASE_PRE || frame->argument_count != 2 ||
        keels2::kh::Read<std::int32_t>(frame->arguments[0]) != 50 ||
        g_recursive_entry.exchange(true, std::memory_order_acq_rel))
    {
        return KH_ACTION_CONTINUE;
    }
    g_recursive_result.store(KeelHookFixtureTarget(1, 1), std::memory_order_release);
    g_recursive_entry.store(false, std::memory_order_release);
    return KH_ACTION_CONTINUE;
}

KeelHookAction ActionMatrixCallback(KeelHookFrame* frame, void*)
{
    if (!frame || frame->argument_count != 2)
    {
        return KH_ACTION_CONTINUE;
    }
    const auto left = keels2::kh::Read<std::int32_t>(frame->arguments[0]);
    if (frame->phase == KH_PHASE_PRE && (left == 7000 || left == 11000))
    {
        keels2::kh::Write(frame->result, std::int32_t{1234});
        return KH_ACTION_SUPERSEDE;
    }
    if (frame->phase == KH_PHASE_PRE && left == 9000)
    {
        keels2::kh::Write(frame->result, std::int32_t{4444});
    }
    if (frame->phase == KH_PHASE_POST && left == 12001)
    {
        keels2::kh::Write(frame->result, std::int32_t{8888});
    }
    return KH_ACTION_CONTINUE;
}

KeelHookAction CleanupCallback(KeelHookFrame* frame, void* user_data)
{
    if (g_cleanup_delay.exchange(false, std::memory_order_acq_rel))
    {
        g_cleanup_entered.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(75));
        Log(KEEL_LOG_INFO, "concurrent callback retained host API access during unload");
    }
    return HighCallback(frame, user_data);
}

KeelHookAction PauseCallback(KeelHookFrame*, void*)
{
    g_pause_calls.fetch_add(1, std::memory_order_acq_rel);
    return KH_ACTION_CONTINUE;
}

KeelHookAction VirtualCallback(KeelHookFrame* frame, void*)
{
    if (!frame || frame->argument_count != 2)
    {
        return KH_ACTION_CONTINUE;
    }
    if (frame->phase == KH_PHASE_PRE)
    {
        const auto value = keels2::kh::Read<std::int32_t>(frame->arguments[1]);
        keels2::kh::Write(frame->arguments[1], value + 5);
        return KH_ACTION_CONTINUE;
    }
    const auto result = keels2::kh::Read<std::int32_t>(frame->result);
    keels2::kh::Write(frame->result, result + 1000);
    return KH_ACTION_OVERRIDE;
}

KeelHookAction AggregateVirtualCallback(KeelHookFrame* frame, void*)
{
    if (!frame || frame->argument_count != 2)
    {
        return KH_ACTION_CONTINUE;
    }
    if (frame->phase == KH_PHASE_PRE)
    {
        auto value = keels2::kh::Read<KeelHookFixtureAggregate>(frame->arguments[1]);
        value.coordinates.integer += 5;
        value.coordinates.fractional += 1.0F;
        value.marker += 10;
        keels2::kh::Write(frame->arguments[1], value);
        keels2::kh::Write(
            frame->result,
            KeelHookFixtureAggregate{{9000, 9000.0F}, 9000});
        return KH_ACTION_CONTINUE;
    }
    auto result = keels2::kh::Read<KeelHookFixtureAggregate>(frame->result);
    result.coordinates.integer += 1000;
    result.coordinates.fractional += 10.0F;
    result.marker += 10000;
    keels2::kh::Write(frame->result, result);
    return KH_ACTION_OVERRIDE;
}

bool EqualAggregate(
    const KeelHookFixtureAggregate& value,
    std::int32_t integer,
    float fractional,
    std::uint64_t marker)
{
    return value.coordinates.integer == integer &&
        value.coordinates.fractional == fractional && value.marker == marker;
}

bool RunVirtualTests()
{
    void* first = KeelHookVirtualFixtureFirst();
    void* second = KeelHookVirtualFixtureSecond();
    const auto& active_prototype =
        keels2::kh::Prototype<std::int32_t(void*, std::int32_t)>::value;
    const KeelHookVirtualTargetSpec active_shared{
        sizeof(KeelHookVirtualTargetSpec),
        KH_MECHANISM_VIRTUAL,
        0,
        0,
        0,
        0,
        first,
        nullptr
    };
    KeelHookTargetHandle active_alias{};
    if (g_hook->resolve_virtual_target(
            g_plugin,
            &active_shared,
            &active_prototype,
            &active_alias) != KEEL_RESULT_OK ||
        active_alias != g_virtual_shared_target)
    {
        return false;
    }
    if (KeelHookVirtualFixtureCallFirst(first, 5) != 1110 ||
        KeelHookVirtualFixtureCallFirst(second, 5) != 1210 ||
        KeelHookVirtualFixtureCallSecond(first, 5) != 205)
    {
        return false;
    }
    if (g_hook->remove_callback(g_plugin, g_virtual_shared_callback) != KEEL_RESULT_OK)
    {
        return false;
    }
    g_virtual_shared_callback = 0;
    if (KeelHookVirtualFixtureCallFirst(first, 5) != 105 ||
        KeelHookVirtualFixtureCallFirst(second, 5) != 205 ||
        g_hook->release_target(g_plugin, g_virtual_shared_target) != KEEL_RESULT_OK)
    {
        return false;
    }
    g_virtual_shared_target = 0;

    const KeelHookFixtureAggregate aggregate_input{{5, 1.0F}, 10};
    if (!EqualAggregate(
            KeelHookVirtualFixtureCallAggregate(first, aggregate_input),
            105,
            2.0F,
            110) ||
        !EqualAggregate(
            KeelHookVirtualFixtureCallAggregate(second, aggregate_input),
            205,
            3.0F,
            210))
    {
        return false;
    }
    const auto& aggregate_prototype =
        keels2::kh::MethodPrototype<KeelHookFixtureAggregate(KeelHookFixtureAggregate)>::value;
    KeelHookVirtualTargetSpec aggregate_shared{
        sizeof(KeelHookVirtualTargetSpec),
        KH_MECHANISM_VIRTUAL,
        0,
        2,
        0,
        0,
        first,
        nullptr
    };
    KeelHookTargetHandle aggregate_rejected{};
    KeelHookPrototype missing_aggregate = aggregate_prototype;
    missing_aggregate.return_aggregate = nullptr;
    if (g_hook->resolve_virtual_target(
            g_plugin,
            &aggregate_shared,
            &missing_aggregate,
            &aggregate_rejected) != KEEL_RESULT_INVALID_ARGUMENT ||
        aggregate_rejected)
    {
        return false;
    }
    KeelHookAggregate invalid_aggregate = *aggregate_prototype.return_aggregate;
    invalid_aggregate.byte_size = 0;
    KeelHookPrototype malformed_aggregate = aggregate_prototype;
    malformed_aggregate.return_aggregate = &invalid_aggregate;
    if (g_hook->resolve_virtual_target(
            g_plugin,
            &aggregate_shared,
            &malformed_aggregate,
            &aggregate_rejected) != KEEL_RESULT_INVALID_ARGUMENT ||
        aggregate_rejected)
    {
        return false;
    }
    KeelHookTargetHandle aggregate_target{};
    KeelHookCallbackHandle aggregate_callback{};
    if (g_hook->resolve_virtual_target(
            g_plugin,
            &aggregate_shared,
            &aggregate_prototype,
            &aggregate_target) != KEEL_RESULT_OK ||
        !aggregate_target ||
        !AddCallback(
            aggregate_target,
            &AggregateVirtualCallback,
            KH_PHASE_BOTH,
            0,
            aggregate_callback) ||
        !EqualAggregate(
            KeelHookVirtualFixtureCallAggregate(first, aggregate_input),
            1110,
            13.0F,
            10120) ||
        !EqualAggregate(
            KeelHookVirtualFixtureCallAggregate(second, aggregate_input),
            1210,
            14.0F,
            10220) ||
        g_hook->remove_callback(g_plugin, aggregate_callback) != KEEL_RESULT_OK ||
        g_hook->release_target(g_plugin, aggregate_target) != KEEL_RESULT_OK ||
        !EqualAggregate(
            KeelHookVirtualFixtureCallAggregate(first, aggregate_input),
            105,
            2.0F,
            110))
    {
        return false;
    }

#if !defined(KEELHOOK_FIXTURE_PROFILE)
#error KEELHOOK_FIXTURE_PROFILE must be defined
#endif
    const char* profile = KEELHOOK_FIXTURE_PROFILE;
    const auto& prototype =
        keels2::kh::Prototype<std::int32_t(void*, std::int32_t)>::value;
    KeelHookVirtualTargetSpec instance{
        sizeof(KeelHookVirtualTargetSpec),
        KH_MECHANISM_VIRTUAL_INSTANCE,
        0,
        0,
        3,
        0,
        first,
        profile
    };
    KeelHookTargetHandle first_target{};
    if (g_hook->resolve_virtual_target(g_plugin, &instance, &prototype, &first_target) !=
            KEEL_RESULT_OK ||
        !first_target)
    {
        return false;
    }
    KeelHookVirtualTargetSpec incompatible = instance;
    incompatible.index = 1;
    incompatible.table_size = 4;
    KeelHookTargetHandle rejected{};
    if (g_hook->resolve_virtual_target(g_plugin, &incompatible, &prototype, &rejected) !=
            KEEL_RESULT_INCOMPATIBLE ||
        rejected)
    {
        return false;
    }
    KeelHookVirtualTargetSpec shared{
        sizeof(KeelHookVirtualTargetSpec),
        KH_MECHANISM_VIRTUAL,
        0,
        0,
        0,
        0,
        second,
        profile
    };
    if (g_hook->resolve_virtual_target(g_plugin, &shared, &prototype, &rejected) !=
            KEEL_RESULT_BUSY ||
        rejected)
    {
        return false;
    }

    KeelHookCallbackHandle first_callback{};
    if (!AddCallback(
            first_target,
            &VirtualCallback,
            KH_PHASE_BOTH,
            0,
            first_callback))
    {
        return false;
    }
    instance.index = 1;
    KeelHookTargetHandle second_target{};
    if (g_hook->resolve_virtual_target(g_plugin, &instance, &prototype, &second_target) !=
            KEEL_RESULT_OK ||
        !second_target || second_target == first_target)
    {
        return false;
    }
    KeelHookCallbackHandle second_callback{};
    if (!AddCallback(
            second_target,
            &VirtualCallback,
            KH_PHASE_BOTH,
            0,
            second_callback))
    {
        return false;
    }
    if (KeelHookVirtualFixtureCallFirst(first, 5) != 1110 ||
        KeelHookVirtualFixtureCallFirst(second, 5) != 205 ||
        KeelHookVirtualFixtureCallSecond(first, 5) != 1210)
    {
        return false;
    }
    if (g_hook->remove_callback(g_plugin, first_callback) != KEEL_RESULT_OK ||
        KeelHookVirtualFixtureCallFirst(first, 5) != 105 ||
        KeelHookVirtualFixtureCallSecond(first, 5) != 1210)
    {
        return false;
    }
    if (g_hook->remove_callback(g_plugin, second_callback) != KEEL_RESULT_OK ||
        KeelHookVirtualFixtureCallFirst(first, 5) != 105 ||
        KeelHookVirtualFixtureCallSecond(first, 5) != 205 ||
        g_hook->release_target(g_plugin, first_target) != KEEL_RESULT_OK ||
        g_hook->release_target(g_plugin, second_target) != KEEL_RESULT_OK)
    {
        return false;
    }
    return true;
}

bool ValidateOrder()
{
    const std::array<std::uint32_t, 6> expected{1, 2, 3, 4, 5, 6};
    std::scoped_lock lock(g_order_mutex);
    return g_order.size() == expected.size() &&
        std::equal(g_order.begin(), g_order.end(), expected.begin());
}

void RunCommand(const KeelCommandInvocation*, void*)
{
    if (g_run.exchange(true, std::memory_order_acq_rel))
    {
        Log(KEEL_LOG_ERROR, "KeelHook integration command ran twice");
        return;
    }
    {
        std::scoped_lock lock(g_order_mutex);
        g_order.clear();
    }
    g_capture.store(true, std::memory_order_release);
    const std::uint64_t first_before = g_original_calls.load(std::memory_order_acquire);
    const std::int32_t first_result = KeelHookFixtureTarget(3, 4);
    g_capture.store(false, std::memory_order_release);
    if (first_result != 900 || g_original_calls.load(std::memory_order_acquire) != first_before + 1 ||
        g_last_left.load(std::memory_order_acquire) != 4 ||
        g_last_right.load(std::memory_order_acquire) != 6 ||
        g_self_calls.load(std::memory_order_acquire) != 1 || !ValidateOrder())
    {
        Log(KEEL_LOG_ERROR, "KeelHook shared callback ordering failed");
        return;
    }
    if (KeelHookFixtureTarget(5, 6) != 900 || g_self_calls.load(std::memory_order_acquire) != 1)
    {
        Log(KEEL_LOG_ERROR, "KeelHook self-removal failed");
        return;
    }
    if (!RunVirtualTests())
    {
        Log(KEEL_LOG_ERROR, "KeelHook virtual target integration failed");
        return;
    }
    if (KeelHookPauseFixtureTarget(5) != 15 ||
        g_pause_calls.load(std::memory_order_acquire) != 1)
    {
        Log(KEEL_LOG_ERROR, "pause admission fixture did not dispatch");
        return;
    }

    std::atomic<bool> thread_failure{};
    std::array<std::thread, 4> workers;
    for (std::size_t thread{}; thread < workers.size(); ++thread)
    {
        workers[thread] = std::thread([&, thread] {
            for (std::int32_t call{}; call < 500; ++call)
            {
                if (KeelHookFixtureTarget(static_cast<std::int32_t>(thread) + call, call) != 900)
                {
                    thread_failure.store(true, std::memory_order_release);
                    return;
                }
            }
        });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }
    if (thread_failure.load(std::memory_order_acquire))
    {
        Log(KEEL_LOG_ERROR, "KeelHook concurrent dispatch failed");
        return;
    }

    KeelHookCallbackHandle recursive{};
    if (!AddCallback(&RecursiveCallback, KH_PHASE_PRE, 150, recursive))
    {
        Log(KEEL_LOG_ERROR, "KeelHook recursive callback registration failed");
        return;
    }
    const std::uint64_t recursive_before = g_original_calls.load(std::memory_order_acquire);
    const bool recursion_passed = KeelHookFixtureTarget(50, 1) == 900 &&
        g_recursive_result.load(std::memory_order_acquire) == 900 &&
        g_original_calls.load(std::memory_order_acquire) == recursive_before + 2;
    if (g_hook->remove_callback(g_plugin, recursive) != KEEL_RESULT_OK || !recursion_passed)
    {
        Log(KEEL_LOG_ERROR, "KeelHook recursive dispatch failed");
        return;
    }

    KeelHookCallbackHandle action_matrix{};
    if (!AddCallback(&ActionMatrixCallback, KH_PHASE_BOTH, 300, action_matrix))
    {
        Log(KEEL_LOG_ERROR, "KeelHook action-matrix callback registration failed");
        return;
    }
    struct ActionCase
    {
        std::int32_t left;
        std::int32_t result;
        std::uint64_t original_calls;
    };
    const std::array<ActionCase, 5> action_cases{{
        {7000, 1234, 0},
        {9000, 90013, 1},
        {10000, 500, 1},
        {11000, 900, 0},
        {12000, 120013, 1}
    }};
    for (const auto& action_case : action_cases)
    {
        const auto before = g_original_calls.load(std::memory_order_acquire);
        const auto actual_result = KeelHookFixtureTarget(action_case.left, 1);
        const auto actual_calls = g_original_calls.load(std::memory_order_acquire) - before;
        if (actual_result != action_case.result || actual_calls != action_case.original_calls)
        {
            char message[192]{};
            std::snprintf(
                message,
                sizeof(message),
                "KeelHook action semantics failed for %d: result %d/%d, original calls %llu/%llu",
                action_case.left,
                actual_result,
                action_case.result,
                static_cast<unsigned long long>(actual_calls),
                static_cast<unsigned long long>(action_case.original_calls));
            Log(KEEL_LOG_ERROR, message);
            return;
        }
    }
    if (g_hook->remove_callback(g_plugin, action_matrix) != KEEL_RESULT_OK)
    {
        Log(KEEL_LOG_ERROR, "KeelHook action-matrix callback removal failed");
        return;
    }
    Log(KEEL_LOG_INFO, "detour, virtual scopes, aggregate calls, ordering, recursion, action semantics, and concurrency passed");
}

void AfterPeerCommand(const KeelCommandInvocation*, void*)
{
    if (!g_run.load(std::memory_order_acquire) ||
        g_after_peer.exchange(true, std::memory_order_acq_rel))
    {
        Log(KEEL_LOG_ERROR, "KeelHook peer cleanup command was out of order");
        return;
    }
    if (KeelHookFixtureTarget(2, 3) != 700)
    {
        Log(KEEL_LOG_ERROR, "peer callback survived peer unload");
        return;
    }
    if (g_hook->remove_callback(g_plugin, g_high) != KEEL_RESULT_OK ||
        g_hook->remove_callback(g_plugin, g_override) != KEEL_RESULT_OK ||
        g_hook->remove_callback(g_plugin, g_low) != KEEL_RESULT_OK)
    {
        Log(KEEL_LOG_ERROR, "target callback cleanup failed");
        return;
    }
    if (KeelHookFixtureTarget(2, 3) != 23)
    {
        Log(KEEL_LOG_ERROR, "last callback removal did not restore the target");
        return;
    }
    if (!AddCallback(&CleanupCallback, KH_PHASE_PRE, 100, g_cleanup))
    {
        Log(KEEL_LOG_ERROR, "automatic cleanup probe registration failed");
        return;
    }
    Log(KEEL_LOG_INFO, "peer cleanup and last-callback restoration passed");
}

void PrepareUnloadCommand(const KeelCommandInvocation*, void*)
{
    if (g_unload_worker.joinable())
    {
        Log(KEEL_LOG_ERROR, "concurrent unload probe was already active");
        return;
    }
    g_cleanup_entered.store(false, std::memory_order_release);
    g_cleanup_delay.store(true, std::memory_order_release);
    g_unload_worker = std::thread([] { static_cast<void>(KeelHookFixtureTarget(8, 9)); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!g_cleanup_entered.load(std::memory_order_acquire) &&
        std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    if (!g_cleanup_entered.load(std::memory_order_acquire))
    {
        g_cleanup_delay.store(false, std::memory_order_release);
        g_unload_worker.join();
        Log(KEEL_LOG_ERROR, "concurrent unload probe did not enter its callback");
        return;
    }
    Log(KEEL_LOG_INFO, "concurrent unload probe armed");
}

void RestoreRetryCommand(const KeelCommandInvocation*, void*)
{
#if !defined(_WIN32)
    std::atomic<bool> ready{};
    std::atomic<bool> release{};
    std::atomic<bool> blocked{};
    std::thread blocker([&] {
        sigset_t signals;
        sigemptyset(&signals);
        for (int signal = SIGRTMIN; signal <= SIGRTMAX; ++signal)
        {
            sigaddset(&signals, signal);
        }
        blocked.store(pthread_sigmask(SIG_BLOCK, &signals, nullptr) == 0, std::memory_order_release);
        ready.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        if (blocked.load(std::memory_order_acquire))
        {
            pthread_sigmask(SIG_UNBLOCK, &signals, nullptr);
        }
    });
    while (!ready.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }
    const KeelResult first = blocked.load(std::memory_order_acquire)
        ? g_hook->remove_callback(g_plugin, g_cleanup)
        : KEEL_RESULT_ENGINE_FAILURE;
    const bool retained = first == KEEL_RESULT_ENGINE_FAILURE && KeelHookFixtureTarget(2, 3) == 33;
    release.store(true, std::memory_order_release);
    blocker.join();
    if (!retained)
    {
        Log(KEEL_LOG_ERROR, "failed physical restoration did not retain the callback");
        return;
    }
#endif
    if (g_hook->remove_callback(g_plugin, g_cleanup) != KEEL_RESULT_OK ||
        KeelHookFixtureTarget(2, 3) != 23 ||
        !AddCallback(&CleanupCallback, KH_PHASE_PRE, 100, g_cleanup))
    {
        Log(KEEL_LOG_ERROR, "callback restoration retry failed");
        return;
    }
    Log(KEEL_LOG_INFO, "callback restoration retry semantics passed");
}

void PrepareShutdownRetryCommand(const KeelCommandInvocation*, void*)
{
#if !defined(_WIN32)
    if (g_shutdown_worker.joinable())
    {
        Log(KEEL_LOG_ERROR, "shutdown retry probe was already active");
        return;
    }
    std::atomic<bool> ready{};
    g_shutdown_worker = std::thread([&ready] {
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
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        pthread_sigmask(SIG_UNBLOCK, &signals, nullptr);
    });
    while (!ready.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }
    g_shutdown_retry_armed.store(true, std::memory_order_release);
    Log(KEEL_LOG_INFO, "shutdown restoration retry probe armed");
#else
    Log(KEEL_LOG_ERROR, "shutdown restoration retry probe is unavailable");
#endif
}

bool RegisterCommand(const char* name, KeelCommandCallback callback)
{
    const KeelCommandSpec spec{
        sizeof(KeelCommandSpec),
        name,
        "KeelHook integration fixture",
        0,
        callback,
        nullptr
    };
    KeelCommandHandle handle{};
    return g_host->register_command(g_plugin, &spec, &handle) == KEEL_RESULT_OK;
}

std::string TargetPattern(void* address)
{
    const auto* bytes = static_cast<const std::uint8_t*>(address);
    std::string result;
    result.reserve(24 * 3);
    for (std::size_t index{}; index < 24; ++index)
    {
        char token[4]{};
        std::snprintf(token, sizeof(token), "%02X", bytes[index]);
        if (!result.empty())
        {
            result.push_back(' ');
        }
        result += token;
    }
    return result;
}

}

extern "C" KEELS2_PLUGIN_EXPORT KEELHOOK_NOINLINE std::int32_t KeelHookFixtureTarget(
    std::int32_t left,
    std::int32_t right)
{
    g_original_calls.fetch_add(1, std::memory_order_relaxed);
    g_last_left.store(left, std::memory_order_release);
    g_last_right.store(right, std::memory_order_release);
    return left * 10 + right;
}

extern "C" KEELS2_PLUGIN_EXPORT KEELHOOK_NOINLINE std::int32_t KeelHookPauseFixtureTarget(
    std::int32_t value)
{
    return value + 10;
}

extern "C" KEELS2_PLUGIN_EXPORT std::int32_t KeelTest_KeelHookLastLeft()
{
    return g_last_left.load(std::memory_order_acquire);
}

extern "C" KEELS2_PLUGIN_EXPORT std::int32_t KeelTest_KeelHookLastRight()
{
    return g_last_right.load(std::memory_order_acquire);
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_KeelHookPauseCalls()
{
    return g_pause_calls.load(std::memory_order_acquire);
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelTest_KeelHookPauseCleanup()
{
    const auto& prototype = keels2::kh::Prototype<std::int32_t(std::int32_t)>::value;
    KeelHookTargetSpec target_spec{
        sizeof(KeelHookTargetSpec),
        KH_TARGET_ADDRESS,
        KH_MECHANISM_DETOUR,
        0,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        FunctionAddress(&KeelHookPauseFixtureTarget),
        0,
        0,
        0
    };
    KeelHookTargetHandle rejected_target{91};
    const KeelHookCallbackSpec callback_spec{
        sizeof(KeelHookCallbackSpec),
        KH_PHASE_PRE,
        0,
        0,
        &PauseCallback,
        nullptr
    };
    KeelHookCallbackHandle rejected_callback{92};
    const bool passed =
        g_hook->resolve_target(
            g_plugin,
            &target_spec,
            &prototype,
            &rejected_target) == KEEL_RESULT_NOT_READY && rejected_target == 0 &&
        g_hook->add_callback(
            g_plugin,
            g_pause_target,
            &callback_spec,
            &rejected_callback) == KEEL_RESULT_NOT_READY && rejected_callback == 0 &&
        g_hook->remove_callback(g_plugin, g_pause_callback) == KEEL_RESULT_OK &&
        g_hook->release_target(g_plugin, g_pause_target) == KEEL_RESULT_OK;
    if (passed)
    {
        g_pause_callback = 0;
        g_pause_target = 0;
    }
    return passed ? KEEL_TRUE : KEEL_FALSE;
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Query(
    const KeelHostQuery* query,
    KeelPluginInfo* info)
{
    if (!query || query->size != sizeof(KeelHostQuery) ||
        query->abi_version != KEELS2_PLUGIN_ABI_VERSION || !info ||
        info->size != sizeof(KeelPluginInfo))
    {
        return KEEL_FALSE;
    }
    *info = {
        sizeof(KeelPluginInfo),
        KEELS2_PLUGIN_ABI_VERSION,
        "KeelHook Target Fixture",
        "KeelS2 Project",
        "1",
        "KeelHook shared-target integration fixture"
    };
    return KEEL_TRUE;
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Load(
    const KeelHostApi* api,
    KeelPluginHandle plugin)
{
    if (!api || api->size != sizeof(KeelHostApi) ||
        api->abi_version != KEELS2_PLUGIN_ABI_VERSION || !api->query_service ||
        !api->register_command || !api->log)
    {
        return KEEL_FALSE;
    }
    g_host = api;
    g_plugin = plugin;
    const void* service = reinterpret_cast<const void*>(1);
    if (api->query_service(plugin, "missing.service", 1, &service) != KEEL_RESULT_NOT_FOUND || service ||
        api->query_service(plugin, KEELHOOK_SERVICE_NAME, KEELHOOK_API_VERSION + 1, &service) !=
            KEEL_RESULT_INCOMPATIBLE || service ||
        api->query_service(plugin, KEELHOOK_SERVICE_NAME, KEELHOOK_API_VERSION, &service) != KEEL_RESULT_OK ||
        !service)
    {
        return KEEL_FALSE;
    }
    g_hook = static_cast<const KeelHookApi*>(service);
    if (g_hook->size != sizeof(KeelHookApi) || g_hook->api_version != KEELHOOK_API_VERSION ||
        !g_hook->resolve_virtual_target)
    {
        return KEEL_FALSE;
    }

    auto target_function = &KeelHookFixtureTarget;
    void* target_address = FunctionAddress(target_function);
    const auto& prototype = keels2::kh::Prototype<std::int32_t(std::int32_t, std::int32_t)>::value;
    KeelHookTargetSpec direct{
        sizeof(KeelHookTargetSpec),
        KH_TARGET_ADDRESS,
        KH_MECHANISM_DETOUR,
        0,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        target_address,
        0,
        0,
        0
    };
    if (g_hook->resolve_target(plugin, &direct, &prototype, &g_target) != KEEL_RESULT_OK || !g_target)
    {
        return KEEL_FALSE;
    }
    const auto& pause_prototype =
        keels2::kh::Prototype<std::int32_t(std::int32_t)>::value;
    KeelHookTargetSpec pause_direct = direct;
    pause_direct.address = FunctionAddress(&KeelHookPauseFixtureTarget);
    if (g_hook->resolve_target(
            plugin,
            &pause_direct,
            &pause_prototype,
            &g_pause_target) != KEEL_RESULT_OK || !g_pause_target ||
        !AddCallback(
            g_pause_target,
            &PauseCallback,
            KH_PHASE_PRE,
            0,
            g_pause_callback))
    {
        return KEEL_FALSE;
    }
    KeelHookTargetSpec malformed = direct;
    malformed.symbol = "KeelHookFixtureTarget";
    KeelHookTargetHandle rejected{};
    if (g_hook->resolve_target(plugin, &malformed, &prototype, &rejected) != KEEL_RESULT_INVALID_ARGUMENT || rejected)
    {
        return KEEL_FALSE;
    }
    KeelHookTargetHandle alias{};
    if (g_hook->resolve_target(plugin, &direct, &prototype, &alias) != KEEL_RESULT_OK || alias != g_target)
    {
        return KEEL_FALSE;
    }

#if defined(_WIN32)
    const char* module_name = "01_keelhook_target.dll";
#else
    const char* module_name = "01_keelhook_target.so";
#endif
#if !defined(KEELHOOK_FIXTURE_PROFILE)
#error KEELHOOK_FIXTURE_PROFILE must be defined
#endif
    const char* profile = KEELHOOK_FIXTURE_PROFILE;
    KeelHookTargetSpec symbol = direct;
    symbol.source = KH_TARGET_SYMBOL;
    symbol.module = module_name;
    symbol.symbol = "KeelHookFixtureTarget";
    symbol.address = nullptr;
    if (g_hook->resolve_target(plugin, &symbol, &prototype, &alias) != KEEL_RESULT_OK || alias != g_target)
    {
        return KEEL_FALSE;
    }
    const std::string pattern_text = TargetPattern(target_address);
    KeelHookTargetSpec pattern = direct;
    pattern.source = KH_TARGET_PATTERN;
    pattern.module = module_name;
    pattern.pattern = pattern_text.c_str();
    pattern.profile = profile;
    pattern.address = nullptr;
    if (g_hook->resolve_target(plugin, &pattern, &prototype, &alias) != KEEL_RESULT_OK || alias != g_target)
    {
        return KEEL_FALSE;
    }
    KeelHookPrototype incompatible = prototype;
    incompatible.return_type = KH_VALUE_INT64;
    if (g_hook->resolve_target(plugin, &direct, &incompatible, &alias) != KEEL_RESULT_INCOMPATIBLE)
    {
        return KEEL_FALSE;
    }

    void* first_virtual = KeelHookVirtualFixtureFirst();
    void* second_virtual = KeelHookVirtualFixtureSecond();
    const auto& virtual_prototype =
        keels2::kh::Prototype<std::int32_t(void*, std::int32_t)>::value;
    KeelHookVirtualTargetSpec shared_virtual{
        sizeof(KeelHookVirtualTargetSpec),
        KH_MECHANISM_VIRTUAL,
        0,
        0,
        0,
        0,
        first_virtual,
        profile
    };
    if (g_hook->resolve_virtual_target(
            plugin,
            &shared_virtual,
            &virtual_prototype,
            &g_virtual_shared_target) != KEEL_RESULT_OK ||
        !g_virtual_shared_target)
    {
        return KEEL_FALSE;
    }
    KeelHookTargetHandle virtual_alias{};
    shared_virtual.instance = second_virtual;
    if (g_hook->resolve_virtual_target(
            plugin,
            &shared_virtual,
            &virtual_prototype,
            &virtual_alias) != KEEL_RESULT_OK ||
        virtual_alias != g_virtual_shared_target)
    {
        return KEEL_FALSE;
    }
    shared_virtual.instance = first_virtual;
    shared_virtual.profile = "wrong-profile";
    KeelHookTargetHandle virtual_rejected{};
    if (g_hook->resolve_virtual_target(
            plugin,
            &shared_virtual,
            &virtual_prototype,
            &virtual_rejected) != KEEL_RESULT_INCOMPATIBLE ||
        virtual_rejected)
    {
        return KEEL_FALSE;
    }
    shared_virtual.profile = profile;
    shared_virtual.table_size = 2;
    if (g_hook->resolve_virtual_target(
            plugin,
            &shared_virtual,
            &virtual_prototype,
            &virtual_rejected) != KEEL_RESULT_INVALID_ARGUMENT ||
        virtual_rejected)
    {
        return KEEL_FALSE;
    }
    shared_virtual.table_size = 0;
    KeelHookVirtualTargetSpec instance_conflict = shared_virtual;
    instance_conflict.mechanism = KH_MECHANISM_VIRTUAL_INSTANCE;
    instance_conflict.table_size = 3;
    if (g_hook->resolve_virtual_target(
            plugin,
            &instance_conflict,
            &virtual_prototype,
            &virtual_rejected) != KEEL_RESULT_BUSY ||
        virtual_rejected)
    {
        return KEEL_FALSE;
    }
    if (!AddCallback(
            g_virtual_shared_target,
            &VirtualCallback,
            KH_PHASE_BOTH,
            0,
            g_virtual_shared_callback) ||
        KeelHookVirtualFixtureCallFirst(first_virtual, 5) != 105 ||
        KeelHookVirtualFixtureCallFirst(second_virtual, 5) != 205)
    {
        return KEEL_FALSE;
    }

    if (!AddCallback(&HighCallback, KH_PHASE_PRE, 100, g_high) ||
        !AddCallback(&OverrideCallback, KH_PHASE_BOTH, 0, g_override) ||
        !AddCallback(&LowCallback, KH_PHASE_BOTH, -100, g_low) ||
        !AddCallback(&SelfRemovingCallback, KH_PHASE_PRE, 200, g_self))
    {
        return KEEL_FALSE;
    }
    if (KeelHookFixtureTarget(2, 3) != 23 || g_self_calls.load(std::memory_order_acquire) != 0)
    {
        return KEEL_FALSE;
    }
    Log(KEEL_LOG_INFO, "callbacks remained staged until plugin activation");
    if (!RegisterCommand("kh_prepare_unload", &PrepareUnloadCommand) ||
        !RegisterCommand("kh_prepare_shutdown_retry", &PrepareShutdownRetryCommand) ||
        !RegisterCommand("kh_restore_retry", &RestoreRetryCommand) ||
        !RegisterCommand("kh_run", &RunCommand) ||
        !RegisterCommand("kh_after_peer", &AfterPeerCommand))
    {
        return KEEL_FALSE;
    }
    Log(KEEL_LOG_INFO, "resolver and incompatible-prototype checks passed");
    g_load_complete.store(true, std::memory_order_release);
    return KEEL_TRUE;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelPlugin_Unload(KeelPluginHandle)
{
    if (g_unload_worker.joinable())
    {
        g_unload_worker.join();
    }
    if (g_shutdown_worker.joinable())
    {
        g_shutdown_worker.join();
    }
    if (g_after_peer.load(std::memory_order_acquire) && KeelHookFixtureTarget(2, 3) == 23)
    {
        Log(KEEL_LOG_INFO, "automatic target-owner cleanup passed before module unload");
    }
    else if (g_shutdown_retry_armed.load(std::memory_order_acquire) &&
        KeelHookFixtureTarget(2, 3) == 23)
    {
        Log(KEEL_LOG_INFO, "shutdown retry restored the target before module unload");
    }
    else if (!g_load_complete.load(std::memory_order_acquire) &&
        (!g_target || KeelHookFixtureTarget(2, 3) == 23))
    {
        Log(KEEL_LOG_INFO, "load rollback cleanup passed before module unload");
    }
    else
    {
        Log(KEEL_LOG_ERROR, "automatic target-owner cleanup failed");
    }
}
