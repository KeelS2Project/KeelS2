#include <dyncall.h>
#include <dyncall_args.h>
#include <dyncall_callback.h>

#include "keelhook_virtual_fixture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{

using Coordinates = KeelHookFixtureCoordinates;
using Aggregate = KeelHookFixtureAggregate;

struct AggregateDescriptors
{
    AggregateDescriptors()
    {
        coordinates = dcNewAggr(2, sizeof(Coordinates));
        aggregate = dcNewAggr(2, sizeof(Aggregate));
        if (!coordinates || !aggregate)
        {
            return;
        }
        dcAggrField(
            coordinates,
            DC_SIGCHAR_INT,
            static_cast<DCint>(offsetof(Coordinates, integer)),
            1);
        dcAggrField(
            coordinates,
            DC_SIGCHAR_FLOAT,
            static_cast<DCint>(offsetof(Coordinates, fractional)),
            1);
        dcCloseAggr(coordinates);
        dcAggrField(
            aggregate,
            DC_SIGCHAR_AGGREGATE,
            static_cast<DCint>(offsetof(Aggregate, coordinates)),
            1,
            coordinates);
        dcAggrField(
            aggregate,
            DC_SIGCHAR_ULONGLONG,
            static_cast<DCint>(offsetof(Aggregate, marker)),
            1);
        dcCloseAggr(aggregate);
    }

    ~AggregateDescriptors()
    {
        if (aggregate)
        {
            dcFreeAggr(aggregate);
        }
        if (coordinates)
        {
            dcFreeAggr(coordinates);
        }
    }

    explicit operator bool() const noexcept
    {
        return coordinates && aggregate;
    }

    DCaggr* coordinates{};
    DCaggr* aggregate{};
};

bool Equal(const Aggregate& left, const Aggregate& right)
{
    return left.coordinates.integer == right.coordinates.integer &&
        left.coordinates.fractional == right.coordinates.fractional &&
        left.marker == right.marker;
}

void* Address(auto function)
{
    static_assert(sizeof(function) == sizeof(void*));
    void* output{};
    std::memcpy(&output, &function, sizeof(output));
    return output;
}

template <typename Function>
Function Callable(void* address)
{
    static_assert(sizeof(Function) == sizeof(address));
    Function output{};
    std::memcpy(&output, &address, sizeof(output));
    return output;
}

Aggregate FreeTarget(Aggregate value)
{
    value.coordinates.integer += 1;
    value.coordinates.fractional += 2.0F;
    value.marker += 3;
    return value;
}

DCsigchar FreeDispatch(DCCallback*, DCArgs* arguments, DCValue* result, void*)
{
    Aggregate value{};
    dcbArgAggr(arguments, &value);
    value.coordinates.integer += 10;
    value.coordinates.fractional += 20.0F;
    value.marker += 30;
    dcbReturnAggr(arguments, result, &value);
    return DC_SIGCHAR_AGGREGATE;
}

struct MethodState
{
    void* original{};
    DCaggr* aggregate{};
    bool failed{};
};

DCsigchar MethodDispatch(DCCallback*, DCArgs* arguments, DCValue* result, void* user_data)
{
    auto& state = *static_cast<MethodState*>(user_data);
    void* instance = dcbArgPointer(arguments);
    Aggregate value{};
    dcbArgAggr(arguments, &value);
    DCCallVM* machine = dcNewCallVM(512);
    Aggregate output{};
    if (!machine)
    {
        state.failed = true;
    }
    else
    {
        dcMode(machine, DC_CALL_C_DEFAULT_THIS);
        dcBeginCallAggr(machine, state.aggregate);
        dcArgPointer(machine, instance);
        dcArgAggr(machine, state.aggregate, &value);
        dcCallAggr(machine, state.original, state.aggregate, &output);
        state.failed = dcGetError(machine) != DC_ERROR_NONE;
        dcFree(machine);
    }
    output.coordinates.integer += 1000;
    output.coordinates.fractional += 2000.0F;
    output.marker += 3000;
    dcbReturnAggr(arguments, result, &output);
    return DC_SIGCHAR_AGGREGATE;
}

}

int main()
{
    AggregateDescriptors descriptors;
    if (!descriptors)
    {
        return 1;
    }
    const Aggregate input{{4, 5.0F}, 6};

    DCCallVM* machine = dcNewCallVM(512);
    if (!machine)
    {
        return 2;
    }
    Aggregate direct{};
    dcMode(machine, DC_CALL_C_DEFAULT);
    dcBeginCallAggr(machine, descriptors.aggregate);
    dcArgAggr(machine, descriptors.aggregate, &input);
    dcCallAggr(machine, Address(&FreeTarget), descriptors.aggregate, &direct);
    const bool direct_ok = dcGetError(machine) == DC_ERROR_NONE &&
        Equal(direct, Aggregate{{5, 7.0F}, 9});
    dcFree(machine);
    if (!direct_ok)
    {
        return 3;
    }

    std::array<DCaggr*, 2> free_aggregates{descriptors.aggregate, descriptors.aggregate};
    DCCallback* free_callback = dcbNewCallback2(
        "A)A",
        &FreeDispatch,
        nullptr,
        free_aggregates.data());
    if (!free_callback)
    {
        return 4;
    }
    using FreeFunction = Aggregate (*)(Aggregate);
    const Aggregate free_result = Callable<FreeFunction>(free_callback)(input);
    dcbFreeCallback(free_callback);
    if (!Equal(free_result, Aggregate{{14, 25.0F}, 36}))
    {
        return 5;
    }

    void* target = KeelHookVirtualFixtureFirst();
    if (!Equal(
            KeelHookVirtualFixtureCallAggregate(target, input),
            Aggregate{{104, 6.0F}, 106}))
    {
        return 6;
    }
    void*** object = static_cast<void***>(target);
    void** original_table = *object;
    MethodState state{original_table[2], descriptors.aggregate, false};
    std::array<DCaggr*, 2> method_aggregates{descriptors.aggregate, descriptors.aggregate};
    DCCallback* method_callback = dcbNewCallback2(
        "_*pA)A",
        &MethodDispatch,
        &state,
        method_aggregates.data());
    if (!method_callback)
    {
        return 7;
    }
#if defined(_WIN32)
    constexpr std::size_t header_count = 1;
#else
    constexpr std::size_t header_count = 2;
#endif
    std::array<void*, header_count + 3> shadow_storage{};
    for (std::size_t index{}; index < header_count; ++index)
    {
        const auto source = static_cast<std::ptrdiff_t>(index) -
            static_cast<std::ptrdiff_t>(header_count);
        shadow_storage[index] = original_table[source];
    }
    void** shadow_table = shadow_storage.data() + header_count;
    shadow_table[0] = original_table[0];
    shadow_table[1] = original_table[1];
    shadow_table[2] = method_callback;
    *object = shadow_table;
    const Aggregate method_result = KeelHookVirtualFixtureCallAggregate(target, input);
    *object = original_table;
    dcbFreeCallback(method_callback);
    if (state.failed || !Equal(method_result, Aggregate{{1104, 2006.0F}, 3106}) ||
        !Equal(
            KeelHookVirtualFixtureCallAggregate(target, input),
            Aggregate{{104, 6.0F}, 106}))
    {
        return 8;
    }
    return 0;
}
