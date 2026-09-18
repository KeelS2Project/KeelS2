#include "keelcall_fixture.h"
#include <keels2/keelhook.hpp>

#include <array>
#include <cstring>
#include <limits>
#include <thread>

#if defined(_MSC_VER)
#define CALL_NOINLINE __declspec(noinline)
#else
#define CALL_NOINLINE __attribute__((noinline))
#endif

namespace keelcall_fixture
{
namespace
{
template<class Function> void* Address(Function function)
{
    void* address{};
    static_assert(sizeof(address) == sizeof(function));
    std::memcpy(&address, &function, sizeof(address));
    return address;
}

template<class Function> KeelHookTargetHandle Resolve(const KeelHookApi& hooks,
    KeelPluginHandle plugin, Function function, const KeelHookPrototype& prototype,
    std::uint32_t flags = 0)
{
    KeelHookTargetSpec spec{};
    spec.size = sizeof(spec);
    spec.source = KH_TARGET_ADDRESS;
    spec.mechanism = KH_MECHANISM_DETOUR;
    spec.flags = flags;
    spec.address = Address(function);
    KeelHookTargetHandle target{};
    return hooks.resolve_target(plugin, &spec, &prototype, &target) == KEEL_RESULT_OK ? target : 0;
}

template<class T> CALL_NOINLINE T Echo(T value) { return value; }
CALL_NOINLINE std::int32_t AddOne(std::int32_t value) { return value + 1; }
CALL_NOINLINE void Nothing() {}
CALL_NOINLINE void* Method(void* self) { return self; }
CALL_NOINLINE void Variadic(const char*, ...) {}
struct Tiny { std::int32_t value; };
CALL_NOINLINE Tiny Aggregate() { return {17}; }
CALL_NOINLINE std::uint64_t Mixed(std::uint64_t a, std::uint64_t b, std::uint64_t c,
    std::uint64_t d, std::uint64_t e, std::uint64_t f, std::uint64_t g, std::uint64_t h,
    double i, double j, double k, double l, double m, double n, double o, double p, double q, double r)
{
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6 && g == 7 && h == 8 &&
        i == 9.5 && j == 10.5 && k == 11.5 && l == 12.5 && m == 13.5 && n == 14.5 &&
        o == 15.5 && p == 16.5 && q == 17.5 && r == 18.5 ? UINT64_MAX : 0;
}

template<class T> bool Scalar(const KeelCallApi& calls, const KeelHookApi& hooks,
    KeelPluginHandle plugin, KeelHookValue input, T expected)
{
    const auto target = Resolve(hooks, plugin, &Echo<T>, keels2::kh::Prototype<T(T)>::value);
    if (!target) return false;
    KeelHookValue result{};
    const auto status = calls.invoke(plugin, target, 0, &input, 1, &result);
    // Compare only the active scalar field; padding and inactive union bytes
    // are not part of the public value contract.
    T actual{};
    if constexpr (std::is_same_v<T, bool>) actual = result.scalar.boolean != KEEL_FALSE;
    else std::memcpy(&actual, &result.scalar, sizeof(T));
    const bool okay = status == KEEL_RESULT_OK && result.type == input.type && actual == expected;
    return hooks.release_target(plugin, target) == KEEL_RESULT_OK && okay;
}

struct Context
{
    const KeelCallApi* calls{};
    const KeelHookApi* hooks{};
    KeelPluginHandle plugin{};
    KeelHookTargetHandle target{};
    KeelHookCallbackHandle callback{};
    std::uint32_t visits{};
    std::uint32_t busy{};
    bool recurse{};
    bool okay{true};
};

KeelHookAction Callback(KeelHookFrame* frame, void* data)
{
    auto& context = *static_cast<Context*>(data);
    if (frame->phase == KH_PHASE_POST)
    {
        frame->result.scalar.int32 *= 10;
        return KH_ACTION_OVERRIDE;
    }
    ++context.visits;
    KeelHookValue argument{};
    argument.type = KH_VALUE_INT32;
    argument.scalar.int32 = 3;
    KeelHookValue result{};
    if (context.recurse)
    {
        const auto status = context.calls->invoke(context.plugin, context.target,
            KEELCALL_INVOKE_HOOKS, &argument, 1, &result);
        if (status == KEEL_RESULT_BUSY) ++context.busy;
        else context.okay = context.okay && status == KEEL_RESULT_OK;
        // Keep each recursion level independent; avoid exponential arithmetic.
        frame->result.scalar.int32 = 1;
        return KH_ACTION_SUPERSEDE;
    }
    context.okay = context.okay && context.calls->invoke(context.plugin, context.target,
        0, &argument, 1, &result) == KEEL_RESULT_OK && result.scalar.int32 == 4;
    KeelHookCallbackSpec spec{sizeof(spec), KH_PHASE_PRE, 0, 0, &Callback, data};
    KeelHookCallbackHandle added{};
    context.okay = context.okay &&
        context.hooks->release_target(context.plugin, context.target) == KEEL_RESULT_BUSY &&
        context.hooks->remove_callback(context.plugin, context.callback) == KEEL_RESULT_BUSY &&
        context.hooks->add_callback(context.plugin, context.target, &spec, &added) == KEEL_RESULT_BUSY && !added;
    frame->arguments[0].scalar.int32 += 5;
    return KH_ACTION_CONTINUE;
}
}

bool Check(const KeelHostApi& host, const KeelHookApi& hooks, KeelPluginHandle plugin)
{
    const void* service = reinterpret_cast<const void*>(1);
    if (host.query_service(plugin, KEELCALL_SERVICE_NAME, KEELCALL_API_VERSION + 1, &service) !=
            KEEL_RESULT_INCOMPATIBLE || service ||
        host.query_service(plugin, KEELCALL_SERVICE_NAME, KEELCALL_API_VERSION, &service) !=
            KEEL_RESULT_OK || !service) return false;
    const auto& calls = *static_cast<const KeelCallApi*>(service);
    if (calls.size != sizeof(calls) || calls.api_version != KEELCALL_API_VERSION || !calls.invoke)
        return false;
    KeelHookValue value{};
#define CHECK_SCALAR(kind, member, native_type, sample) \
    value = {}; value.type = kind; value.scalar.member = sample; \
    if (!Scalar<native_type>(calls, hooks, plugin, value, sample)) return false
    CHECK_SCALAR(KH_VALUE_BOOL, boolean, bool, true);
    CHECK_SCALAR(KH_VALUE_INT8, int8, std::int8_t, INT8_MIN);
    CHECK_SCALAR(KH_VALUE_UINT8, uint8, std::uint8_t, UINT8_MAX);
    CHECK_SCALAR(KH_VALUE_INT16, int16, std::int16_t, INT16_MIN);
    CHECK_SCALAR(KH_VALUE_UINT16, uint16, std::uint16_t, UINT16_MAX);
    CHECK_SCALAR(KH_VALUE_INT32, int32, std::int32_t, INT32_MIN);
    CHECK_SCALAR(KH_VALUE_UINT32, uint32, std::uint32_t, UINT32_MAX);
    CHECK_SCALAR(KH_VALUE_INT64, int64, std::int64_t, INT64_MIN);
    CHECK_SCALAR(KH_VALUE_UINT64, uint64, std::uint64_t, UINT64_MAX);
    CHECK_SCALAR(KH_VALUE_FLOAT32, float32, float, -2.75f);
    CHECK_SCALAR(KH_VALUE_FLOAT64, float64, double, 1.0000000000000002);
    CHECK_SCALAR(KH_VALUE_POINTER, pointer, void*, static_cast<void*>(&value));
#undef CHECK_SCALAR
    const auto empty = Resolve(hooks, plugin, &Nothing, keels2::kh::Prototype<void()>::value);
    if (!empty || calls.invoke(plugin, empty, 0, nullptr, 0, &value) != KEEL_RESULT_OK ||
        value.type != KH_VALUE_VOID || hooks.release_target(plugin, empty) != KEEL_RESULT_OK) return false;
    const auto method = Resolve(hooks, plugin, &Method,
        keels2::kh::Prototype<void*(void*)>::value, KH_TARGET_METHOD);
    KeelHookValue self{};
    self.type = KH_VALUE_POINTER;
    if (!method || calls.invoke(plugin, method, 0, &self, 1, &value) != KEEL_RESULT_INVALID_ARGUMENT)
        return false;
    self.scalar.pointer = &self;
    if (calls.invoke(plugin, method, 0, &self, 1, &value) != KEEL_RESULT_OK ||
        value.scalar.pointer != &self || hooks.release_target(plugin, method) != KEEL_RESULT_OK) return false;

    const auto mixed = Resolve(hooks, plugin, &Mixed, keels2::kh::Prototype<decltype(Mixed)>::value);
    std::array<KeelHookValue,18> mixed_arguments{};
    for (std::size_t index{}; index < mixed_arguments.size(); ++index)
    {
        auto& argument = mixed_arguments[index];
        argument.type = index < 8 ? KH_VALUE_UINT64 : KH_VALUE_FLOAT64;
        if (index < 8) argument.scalar.uint64 = index + 1;
        else argument.scalar.float64 = static_cast<double>(index) + 1.5;
    }
    if (!mixed || calls.invoke(plugin, mixed, 0, mixed_arguments.data(),
        static_cast<std::uint32_t>(mixed_arguments.size()), &value) != KEEL_RESULT_OK ||
        value.type != KH_VALUE_UINT64 || value.scalar.uint64 != UINT64_MAX ||
        hooks.release_target(plugin, mixed) != KEEL_RESULT_OK) return false;
    const auto boolean = Resolve(hooks, plugin, &Echo<bool>, keels2::kh::Prototype<bool(bool)>::value);
    KeelHookValue bad_boolean{}; bad_boolean.type = KH_VALUE_BOOL; bad_boolean.scalar.boolean = 2;
    if (!boolean || calls.invoke(plugin, boolean, 0, &bad_boolean, 1, &value) != KEEL_RESULT_INVALID_ARGUMENT ||
        value.type != KH_VALUE_VOID || hooks.release_target(plugin, boolean) != KEEL_RESULT_OK) return false;
    auto variadic_prototype = keels2::kh::Prototype<void(const char*)>::value;
    variadic_prototype.flags = KH_PROTOTYPE_VAFMT;
    const auto variadic = Resolve(hooks, plugin, &Variadic, variadic_prototype);
    if (!variadic || calls.invoke(plugin, variadic, 0, &self, 1, &value) != KEEL_RESULT_UNSUPPORTED ||
        hooks.release_target(plugin, variadic) != KEEL_RESULT_OK) return false;
    const KeelHookAggregateField field{sizeof(field), KH_VALUE_INT32, 0, 1, nullptr};
    const KeelHookAggregate aggregate{sizeof(aggregate), sizeof(Tiny), 1, 0, &field};
    const KeelHookPrototype aggregate_prototype{sizeof(aggregate_prototype), KH_CALL_NATIVE,
        KH_VALUE_AGGREGATE, 0, nullptr, &aggregate, nullptr, 0, 0, nullptr, nullptr};
    const auto aggregate_target = Resolve(hooks, plugin, &Aggregate, aggregate_prototype);
    if (!aggregate_target || calls.invoke(plugin, aggregate_target, 0, nullptr, 0, &value) != KEEL_RESULT_UNSUPPORTED ||
        hooks.release_target(plugin, aggregate_target) != KEEL_RESULT_OK) return false;

    const auto target = Resolve(hooks, plugin, &AddOne,
        keels2::kh::Prototype<std::int32_t(std::int32_t)>::value);
    if (!target) return false;
    KeelHookValue input{};
    input.type = KH_VALUE_INT32;
    input.scalar.int32 = 5;
    if (calls.invoke(plugin, target, 0, &input, 1, &input) != KEEL_RESULT_OK ||
        input.type != KH_VALUE_INT32 || input.scalar.int32 != 6) return false;
    input.scalar.int32 = 5;
    auto rejects = [&](KeelResult expected, KeelPluginHandle owner, KeelHookTargetHandle handle,
        std::uint32_t flags, const KeelHookValue* args, std::uint32_t count) {
        value = input;
        return calls.invoke(owner, handle, flags, args, count, &value) == expected &&
            value.type == KH_VALUE_VOID && value.reserved == 0 && value.scalar.uint64 == 0;
    };
    if (!rejects(KEEL_RESULT_INVALID_ARGUMENT, plugin, target, 2, &input, 1) ||
        !rejects(KEEL_RESULT_INVALID_ARGUMENT, plugin, target, 0, nullptr, 1) ||
        !rejects(KEEL_RESULT_INVALID_ARGUMENT, plugin, target, 0, &input, 33) ||
        !rejects(KEEL_RESULT_INVALID_ARGUMENT, plugin, target, 0, nullptr, 0) ||
        !rejects(KEEL_RESULT_NOT_READY, 0, target, 0, &input, 1) ||
        !rejects(KEEL_RESULT_NOT_FOUND, plugin, UINT64_MAX, 0, &input, 1) ||
        calls.invoke(plugin, target, 0, &input, 1, nullptr) != KEEL_RESULT_INVALID_ARGUMENT) return false;
    input.reserved = 1;
    if (!rejects(KEEL_RESULT_INVALID_ARGUMENT, plugin, target, 0, &input, 1)) return false;
    input.reserved = 0;
    input.type = KH_VALUE_UINT32;
    if (!rejects(KEEL_RESULT_INVALID_ARGUMENT, plugin, target, 0, &input, 1)) return false;
    input.type = KH_VALUE_INT32;
    KeelResult worker{};
    std::thread thread([&] { worker = calls.invoke(plugin, target, 0, &input, 1, &value); });
    thread.join();
    if (worker != KEEL_RESULT_WRONG_THREAD || value.type != KH_VALUE_VOID) return false;

    static Context context;
    context = {&calls, &hooks, plugin, target};
    const KeelHookCallbackSpec spec{sizeof(spec), KH_PHASE_BOTH, 0, 0, &Callback, &context};
    if (hooks.add_callback(plugin, target, &spec, &context.callback) != KEEL_RESULT_OK) return false;
    const bool original = calls.invoke(plugin, target, 0, &input, 1, &value) == KEEL_RESULT_OK &&
        value.scalar.int32 == 6 && context.visits == 0;
    const bool hooked = calls.invoke(plugin, target, KEELCALL_INVOKE_HOOKS, &input, 1, &value) == KEEL_RESULT_OK &&
        value.scalar.int32 == 110 && context.visits == 1 && input.scalar.int32 == 5;
    context.recurse = true;
    context.visits = 0;
    const bool nested = calls.invoke(plugin, target, KEELCALL_INVOKE_HOOKS, &input, 1, &value) == KEEL_RESULT_OK &&
        context.visits == KEELCALL_MAX_DEPTH && context.busy == 1;
    const bool disabled = hooks.set_callback_enabled(plugin, context.callback, KEEL_FALSE) == KEEL_RESULT_OK &&
        calls.invoke(plugin, target, KEELCALL_INVOKE_HOOKS, &input, 1, &value) == KEEL_RESULT_OK &&
        value.scalar.int32 == 6;
    // Always attempt cleanup. Static state also survives a failed removal.
    if (hooks.remove_callback(plugin, context.callback) != KEEL_RESULT_OK) return false;
    const bool released = hooks.release_target(plugin, target) == KEEL_RESULT_OK;
    return original && hooked && nested && disabled && context.okay && released &&
        rejects(KEEL_RESULT_NOT_FOUND, plugin, target, 0, &input, 1);
}
}
