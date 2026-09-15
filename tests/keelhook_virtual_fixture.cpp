#include "keelhook_virtual_fixture.h"

#include <cstddef>
#include <cstdarg>
#include <cstdio>
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

    std::int32_t Vafmt(std::int32_t prefix, const char* format, ...) override
    {
        char buffer[4096]{};
        va_list arguments;
        va_start(arguments, format);
        const int written = std::vsnprintf(buffer, sizeof(buffer), format, arguments);
        va_end(arguments);
        return written < 0 ? -1 : prefix + written;
    }

private:
    std::int32_t bias_{};
};

class MultipleVirtualFixtureImplementation final : public KeelHookVirtualFixtureMultiple
{
public:
    explicit MultipleVirtualFixtureImplementation(std::int32_t bias)
        : bias_(bias)
    {
    }

    std::int32_t Primary(std::int32_t value) override
    {
        return bias_ + value;
    }

    std::int32_t Adjusted(std::int32_t value) override
    {
        return bias_ * 3 + value;
    }

private:
    std::int32_t bias_{};
};

VirtualFixtureImplementation g_first{100};
VirtualFixtureImplementation g_second{200};
MultipleVirtualFixtureImplementation g_multiple{300};

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

extern "C" KEELHOOK_VIRTUAL_NOINLINE std::int32_t KeelHookVirtualFixtureCallVafmt(
    void* instance,
    std::int32_t prefix)
{
    return static_cast<KeelHookVirtualFixtureInterface*>(instance)->Vafmt(
        prefix,
        "i=%d d=%.1f s=%s",
        17,
        2.5,
        "keel");
}

extern "C" void* KeelHookVirtualFixtureMultipleInstance()
{
    return static_cast<KeelHookVirtualFixtureMultiple*>(&g_multiple);
}

extern "C" std::int64_t KeelHookVirtualFixtureSecondaryOffset()
{
    auto* instance = static_cast<KeelHookVirtualFixtureMultiple*>(&g_multiple);
    auto* secondary = static_cast<KeelHookVirtualFixtureSecondary*>(instance);
    return reinterpret_cast<const char*>(secondary) - reinterpret_cast<const char*>(instance);
}

extern "C" KEELHOOK_VIRTUAL_NOINLINE std::int32_t KeelHookVirtualFixtureCallPrimary(
    void* instance,
    std::int32_t value)
{
    return static_cast<KeelHookVirtualFixtureMultiple*>(instance)->Primary(value);
}

extern "C" KEELHOOK_VIRTUAL_NOINLINE std::int32_t KeelHookVirtualFixtureCallAdjusted(
    void* instance,
    std::int32_t value)
{
    return static_cast<KeelHookVirtualFixtureMultiple*>(instance)->Adjusted(value);
}
