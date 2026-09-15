#ifndef KEELS2_TEST_FACTORY_FIXTURE_H
#define KEELS2_TEST_FACTORY_FIXTURE_H

#include <keels2/bootstrap_api.h>
#include <keels2/factories.h>

#include <array>
#include <atomic>

struct FactoryProbe
{
    virtual int Value() = 0;
};

struct FactoryFixture
{
    const KeelFactoriesApi* api{};
    const KeelSource2Api* source2{};
    KeelPluginHandle plugin{};
    KeelCreateInterfaceFn engine{};
    KeelCreateInterfaceFn server{};
    std::array<KeelFactorySubscriptionHandle, 7> subscriptions{};
    std::atomic<int> mode{};
    std::atomic<unsigned> calls{};
    std::atomic<unsigned> errors{};
    std::atomic<unsigned> order{};
    std::atomic<unsigned> unloaded{};
    std::atomic<bool> entered{};
    std::atomic<bool> release{};
    std::atomic<bool> recurse{};
    std::atomic<bool> remove_self{};
    std::atomic<bool> remove_peer{};
    std::atomic<bool> query_on_unload{};
};

#endif
