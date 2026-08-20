#include "keelhook_virtual_fixture.h"

#include <cstddef>
#include <cstring>

#if defined(_MSC_VER)
#define KEELHOOK_VIRTUAL_NOINLINE __declspec(noinline)
#else
#define KEELHOOK_VIRTUAL_NOINLINE __attribute__((noinline))
#endif

namespace
{

class VirtualFixtureImplementation final : public KeelHookVirtualFixtureInterface
{
public:
    explicit VirtualFixtureImplementation(std::int32_t bias)
        : bias_(bias)
    {
    }

    std::int32_t First(std::int32_t value) override
    {
        return bias_ + value;
    }

    std::int32_t Second(std::int32_t value) override
    {
        return bias_ * 2 + value;
    }

    KeelHookFixtureAggregate Aggregate(KeelHookFixtureAggregate value) override
    {
        value.coordinates.integer += bias_;
        value.coordinates.fractional += static_cast<float>(bias_) / 100.0F;
        value.marker += static_cast<std::uint64_t>(bias_);
        return value;
    }

private:
    std::int32_t bias_{};
};

VirtualFixtureImplementation g_first{100};
VirtualFixtureImplementation g_second{200};

std::int32_t Call(void* instance, std::size_t index, std::int32_t value)
{
    using Method = std::int32_t (*)(void*, std::int32_t);
    void* address = (*static_cast<void***>(instance))[index];
    Method method{};
    static_assert(sizeof(method) == sizeof(address));
    std::memcpy(&method, &address, sizeof(method));
    return method(instance, value);
}

}

extern "C" void* KeelHookVirtualFixtureFirst()
{
    return &g_first;
}

extern "C" void* KeelHookVirtualFixtureSecond()
{
    return &g_second;
}

extern "C" KEELHOOK_VIRTUAL_NOINLINE std::int32_t KeelHookVirtualFixtureCallFirst(
    void* instance,
    std::int32_t value)
{
    return Call(instance, 0, value);
}

extern "C" KEELHOOK_VIRTUAL_NOINLINE std::int32_t KeelHookVirtualFixtureCallSecond(
    void* instance,
    std::int32_t value)
{
    return Call(instance, 1, value);
}

extern "C" KEELHOOK_VIRTUAL_NOINLINE KeelHookFixtureAggregate KeelHookVirtualFixtureCallAggregate(
    void* instance,
    KeelHookFixtureAggregate value)
{
    return static_cast<KeelHookVirtualFixtureInterface*>(instance)->Aggregate(value);
}
