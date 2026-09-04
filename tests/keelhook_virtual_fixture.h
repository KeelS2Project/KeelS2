#ifndef KEELS2_TESTS_KEELHOOK_VIRTUAL_FIXTURE_H
#define KEELS2_TESTS_KEELHOOK_VIRTUAL_FIXTURE_H

#include <cstdint>

struct KeelHookFixtureCoordinates
{
    std::int32_t integer;
    float fractional;
};

struct KeelHookFixtureAggregate
{
    KeelHookFixtureCoordinates coordinates;
    std::uint64_t marker;
};

class KeelHookVirtualFixtureInterface
{
public:
    virtual std::int32_t First(std::int32_t value) = 0;
    virtual std::int32_t Second(std::int32_t value) = 0;
    virtual KeelHookFixtureAggregate Aggregate(KeelHookFixtureAggregate value) = 0;
    virtual std::int32_t Vafmt(std::int32_t prefix, const char* format, ...) = 0;
};

class KeelHookVirtualFixturePrimary
{
public:
    virtual std::int32_t Primary(std::int32_t value) = 0;
};

class KeelHookVirtualFixtureSecondary
{
public:
    virtual std::int32_t Adjusted(std::int32_t value) = 0;
};

class KeelHookVirtualFixtureMultiple :
    public KeelHookVirtualFixturePrimary,
    public KeelHookVirtualFixtureSecondary
{
};

extern "C" void* KeelHookVirtualFixtureFirst();
extern "C" void* KeelHookVirtualFixtureSecond();
extern "C" std::int32_t KeelHookVirtualFixtureCallFirst(void* instance, std::int32_t value);
extern "C" std::int32_t KeelHookVirtualFixtureCallSecond(void* instance, std::int32_t value);
extern "C" KeelHookFixtureAggregate KeelHookVirtualFixtureCallAggregate(
    void* instance,
    KeelHookFixtureAggregate value);
extern "C" std::int32_t KeelHookVirtualFixtureCallVafmt(
    void* instance,
    std::int32_t prefix);
extern "C" void* KeelHookVirtualFixtureMultipleInstance();
extern "C" std::int64_t KeelHookVirtualFixtureSecondaryOffset();
extern "C" std::int32_t KeelHookVirtualFixtureCallPrimary(
    void* instance,
    std::int32_t value);
extern "C" std::int32_t KeelHookVirtualFixtureCallAdjusted(
    void* instance,
    std::int32_t value);

#endif
