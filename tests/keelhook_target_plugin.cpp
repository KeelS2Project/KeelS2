#include <keels2/keelhook.hpp>
#include <keels2/plugin.h>

#include "keelhook_virtual_fixture.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
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

struct alignas(KEELHOOK_MAX_AGGREGATE_ALIGNMENT) KeelHookFixtureObject
{
    KeelHookFixtureObject() noexcept
    {
        live.fetch_add(1, std::memory_order_relaxed);
    }

    explicit KeelHookFixtureObject(std::int32_t initial) noexcept
        : value(initial)
    {
        live.fetch_add(1, std::memory_order_relaxed);
    }

    KeelHookFixtureObject(const KeelHookFixtureObject& other) noexcept
        : value(other.value)
    {
        live.fetch_add(1, std::memory_order_relaxed);
        copies.fetch_add(1, std::memory_order_relaxed);
    }

    KeelHookFixtureObject& operator=(const KeelHookFixtureObject& other) noexcept
    {
        value = other.value;
        assignments.fetch_add(1, std::memory_order_relaxed);
        return *this;
    }

    ~KeelHookFixtureObject() noexcept
    {
        live.fetch_sub(1, std::memory_order_relaxed);
    }

    std::int32_t value{};
    static inline std::atomic<std::int32_t> live{};
    static inline std::atomic<std::uint32_t> copies{};
    static inline std::atomic<std::uint32_t> assignments{};
};

extern "C" KEELS2_PLUGIN_EXPORT std::int32_t KeelHookFixtureTarget(
    std::int32_t left,
    std::int32_t right);
extern "C" KEELS2_PLUGIN_EXPORT std::int32_t KeelHookPauseFixtureTarget(
    std::int32_t value);
extern "C" KEELS2_PLUGIN_EXPORT std::int32_t KeelHookControlFixtureTarget(
    std::int32_t left,
    std::int32_t right);
extern "C" KEELS2_PLUGIN_EXPORT KeelHookFixtureObject KeelHookObjectFixtureTarget(
    KeelHookFixtureObject value);
extern "C" KEELS2_PLUGIN_EXPORT std::int32_t& KeelHookReferenceFixtureTarget(bool alternate);
extern "C" KEELS2_PLUGIN_EXPORT std::int32_t KeelHookVafmtFixtureTarget(
    std::int32_t prefix,
    const char* format,
    ...);

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
KeelHookTargetHandle g_control_target{};
KeelHookCallbackHandle g_control_leader{};
KeelHookCallbackHandle g_control_callback{};
KeelHookCallbackHandle g_control_follower{};
KeelHookTargetHandle g_virtual_shared_target{};
KeelHookCallbackHandle g_virtual_shared_callback{};
std::atomic<std::uint64_t> g_original_calls{};
std::atomic<std::int32_t> g_last_left{};
std::atomic<std::int32_t> g_last_right{};
std::atomic<std::uint32_t> g_self_calls{};
std::atomic<std::uint32_t> g_pause_calls{};
std::atomic<std::uint32_t> g_control_calls{};
std::atomic<std::uint32_t> g_control_leader_calls{};
std::atomic<std::uint32_t> g_control_follower_pre_calls{};
std::atomic<std::uint32_t> g_control_follower_post_calls{};
std::atomic<std::uint32_t> g_control_recalled_pre_calls{};
std::atomic<std::uint32_t> g_control_recalled_post_calls{};
std::atomic<std::uint32_t> g_control_errors{};
std::atomic<std::uint32_t> g_parity_errors{};
std::atomic<std::uint32_t> g_object_original_calls{};
std::atomic<std::uint32_t> g_reference_original_calls{};
std::atomic<bool> g_capture{};
std::atomic<bool> g_run{};
std::atomic<bool> g_after_peer{};
std::atomic<bool> g_load_complete{};
std::atomic<bool> g_recursive_entry{};
std::atomic<std::int32_t> g_recursive_result{};
std::atomic<bool> g_cleanup_delay{};
std::atomic<bool> g_cleanup_entered{};
std::atomic<bool> g_shutdown_retry_armed{};
std::uint64_t g_benchmark_no_hook{};
std::uint64_t g_benchmark_multi_plugin{};
std::int64_t g_benchmark_sink{};
std::thread g_unload_worker;
std::thread g_shutdown_worker;
std::mutex g_order_mutex;
std::vector<std::uint32_t> g_order;
std::array<char, KEELHOOK_VAFMT_BUFFER_SIZE> g_vafmt_seen{};
std::int32_t g_reference_primary{41};
std::int32_t g_reference_alternate{91};
constexpr char g_vafmt_expected[] = "i=17 d=2.5 s=keel u=99 x=-3 y=4.25 z=tail";
constexpr char g_method_vafmt_expected[] = "i=17 d=2.5 s=keel";
constexpr char g_vafmt_replacement[] = "callback 100% ready %s";

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

bool FuzzInputContracts(const KeelHostApi* api, KeelPluginHandle plugin)
{
    std::uint64_t state = 0x4b48446573633039ull;
    const auto next = [&] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    std::array<char, 128> name{};
    std::array<KeelHookValueType, KEELHOOK_MAX_ARGUMENTS> types{};
    std::array<const KeelHookAggregate*, KEELHOOK_MAX_ARGUMENTS> aggregates{};
    std::array<KeelHookAggregateField, 4> fields{};
    for (std::size_t iteration{}; iteration < 256; ++iteration)
    {
        std::snprintf(
            name.data(),
            name.size(),
            "fuzz.%016llx.%zu",
            static_cast<unsigned long long>(next()),
            iteration);
        const void* service = reinterpret_cast<const void*>(1);
        const KeelResult query = api->query_service(
            plugin,
            name.data(),
            static_cast<std::uint32_t>(next()),
            &service);
        if (query == KEEL_RESULT_OK || service)
        {
            return false;
        }
        for (auto& type : types)
        {
            type = static_cast<KeelHookValueType>(next() % 16u);
        }
        for (auto& field : fields)
        {
            field = {
                (next() & 1u) != 0 ? static_cast<std::uint32_t>(sizeof(field)) : 0u,
                static_cast<KeelHookValueType>(next() % 16u),
                static_cast<std::uint32_t>(next() % 65u),
                static_cast<std::uint32_t>(next() % 4u),
                nullptr
            };
        }
        const KeelHookAggregate aggregate{
            (next() & 1u) != 0 ? static_cast<std::uint32_t>(sizeof(aggregate)) : 0u,
            static_cast<std::uint32_t>(next() % 65u),
            static_cast<std::uint32_t>(next() % (fields.size() + 1)),
            static_cast<std::uint32_t>(next() & 1u),
            fields.data()
        };
        const std::uint32_t argument_count = static_cast<std::uint32_t>(next() % 35u);
        const KeelHookPrototype fuzz_prototype{
            (next() & 1u) != 0 ? static_cast<std::uint32_t>(sizeof(KeelHookPrototype)) : 0u,
            static_cast<KeelHookCallingConvention>(next() % 3u),
            static_cast<KeelHookValueType>(next() % 16u),
            argument_count,
            types.data(),
            (next() & 1u) != 0 ? &aggregate : nullptr,
            aggregates.data(),
            static_cast<std::uint32_t>(next() % 35u),
            static_cast<std::uint32_t>(next() & 1u),
            nullptr,
            nullptr
        };
        const KeelHookTargetSpec fuzz_target{
            sizeof(KeelHookTargetSpec),
            KH_TARGET_PROFILE,
            KH_MECHANISM_DETOUR,
            static_cast<std::uint32_t>(next() & 3u),
            nullptr,
            name.data(),
            nullptr,
            nullptr,
            nullptr,
            0,
            0,
            static_cast<std::uint32_t>(next() & 1u)
        };
        KeelHookTargetHandle target{std::numeric_limits<KeelHookTargetHandle>::max()};
        if (g_hook->resolve_target(
                plugin,
                &fuzz_target,
                &fuzz_prototype,
                &target) == KEEL_RESULT_OK || target)
        {
            return false;
        }
    }
    return true;
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

KeelHookAction ControlCallback(KeelHookFrame* frame, void*)
{
    if (!frame || frame->phase != KH_PHASE_PRE || frame->argument_count != 2)
    {
        g_control_errors.fetch_add(1, std::memory_order_relaxed);
        return KH_ACTION_CONTINUE;
    }
    g_control_calls.fetch_add(1, std::memory_order_relaxed);
    const auto left = keels2::kh::Read<std::int32_t>(frame->arguments[0]);
    if (left == 100)
    {
        keels2::kh::Write(frame->arguments[0], std::int32_t{2});
        keels2::kh::Write(frame->arguments[1], std::int32_t{3});
        if (g_hook->call_original(g_plugin, frame) != KEEL_RESULT_OK ||
            (frame->flags & KH_FRAME_ORIGINAL_CALLED) == 0 ||
            keels2::kh::Read<std::int32_t>(frame->result) != 23 ||
            g_hook->call_original(g_plugin, frame) != KEEL_RESULT_ALREADY_EXISTS)
        {
            g_control_errors.fetch_add(1, std::memory_order_relaxed);
        }
    }
    else if (left == 200 || left == 201)
    {
        keels2::kh::Write(frame->arguments[0], std::int32_t{4});
        keels2::kh::Write(frame->arguments[1], std::int32_t{5});
        if (g_hook->recall(g_plugin, frame) != KEEL_RESULT_OK ||
            (frame->flags & (KH_FRAME_ORIGINAL_CALLED | KH_FRAME_RECALLED)) !=
                (KH_FRAME_ORIGINAL_CALLED | KH_FRAME_RECALLED) ||
            keels2::kh::Read<std::int32_t>(frame->result) != (left == 201 ? 999 : 45) ||
            g_hook->recall(g_plugin, frame) != KEEL_RESULT_ALREADY_EXISTS)
        {
            g_control_errors.fetch_add(1, std::memory_order_relaxed);
        }
    }
    else if (left == 300)
    {
        keels2::kh::Write(frame->arguments[0], std::int32_t{6});
        keels2::kh::Write(frame->arguments[1], std::int32_t{7});
        if (g_hook->call_original(g_plugin, frame) != KEEL_RESULT_OK ||
            keels2::kh::Read<std::int32_t>(frame->result) != 67)
        {
            g_control_errors.fetch_add(1, std::memory_order_relaxed);
        }
        keels2::kh::Write(frame->result, std::int32_t{777});
        return KH_ACTION_SUPERSEDE;
    }
    else if (left == 400)
    {
        keels2::kh::Write(frame->result, std::int32_t{888});
        return KH_ACTION_SUPERSEDE;
    }
    return KH_ACTION_CONTINUE;
}

KeelHookAction ControlLeaderCallback(KeelHookFrame* frame, void*)
{
    if (!frame || frame->phase != KH_PHASE_PRE)
    {
        g_control_errors.fetch_add(1, std::memory_order_relaxed);
        return KH_ACTION_CONTINUE;
    }
    g_control_leader_calls.fetch_add(1, std::memory_order_relaxed);
    if (keels2::kh::Read<std::int32_t>(frame->arguments[0]) == 201)
    {
        keels2::kh::Write(frame->result, std::int32_t{999});
        return KH_ACTION_OVERRIDE;
    }
    return KH_ACTION_CONTINUE;
}

KeelHookAction ControlFollowerCallback(KeelHookFrame* frame, void*)
{
    if (!frame)
    {
        g_control_errors.fetch_add(1, std::memory_order_relaxed);
        return KH_ACTION_CONTINUE;
    }
    if (frame->phase == KH_PHASE_PRE)
    {
        g_control_follower_pre_calls.fetch_add(1, std::memory_order_relaxed);
        if ((frame->flags & KH_FRAME_RECALLED) != 0)
        {
            g_control_recalled_pre_calls.fetch_add(1, std::memory_order_relaxed);
        }
    }
    else if (frame->phase == KH_PHASE_POST)
    {
        g_control_follower_post_calls.fetch_add(1, std::memory_order_relaxed);
        if ((frame->flags & KH_FRAME_RECALLED) != 0)
        {
            g_control_recalled_post_calls.fetch_add(1, std::memory_order_relaxed);
        }
    }
    else
    {
        g_control_errors.fetch_add(1, std::memory_order_relaxed);
    }
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

KeelHookAction ObjectCallback(KeelHookFrame* frame, void*)
{
    if (!frame || frame->argument_count != 1)
    {
        g_parity_errors.fetch_add(1, std::memory_order_relaxed);
        return KH_ACTION_CONTINUE;
    }
    if (frame->phase == KH_PHASE_PRE)
    {
        if (!keels2::kh::ValidValue<KeelHookFixtureObject>(frame->arguments[0]))
        {
            g_parity_errors.fetch_add(1, std::memory_order_relaxed);
            return KH_ACTION_CONTINUE;
        }
        auto value = keels2::kh::Read<KeelHookFixtureObject>(frame->arguments[0]);
        const std::int32_t original = value.value;
        value.value += 5;
        if (!keels2::kh::Write(frame->arguments[0], value))
        {
            g_parity_errors.fetch_add(1, std::memory_order_relaxed);
            return KH_ACTION_CONTINUE;
        }
        if (original == 20)
        {
            if (g_hook->call_original(g_plugin, frame) != KEEL_RESULT_OK ||
                (frame->flags & KH_FRAME_ORIGINAL_CALLED) == 0 ||
                !keels2::kh::ValidValue<KeelHookFixtureObject>(frame->result))
            {
                g_parity_errors.fetch_add(1, std::memory_order_relaxed);
                return KH_ACTION_CONTINUE;
            }
            auto result = keels2::kh::Read<KeelHookFixtureObject>(frame->result);
            if (result.value != 35)
            {
                g_parity_errors.fetch_add(1, std::memory_order_relaxed);
            }
            result.value += 200;
            if (!keels2::kh::Write(frame->result, result))
            {
                g_parity_errors.fetch_add(1, std::memory_order_relaxed);
                return KH_ACTION_CONTINUE;
            }
            return KH_ACTION_OVERRIDE;
        }
        return KH_ACTION_CONTINUE;
    }
    if (!keels2::kh::ValidValue<KeelHookFixtureObject>(frame->result))
    {
        g_parity_errors.fetch_add(1, std::memory_order_relaxed);
        return KH_ACTION_CONTINUE;
    }
    auto result = keels2::kh::Read<KeelHookFixtureObject>(frame->result);
    result.value += 100;
    if (!keels2::kh::Write(frame->result, result))
    {
        g_parity_errors.fetch_add(1, std::memory_order_relaxed);
        return KH_ACTION_CONTINUE;
    }
    return KH_ACTION_OVERRIDE;
}

KeelHookAction ReferenceCallback(KeelHookFrame* frame, void*)
{
    if (!frame || frame->argument_count != 1)
    {
        g_parity_errors.fetch_add(1, std::memory_order_relaxed);
        return KH_ACTION_CONTINUE;
    }
    const bool alternate = keels2::kh::Read<bool>(frame->arguments[0]);
    if (frame->phase == KH_PHASE_PRE && alternate)
    {
        if (!keels2::kh::WriteReference<std::int32_t&>(
                frame->result,
                g_reference_alternate))
        {
            g_parity_errors.fetch_add(1, std::memory_order_relaxed);
            return KH_ACTION_CONTINUE;
        }
        return KH_ACTION_SUPERSEDE;
    }
    if (frame->phase == KH_PHASE_POST && !alternate)
    {
        if (keels2::kh::ReadReference<std::int32_t&>(frame->result) !=
                &g_reference_primary ||
            !keels2::kh::WriteReference<std::int32_t&>(
                frame->result,
                g_reference_alternate))
        {
            g_parity_errors.fetch_add(1, std::memory_order_relaxed);
            return KH_ACTION_CONTINUE;
        }
        return KH_ACTION_OVERRIDE;
    }
    return KH_ACTION_CONTINUE;
}

KeelHookAction VafmtCallback(KeelHookFrame* frame, void*)
{
    if (!frame || frame->argument_count != 2)
    {
        g_parity_errors.fetch_add(1, std::memory_order_relaxed);
        return KH_ACTION_CONTINUE;
    }
    const char* text = keels2::kh::Read<const char*>(frame->arguments[1]);
    if (frame->phase == KH_PHASE_PRE)
    {
        const char* replacement = g_vafmt_replacement;
        if (!text || std::strcmp(text, g_vafmt_expected) != 0)
        {
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "vafmt callback received '%s'",
                text ? text : "<null>");
            Log(KEEL_LOG_ERROR, message);
            g_parity_errors.fetch_add(1, std::memory_order_relaxed);
        }
        if (
            !keels2::kh::Write(frame->arguments[0], std::int32_t{11}) ||
            !keels2::kh::Write(frame->arguments[1], replacement))
        {
            g_parity_errors.fetch_add(1, std::memory_order_relaxed);
        }
        return KH_ACTION_CONTINUE;
    }
    const std::int32_t expected = 11 + static_cast<std::int32_t>(
        std::strlen(g_vafmt_replacement));
    const std::int32_t result = keels2::kh::Read<std::int32_t>(frame->result);
    if (!text || std::strcmp(text, g_vafmt_replacement) != 0 || result != expected ||
        std::strcmp(g_vafmt_seen.data(), g_vafmt_replacement) != 0 ||
        !keels2::kh::Write(frame->result, result + 1000))
    {
        g_parity_errors.fetch_add(1, std::memory_order_relaxed);
        return KH_ACTION_CONTINUE;
    }
    return KH_ACTION_OVERRIDE;
}

KeelHookAction MethodVafmtCallback(KeelHookFrame* frame, void*)
{
    if (!frame || frame->argument_count != 3 ||
        keels2::kh::Read<void*>(frame->arguments[0]) == nullptr)
    {
        g_parity_errors.fetch_add(1, std::memory_order_relaxed);
        return KH_ACTION_CONTINUE;
    }
    const char* text = keels2::kh::Read<const char*>(frame->arguments[2]);
    if (frame->phase == KH_PHASE_PRE)
    {
        const char* replacement = g_vafmt_replacement;
        if (!text || std::strcmp(text, g_method_vafmt_expected) != 0 ||
            !keels2::kh::Write(frame->arguments[1], std::int32_t{11}) ||
            !keels2::kh::Write(frame->arguments[2], replacement))
        {
            g_parity_errors.fetch_add(1, std::memory_order_relaxed);
        }
        return KH_ACTION_CONTINUE;
    }
    const std::int32_t expected = 11 + static_cast<std::int32_t>(
        std::strlen(g_vafmt_replacement));
    const std::int32_t result = keels2::kh::Read<std::int32_t>(frame->result);
    if (!text || std::strcmp(text, g_vafmt_replacement) != 0 || result != expected ||
        !keels2::kh::Write(frame->result, result + 2000))
    {
        g_parity_errors.fetch_add(1, std::memory_order_relaxed);
        return KH_ACTION_CONTINUE;
    }
    return KH_ACTION_OVERRIDE;
}

KeelHookTargetSpec DirectSpec(void* address)
{
    return {
        sizeof(KeelHookTargetSpec),
        KH_TARGET_ADDRESS,
        KH_MECHANISM_DETOUR,
        0,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        address,
        0,
        0,
        0
    };
}

bool RunObjectTests()
{
    const auto& prototype =
        keels2::kh::Prototype<KeelHookFixtureObject(KeelHookFixtureObject)>::value;
    const KeelHookTargetSpec spec = DirectSpec(FunctionAddress(&KeelHookObjectFixtureTarget));
    KeelHookObject invalid_object = *prototype.return_object;
    invalid_object.identity = nullptr;
    KeelHookPrototype invalid_prototype = prototype;
    invalid_prototype.return_object = &invalid_object;
    KeelHookTargetHandle rejected{97};
    if (g_hook->resolve_target(
            g_plugin,
            &spec,
            &invalid_prototype,
            &rejected) != KEEL_RESULT_INVALID_ARGUMENT || rejected)
    {
        return false;
    }

    KeelHookTargetHandle target{};
    if (g_hook->resolve_target(g_plugin, &spec, &prototype, &target) != KEEL_RESULT_OK ||
        !target)
    {
        return false;
    }
    KeelHookObject incompatible_object = *prototype.return_object;
    incompatible_object.identity = "incompatible.fixture.object";
    KeelHookPrototype incompatible_prototype = prototype;
    incompatible_prototype.return_object = &incompatible_object;
    if (g_hook->resolve_target(
            g_plugin,
            &spec,
            &incompatible_prototype,
            &rejected) != KEEL_RESULT_INCOMPATIBLE || rejected)
    {
        static_cast<void>(g_hook->release_target(g_plugin, target));
        return false;
    }

    KeelHookCallbackHandle callback{};
    if (!AddCallback(target, &ObjectCallback, KH_PHASE_BOTH, 0, callback))
    {
        static_cast<void>(g_hook->release_target(g_plugin, target));
        return false;
    }
    const std::int32_t live_before = KeelHookFixtureObject::live.load(std::memory_order_acquire);
    const std::uint32_t copies_before =
        KeelHookFixtureObject::copies.load(std::memory_order_acquire);
    const std::uint32_t assignments_before =
        KeelHookFixtureObject::assignments.load(std::memory_order_acquire);
    const std::uint32_t originals_before =
        g_object_original_calls.load(std::memory_order_acquire);
    bool passed{};
    {
        KeelHookFixtureObject first{7};
        KeelHookFixtureObject second{20};
        const KeelHookFixtureObject first_result = KeelHookObjectFixtureTarget(first);
        const KeelHookFixtureObject second_result = KeelHookObjectFixtureTarget(second);
        passed = first_result.value == 122 && second_result.value == 335;
    }
    passed = passed &&
        KeelHookFixtureObject::live.load(std::memory_order_acquire) == live_before &&
        KeelHookFixtureObject::copies.load(std::memory_order_acquire) > copies_before &&
        KeelHookFixtureObject::assignments.load(std::memory_order_acquire) > assignments_before &&
        g_object_original_calls.load(std::memory_order_acquire) == originals_before + 2 &&
        g_parity_errors.load(std::memory_order_acquire) == 0;
    const KeelResult remove = g_hook->remove_callback(g_plugin, callback);
    const KeelResult release = g_hook->release_target(g_plugin, target);
    return passed && remove == KEEL_RESULT_OK && release == KEEL_RESULT_OK &&
        KeelHookFixtureObject::live.load(std::memory_order_acquire) == live_before;
}

bool RunReferenceTests()
{
    const auto& prototype = keels2::kh::Prototype<std::int32_t&(bool)>::value;
    const KeelHookTargetSpec spec = DirectSpec(FunctionAddress(&KeelHookReferenceFixtureTarget));
    KeelHookTargetHandle target{};
    KeelHookCallbackHandle callback{};
    if (g_hook->resolve_target(g_plugin, &spec, &prototype, &target) != KEEL_RESULT_OK ||
        !target || !AddCallback(target, &ReferenceCallback, KH_PHASE_BOTH, 0, callback))
    {
        return false;
    }
    const std::uint32_t originals_before =
        g_reference_original_calls.load(std::memory_order_acquire);
    std::int32_t* first = &KeelHookReferenceFixtureTarget(false);
    std::int32_t* second = &KeelHookReferenceFixtureTarget(true);
    const bool passed = first == &g_reference_alternate && second == &g_reference_alternate &&
        g_reference_original_calls.load(std::memory_order_acquire) == originals_before + 1 &&
        g_parity_errors.load(std::memory_order_acquire) == 0;
    const KeelResult remove = g_hook->remove_callback(g_plugin, callback);
    const KeelResult release = g_hook->release_target(g_plugin, target);
    return passed && remove == KEEL_RESULT_OK && release == KEEL_RESULT_OK;
}

bool RunVafmtTests()
{
    const auto& prototype = keels2::kh::VafmtPrototype<std::int32_t(std::int32_t)>::value;
    const KeelHookTargetSpec spec = DirectSpec(FunctionAddress(&KeelHookVafmtFixtureTarget));
    KeelHookTargetHandle target{};
    KeelHookCallbackHandle callback{};
    if (g_hook->resolve_target(g_plugin, &spec, &prototype, &target) != KEEL_RESULT_OK ||
        !target || !AddCallback(target, &VafmtCallback, KH_PHASE_BOTH, 0, callback))
    {
        return false;
    }
    g_vafmt_seen.fill('\0');
    const std::int32_t expected = 1011 + static_cast<std::int32_t>(
        std::strlen(g_vafmt_replacement));
    const std::int32_t result = KeelHookVafmtFixtureTarget(
        10,
        "i=%d d=%.1f s=%s u=%llu x=%d y=%.2f z=%s",
        17,
        2.5,
        "keel",
        99ull,
        -3,
        4.25,
        "tail");
    const bool passed = result == expected &&
        std::strcmp(g_vafmt_seen.data(), g_vafmt_replacement) == 0 &&
        g_parity_errors.load(std::memory_order_acquire) == 0;
    const KeelResult remove = g_hook->remove_callback(g_plugin, callback);
    const KeelResult release = g_hook->release_target(g_plugin, target);
    return passed && remove == KEEL_RESULT_OK && release == KEEL_RESULT_OK;
}

bool RunParityTests()
{
    g_parity_errors.store(0, std::memory_order_release);
    return RunObjectTests() && RunReferenceTests() && RunVafmtTests();
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
        nullptr,
        0,
        0
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
        nullptr,
        0,
        0
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

    const std::int32_t method_vafmt_original = 10 + static_cast<std::int32_t>(
        std::strlen(g_method_vafmt_expected));
    if (KeelHookVirtualFixtureCallVafmt(first, 10) != method_vafmt_original)
    {
        return false;
    }
    const auto& method_vafmt_prototype =
        keels2::kh::MethodVafmtPrototype<std::int32_t(std::int32_t)>::value;
    KeelHookVirtualTargetSpec method_vafmt_shared{
        sizeof(KeelHookVirtualTargetSpec),
        KH_MECHANISM_VIRTUAL,
        0,
        3,
        0,
        0,
        first,
        nullptr,
        0,
        0
    };
    KeelHookTargetHandle method_vafmt_target{};
    KeelHookCallbackHandle method_vafmt_callback{};
    const std::int32_t method_vafmt_hooked = 2011 + static_cast<std::int32_t>(
        std::strlen(g_vafmt_replacement));
    if (g_hook->resolve_virtual_target(
            g_plugin,
            &method_vafmt_shared,
            &method_vafmt_prototype,
            &method_vafmt_target) != KEEL_RESULT_OK || !method_vafmt_target ||
        !AddCallback(
            method_vafmt_target,
            &MethodVafmtCallback,
            KH_PHASE_BOTH,
            0,
            method_vafmt_callback) ||
        KeelHookVirtualFixtureCallVafmt(first, 10) != method_vafmt_hooked ||
        g_parity_errors.load(std::memory_order_acquire) != 0 ||
        g_hook->remove_callback(g_plugin, method_vafmt_callback) != KEEL_RESULT_OK ||
        g_hook->release_target(g_plugin, method_vafmt_target) != KEEL_RESULT_OK ||
        KeelHookVirtualFixtureCallVafmt(first, 10) != method_vafmt_original)
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
        profile,
        0,
        0
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
        profile,
        0,
        0
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

    void* multiple = KeelHookVirtualFixtureMultipleInstance();
    const std::int64_t secondary_offset = KeelHookVirtualFixtureSecondaryOffset();
    using AdjustedMethod = std::int32_t (KeelHookVirtualFixtureMultiple::*)(std::int32_t);
    const AdjustedMethod adjusted_method = static_cast<AdjustedMethod>(
        &KeelHookVirtualFixtureSecondary::Adjusted);
    const auto adjusted_info = keels2::kh::VirtualInfo(adjusted_method);
    if (!multiple || secondary_offset <= 0 || !adjusted_info || adjusted_info->index != 0 ||
        adjusted_info->this_adjustment != secondary_offset ||
        KeelHookVirtualFixtureCallPrimary(multiple, 5) != 305 ||
        KeelHookVirtualFixtureCallAdjusted(multiple, 5) != 905)
    {
        return false;
    }
    KeelHookVirtualTargetSpec offset_shared{
        sizeof(KeelHookVirtualTargetSpec),
        KH_MECHANISM_VIRTUAL,
        0,
        adjusted_info->index,
        0,
        0,
        multiple,
        profile,
        0,
        secondary_offset
    };
    KeelHookTargetHandle adjusted_target{};
    if (g_hook->resolve_virtual_target(
            g_plugin,
            &offset_shared,
            &prototype,
            &adjusted_target) != KEEL_RESULT_OK || !adjusted_target)
    {
        return false;
    }
    KeelHookVirtualTargetSpec adjusted_shared = offset_shared;
    adjusted_shared.this_adjustment = adjusted_info->this_adjustment;
    adjusted_shared.vtable_offset = 0;
    KeelHookTargetHandle adjusted_alias{};
    KeelHookCallbackHandle adjusted_callback{};
    if (g_hook->resolve_virtual_target(
            g_plugin,
            &adjusted_shared,
            &prototype,
            &adjusted_alias) != KEEL_RESULT_OK || adjusted_alias != adjusted_target ||
        !AddCallback(
            adjusted_target,
            &VirtualCallback,
            KH_PHASE_BOTH,
            0,
            adjusted_callback) ||
        KeelHookVirtualFixtureCallPrimary(multiple, 5) != 305 ||
        KeelHookVirtualFixtureCallAdjusted(multiple, 5) != 1910 ||
        g_hook->remove_callback(g_plugin, adjusted_callback) != KEEL_RESULT_OK ||
        g_hook->release_target(g_plugin, adjusted_target) != KEEL_RESULT_OK ||
        KeelHookVirtualFixtureCallAdjusted(multiple, 5) != 905)
    {
        return false;
    }

    KeelHookVirtualTargetSpec offset_instance = offset_shared;
    offset_instance.mechanism = KH_MECHANISM_VIRTUAL_INSTANCE;
    offset_instance.table_size = 1;
    KeelHookTargetHandle adjusted_instance_target{};
    KeelHookCallbackHandle adjusted_instance_callback{};
    if (g_hook->resolve_virtual_target(
            g_plugin,
            &offset_instance,
            &prototype,
            &adjusted_instance_target) != KEEL_RESULT_OK || !adjusted_instance_target ||
        !AddCallback(
            adjusted_instance_target,
            &VirtualCallback,
            KH_PHASE_BOTH,
            0,
            adjusted_instance_callback) ||
        KeelHookVirtualFixtureCallPrimary(multiple, 5) != 305 ||
        KeelHookVirtualFixtureCallAdjusted(multiple, 5) != 1910 ||
        g_hook->remove_callback(g_plugin, adjusted_instance_callback) != KEEL_RESULT_OK ||
        g_hook->release_target(g_plugin, adjusted_instance_target) != KEEL_RESULT_OK ||
        KeelHookVirtualFixtureCallAdjusted(multiple, 5) != 905)
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

std::uint64_t BenchmarkTarget(std::int32_t expected)
{
    constexpr std::int32_t iterations = 20000;
    std::int64_t sum{};
    const auto begin = std::chrono::steady_clock::now();
    for (std::int32_t iteration{}; iteration < iterations; ++iteration)
    {
        sum += KeelHookFixtureTarget(7, 3);
    }
    const auto end = std::chrono::steady_clock::now();
    g_benchmark_sink = sum;
    if (sum != static_cast<std::int64_t>(expected) * iterations)
    {
        return 0;
    }
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
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
    g_benchmark_multi_plugin = BenchmarkTarget(900);
    if (!g_benchmark_multi_plugin)
    {
        Log(KEEL_LOG_ERROR, "KeelHook multi-plugin benchmark failed");
        return;
    }
    if (!RunVirtualTests())
    {
        Log(KEEL_LOG_ERROR, "KeelHook virtual target integration failed");
        return;
    }
    if (!RunParityTests())
    {
        Log(KEEL_LOG_ERROR, "KeelHook object, reference-return, or vafmt integration failed");
        return;
    }
    Log(KEEL_LOG_INFO, "object lifetimes, reference returns, and vafmt forwarding passed");
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
    KeelHookFrame detached{};
    const std::uint64_t control_before = g_original_calls.load(std::memory_order_acquire);
    const std::uint32_t callbacks_before = g_control_calls.load(std::memory_order_acquire);
    const std::uint32_t leaders_before = g_control_leader_calls.load(std::memory_order_acquire);
    const std::uint32_t follower_pre_before =
        g_control_follower_pre_calls.load(std::memory_order_acquire);
    const std::uint32_t follower_post_before =
        g_control_follower_post_calls.load(std::memory_order_acquire);
    const std::uint32_t recalled_pre_before =
        g_control_recalled_pre_calls.load(std::memory_order_acquire);
    const std::uint32_t recalled_post_before =
        g_control_recalled_post_calls.load(std::memory_order_acquire);
    const KeelResult detached_original = g_hook->call_original(g_plugin, &detached);
    const KeelResult detached_recall = g_hook->recall(g_plugin, &detached);
    const KeelResult disable =
        g_hook->set_callback_enabled(g_plugin, g_control_callback, KEEL_FALSE);
    const std::int32_t disabled_result = KeelHookControlFixtureTarget(10, 2);
    const std::uint32_t disabled_calls = g_control_calls.load(std::memory_order_acquire);
    const KeelResult enable =
        g_hook->set_callback_enabled(g_plugin, g_control_callback, KEEL_TRUE);
    const std::array<std::int32_t, 5> control_results{
        KeelHookControlFixtureTarget(100, 1),
        KeelHookControlFixtureTarget(200, 1),
        KeelHookControlFixtureTarget(201, 1),
        KeelHookControlFixtureTarget(300, 1),
        KeelHookControlFixtureTarget(400, 1)
    };
    const std::uint64_t original_delta =
        g_original_calls.load(std::memory_order_acquire) - control_before;
    const std::uint32_t callback_delta =
        g_control_calls.load(std::memory_order_acquire) - callbacks_before;
    const std::uint32_t leader_delta =
        g_control_leader_calls.load(std::memory_order_acquire) - leaders_before;
    const std::uint32_t follower_pre_delta =
        g_control_follower_pre_calls.load(std::memory_order_acquire) - follower_pre_before;
    const std::uint32_t follower_post_delta =
        g_control_follower_post_calls.load(std::memory_order_acquire) - follower_post_before;
    const std::uint32_t recalled_pre_delta =
        g_control_recalled_pre_calls.load(std::memory_order_acquire) - recalled_pre_before;
    const std::uint32_t recalled_post_delta =
        g_control_recalled_post_calls.load(std::memory_order_acquire) - recalled_post_before;
    const std::uint32_t control_errors = g_control_errors.load(std::memory_order_acquire);
    const KeelResult remove_leader = g_hook->remove_callback(g_plugin, g_control_leader);
    const KeelResult remove_control = g_hook->remove_callback(g_plugin, g_control_callback);
    const KeelResult remove_follower = g_hook->remove_callback(g_plugin, g_control_follower);
    const KeelResult release_control = g_hook->release_target(g_plugin, g_control_target);
    const std::int32_t restored_result = KeelHookControlFixtureTarget(8, 9);
    if (detached_original != KEEL_RESULT_NOT_READY || detached_recall != KEEL_RESULT_NOT_READY ||
        disable != KEEL_RESULT_OK || disabled_result != 102 || disabled_calls != callbacks_before ||
        enable != KEEL_RESULT_OK ||
        control_results != std::array<std::int32_t, 5>{23, 45, 999, 777, 888} ||
        original_delta != 5 || callback_delta != 5 || leader_delta != 6 ||
        follower_pre_delta != 6 || follower_post_delta != 6 || recalled_pre_delta != 2 ||
        recalled_post_delta != 2 || control_errors != 0 || remove_leader != KEEL_RESULT_OK ||
        remove_control != KEEL_RESULT_OK || remove_follower != KEEL_RESULT_OK ||
        release_control != KEEL_RESULT_OK || restored_result != 89)
    {
        char message[384]{};
        std::snprintf(
            message,
            sizeof(message),
            "KeelHook explicit control semantics failed: results=%d,%d,%d,%d,%d deltas=%llu,%u,%u,%u,%u,%u,%u errors=%u",
            control_results[0],
            control_results[1],
            control_results[2],
            control_results[3],
            control_results[4],
            static_cast<unsigned long long>(original_delta),
            callback_delta,
            leader_delta,
            follower_pre_delta,
            follower_post_delta,
            recalled_pre_delta,
            recalled_post_delta,
            control_errors);
        Log(KEEL_LOG_ERROR, message);
        return;
    }
    g_control_leader = 0;
    g_control_callback = 0;
    g_control_follower = 0;
    g_control_target = 0;
    Log(KEEL_LOG_INFO, "detour, virtual scopes, aggregate calls, ordering, recursion, action semantics, explicit control, and concurrency passed");
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
    if (g_hook->set_callback_enabled(g_plugin, g_override, KEEL_FALSE) != KEEL_RESULT_OK ||
        g_hook->set_callback_enabled(g_plugin, g_low, KEEL_FALSE) != KEEL_RESULT_OK)
    {
        Log(KEEL_LOG_ERROR, "KeelHook one-callback benchmark setup failed");
        return;
    }
    const std::uint64_t one_hook = BenchmarkTarget(83);
    if (!g_benchmark_no_hook || !one_hook || !g_benchmark_multi_plugin)
    {
        Log(KEEL_LOG_ERROR, "KeelHook dispatch benchmark failed");
        return;
    }
    char benchmark[256]{};
    std::snprintf(
        benchmark,
        sizeof(benchmark),
        "dispatch benchmark ns/call: no-hook=%llu one-hook=%llu multi-plugin=%llu",
        static_cast<unsigned long long>(g_benchmark_no_hook / 20000u),
        static_cast<unsigned long long>(one_hook / 20000u),
        static_cast<unsigned long long>(g_benchmark_multi_plugin / 20000u));
    Log(KEEL_LOG_INFO, benchmark);
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

extern "C" KEELS2_PLUGIN_EXPORT KEELHOOK_NOINLINE std::int32_t KeelHookControlFixtureTarget(
    std::int32_t left,
    std::int32_t right)
{
    g_original_calls.fetch_add(1, std::memory_order_relaxed);
    return left * 10 + right;
}

extern "C" KEELS2_PLUGIN_EXPORT KEELHOOK_NOINLINE KeelHookFixtureObject
KeelHookObjectFixtureTarget(KeelHookFixtureObject value)
{
    g_object_original_calls.fetch_add(1, std::memory_order_relaxed);
    value.value += 10;
    return value;
}

extern "C" KEELS2_PLUGIN_EXPORT KEELHOOK_NOINLINE std::int32_t&
KeelHookReferenceFixtureTarget(bool alternate)
{
    g_reference_original_calls.fetch_add(1, std::memory_order_relaxed);
    return alternate ? g_reference_alternate : g_reference_primary;
}

extern "C" KEELS2_PLUGIN_EXPORT KEELHOOK_NOINLINE std::int32_t KeelHookVafmtFixtureTarget(
    std::int32_t prefix,
    const char* format,
    ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(
        g_vafmt_seen.data(),
        g_vafmt_seen.size(),
        format,
        arguments);
    va_end(arguments);
    g_vafmt_seen.back() = '\0';
    return written < 0 ? -1 : prefix + written;
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
    const void* service_v3 = reinterpret_cast<const void*>(1);
    const void* service_v4 = reinterpret_cast<const void*>(1);
    if (api->query_service(plugin, "missing.service", 1, &service) != KEEL_RESULT_NOT_FOUND || service ||
        api->query_service(plugin, KEELHOOK_SERVICE_NAME, KEELHOOK_API_VERSION + 1, &service) !=
            KEEL_RESULT_INCOMPATIBLE || service ||
        api->query_service(plugin, KEELHOOK_SERVICE_NAME, KEELHOOK_API_VERSION_3, &service_v3) !=
            KEEL_RESULT_OK || !service_v3 ||
        api->query_service(plugin, KEELHOOK_SERVICE_NAME, KEELHOOK_API_VERSION_4, &service_v4) !=
            KEEL_RESULT_OK || !service_v4 ||
        api->query_service(plugin, KEELHOOK_SERVICE_NAME, KEELHOOK_API_VERSION, &service) != KEEL_RESULT_OK ||
        !service)
    {
        return KEEL_FALSE;
    }
    const auto* hook_v3 = static_cast<const KeelHookApiV3*>(service_v3);
    const auto* hook_v4 = static_cast<const KeelHookApiV4*>(service_v4);
    if (hook_v3->size != sizeof(KeelHookApiV3) ||
        hook_v3->api_version != KEELHOOK_API_VERSION_3 || !hook_v3->resolve_target ||
        !hook_v3->resolve_virtual_target || hook_v4->size != sizeof(KeelHookApiV4) ||
        hook_v4->api_version != KEELHOOK_API_VERSION_4 || !hook_v4->resolve_target ||
        !hook_v4->call_original || !hook_v4->recall || !hook_v4->set_callback_enabled)
    {
        return KEEL_FALSE;
    }
    g_hook = static_cast<const KeelHookApi*>(service);
    if (g_hook->size != sizeof(KeelHookApi) || g_hook->api_version != KEELHOOK_API_VERSION ||
        !g_hook->resolve_virtual_target)
    {
        return KEEL_FALSE;
    }
    g_benchmark_no_hook = BenchmarkTarget(73);
    if (!g_benchmark_no_hook)
    {
        return KEEL_FALSE;
    }
    if (!FuzzInputContracts(api, plugin))
    {
        return KEEL_FALSE;
    }
    Log(KEEL_LOG_INFO, "descriptor and service-query fuzz passed");

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
    const KeelHookPrototypeV4 legacy_prototype{
        sizeof(KeelHookPrototypeV4),
        prototype.calling_convention,
        prototype.return_type,
        prototype.argument_count,
        prototype.argument_types,
        prototype.return_aggregate,
        prototype.argument_aggregates,
        prototype.fixed_argument_count,
        prototype.flags
    };
    KeelHookTargetHandle current_alias{};
    if (hook_v4->resolve_target(plugin, &direct, &legacy_prototype, &g_target) != KEEL_RESULT_OK ||
        !g_target ||
        g_hook->resolve_target(plugin, &direct, &prototype, &current_alias) != KEEL_RESULT_OK ||
        current_alias != g_target)
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
    KeelHookTargetSpec control_direct = direct;
    control_direct.address = FunctionAddress(&KeelHookControlFixtureTarget);
    if (g_hook->resolve_target(
            plugin,
            &control_direct,
            &prototype,
            &g_control_target) != KEEL_RESULT_OK || !g_control_target ||
        !AddCallback(
            g_control_target,
            &ControlLeaderCallback,
            KH_PHASE_PRE,
            100,
            g_control_leader) ||
        !AddCallback(
            g_control_target,
            &ControlCallback,
            KH_PHASE_PRE,
            0,
            g_control_callback) ||
        !AddCallback(
            g_control_target,
            &ControlFollowerCallback,
            KH_PHASE_BOTH,
            -100,
            g_control_follower))
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
    KeelHookTargetSpec profile_target = direct;
    profile_target.source = KH_TARGET_PROFILE;
    profile_target.symbol = "fixture.missing.target";
    profile_target.address = nullptr;
    if (g_hook->resolve_target(
            plugin,
            &profile_target,
            &prototype,
            &rejected) != KEEL_RESULT_NOT_FOUND || rejected)
    {
        return KEEL_FALSE;
    }
    profile_target.module = "invalid";
    if (g_hook->resolve_target(
            plugin,
            &profile_target,
            &prototype,
            &rejected) != KEEL_RESULT_INVALID_ARGUMENT || rejected)
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
        profile,
        0,
        0
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
