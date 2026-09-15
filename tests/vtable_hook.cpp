#include "keelhook_virtual_fixture.h"

#include <keels2/hooking/vtable.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>

namespace
{

using Method = std::int32_t (*)(void*, std::int32_t);

Method g_shared_original{};
Method g_first_original{};
Method g_second_original{};

void* Address(Method function)
{
    static_assert(sizeof(function) == sizeof(void*));
    void* result{};
    std::memcpy(&result, &function, sizeof(result));
    return result;
}

Method Function(void* address)
{
    static_assert(sizeof(Method) == sizeof(address));
    Method result{};
    std::memcpy(&result, &address, sizeof(result));
    return result;
}

std::int32_t SharedReplacement(void* instance, std::int32_t value)
{
    return g_shared_original(instance, value) + 1000;
}

std::int32_t FirstReplacement(void* instance, std::int32_t value)
{
    return g_first_original(instance, value) + 2000;
}

std::int32_t SecondReplacement(void* instance, std::int32_t value)
{
    return g_second_original(instance, value) + 3000;
}

}

int main()
{
    void* first = KeelHookVirtualFixtureFirst();
    void* second = KeelHookVirtualFixtureSecond();
    void** first_slot{};
    void* first_original{};
    void** second_slot{};
    void* second_original{};
    if (keels2::hooking::ResolveVtableSlot(first, 0, first_slot, first_original) !=
            keels2::hooking::VtableHookResult::ok ||
        keels2::hooking::ResolveVtableSlot(second, 0, second_slot, second_original) !=
            keels2::hooking::VtableHookResult::ok ||
        first_slot != second_slot || first_original != second_original)
    {
        return 1;
    }

    g_shared_original = Function(first_original);
    std::unique_ptr<keels2::hooking::SharedVtableHook> shared;
    if (keels2::hooking::SharedVtableHook::Create(
            first_slot,
            Address(&SharedReplacement),
            shared) != keels2::hooking::VtableHookResult::ok ||
        !shared || shared->Enable() != keels2::hooking::VtableHookResult::ok ||
        KeelHookVirtualFixtureCallFirst(first, 5) != 1105 ||
        KeelHookVirtualFixtureCallFirst(second, 5) != 1205 ||
        shared->Disable() != keels2::hooking::VtableHookResult::ok ||
        KeelHookVirtualFixtureCallFirst(first, 5) != 105 ||
        KeelHookVirtualFixtureCallFirst(second, 5) != 205)
    {
        return 2;
    }
    std::atomic<bool> stop{};
    std::atomic<bool> failed{};
    std::array<std::thread, 4> workers;
    for (std::size_t index{}; index < workers.size(); ++index)
    {
        workers[index] = std::thread([&, index] {
            void* object = index % 2 == 0 ? first : second;
            const std::int32_t original = index % 2 == 0 ? 105 : 205;
            while (!stop.load(std::memory_order_acquire))
            {
                const std::int32_t actual = KeelHookVirtualFixtureCallFirst(object, 5);
                if (actual != original && actual != original + 1000)
                {
                    failed.store(true, std::memory_order_release);
                    stop.store(true, std::memory_order_release);
                }
            }
        });
    }
    for (std::size_t iteration{}; iteration < 250 && !failed.load(std::memory_order_acquire); ++iteration)
    {
        if (shared->Enable() != keels2::hooking::VtableHookResult::ok ||
            shared->Disable() != keels2::hooking::VtableHookResult::ok)
        {
            failed.store(true, std::memory_order_release);
        }
    }
    stop.store(true, std::memory_order_release);
    for (auto& worker : workers)
    {
        worker.join();
    }
    if (failed.load(std::memory_order_acquire))
    {
        return 5;
    }

    std::shared_ptr<keels2::hooking::InstanceVtable> instance;
    if (keels2::hooking::InstanceVtable::Create(first, 2, instance) !=
            keels2::hooking::VtableHookResult::ok ||
        !instance || !instance->Intact())
    {
        return 3;
    }
    g_first_original = Function(instance->Original(0));
    g_second_original = Function(instance->Original(1));
    if (instance->Enable(0, Address(&FirstReplacement)) !=
            keels2::hooking::VtableHookResult::ok ||
        instance->Enable(1, Address(&SecondReplacement)) !=
            keels2::hooking::VtableHookResult::ok ||
        KeelHookVirtualFixtureCallFirst(first, 5) != 2105 ||
        KeelHookVirtualFixtureCallSecond(first, 5) != 3205 ||
        KeelHookVirtualFixtureCallFirst(second, 5) != 205 ||
        instance->Disable(0, Address(&FirstReplacement)) !=
            keels2::hooking::VtableHookResult::ok ||
        KeelHookVirtualFixtureCallFirst(first, 5) != 105 ||
        KeelHookVirtualFixtureCallSecond(first, 5) != 3205 ||
        instance->Disable(1, Address(&SecondReplacement)) !=
            keels2::hooking::VtableHookResult::ok ||
        !instance->Empty() || !instance->Intact() ||
        KeelHookVirtualFixtureCallFirst(first, 5) != 105 ||
        KeelHookVirtualFixtureCallSecond(first, 5) != 205)
    {
        return 4;
    }
    stop.store(false, std::memory_order_release);
    failed.store(false, std::memory_order_release);
    for (auto& worker : workers)
    {
        worker = std::thread([&] {
            while (!stop.load(std::memory_order_acquire))
            {
                const std::int32_t actual = KeelHookVirtualFixtureCallFirst(first, 5);
                if (actual != 105 && actual != 2105)
                {
                    failed.store(true, std::memory_order_release);
                    stop.store(true, std::memory_order_release);
                }
            }
        });
    }
    for (std::size_t iteration{}; iteration < 250 && !failed.load(std::memory_order_acquire); ++iteration)
    {
        if (instance->Enable(0, Address(&FirstReplacement)) !=
                keels2::hooking::VtableHookResult::ok ||
            instance->Disable(0, Address(&FirstReplacement)) !=
                keels2::hooking::VtableHookResult::ok)
        {
            failed.store(true, std::memory_order_release);
        }
    }
    stop.store(true, std::memory_order_release);
    for (auto& worker : workers)
    {
        worker.join();
    }
    if (failed.load(std::memory_order_acquire) || !instance->Empty() || !instance->Intact())
    {
        return 6;
    }
    return 0;
}
