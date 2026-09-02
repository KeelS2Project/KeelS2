#include <keels2/bootstrap_api.h>
#include <keels2/convar.h>
#include <keels2/cs2/cvar_abi.h>
#include <igameevents.h>
#include <playerslot.h>
#include <keels2/lifecycle.h>
#include <keels2/platform/dynamic_library.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#define KEELS2_TEST_EXPORT __declspec(dllexport)
#else
#define KEELS2_TEST_EXPORT __attribute__((visibility("default")))
#endif

namespace
{

std::mutex g_engine_allocator_mutex;
std::unordered_set<const void*> g_engine_allocations;
std::uint32_t g_engine_allocation_count{};
std::uint32_t g_engine_free_count{};
std::uint32_t g_engine_invalid_free_count{};

bool IsEngineAllocation(const void* value)
{
    std::scoped_lock lock(g_engine_allocator_mutex);
    return value && g_engine_allocations.contains(value);
}

std::size_t EngineOutstandingAllocationCount()
{
    std::scoped_lock lock(g_engine_allocator_mutex);
    return g_engine_allocations.size();
}

std::uint32_t EngineAllocationCount()
{
    std::scoped_lock lock(g_engine_allocator_mutex);
    return g_engine_allocation_count;
}

std::uint32_t EngineFreeCount()
{
    std::scoped_lock lock(g_engine_allocator_mutex);
    return g_engine_free_count;
}

std::uint32_t EngineInvalidFreeCount()
{
    std::scoped_lock lock(g_engine_allocator_mutex);
    return g_engine_invalid_free_count;
}

bool ResetEngineAllocatorDiagnostics()
{
    std::scoped_lock lock(g_engine_allocator_mutex);
    if (!g_engine_allocations.empty())
    {
        return false;
    }
    g_engine_allocation_count = 0;
    g_engine_free_count = 0;
    g_engine_invalid_free_count = 0;
    return true;
}

}

extern "C" KEELS2_TEST_EXPORT char* MemAlloc_StrDupFunc(const char* value)
{
    const char* source = value ? value : "";
    const std::size_t size = std::strlen(source) + 1;
    auto* copy = static_cast<char*>(std::malloc(size));
    if (copy)
    {
        std::memcpy(copy, source, size);
        std::scoped_lock lock(g_engine_allocator_mutex);
        g_engine_allocations.insert(copy);
        ++g_engine_allocation_count;
    }
    return copy;
}

extern "C" KEELS2_TEST_EXPORT void MemAlloc_FreeFunc(void* value)
{
    if (!value)
    {
        return;
    }
    {
        std::scoped_lock lock(g_engine_allocator_mutex);
        if (g_engine_allocations.erase(value) == 0)
        {
            ++g_engine_invalid_free_count;
            return;
        }
        ++g_engine_free_count;
    }
    std::free(value);
}

namespace
{

#define KEELS2_EMPTY_SLOT(number) void Slot##number() override {}

class FakeCvar final : public keels2::cs2::CvarInterface
{
public:
    KEELS2_EMPTY_SLOT(00)
    KEELS2_EMPTY_SLOT(01)
    KEELS2_EMPTY_SLOT(02)
    KEELS2_EMPTY_SLOT(03)
    KEELS2_EMPTY_SLOT(04)
    KEELS2_EMPTY_SLOT(05)
    KEELS2_EMPTY_SLOT(06)
    KEELS2_EMPTY_SLOT(07)
    KEELS2_EMPTY_SLOT(08)
    KEELS2_EMPTY_SLOT(09)
    KEELS2_EMPTY_SLOT(10)

    keels2::cs2::ConVarRef FindConVar(const char* name, bool) override
    {
        std::scoped_lock lock(convar_mutex);
        const auto entry = ConVarByName(name);
        return entry
            ? keels2::cs2::ConVarRef(entry->access_index, 0)
            : keels2::cs2::ConVarRef{};
    }

    KEELS2_EMPTY_SLOT(12)
    KEELS2_EMPTY_SLOT(13)

    void CallChangeCallback(
        keels2::cs2::ConVarRef reference,
        std::int32_t slot,
        const keels2::cs2::ConVarValue* new_value,
        const keels2::cs2::ConVarValue* old_value,
        void*) override
    {
        if (!new_value || !old_value)
        {
            return;
        }
        std::shared_ptr<ConVarEntry> entry;
        std::vector<ConVarCallback> callbacks;
        {
            std::scoped_lock lock(convar_mutex);
            entry = ConVarByAccess(reference.AccessIndex());
            if (!entry)
            {
                return;
            }
            entry->data.flags |= keels2::cs2::kPerformingCallbacksFlag;
            entry->current_value = ReadLiveConVarValue(entry->type, new_value);
            WriteLiveConVarValue(*entry, entry->current_value);
            for (const auto& callback : entry->callbacks)
            {
                if (callback.active)
                {
                    callbacks.push_back(callback);
                }
            }
            ++convar_change_count;
            last_change_slot = slot;
        }
        for (const auto& callback : callbacks)
        {
            keels2::cs2::ConVarObject object{callback.reference, &entry->data};
            callback.provider(
                &object,
                slot,
                new_value,
                old_value,
                nullptr,
                callback.callback);
        }
        {
            std::scoped_lock lock(convar_mutex);
            entry->data.flags &= ~keels2::cs2::kPerformingCallbacksFlag;
        }
        DrainQueuedValues();
    }

    KEELS2_EMPTY_SLOT(15)

    bool CallFilterCallback(
        keels2::cs2::ConVarRef,
        std::int32_t slot,
        const keels2::cs2::ConVarValue*,
        const keels2::cs2::ConVarValue*,
        void*) override
    {
        ++convar_filter_count;
        last_filter_slot = slot;
        const bool accepted = !reject_next_filter;
        reject_next_filter = false;
        return accepted;
    }

    KEELS2_EMPTY_SLOT(17)
    KEELS2_EMPTY_SLOT(18)
    KEELS2_EMPTY_SLOT(19)
    KEELS2_EMPTY_SLOT(20)
    KEELS2_EMPTY_SLOT(21)
    KEELS2_EMPTY_SLOT(22)
    void CallGlobalChangeCallbacks(
        keels2::cs2::ConVarObject*,
        std::int32_t slot,
        const char* new_value,
        const char* old_value,
        void*) override
    {
        ++convar_global_change_count;
        last_global_slot = slot;
        last_global_new = new_value ? new_value : "";
        last_global_old = old_value ? old_value : "";
        DrainQueuedValues();
    }
    KEELS2_EMPTY_SLOT(24)
    KEELS2_EMPTY_SLOT(25)
    KEELS2_EMPTY_SLOT(26)
    KEELS2_EMPTY_SLOT(27)
    KEELS2_EMPTY_SLOT(28)
    KEELS2_EMPTY_SLOT(29)
    KEELS2_EMPTY_SLOT(30)
    KEELS2_EMPTY_SLOT(31)
    KEELS2_EMPTY_SLOT(32)
    KEELS2_EMPTY_SLOT(33)
    KEELS2_EMPTY_SLOT(34)
    KEELS2_EMPTY_SLOT(35)
    KEELS2_EMPTY_SLOT(36)
    KEELS2_EMPTY_SLOT(37)

    void RegisterConVar(
        const keels2::cs2::ConVarCreation& setup,
        std::uint64_t additional_flags,
        keels2::cs2::ConVarRef* reference,
        keels2::cs2::ConVarData** data) override
    {
        if (!reference || !data || !setup.name || !setup.name[0])
        {
            return;
        }
        *reference = {};
        *data = nullptr;

        keels2::cs2::ConVarValue default_value{};
        keels2::cs2::ConVarValue minimum{};
        keels2::cs2::ConVarValue maximum{};
        std::memcpy(
            &default_value,
            setup.value_info.default_value,
            sizeof(default_value));
        if (setup.value_info.has_minimum)
        {
            std::memcpy(
                &minimum,
                setup.value_info.minimum_value,
                sizeof(minimum));
        }
        if (setup.value_info.has_maximum)
        {
            std::memcpy(
                &maximum,
                setup.value_info.maximum_value,
                sizeof(maximum));
        }

        std::array<void*, 3> registration_strings{};
        std::size_t registration_string_count{};
        std::size_t invalid_registration_string_count{};
        if (setup.value_info.type == keels2::cs2::ConVarType::string)
        {
            if (setup.value_info.has_default)
            {
                registration_strings[registration_string_count++] = default_value.string;
            }
            if (setup.value_info.has_minimum)
            {
                registration_strings[registration_string_count++] = minimum.string;
            }
            if (setup.value_info.has_maximum)
            {
                registration_strings[registration_string_count++] = maximum.string;
            }
            for (std::size_t index{}; index < registration_string_count; ++index)
            {
                if (!IsEngineAllocation(registration_strings[index]))
                {
                    ++invalid_registration_string_count;
                }
            }
        }

        bool rejected{};
        {
            std::scoped_lock lock(convar_mutex);
            convar_registration_string_count +=
                static_cast<std::uint32_t>(registration_string_count);
            convar_invalid_registration_string_count +=
                static_cast<std::uint32_t>(invalid_registration_string_count);
            rejected = reject_convar_registration_name == setup.name;
            if (rejected)
            {
                reject_convar_registration_name.clear();
                ++convar_registration_reject_count;
            }
        }

        if (rejected)
        {
            for (std::size_t index{}; index < registration_string_count; ++index)
            {
                MemAlloc_FreeFunc(registration_strings[index]);
            }
            return;
        }

        std::shared_ptr<ConVarEntry> entry;
        ConVarCallback callback;
        {
            std::scoped_lock lock(convar_mutex);
            entry = ConVarByName(setup.name);
            if (!entry)
            {
                entry = std::make_shared<ConVarEntry>();
                entry->access_index = next_convar_access++;
                entry->name = setup.name;
                entry->description = setup.help ? setup.help : "";
                entry->type = setup.value_info.type;
                entry->flags = setup.flags | additional_flags;
                StoreConVarValue(
                    entry->type,
                    default_value,
                    entry->default_value,
                    entry->default_string);
                SetCurrentValue(*entry, default_value);
                if (setup.value_info.has_minimum)
                {
                    entry->has_minimum = true;
                    StoreConVarValue(
                        entry->type,
                        minimum,
                        entry->minimum_value,
                        entry->minimum_string);
                }
                if (setup.value_info.has_maximum)
                {
                    entry->has_maximum = true;
                    StoreConVarValue(
                        entry->type,
                        maximum,
                        entry->maximum_value,
                        entry->maximum_string);
                }
                RefreshConVarData(*entry);
                convars.push_back(entry);
            }

            const std::int32_t registered_index = next_convar_registration++;
            callback.reference = keels2::cs2::ConVarRef(
                entry->access_index,
                registered_index);
            callback.provider = setup.value_info.change_provider;
            callback.callback = setup.value_info.change_callback;
            callback.active = callback.provider && callback.callback;
            callback.registered = true;
            entry->callbacks.push_back(callback);
            entry->data.callback_index = callback.active
                ? static_cast<std::uint32_t>(registered_index)
                : 0;
            *reference = callback.reference;
            *data = &entry->data;
            ++convar_register_count;
        }

        for (std::size_t index{}; index < registration_string_count; ++index)
        {
            MemAlloc_FreeFunc(registration_strings[index]);
        }

        if (callback.active)
        {
            keels2::cs2::ConVarObject object{callback.reference, &entry->data};
            callback.provider(
                &object,
                0,
                &entry->default_value,
                &entry->default_value,
                nullptr,
                callback.callback);
        }
    }

    void UnregisterConVarCallbacks(keels2::cs2::ConVarRef reference) override
    {
        std::scoped_lock lock(convar_mutex);
        for (const auto& entry : convars)
        {
            const auto callback = std::find_if(
                entry->callbacks.begin(),
                entry->callbacks.end(),
                [reference](const ConVarCallback& candidate) {
                    return candidate.registered &&
                        candidate.reference.AccessIndex() == reference.AccessIndex() &&
                        candidate.reference.RegisteredIndex() == reference.RegisteredIndex();
                });
            if (callback != entry->callbacks.end())
            {
                callback->active = false;
                callback->registered = false;
                ++convar_unregister_count;
                return;
            }
        }
    }

    KEELS2_EMPTY_SLOT(40)

    keels2::cs2::ConVarData* GetConVarData(keels2::cs2::ConVarRef reference) override
    {
        std::scoped_lock lock(convar_mutex);
        const auto entry = ConVarByAccess(reference.AccessIndex());
        return entry ? &entry->data : nullptr;
    }

    keels2::cs2::CommandRef RegisterConCommand(
        const keels2::cs2::CommandCreation& setup,
        std::uint64_t) override
    {
        ++register_count;
        const auto index = static_cast<std::uint16_t>(entries.size() + 1);
        Entry entry;
        entry.reference = keels2::cs2::CommandRef(index, static_cast<std::int32_t>(index));
        entry.name = setup.name ? setup.name : "";
        entry.callback = setup.callback_info.callback.interface_pointer;
        entry.active = entry.callback != nullptr;
        entries.push_back(entry);
        return entry.active ? entry.reference : keels2::cs2::CommandRef();
    }

    void UnregisterConCommandCallbacks(keels2::cs2::CommandRef reference) override
    {
        const auto iterator = std::find_if(entries.begin(), entries.end(), [reference](const Entry& entry) {
            return entry.reference.AccessIndex() == reference.AccessIndex() && entry.active;
        });
        if (iterator != entries.end())
        {
            ++unregister_count;
            iterator->active = false;
            retired_callbacks.push_back(iterator->callback);
            iterator->callback = nullptr;
        }
    }

    void* GetConCommandData(keels2::cs2::CommandRef) override
    {
        return nullptr;
    }

    void QueueThreadSetValue(
        keels2::cs2::ConVarObject* reference,
        std::int32_t slot,
        void*,
        keels2::cs2::ConVarValue* value) override
    {
        if (!reference || !reference->data || !value)
        {
            return;
        }
        {
            std::scoped_lock lock(convar_mutex);
            const auto entry = ConVarByData(reference->data);
            if (!entry)
            {
                return;
            }
            PendingValue pending;
            pending.entry = entry;
            pending.slot = slot;
            StoreConVarValue(entry->type, *value, pending.value, pending.string_storage);
            queued_values.push_back(std::move(pending));
            ++convar_queue_count;
            last_queue_slot = slot;
        }
    }

    bool SeedInt32(const char* name, std::int32_t value)
    {
        if (!name || !name[0])
        {
            return false;
        }
        std::scoped_lock lock(convar_mutex);
        if (ConVarByName(name))
        {
            return false;
        }
        auto entry = std::make_shared<ConVarEntry>();
        entry->access_index = next_convar_access++;
        entry->name = name;
        entry->description = "seeded engine ConVar";
        entry->type = keels2::cs2::ConVarType::int32;
        entry->flags = KEELS2_CVAR_FLAG_RELEASE;
        entry->default_value.int32 = value;
        entry->current_value.int32 = value;
        RefreshConVarData(*entry);
        convars.push_back(std::move(entry));
        return true;
    }

    bool SetInt32(const char* name, std::int32_t value)
    {
        std::shared_ptr<ConVarEntry> entry;
        {
            std::scoped_lock lock(convar_mutex);
            entry = ConVarByName(name);
        }
        if (!entry || entry->type != keels2::cs2::ConVarType::int32)
        {
            return false;
        }
        keels2::cs2::ConVarValue changed{};
        changed.int32 = value;
        ApplyValue(*entry, 0, changed);
        return true;
    }

    void RejectNextFilter()
    {
        reject_next_filter = true;
    }

    void RejectConVarRegistration(const char* name)
    {
        std::scoped_lock lock(convar_mutex);
        reject_convar_registration_name = name ? name : "";
    }

    bool ReadInt32(const char* name, std::int32_t& value) const
    {
        std::scoped_lock lock(convar_mutex);
        const auto entry = ConVarByName(name);
        if (!entry || entry->type != keels2::cs2::ConVarType::int32)
        {
            return false;
        }
        const auto current = ReadLiveConVarValue(entry->type, entry->data.values);
        value = current.int32;
        return true;
    }

    bool ReadFloat32(const char* name, float& value) const
    {
        std::scoped_lock lock(convar_mutex);
        const auto entry = ConVarByName(name);
        if (!entry || entry->type != keels2::cs2::ConVarType::float32)
        {
            return false;
        }
        const auto current = ReadLiveConVarValue(entry->type, entry->data.values);
        value = current.float32;
        return true;
    }

    bool LiveValueTailIntact(const char* name) const
    {
        std::scoped_lock lock(convar_mutex);
        const auto entry = ConVarByName(name);
        if (!entry)
        {
            return false;
        }
        const std::size_t size = LiveConVarValueSize(entry->type);
        return size != 0 && std::all_of(
            entry->data.values + size,
            entry->data.values + sizeof(entry->data.values),
            [](std::byte value) { return value == kLiveValueCanary; });
    }

    bool HasConVar(const char* name) const
    {
        std::scoped_lock lock(convar_mutex);
        return ConVarByName(name) != nullptr;
    }

    std::size_t ActiveConVarCallbacks(const char* name) const
    {
        std::scoped_lock lock(convar_mutex);
        const auto entry = ConVarByName(name);
        return entry
            ? static_cast<std::size_t>(std::count_if(
                entry->callbacks.begin(),
                entry->callbacks.end(),
                [](const ConVarCallback& callback) { return callback.active; }))
            : 0;
    }

    bool Dispatch(std::initializer_list<const char*> arguments)
    {
        if (arguments.size() == 0 || !*arguments.begin())
        {
            return false;
        }
        const auto iterator = std::find_if(entries.begin(), entries.end(), [arguments](const Entry& entry) {
            return entry.active && entry.name == *arguments.begin();
        });
        if (iterator == entries.end() || !iterator->callback)
        {
            return false;
        }

        return DispatchCallback(iterator->callback, arguments);
    }

    bool DispatchRetired(std::initializer_list<const char*> arguments)
    {
        return !retired_callbacks.empty() &&
            DispatchCallback(retired_callbacks.back(), arguments);
    }

    std::size_t ActiveCount() const
    {
        return static_cast<std::size_t>(std::count_if(entries.begin(), entries.end(), [](const Entry& entry) {
            return entry.active;
        }));
    }

    bool HasActive(const char* name) const
    {
        return std::any_of(entries.begin(), entries.end(), [name](const Entry& entry) {
            return entry.active && entry.name == name;
        });
    }

    void DrainQueuedConVarValues()
    {
        DrainQueuedValues();
    }

    void Reset()
    {
        std::scoped_lock lock(convar_mutex);
        entries.clear();
        retired_callbacks.clear();
        convars.clear();
        next_convar_access = 1;
        next_convar_registration = 1;
        register_count = 0;
        unregister_count = 0;
        convar_register_count = 0;
        convar_unregister_count = 0;
        convar_registration_string_count = 0;
        convar_invalid_registration_string_count = 0;
        convar_registration_reject_count = 0;
        convar_queue_count = 0;
        convar_filter_count = 0;
        convar_change_count = 0;
        convar_global_change_count = 0;
        last_filter_slot = -999;
        last_change_slot = -999;
        last_global_slot = -999;
        last_queue_slot = -999;
        last_global_new.clear();
        last_global_old.clear();
        queued_values.clear();
        draining_queued_values = false;
        reject_next_filter = false;
        reject_convar_registration_name.clear();
    }

    std::uint32_t register_count{};
    std::uint32_t unregister_count{};
    std::uint32_t convar_register_count{};
    std::uint32_t convar_unregister_count{};
    std::uint32_t convar_registration_string_count{};
    std::uint32_t convar_invalid_registration_string_count{};
    std::uint32_t convar_registration_reject_count{};
    std::uint32_t convar_queue_count{};
    std::uint32_t convar_filter_count{};
    std::uint32_t convar_change_count{};
    std::uint32_t convar_global_change_count{};
    std::int32_t last_filter_slot{-999};
    std::int32_t last_change_slot{-999};
    std::int32_t last_global_slot{-999};
    std::int32_t last_queue_slot{-999};
    std::string last_global_new;
    std::string last_global_old;

private:
    struct ConVarCallback
    {
        keels2::cs2::ConVarRef reference;
        keels2::cs2::GenericChangeProvider provider{};
        keels2::cs2::GenericChangeCallback callback{};
        bool active{};
        bool registered{};
    };

    struct ConVarEntry
    {
        ~ConVarEntry()
        {
            if (type == keels2::cs2::ConVarType::string)
            {
                const auto current = ReadLiveConVarValue(type, data.values);
                MemAlloc_FreeFunc(current.string);
            }
        }

        std::uint16_t access_index{};
        std::string name;
        std::string description;
        std::string default_string;
        std::string minimum_string;
        std::string maximum_string;
        keels2::cs2::ConVarType type{keels2::cs2::ConVarType::invalid};
        std::uint64_t flags{};
        keels2::cs2::ConVarValue default_value{};
        keels2::cs2::ConVarValue minimum_value{};
        keels2::cs2::ConVarValue maximum_value{};
        keels2::cs2::ConVarValue current_value{};
        bool has_minimum{};
        bool has_maximum{};
        keels2::cs2::ConVarData data{};
        std::vector<ConVarCallback> callbacks;
    };

    struct PendingValue
    {
        std::shared_ptr<ConVarEntry> entry;
        std::int32_t slot{};
        keels2::cs2::ConVarValue value{};
        std::string string_storage;
    };

    inline static constexpr std::byte kLiveValueCanary{0xa5};

    static std::size_t LiveConVarValueSize(keels2::cs2::ConVarType type)
    {
        switch (type)
        {
            case keels2::cs2::ConVarType::boolean:
                return sizeof(keels2::cs2::ConVarValue::boolean);
            case keels2::cs2::ConVarType::int32:
                return sizeof(keels2::cs2::ConVarValue::int32);
            case keels2::cs2::ConVarType::float32:
                return sizeof(keels2::cs2::ConVarValue::float32);
            case keels2::cs2::ConVarType::string:
                return sizeof(keels2::cs2::ConVarValue::string);
            default:
                return 0;
        }
    }

    static keels2::cs2::ConVarValue ReadLiveConVarValue(
        keels2::cs2::ConVarType type,
        const void* source)
    {
        keels2::cs2::ConVarValue value{};
        const std::size_t size = LiveConVarValueSize(type);
        if (source && size != 0)
        {
            std::memcpy(&value, source, size);
        }
        return value;
    }

    static void WriteLiveConVarValue(
        ConVarEntry& entry,
        const keels2::cs2::ConVarValue& value)
    {
        const std::size_t size = LiveConVarValueSize(entry.type);
        if (size != 0)
        {
            std::memcpy(entry.data.values, &value, size);
        }
    }

    static void StoreConVarValue(
        keels2::cs2::ConVarType type,
        const keels2::cs2::ConVarValue& source,
        keels2::cs2::ConVarValue& destination,
        std::string& string_storage)
    {
        destination = source;
        if (type == keels2::cs2::ConVarType::string)
        {
            string_storage = source.string ? source.string : "";
            destination.string = string_storage.data();
        }
    }

    static bool EqualConVarValue(
        keels2::cs2::ConVarType type,
        const keels2::cs2::ConVarValue& first,
        const keels2::cs2::ConVarValue& second)
    {
        switch (type)
        {
            case keels2::cs2::ConVarType::boolean:
                return first.boolean == second.boolean;
            case keels2::cs2::ConVarType::int32:
                return first.int32 == second.int32;
            case keels2::cs2::ConVarType::float32:
                return first.float32 == second.float32;
            case keels2::cs2::ConVarType::string:
                return std::strcmp(
                    first.string ? first.string : "",
                    second.string ? second.string : "") == 0;
            default:
                return false;
        }
    }

    static void SetCurrentValue(
        ConVarEntry& entry,
        const keels2::cs2::ConVarValue& value)
    {
        if (entry.type == keels2::cs2::ConVarType::string)
        {
            char* replacement = MemAlloc_StrDupFunc(value.string ? value.string : "");
            if (!replacement)
            {
                return;
            }
            const auto current = ReadLiveConVarValue(entry.type, entry.data.values);
            MemAlloc_FreeFunc(current.string);
            entry.current_value = value;
            entry.current_value.string = replacement;
        }
        else
        {
            entry.current_value = value;
        }
        WriteLiveConVarValue(entry, entry.current_value);
    }

    void ApplyValue(
        ConVarEntry& entry,
        std::int32_t slot,
        const keels2::cs2::ConVarValue& requested)
    {
        auto old_value = ReadLiveConVarValue(entry.type, entry.data.values);
        std::string old_string;
        if (entry.type == keels2::cs2::ConVarType::string)
        {
            old_string = old_value.string ? old_value.string : "";
            old_value.string = old_string.data();
        }

        keels2::cs2::ConVarValue new_value{};
        std::string new_string;
        StoreConVarValue(entry.type, requested, new_value, new_string);
        if (EqualConVarValue(entry.type, old_value, new_value))
        {
            return;
        }
        SetCurrentValue(entry, new_value);
        new_value = ReadLiveConVarValue(entry.type, entry.data.values);
        ++entry.data.times_changed;
        const keels2::cs2::ConVarRef reference(entry.access_index, 0);
        CallChangeCallback(reference, slot, &new_value, &old_value, nullptr);
        keels2::cs2::ConVarObject object{reference, &entry.data};
        CallGlobalChangeCallbacks(&object, slot, "new", "old", nullptr);
    }

    void DrainQueuedValues()
    {
        {
            std::scoped_lock lock(convar_mutex);
            if (draining_queued_values)
            {
                return;
            }
            draining_queued_values = true;
        }
        while (true)
        {
            PendingValue pending;
            {
                std::scoped_lock lock(convar_mutex);
                if (queued_values.empty())
                {
                    draining_queued_values = false;
                    return;
                }
                pending = std::move(queued_values.front());
                queued_values.erase(queued_values.begin());
            }
            if (pending.entry)
            {
                if (pending.entry->type == keels2::cs2::ConVarType::string)
                {
                    pending.value.string = pending.string_storage.data();
                }
                ApplyValue(*pending.entry, pending.slot, pending.value);
            }
        }
    }

    static void RefreshConVarData(ConVarEntry& entry)
    {
        entry.data.name = entry.name.c_str();
        entry.data.default_value = &entry.default_value;
        entry.data.minimum_value = entry.has_minimum ? &entry.minimum_value : nullptr;
        entry.data.maximum_value = entry.has_maximum ? &entry.maximum_value : nullptr;
        entry.data.help = entry.description.c_str();
        entry.data.type = entry.type;
        entry.data.flags = entry.flags;
        std::fill_n(
            entry.data.values,
            sizeof(entry.data.values),
            kLiveValueCanary);
        WriteLiveConVarValue(entry, entry.current_value);
    }

    std::shared_ptr<ConVarEntry> ConVarByName(const char* name) const
    {
        if (!name)
        {
            return {};
        }
        const std::string_view requested{name};
        const auto entry = std::find_if(
            convars.begin(),
            convars.end(),
            [requested](const auto& candidate) {
                if (candidate->name.size() != requested.size())
                {
                    return false;
                }
                return std::equal(
                    candidate->name.begin(),
                    candidate->name.end(),
                    requested.begin(),
                    [](char left, char right) {
                        const auto fold = [](char value) {
                            return value >= 'A' && value <= 'Z'
                                ? static_cast<char>(value + ('a' - 'A'))
                                : value;
                        };
                        return fold(left) == fold(right);
                    });
            });
        return entry == convars.end() ? std::shared_ptr<ConVarEntry>{} : *entry;
    }

    std::shared_ptr<ConVarEntry> ConVarByAccess(std::uint16_t access) const
    {
        const auto entry = std::find_if(convars.begin(), convars.end(), [access](const auto& candidate) {
            return candidate->access_index == access;
        });
        return entry == convars.end() ? std::shared_ptr<ConVarEntry>{} : *entry;
    }

    std::shared_ptr<ConVarEntry> ConVarByData(const keels2::cs2::ConVarData* data) const
    {
        const auto entry = std::find_if(convars.begin(), convars.end(), [data](const auto& candidate) {
            return &candidate->data == data;
        });
        return entry == convars.end() ? std::shared_ptr<ConVarEntry>{} : *entry;
    }

    static bool DispatchCallback(
        keels2::cs2::ICommandCallback* callback,
        std::initializer_list<const char*> arguments)
    {
        if (!callback || arguments.size() == 0 || !*arguments.begin())
        {
            return false;
        }
        std::vector<const char*> values(arguments);
        std::array<unsigned char, keels2::cs2::kCommandSize> command{};
        const auto count = static_cast<std::int32_t>(values.size());
        const char* const* value_pointer = values.data();
        std::memcpy(
            command.data() + keels2::cs2::kCommandArgumentCountOffset,
            &count,
            sizeof(count)
        );
        std::memcpy(
            command.data() + keels2::cs2::kCommandArgumentValuesOffset,
            &value_pointer,
            sizeof(value_pointer)
        );
        const std::array<std::int32_t, 2> context{-1, -1};
        callback->CommandCallback(context.data(), command.data());
        return true;
    }
    struct Entry
    {
        keels2::cs2::CommandRef reference;
        std::string name;
        keels2::cs2::ICommandCallback* callback{};
        bool active{};
    };

    std::vector<Entry> entries;
    std::vector<keels2::cs2::ICommandCallback*> retired_callbacks;
    mutable std::mutex convar_mutex;
    std::vector<std::shared_ptr<ConVarEntry>> convars;
    std::vector<PendingValue> queued_values;
    bool draining_queued_values{};
    bool reject_next_filter{};
    std::string reject_convar_registration_name;
    std::uint16_t next_convar_access{1};
    std::int32_t next_convar_registration{1};
};

#undef KEELS2_EMPTY_SLOT

FakeCvar g_cvar;
bool g_expose_cvar{true};
void* g_cvar_override{};

struct RawInterface
{
    void** vtable;
};

void RegisterLoopModeFixture(void*, const char*, void*, void**)
{
}

void UnregisterLoopModeFixture(void*, const char*, void*, void**)
{
}

std::uint32_t g_loop_init_calls{};
std::uint32_t g_loop_shutdown_calls{};

bool LoopInitFixture(void*, void*, void*)
{
    ++g_loop_init_calls;
    return true;
}

void LoopShutdownFixture(void*)
{
    ++g_loop_shutdown_calls;
}

void* CreateLoopModeFixture(void*);
void DestroyLoopModeFixture(void*, void*);

class FakeGameEvent final : public IGameEvent
{
public:
    const char* GetName() const override
    {
        return "round_start";
    }

    int GetID() const override { return 1; }
    bool IsReliable() const override { return true; }
    bool IsLocal() const override { return false; }
    bool IsEmpty(const GameEventKeySymbol_t&) override { return false; }
    bool GetBool(const GameEventKeySymbol_t&, bool value) override { return value; }
    int GetInt(const GameEventKeySymbol_t&, int value) override { return value; }
    uint64 GetUint64(const GameEventKeySymbol_t&, uint64 value) override { return value; }
    float GetFloat(const GameEventKeySymbol_t&, float value) override { return value; }
    const char* GetString(const GameEventKeySymbol_t&, const char* value) override
    {
        return value;
    }
    void* GetPtr(const GameEventKeySymbol_t&) override { return nullptr; }
    CEntityHandle GetEHandle(const GameEventKeySymbol_t&, CEntityHandle value) override
    {
        return value;
    }
    CEntityInstance* GetEntity(
        const GameEventKeySymbol_t&,
        CEntityInstance* value) override
    {
        return value;
    }
    CEntityIndex GetEntityIndex(
        const GameEventKeySymbol_t&,
        CEntityIndex value) override
    {
        return value;
    }
    CPlayerSlot GetPlayerSlot(const GameEventKeySymbol_t&) override
    {
        return CPlayerSlot(-1);
    }
    CEntityInstance* GetPlayerController(const GameEventKeySymbol_t&) override
    {
        return nullptr;
    }
    CEntityInstance* GetPlayerPawn(const GameEventKeySymbol_t&) override { return nullptr; }
    CEntityHandle GetPawnEHandle(const GameEventKeySymbol_t&) override { return {}; }
    CEntityIndex GetPawnEntityIndex(const GameEventKeySymbol_t&) override
    {
        return CEntityIndex(-1);
    }
    void SetBool(const GameEventKeySymbol_t&, bool) override {}
    void SetInt(const GameEventKeySymbol_t&, int) override {}
    void SetUint64(const GameEventKeySymbol_t&, uint64) override {}
    void SetFloat(const GameEventKeySymbol_t&, float) override {}
    void SetString(const GameEventKeySymbol_t&, const char*) override {}
    void SetPtr(const GameEventKeySymbol_t&, void*) override {}
    void SetEntity(const GameEventKeySymbol_t&, CEntityInstance*) override {}
    void SetEntity(const GameEventKeySymbol_t&, CEntityIndex) override {}
    void SetPlayer(const GameEventKeySymbol_t&, CEntityInstance*) override {}
    void SetPlayer(const GameEventKeySymbol_t&, CPlayerSlot) override {}
    void SetPlayerRaw(
        const GameEventKeySymbol_t&,
        const GameEventKeySymbol_t&,
        CEntityInstance*) override
    {
    }
    bool HasKey(const GameEventKeySymbol_t&) override { return true; }
    void unk001() override {}
    KeyValues3* GetDataKeys() const override { return nullptr; }
};

template <typename Function>
void* FunctionAddress(Function function)
{
    static_assert(sizeof(Function) == sizeof(void*));
    void* address{};
    std::memcpy(&address, &function, sizeof(address));
    return address;
}

std::array<void*, 15> g_engine_service_vtable = [] {
    std::array<void*, 15> table{};
    table[13] = FunctionAddress(&RegisterLoopModeFixture);
    table[14] = FunctionAddress(&UnregisterLoopModeFixture);
    return table;
}();
RawInterface g_engine_service{g_engine_service_vtable.data()};

void NamedInterfaceFixture(void*)
{
}

std::array<void*, 1> g_named_interface_vtable{
    FunctionAddress(&NamedInterfaceFixture)
};
RawInterface g_named_interface{g_named_interface_vtable.data()};
RawInterface g_null_vtable_interface{};
std::uint32_t g_transient_interface_queries{};
void* g_schema_system_interface{};
void* g_game_resource_interface{};

std::array<void*, 8> g_loop_vtable = [] {
    std::array<void*, 8> table{};
    table[0] = FunctionAddress(&LoopInitFixture);
    table[1] = FunctionAddress(&LoopShutdownFixture);
    return table;
}();
RawInterface g_loop{g_loop_vtable.data()};

void* CreateLoopModeFixture(void*)
{
    return &g_loop;
}

void DestroyLoopModeFixture(void*, void*)
{
}

std::array<void*, 5> g_loop_factory_vtable = [] {
    std::array<void*, 5> table{};
    table[2] = FunctionAddress(&CreateLoopModeFixture);
    table[3] = FunctionAddress(&DestroyLoopModeFixture);
    return table;
}();
RawInterface g_loop_factory{g_loop_factory_vtable.data()};

FakeGameEvent g_game_event_instance;

template <typename Function>
Function VtableFunction(void* object, std::size_t index)
{
    void* address = (*static_cast<void***>(object))[index];
    static_assert(sizeof(Function) == sizeof(address));
    Function function{};
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

bool DispatchSource2LevelInit()
{
    VtableFunction<void (*)(void*, const char*, void*, void**)>(
        &g_engine_service,
        13)(&g_engine_service, "game", &g_loop_factory, nullptr);
    void* loop = VtableFunction<void* (*)(void*)>(&g_loop_factory, 2)(&g_loop_factory);
    if (loop != &g_loop)
    {
        return false;
    }
    return VtableFunction<bool (*)(void*, void*, void*)>(loop, 0)(
        loop,
        &g_game_event_instance,
        &g_engine_service);
}

void DispatchSource2LevelShutdown()
{
    VtableFunction<void (*)(void*)>(&g_loop, 1)(&g_loop);
}

void UnregisterSource2LoopMode()
{
    VtableFunction<void (*)(void*, const char*, void*, void**)>(
        &g_engine_service,
        14)(&g_engine_service, "game", &g_loop_factory, nullptr);
}

void* EngineFactory(const char* name, int* return_code)
{
    void* result{};
    bool inconsistent{};
    if (g_expose_cvar && name && std::strcmp(name, keels2::cs2::kCvarInterfaceVersion) == 0)
    {
        result = g_cvar_override ? g_cvar_override : &g_cvar;
    }
    else if (name && std::strcmp(name, "EngineServiceMgr001") == 0)
    {
        result = &g_engine_service;
    }
    else if (name && std::strcmp(name, "SchemaSystem_001") == 0)
    {
        result = g_schema_system_interface;
    }
    else if (name && std::strcmp(name, "GameResourceServiceServerV001") == 0)
    {
        result = g_game_resource_interface;
    }
    else if (name && std::strcmp(name, "NetworkServerService_001") == 0)
    {
        result = &g_named_interface;
    }
    else if (name && std::strcmp(name, "VFileSystem017") == 0)
    {
        result = &g_named_interface;
    }
    else if (name && std::strcmp(name, "VPhysics2_Interface_001") == 0)
    {
        result = &g_named_interface;
    }
    else if (name && std::strcmp(name, "NetworkSystemVersion001") == 0)
    {
        result = &g_named_interface;
    }
    else if (name && std::strcmp(name, "TransientService001") == 0 &&
        ++g_transient_interface_queries > 1)
    {
        result = &g_named_interface;
    }
    else if (name && std::strcmp(name, "InconsistentService001") == 0)
    {
        result = &g_named_interface;
        inconsistent = true;
    }
    else if (name && std::strcmp(name, "NullVtableService001") == 0)
    {
        result = &g_null_vtable_interface;
    }
    if (return_code)
    {
        *return_code = result && !inconsistent ? 0 : 1;
    }
    return result;
}

bool CopyFile(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error)
    {
        return false;
    }
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
    return !error;
}

std::filesystem::path RuntimePluginPath(
    const std::filesystem::path& plugin_directory,
    const std::filesystem::path& logical_path)
{
    const std::filesystem::path root = plugin_directory / ".runtime";
    std::filesystem::path selected;
    std::uint64_t selected_handle{};
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(root, error), end;
         !error && iterator != end;
         iterator.increment(error))
    {
        if (!iterator->is_directory(error))
        {
            continue;
        }
        std::uint64_t handle{};
        bool valid = true;
        const std::string name = iterator->path().filename().string();
        for (const char raw_character : name)
        {
            const auto character = static_cast<unsigned char>(raw_character);
            if (character < '0' || character > '9' ||
                handle > (UINT64_MAX - static_cast<std::uint64_t>(character - '0')) / 10u)
            {
                valid = false;
                break;
            }
            handle = handle * 10u + static_cast<std::uint64_t>(character - '0');
        }
        const std::filesystem::path candidate = iterator->path() / logical_path.filename();
        if (valid && !name.empty() && handle >= selected_handle &&
            std::filesystem::is_regular_file(candidate, error) && !error)
        {
            selected = candidate;
            selected_handle = handle;
        }
    }
    return selected;
}

bool VtableEntryBelongsTo(
    void* object,
    std::size_t index,
    const std::filesystem::path& expected_module)
{
    if (!object)
    {
        return false;
    }
    void* address = (*static_cast<void***>(object))[index];
    std::filesystem::path module;
    std::string error;
    if (!keels2::platform::ModulePathFromAddress(address, module, error))
    {
        return false;
    }
    std::error_code filesystem_error;
    return std::filesystem::equivalent(module, expected_module, filesystem_error) && !filesystem_error;
}

bool Contains(const std::string& messages, const char* expected)
{
    return messages.find(expected) != std::string::npos;
}

std::size_t Count(const std::string& messages, const char* expected)
{
    std::size_t count{};
    std::size_t position{};
    const std::size_t length = std::strlen(expected);
    while ((position = messages.find(expected, position)) != std::string::npos)
    {
        ++count;
        position += length;
    }
    return count;
}

bool ContainsInOrder(const std::string& messages, const char* first, const char* second)
{
    const auto first_position = messages.find(first);
    if (first_position == std::string::npos)
    {
        return false;
    }
    return messages.find(second, first_position + std::strlen(first)) != std::string::npos;
}

bool ValidateMessages(const std::string& scenario, const std::string& messages)
{
    if (scenario == "missing_server" || scenario == "invalid_layout")
    {
        return Count(messages, "[KeelS2/bootstrap] genuine server module is unavailable:") == 1;
    }
    if (scenario == "unknown_build")
    {
        return Contains(messages, "[KeelS2/bootstrap] unsupported cs2 server module") &&
            Contains(messages, "lifecycle capture and plugin loading are disabled; genuine interfaces remain available") &&
            !Contains(messages, "selected compatibility profile:") &&
            !Contains(messages, "could not load host:");
    }

    const bool selected_profile = Contains(messages, "[KeelS2/bootstrap] selected compatibility profile: test-fixture-");
    if (scenario == "missing_host")
    {
        return selected_profile && Contains(messages, "[KeelS2/bootstrap] could not load host:");
    }
    if (scenario == "missing_plugin")
    {
#if defined(NDEBUG)
        const bool missing_message = Contains(messages, "[KeelS2] No plugins loaded (none found)");
#else
        const bool missing_message = Contains(messages, "[KeelS2] WARNING: no plugins found in:");
#endif
        return selected_profile && missing_message &&
            Contains(messages, "Listing 0 plugins:") &&
            Contains(messages, "host started for cs2") && Contains(messages, "host stopped");
    }
    if (scenario == "invalid_plugin")
    {
        return selected_profile && Contains(messages, "plugin query was rejected:") &&
            Contains(messages, "Listing 1 plugins:") && Contains(messages, " - invalid") &&
            Contains(messages, "host started for cs2") && Contains(messages, "host stopped");
    }
    if (scenario == "failed_plugin_load")
    {
        return selected_profile && Contains(messages, "plugin load was rejected: Failing Test Plugin") &&
            Contains(messages, "[Failing Test Plugin] WARNING: intentional warning severity probe") &&
            Contains(messages, "[Failing Test Plugin] ERROR: intentional error severity probe") &&
            Contains(messages, "Failing Test Plugin (1) - error") &&
            Contains(messages, "host started for cs2") && Contains(messages, "host stopped");
    }
    if (scenario == "reverse_unload")
    {
        return selected_profile && Contains(messages, "plugin loaded: Lifecycle First 1") &&
            Contains(messages, "plugin loaded: Lifecycle Second 1") &&
            ContainsInOrder(messages, "[Lifecycle Second] second unload callback completed", "[Lifecycle First] first unload callback completed") &&
            ContainsInOrder(messages, "[Lifecycle First] first unload callback completed", "host stopped");
    }
    if (scenario == "missing_cvar")
    {
        return selected_profile && Contains(messages, "game adapter failed: VEngineCvar007 is unavailable") &&
            Contains(messages, "[KeelS2/bootstrap] host rejected startup");
    }
    if (scenario == "missing_source2_server")
    {
        return selected_profile &&
            Contains(messages, "game adapter failed: Source2Server001 is unavailable") &&
            Contains(messages, "[KeelS2/bootstrap] host rejected startup");
    }
    if (scenario == "missing_game_clients")
    {
        return selected_profile &&
            Contains(messages, "game adapter failed: Source2GameClients001 is unavailable") &&
            Contains(messages, "[KeelS2/bootstrap] host rejected startup");
    }
    if (scenario == "wrong_cvar_provenance")
    {
        return selected_profile &&
            Contains(messages, "game adapter failed: VEngineCvar007 resolves to unexpected module") &&
            Contains(messages, "[KeelS2/bootstrap] host rejected startup");
    }
    if (scenario == "duplicate_command")
    {
        return selected_profile &&
            Contains(messages, "cannot register duplicate command \"keel_test\"") &&
            Contains(messages, "[Duplicate Command Test] duplicate command rejection passed") &&
            Contains(messages, "plugin loaded: Duplicate Command Test 1");
    }
    if (scenario == "duplicate_plugin_name")
    {
        return selected_profile && Contains(messages, "plugin friendly name conflict \"KeelS2 Basic\"") &&
            Contains(messages, "01_basic") && Contains(messages, "02_basic") &&
            Contains(messages, "Listing 2 plugins:") && Contains(messages, " - error");
    }
    if (scenario == "reserved_command")
    {
        return selected_profile && Contains(messages, "cannot register reserved command \"keel\"") &&
            Contains(messages, "[Reserved Command Test] reserved command rejection passed") &&
            Contains(messages, "plugin loaded: Reserved Command Test 1");
    }
    if (scenario == "core_commands")
    {
        return selected_profile && Contains(messages, "KeelS2 Menu") &&
            Contains(messages, "KeelS2 Plugins Menu") && Contains(messages, "KeelS2 0.9.0") &&
            Contains(messages, "load <file>     - Load a plugin module") &&
            Contains(messages, "unload <plugin> - Unload a loaded plugin") &&
            Contains(messages, "Game: cs2") && Contains(messages, "KeelS2 status: running") &&
            Contains(messages, "https://keels2.com") && Contains(messages, "Listing 3 plugins:") &&
            Contains(messages, "Plugin [01]") && Contains(messages, "Name: KeelS2 Basic") &&
            Contains(messages, "plugin selector \"lifecycle\" is ambiguous:") &&
            Contains(messages, "keel_test - Verifies the KeelS2 native plugin command path") &&
            Contains(messages, "Commands for [01] KeelS2 Basic:") &&
            Contains(messages, "KeelS2 Inspection Menu") &&
            Contains(messages, "Hook targets: 5") &&
            Contains(messages, "Source 2 interfaces:") &&
            Contains(messages, "Source2Server001 factory=2") &&
            Contains(messages, "Built-in services: 11") &&
            Contains(messages, "Published services: 0") &&
            Contains(messages, "Commands:") &&
            Contains(messages, "keel_test owner=KeelS2 Basic [") &&
            Contains(messages, "ConVars: 0") &&
            Contains(messages, "Compatibility identity: test-fixture-") &&
            Contains(messages, "Server fingerprint: test-fixture-");
    }
    if (scenario == "plugin_lifecycle")
    {
        const auto final_unload = messages.rfind("[KeelS2 Basic] unload callback completed");
        const auto host_stopped = messages.find("host stopped");
        return selected_profile &&
            Count(messages, "plugin loaded: KeelS2 Basic 0.9.0") == 3 &&
            Count(messages, "[KeelS2 Basic] unload callback completed") == 3 &&
            Count(messages, "[KeelS2] plugin unloaded: [01] KeelS2 Basic") == 3 &&
            Contains(messages, "plugin filename must name one module in the plugins directory") &&
            Contains(messages, "plugin filename has an unsupported extension: 01_basic.txt") &&
            Contains(messages, "plugin file \"missing\" was not found") &&
            Contains(messages, "plugin file \"01_basic") &&
            Contains(messages, "is already loaded as [01] KeelS2 Basic") &&
            Contains(messages, "plugin \"999\" was not found") &&
            Contains(messages, "Listing 0 plugins:") && Contains(messages, "Listing 1 plugins:") &&
            Contains(messages, "Plugin [01]") &&
            Contains(messages, "Commands for [01] KeelS2 Basic:") &&
            Contains(messages, "[KeelS2] Usage: keel plugins load <file>") &&
            Contains(messages, "[KeelS2] Usage: keel plugins unload <plugin>") &&
            Contains(messages, "[KeelS2] Usage: keel plugins info <plugin>") &&
            !Contains(messages, "[KeelS2] ERROR: usage:") &&
            Count(messages, "KeelS2 0.9.0 is active. The basic native plugin is responding.") == 2 &&
            final_unload != std::string::npos && host_stopped != std::string::npos &&
            final_unload < host_stopped;
    }
    if (scenario == "command_removal")
    {
        return selected_profile &&
            Contains(messages, "[KeelS2 Basic] keel_test command unregistered.") &&
            Contains(messages, "[KeelS2 Basic] unload callback completed") &&
            !Contains(messages, "Could not unregister the keel_test command.");
    }
    if (scenario == "plugin_index_compaction")
    {
        return selected_profile &&
            Contains(messages, "  [01] KeelS2 Basic") &&
            Contains(messages, "  [02] Lifecycle First") &&
            Contains(messages, "  [03] Lifecycle Second") &&
            Contains(messages, "[KeelS2] plugin unloaded: [02] Lifecycle First") &&
            ContainsInOrder(messages, "[KeelS2] plugin unloaded: [02] Lifecycle First", "  [02] Lifecycle Second") &&
            ContainsInOrder(messages, "  [02] Lifecycle Second", "  [03] Lifecycle First") &&
            Contains(messages, "Commands for [02] Lifecycle Second:") &&
            Contains(messages, "Commands for [03] Lifecycle First:") &&
            Contains(messages, "[KeelS2] plugin unloaded: [02] Lifecycle Second") &&
            ContainsInOrder(messages, "[KeelS2] plugin unloaded: [02] Lifecycle Second", "  [02] Lifecycle First") &&
            !Contains(messages, "Plugin [04]");
    }
    if (scenario == "source2_service")
    {
        return selected_profile &&
            Contains(messages, "[Source2 Service Test] Source 2 interface gateway load validation passed") &&
            Contains(messages, "[Source2 Service Test] Source 2 interface gateway runtime validation passed") &&
            Contains(messages, "[Source2 Service Test] Source 2 interface views invalidated before unload") &&
            Contains(messages, "plugin loaded: Source2 Service Test 1") &&
            Contains(messages, "plugin unloaded: [01] Source2 Service Test") &&
            Contains(messages, "host stopped") &&
            !Contains(messages, "Source 2 interface gateway load validation failed") &&
            !Contains(messages, "Source 2 interface gateway runtime validation failed") &&
            !Contains(messages, "Source 2 interface view remained active during unload");
    }
    if (scenario == "schema_entity_service")
    {
        return selected_profile &&
            Count(messages, "plugin loaded: Schema Entity Test 0.6") == 2 &&
            Count(messages, "[Schema Entity Test] schema field resolution passed") == 2 &&
            Count(messages, "[Schema Entity Test] entity lookup and typed read passed") == 2 &&
            Contains(messages, "[Schema Entity Test] entity destruction invalidation passed") &&
            Contains(messages, "[Schema Entity Test] entity serial reuse validation passed") &&
            Contains(messages, "[Schema Entity Test] map epoch invalidation passed") &&
            Contains(messages, "[Schema Entity Test] wrong-thread access rejected") &&
            Count(messages, "[Schema Entity Test] schema and entity views invalidated before unload") == 2 &&
            Count(messages, "plugin unloaded: [01] Schema Entity Test") == 2 &&
            !Contains(messages, "schema and entity load validation failed") &&
            !Contains(messages, "entity lookup or typed read failed") &&
            !Contains(messages, "invalidation failed") &&
            !Contains(messages, "serial reuse validation failed") &&
            !Contains(messages, "wrong-thread access was accepted") &&
            !Contains(messages, "view remained active during unload");
    }
    if (scenario == "lifecycle_service")
    {
        return selected_profile &&
            Count(messages, "plugin loaded: Lifecycle Test 0.5B") == 2 &&
            Count(messages, "[Lifecycle Test] loaded") == 2 &&
            Count(messages, "[Lifecycle Test] unloaded") == 2 &&
            Count(messages, "[Lifecycle Test] GameFrame") == 2 &&
            Count(messages, "[Lifecycle Test] ClientConnected") == 2 &&
            Count(messages, "[Lifecycle Test] ClientPutInServer") == 2 &&
            Count(messages, "[Lifecycle Test] ClientActive") == 2 &&
            Count(messages, "[Lifecycle Test] ClientFullyConnected") == 2 &&
            Count(messages, "[Lifecycle Test] ClientDisconnecting") == 2 &&
            Count(messages, "[Lifecycle Test] ClientSettingsChanged") == 2 &&
            Count(messages, "[Lifecycle Test] registration order passed") == 2 &&
            Count(messages, "[Lifecycle Test] self unsubscribe safely deferred") == 2 &&
            Count(messages, "plugin paused: [01] Lifecycle Test") == 1 &&
            Count(messages, "plugin resumed: [01] Lifecycle Test") == 1 &&
            Count(messages, "plugin unloaded: [01] Lifecycle Test") == 2 &&
            Contains(messages, "host stopped") &&
            !Contains(messages, "[Lifecycle Test] invalid") &&
            !Contains(messages, "[Lifecycle Test] registration order failed") &&
            !Contains(messages, "[Lifecycle Test] self unsubscribe failed") &&
            !Contains(messages, "[Lifecycle Test] removed subscription dispatched") &&
            !Contains(messages, "[Lifecycle Test] callback entered during load");
    }
    if (scenario == "source2_callbacks")
    {
        return selected_profile &&
            Count(messages, "plugin loaded: Source2 Callbacks First 0.5E") == 2 &&
            Count(messages, "plugin loaded: Source2 Callbacks Second 0.5E") == 1 &&
            Count(messages, "plugin loaded: Source2 Callbacks Peer 0.5E") == 1 &&
            Count(messages, "[Source2 First] LevelInit") == 3 &&
            Count(messages, "[Source2 Second] LevelInit") == 1 &&
            Count(messages, "[Source2 Peer] LevelInit") == 1 &&
            Count(messages, "[Source2 First] LevelShutdown") == 2 &&
            Count(messages, "[Source2 Second] LevelShutdown") == 1 &&
            Count(messages, "[Source2 Peer] LevelShutdown") == 1 &&
            Count(messages, "[Source2 First] round_start") == 3 &&
            Count(messages, "[Source2 Second] round_start") == 2 &&
            Count(messages, "[Source2 Peer] round_start") == 2 &&
            Count(messages, "[Source2 First] ClientConnect") == 3 &&
            Count(messages, "[Source2 Second] ClientConnect") == 3 &&
            Count(messages, "[Source2 Peer] ClientConnect") == 1 &&
            Count(messages, "[Source2 First] ClientCommand") == 2 &&
            Count(messages, "[Source2 Second] ClientCommand") == 2 &&
            Count(messages, "[Source2 Peer] ClientCommand") == 1 &&
            ContainsInOrder(
                messages,
                "[Source2 Peer] ClientConnect",
                "[Source2 First] ClientConnect") &&
            ContainsInOrder(
                messages,
                "[Source2 First] ClientConnect",
                "[Source2 Second] ClientConnect") &&
            Count(messages, "plugin paused: [03] Source2 Callbacks Peer") == 1 &&
            Count(messages, "plugin paused: [01] Source2 Callbacks First") == 1 &&
            Count(messages, "plugin paused: [02] Source2 Callbacks Second") == 1 &&
            Count(messages, "plugin resumed: [02] Source2 Callbacks Second") == 1 &&
            Count(messages, "plugin resumed: [01] Source2 Callbacks First") == 1 &&
            Count(messages, "plugin resumed: [03] Source2 Callbacks Peer") == 1 &&
            Count(messages, "[Source2 First] unloaded") == 2 &&
            Count(messages, "[Source2 Second] unloaded") == 1 &&
            Count(messages, "[Source2 Peer] unloaded") == 1 &&
            Contains(messages, "host stopped") &&
            !Contains(messages, "invalid") &&
            !Contains(messages, "listener registration failed") &&
            !Contains(messages, "Source 2 callback recursion limit reached") &&
            !Contains(messages, "plugin threw during a Source 2 callback");
    }
    if (scenario == "authoring_concurrency")
    {
        return selected_profile &&
            Count(messages, "plugin loaded: Authoring Concurrency Test 0.5C") == 1 &&
            Count(messages, "[Authoring Concurrency] loaded") == 1 &&
            Count(messages, "[Authoring Concurrency] unloaded after callback drain") == 1 &&
            Count(messages, "plugin unloaded: [01] Authoring Concurrency Test") == 1 &&
            Contains(messages, "host stopped");
    }
    if (scenario == "convar_service")
    {
        return selected_profile &&
            Count(messages, "plugin loaded: ConVar Service Test 0.5D") == 2 &&
            Count(messages, "[ConVar Service Test] staged ConVar contract passed without callback reentry") == 2 &&
            Count(messages, "[ConVar Service Test] self release returned busy without disabling the ConVar") == 2 &&
            Count(messages, "[ConVar Service Test] unloaded after ConVar callback drain") == 2 &&
            Count(messages, "plugin unloaded: [01] ConVar Service Test") == 2 &&
            Count(messages, "plugin threw during a ConVar callback") == 1 &&
            Count(messages, "incompatible persistent ConVar definition was rejected") == 1 &&
            Count(messages, "plugin load was rejected: ConVar Service Test") == 2 &&
            Count(messages, "ConVar broker load validation failed") == 1 &&
            Count(messages, "VEngineCvar007 rejected the ConVar") == 1 &&
            Contains(messages, "host stopped") &&
            !Contains(messages, "native resources could not be rolled back");
    }
    if (scenario == "convar_failed_load")
    {
        return selected_profile &&
            Count(messages, "rejecting load after staged ConVar creation") == 1 &&
            Count(messages, "plugin load was rejected: ConVar Service Test") == 1 &&
            !Contains(messages, "staged ConVar contract passed without callback reentry") &&
            !Contains(messages, "unloaded after ConVar callback drain") &&
            !Contains(messages, "native resources could not be rolled back") &&
            Contains(messages, "host stopped");
    }
    if (scenario == "convar_facade")
    {
        return selected_profile &&
            Count(messages, "plugin loaded: KeelS2 Source 2 Sample 0.9.0") == 2 &&
            Count(messages, "[KeelS2 Source 2 Sample] ready command=keel_sample") == 2 &&
            Count(messages, "[KeelS2 Source 2 Sample] LevelInit context=complete") == 2 &&
            Count(messages, "[KeelS2 Source 2 Sample] LevelShutdown") == 2 &&
            Count(messages, "[KeelS2 Source 2 Sample] event=round_start") == 2 &&
            Count(messages, "[KeelS2 Source 2 Sample] GameFrame") == 2 &&
            Count(messages, "[KeelS2 Source 2 Sample] ClientConnected") == 2 &&
            Count(messages, "[KeelS2 Source 2 Sample] ClientPutInServer") == 2 &&
            Count(messages, "[KeelS2 Source 2 Sample] ClientActive") == 2 &&
            Count(messages, "[KeelS2 Source 2 Sample] ClientFullyConnected") == 2 &&
            Count(messages, "[KeelS2 Source 2 Sample] ClientDisconnecting") == 2 &&
            Count(messages, "[KeelS2 Source 2 Sample] ClientSettingsChanged") == 2 &&
            Count(messages, "[KeelS2 Source 2 Sample] ClientConnect slot=") == 2 &&
            Count(messages, "[KeelS2 Source 2 Sample] ClientCommand slot=") == 2 &&
            Count(messages, "[KeelS2 Source 2 Sample] caller=") == 14 &&
            Count(messages, "[KeelS2 Source 2 Sample] ERROR: usage: keel_sample [bump]") == 1 &&
            Count(messages, "[KeelS2 Source 2 Sample] keels2_sample_int changed") == 14 &&
            Contains(messages, "old=42 new=99") &&
            Contains(messages, "old=99 new=100") &&
            Contains(messages, "old=50 new=100") &&
            Contains(messages, "old=100 new=0") &&
            Contains(messages, "old=9 new=10") &&
            Contains(messages, "int=9 float=4 mp_limitteams=2") &&
            Contains(messages, "int=10 float=4 mp_limitteams=2") &&
            Contains(messages, "GameFrame simulating=true first_tick=false last_tick=true") &&
            Count(messages, "plugin paused: [01] KeelS2 Source 2 Sample") == 1 &&
            Count(messages, "plugin resumed: [01] KeelS2 Source 2 Sample") == 1 &&
            Count(messages, "[KeelS2 Source 2 Sample] unloaded; ordinary resources required no manual cleanup") == 2 &&
            Count(messages, "plugin unloaded: [01] KeelS2 Source 2 Sample") == 2 &&
            Contains(messages, "host stopped") &&
            !Contains(messages, "old=100 new=50") &&
            !Contains(messages, "registration failed") &&
            !Contains(messages, "log formatting failed") &&
            !Contains(messages, "exception escaped a ConVar callback") &&
            !Contains(messages, "plugin threw during a Source 2 callback");
    }
    if (scenario == "convar_authoring")
    {
        return selected_profile &&
            Count(messages, "plugin loaded: ConVar Authoring Contract 0.7.0") == 2 &&
            Count(
                messages,
                "[ConVar Authoring Contract] loaded int=11 float=2.5 bool=true "
                "string=keels2 limitteams=2") == 2 &&
            Count(messages, "[ConVar Authoring Contract] unload invalidation=true") == 2 &&
            Count(messages, "plugin paused: [01] ConVar Authoring Contract") == 1 &&
            Count(messages, "plugin resumed: [01] ConVar Authoring Contract") == 1 &&
            Count(messages, "plugin unloaded: [01] ConVar Authoring Contract") == 2 &&
            Contains(messages, "host stopped") &&
            !Contains(messages, "unload invalidation=false") &&
            !Contains(messages, "log formatting failed") &&
            !Contains(messages, "exception escaped a ConVar callback") &&
            !Contains(messages, "plugin threw during a ConVar callback");
    }
    if (scenario == "lifecycle_pre_init")
    {
        return selected_profile &&
            Count(messages, "plugin loaded: Lifecycle Test 0.5B") == 1 &&
            Count(messages, "[Lifecycle Test] loaded") == 1 &&
            Count(
                messages,
                "lifecycle GameFrame hook armed: Source2Server001 slot 19, shared vtable") == 1 &&
            Count(messages, "native GameFrame hook entered") == 1 &&
            Count(messages, "[Lifecycle Test] GameFrame") == 1 &&
            Count(messages, "[Lifecycle Test] unloaded") == 1 &&
            Count(messages, "plugin unloaded: [01] Lifecycle Test") == 1 &&
            Contains(messages, "host stopped") &&
            !Contains(messages, "[Lifecycle Test] callback entered during load");
    }
    if (scenario == "failed_server_init")
    {
        return selected_profile &&
            Contains(messages, "host started for cs2") &&
            Contains(messages, "host stopped") &&
            !Contains(messages, "host module retained");
    }
    if (scenario == "missing_game_event_capture")
    {
        return selected_profile &&
            Contains(
                messages,
                "game adapter completion failed: CGameEventManager::LoadEventsFromFile was not observed during Source2Server001::Init") &&
            Contains(messages, "[KeelS2/bootstrap] host rejected post-init completion") &&
            Contains(messages, "host stopped") &&
            !Contains(messages, "host module retained");
    }
    if (scenario == "lifecycle_failed_load")
    {
        return selected_profile &&
            Contains(messages, "[Lifecycle Test] loaded") &&
            Contains(messages, "[Lifecycle Test] rejecting load after staged subscriptions") &&
            Contains(messages, "[Lifecycle Test] unloaded") &&
            Contains(messages, "plugin load was rejected: Lifecycle Test") &&
            !Contains(messages, "[Lifecycle Test] GameFrame") &&
            !Contains(messages, "[Lifecycle Test] ClientConnected") &&
            !Contains(messages, "[Lifecycle Test] ClientPutInServer") &&
            !Contains(messages, "[Lifecycle Test] ClientActive") &&
            !Contains(messages, "[Lifecycle Test] ClientFullyConnected") &&
            !Contains(messages, "[Lifecycle Test] ClientDisconnecting") &&
            !Contains(messages, "[Lifecycle Test] ClientSettingsChanged") &&
            !Contains(messages, "[Lifecycle Test] registration order passed") &&
            !Contains(messages, "[Lifecycle Test] registration order failed") &&
            !Contains(messages, "[Lifecycle Test] self unsubscribe safely deferred") &&
            !Contains(messages, "[Lifecycle Test] self unsubscribe failed") &&
            !Contains(messages, "[Lifecycle Test] removed subscription dispatched") &&
            !Contains(messages, "[Lifecycle Test] callback entered during load") &&
            !Contains(messages, "native resources could not be rolled back") &&
            Contains(messages, "host stopped");
    }
    if (scenario == "keelhook")
    {
        return selected_profile &&
            Contains(messages, "descriptor and service-query fuzz passed") &&
            Contains(messages, "[KeelHook Target Fixture] resolver and incompatible-prototype checks passed") &&
            Contains(messages, "callbacks remained staged until plugin activation") &&
            Contains(messages, "[KeelHook Peer Fixture] shared physical target joined") &&
            Contains(messages, "[KeelHook Peer Fixture] shared typed lease reset and reuse passed") &&
            Contains(messages, "[KeelHook Peer Fixture] shared typed callbacks dispatched independently") &&
            Contains(messages, "plugin unload is busy in KeelHook: KeelHook Target Fixture") &&
            Contains(messages, "detour, virtual scopes, aggregate calls, ordering, recursion, action semantics, explicit control, and concurrency passed") &&
            Contains(messages, "plugin paused: [01] KeelHook Target Fixture") &&
            Contains(messages, "plugin resumed: [01] KeelHook Target Fixture") &&
            Contains(messages, "peer unload callback ran after automatic cleanup") &&
            Contains(messages, "peer cleanup and last-callback restoration passed") &&
            Contains(messages, "dispatch benchmark ns/call: no-hook=") &&
            Contains(messages, "callback restoration retry semantics passed") &&
            Contains(messages, "automatic target-owner cleanup passed before module unload") &&
            Contains(messages, "concurrent unload probe armed") &&
            Contains(messages, "concurrent callback retained host API access during unload") &&
            Count(messages, "plugin loaded:") == 2 && Count(messages, "plugin unloaded:") == 2 &&
            Count(messages, "KeelHook: target is already managed with an incompatible prototype") == 1 &&
            !Contains(messages, "automatic target-owner cleanup failed");
    }
    if (scenario == "keelhook_shutdown_retry")
    {
        return selected_profile &&
            Contains(messages, "shutdown restoration retry probe armed") &&
            Contains(messages, "KeelHook could not restore every physical target during shutdown") &&
            Contains(messages, "host cleanup is waiting for native resources to become safe") &&
            Contains(messages, "shutdown retry restored the target before module unload") &&
            Contains(messages, "host stopped") &&
            Count(messages, "plugin loaded:") == 2 && Count(messages, "plugin unloaded:") == 2 &&
            !Contains(messages, "automatic target-owner cleanup failed");
    }
    if (scenario == "plugin_runtime_service")
    {
        return selected_profile &&
            Contains(messages, "[Plugin Runtime Test] ready") &&
            Contains(messages, "[Plugin Runtime Test] loading transition safely rejected") &&
            Contains(messages, "[Plugin Runtime Test] loaded Plugin Runtime Observer") &&
            Contains(messages, "[Plugin Runtime Test] loaded KeelS2 Basic") &&
            Contains(messages, "[Plugin Runtime Test] self-pause safely reported busy") &&
            Contains(messages, "[Plugin Runtime Test] all initial plugins loaded") &&
            Count(messages, "[Plugin Runtime Test] snapshot APIs passed") == 2 &&
            Contains(messages, "[Plugin Runtime Test] paused KeelS2 Basic") &&
            Contains(messages, "[Plugin Runtime Test] resumed KeelS2 Basic") &&
            Contains(messages, "[Plugin Runtime Test] unloaded KeelS2 Basic") &&
            Contains(messages, "  [02] KeelS2 Basic (0.9.0) by KeelS2 Project - paused") &&
            Count(messages, "KeelS2 0.9.0 is active. The basic native plugin is responding.") == 2 &&
            Contains(messages, "[Plugin Runtime Test] unloaded cleanly") &&
            !Contains(messages, "invalid event") &&
            !Contains(messages, "did not report busy") &&
            !Contains(messages, "snapshot failed");
    }
    if (scenario == "plugin_runtime_concurrency")
    {
        return selected_profile &&
            Contains(messages, "[Plugin Runtime Test] ready") &&
            Contains(messages, "[Plugin Runtime Test] loading transition safely rejected") &&
            Contains(messages, "[Plugin Runtime Test] loaded Plugin Runtime Observer") &&
            Contains(messages, "[Plugin Runtime Test] loaded KeelS2 Basic") &&
            Contains(messages, "[Plugin Runtime Test] self-pause safely reported busy") &&
            Contains(messages, "[Plugin Runtime Test] all initial plugins loaded") &&
            Contains(messages, "[Plugin Runtime Test] paused KeelS2 Basic") &&
            Contains(messages, "plugin transition is already active: KeelS2 Basic") &&
            Contains(messages, "[Plugin Runtime Test] unloaded cleanly") &&
            Contains(messages, "KeelS2 0.9.0 is active. The basic native plugin is responding.") &&
            Contains(messages, "host stopped") &&
            !Contains(messages, "invalid event") &&
            !Contains(messages, "did not report busy") &&
            !Contains(messages, "snapshot failed");
    }
    if (scenario == "plugin_transition_shutdown_retry")
    {
        return selected_profile &&
            Contains(messages, "[Plugin Runtime Test] ready") &&
            Contains(messages, "[Plugin Runtime Test] paused KeelS2 Basic") &&
            Contains(messages, "plugin transition is already active: KeelS2 Basic") &&
            Contains(messages, "host cleanup is waiting for an active plugin transition") &&
            Contains(messages, "host cleanup is waiting for native resources to become safe") &&
            Contains(messages, "[Plugin Runtime Test] unloaded cleanly") &&
            Contains(messages, "host stopped") &&
            !Contains(messages, "host module retained") &&
            !Contains(messages, "invalid event") &&
            !Contains(messages, "snapshot failed");
    }
    if (scenario == "plugin_dependencies")
    {
        return selected_profile &&
            Count(messages, "[Dependency Test] load Dependency Core") == 1 &&
            Count(messages, "[Dependency Test] load Dependency Middle") == 1 &&
            Count(messages, "[Dependency Test] load Dependency Leaf") == 1 &&
            ContainsInOrder(
                messages,
                "[Dependency Test] load Dependency Core",
                "[Dependency Test] load Dependency Middle") &&
            ContainsInOrder(
                messages,
                "[Dependency Test] load Dependency Middle",
                "[Dependency Test] load Dependency Leaf") &&
            Contains(messages, "Dependency Missing: missing dependency Not Installed 1.0.0") &&
            Contains(messages, "Dependency Mismatch: dependency version mismatch for Dependency Core: found 1.2.3, required 9.9.9") &&
            Count(messages, "dependency cycle detected") == 2 &&
            Contains(messages, "Dependency Self: plugin cannot depend on itself") &&
            Contains(messages, "dependency manifest is incompatible") &&
            Contains(messages, "plugin pause is blocked by running dependent Dependency Middle: Dependency Core") &&
            Contains(messages, "plugin unload is blocked by running dependent Dependency Middle: Dependency Core") &&
            Contains(messages, "plugin pause is blocked by running dependent Dependency Leaf: Dependency Middle") &&
            Contains(messages, "plugin paused: [") &&
            Contains(messages, "] Dependency Core") &&
            Contains(messages, "plugin resume was rejected: Dependency Leaf: dependency is not running: Dependency Middle") &&
            Contains(messages, "plugin resume was rejected: Dependency Middle: dependency is not running: Dependency Core") &&
            Contains(messages, "plugin resumed: [") &&
            ContainsInOrder(
                messages,
                "[Dependency Test] unload Dependency Leaf",
                "[Dependency Test] unload Dependency Middle") &&
            ContainsInOrder(
                messages,
                "[Dependency Test] unload Dependency Middle",
                "[Dependency Test] unload Dependency Core") &&
            !Contains(messages, "[Dependency Test] load Dependency Missing") &&
            !Contains(messages, "[Dependency Test] load Dependency Mismatch") &&
            !Contains(messages, "[Dependency Test] load Dependency Cycle") &&
            !Contains(messages, "[Dependency Test] load Dependency Self") &&
            !Contains(messages, "[Dependency Test] load Dependency Malformed") &&
            Contains(messages, "host stopped");
    }
    if (scenario == "published_services")
    {
        return selected_profile &&
            Contains(messages, "[Published Service Provider] versioned service published") &&
            Contains(messages, "[Published Service Consumer] versioned service consumed") &&
            Contains(messages, "plugin pause is blocked by running dependent Published Service Consumer: Published Service Provider") &&
            Contains(messages, "plugin unload is blocked by running dependent Published Service Consumer: Published Service Provider") &&
            Contains(messages, "plugin reload is blocked by running dependent Published Service Consumer: Published Service Provider") &&
            Contains(messages, "[Published Service Consumer] service lease released") &&
            Contains(messages, "[Published Service Provider] provider unloaded after publication withdrawal") &&
            Contains(messages, "[Published Service Consumer] withdrawn service is no longer queryable") &&
            !Contains(messages, "service lease release failed") &&
            !Contains(messages, "withdrawn service remained queryable") &&
            Contains(messages, "host stopped");
    }
    if (scenario == "reload_retry")
    {
        return selected_profile &&
            Contains(messages, "plugin retry succeeded: KeelS2 Basic") &&
            Count(messages, "plugin reloaded transactionally: KeelS2 Basic") == 102 &&
            Count(messages, "plugin reload failed; previous image restored: KeelS2 Basic") == 5 &&
            Contains(messages, "plugin paused: [01] KeelS2 Basic") &&
            Contains(messages, "plugin resumed: [01] KeelS2 Basic") &&
            !Contains(messages, "plugin reload and rollback both failed") &&
            Contains(messages, "host stopped");
    }
    if (scenario == "abi_v4_compatibility")
    {
        return selected_profile &&
            Contains(messages, "plugin loaded: KeelS2 ABI 4 Fixture 0.8.0") &&
            Contains(messages, "[KeelS2 ABI 4 Fixture] frozen ABI 4 fixture loaded") &&
            ContainsInOrder(
                messages,
                "[KeelS2 ABI 4 Fixture] frozen ABI 4 fixture unloaded",
                "host stopped");
    }
    if (scenario == "success")
    {
        return selected_profile && Contains(messages, "plugin loaded: KeelS2 Basic 0.9.0") &&
            Contains(messages, "[KeelS2 Basic] KeelS2 0.9.0 is active. The basic native plugin is responding.") &&
            Contains(messages, "Plugin [01]") && Contains(messages, "Name: KeelS2 Basic") &&
            !Contains(messages, "[KeelS2] [INFO]") &&
            ContainsInOrder(messages, "[KeelS2 Basic] unload callback completed", "host stopped");
    }
    return false;
}

}

int main(int argument_count, char** arguments)
{
    if (argument_count != 41)
    {
        return 1;
    }

    const std::string scenario = arguments[1];
    const std::filesystem::path production_proxy_source = arguments[2];
    const std::filesystem::path test_proxy_source = arguments[3];
    const std::filesystem::path host_source = arguments[4];
    const std::filesystem::path plugin_source = arguments[5];
    const std::filesystem::path invalid_plugin_source = arguments[6];
    const std::filesystem::path failing_plugin_source = arguments[7];
    const std::filesystem::path real_server_source = arguments[8];
    const std::filesystem::path lifecycle_first_source = arguments[9];
    const std::filesystem::path lifecycle_second_source = arguments[10];
    const std::filesystem::path reserved_command_source = arguments[11];
    const std::filesystem::path duplicate_command_source = arguments[12];
    const std::filesystem::path fake_tier0_source = arguments[13];
    const std::filesystem::path keelhook_target_source = arguments[14];
    const std::filesystem::path keelhook_peer_source = arguments[15];
    const std::filesystem::path source2_service_source = arguments[16];
    const std::filesystem::path lifecycle_service_source = arguments[17];
    const std::filesystem::path authoring_concurrency_source = arguments[18];
    const std::filesystem::path convar_service_source = arguments[19];
    const std::filesystem::path convar_facade_source = arguments[20];
    const std::filesystem::path plugin_runtime_service_source = arguments[21];
    const std::filesystem::path dependency_core_source = arguments[22];
    const std::filesystem::path dependency_middle_source = arguments[23];
    const std::filesystem::path dependency_leaf_source = arguments[24];
    const std::filesystem::path dependency_missing_source = arguments[25];
    const std::filesystem::path dependency_mismatch_source = arguments[26];
    const std::filesystem::path dependency_cycle_a_source = arguments[27];
    const std::filesystem::path dependency_cycle_b_source = arguments[28];
    const std::filesystem::path dependency_self_source = arguments[29];
    const std::filesystem::path dependency_malformed_source = arguments[30];
    const std::filesystem::path source2_callbacks_first_source = arguments[31];
    const std::filesystem::path source2_callbacks_second_source = arguments[32];
    const std::filesystem::path source2_callbacks_peer_source = arguments[33];
    const std::filesystem::path schema_entity_service_source = arguments[34];
    const std::filesystem::path schema_entity_fixture_source = arguments[35];
    const std::filesystem::path convar_authoring_source = arguments[36];
    const std::filesystem::path published_service_provider_source = arguments[37];
    const std::filesystem::path published_service_consumer_source = arguments[38];
    const std::filesystem::path abi_v4_fixture_source = arguments[39];
    const std::filesystem::path fixture = std::filesystem::path(arguments[40]) / scenario;
    const std::filesystem::path shutdown_trace = fixture / "shutdown.trace";

#if defined(_WIN32)
    const char* platform = "win64";
    const char* server_name = "server.dll";
    const char* host_name = "keels2_host.dll";
    const char* adapter_name = "keels2_game_cs2.dll";
    const char* plugin_extension = ".dll";
#else
    const char* platform = "linuxsteamrt64";
    const char* server_name = "libserver.so";
    const char* host_name = "libkeels2_host.so";
    const char* adapter_name = "libkeels2_game_cs2.so";
    const char* plugin_extension = ".so";
#endif

    const bool invalid_layout = scenario == "invalid_layout";
    const bool missing_server = scenario == "missing_server" || invalid_layout;
    const bool unknown_build = scenario == "unknown_build";
    const bool missing_host = scenario == "missing_host";
    const bool missing_plugin = scenario == "missing_plugin";
    const bool invalid_plugin = scenario == "invalid_plugin";
    const bool failed_plugin_load = scenario == "failed_plugin_load";
    const bool reverse_unload = scenario == "reverse_unload";
    const bool missing_cvar = scenario == "missing_cvar";
    const bool missing_source2_server = scenario == "missing_source2_server";
    const bool missing_game_clients = scenario == "missing_game_clients";
    const bool wrong_cvar_provenance = scenario == "wrong_cvar_provenance";
    const bool duplicate_command = scenario == "duplicate_command";
    const bool duplicate_plugin_name = scenario == "duplicate_plugin_name";
    const bool reserved_command = scenario == "reserved_command";
    const bool core_commands = scenario == "core_commands";
    const bool plugin_lifecycle = scenario == "plugin_lifecycle";
    const bool command_removal = scenario == "command_removal";
    const bool plugin_index_compaction = scenario == "plugin_index_compaction";
    const bool source2_service = scenario == "source2_service";
    const bool schema_entity_service = scenario == "schema_entity_service";
    const bool lifecycle_service = scenario == "lifecycle_service";
    const bool source2_callbacks = scenario == "source2_callbacks";
    const bool authoring_concurrency = scenario == "authoring_concurrency";
    const bool convar_service = scenario == "convar_service";
    const bool convar_failed_load = scenario == "convar_failed_load";
    const bool convar_facade = scenario == "convar_facade";
    const bool convar_authoring = scenario == "convar_authoring";
    const bool plugin_runtime_service = scenario == "plugin_runtime_service";
    const bool plugin_runtime_concurrency = scenario == "plugin_runtime_concurrency";
    const bool plugin_transition_shutdown_retry =
        scenario == "plugin_transition_shutdown_retry";
    const bool plugin_dependencies = scenario == "plugin_dependencies";
    const bool published_services = scenario == "published_services";
    const bool reload_retry = scenario == "reload_retry";
    const bool abi_v4_compatibility = scenario == "abi_v4_compatibility";
    const bool lifecycle_pre_init = scenario == "lifecycle_pre_init";
    const bool lifecycle_failed_load = scenario == "lifecycle_failed_load";
    const bool failed_server_init = scenario == "failed_server_init";
    const bool missing_game_event_capture = scenario == "missing_game_event_capture";
    const bool keelhook = scenario == "keelhook";
    const bool keelhook_shutdown_retry = scenario == "keelhook_shutdown_retry";
    const bool success = scenario == "success";
    if (!missing_server && !unknown_build && !missing_host && !missing_plugin &&
        !invalid_plugin && !failed_plugin_load && !reverse_unload && !missing_cvar &&
        !missing_source2_server && !missing_game_clients && !wrong_cvar_provenance &&
        !duplicate_command && !duplicate_plugin_name && !reserved_command &&
        !core_commands && !plugin_lifecycle && !command_removal &&
        !plugin_index_compaction && !source2_service && !schema_entity_service &&
        !lifecycle_service &&
        !source2_callbacks &&
        !authoring_concurrency && !convar_service && !convar_failed_load && !convar_facade &&
        !convar_authoring &&
        !plugin_runtime_service && !plugin_runtime_concurrency &&
        !plugin_transition_shutdown_retry && !plugin_dependencies && !published_services &&
        !reload_retry && !abi_v4_compatibility &&
        !lifecycle_pre_init && !lifecycle_failed_load && !failed_server_init &&
        !missing_game_event_capture && !keelhook &&
        !keelhook_shutdown_retry && !success)
    {
        return 2;
    }

    keels2::platform::DynamicLibrary fake_tier0;
    std::string loader_error;
    if (!fake_tier0.Open(fake_tier0_source, loader_error))
    {
        return 3;
    }
    using MessagesFunction = const char* (*)();
    using ClearMessagesFunction = void (*)();
    const auto messages = reinterpret_cast<MessagesFunction>(fake_tier0.Symbol("KeelTest_Messages"));
    const auto clear_messages = reinterpret_cast<ClearMessagesFunction>(fake_tier0.Symbol("KeelTest_ClearMessages"));
    if (!messages || !clear_messages)
    {
        return 4;
    }
    clear_messages();
    g_cvar.Reset();
    keels2::platform::DynamicLibrary schema_entity_fixture;
    if (!schema_entity_fixture.Open(schema_entity_fixture_source, loader_error))
    {
        return 123;
    }
    using InterfaceFunction = void* (*)();
    using SchemaEntityVoidFunction = void (*)();
    using SchemaEntityReadyFunction = void (*)(bool);
    using SchemaEntityCountFunction = std::uint32_t (*)();
    const auto schema_system = reinterpret_cast<InterfaceFunction>(
        schema_entity_fixture.Symbol("KeelTest_SchemaSystem"));
    const auto game_resource = reinterpret_cast<InterfaceFunction>(
        schema_entity_fixture.Symbol("KeelTest_GameResourceService"));
    const auto reset_schema_entities = reinterpret_cast<SchemaEntityVoidFunction>(
        schema_entity_fixture.Symbol("KeelTest_ResetSchemaEntities"));
    const auto set_entity_system_ready = reinterpret_cast<SchemaEntityReadyFunction>(
        schema_entity_fixture.Symbol("KeelTest_SetEntitySystemReady"));
    const auto destroy_entity = reinterpret_cast<SchemaEntityVoidFunction>(
        schema_entity_fixture.Symbol("KeelTest_DestroyEntity"));
    const auto reuse_entity = reinterpret_cast<SchemaEntityVoidFunction>(
        schema_entity_fixture.Symbol("KeelTest_ReuseEntity"));
    const auto schema_lookup_count = reinterpret_cast<SchemaEntityCountFunction>(
        schema_entity_fixture.Symbol("KeelTest_SchemaLookupCount"));
    if (!schema_system || !game_resource || !reset_schema_entities ||
        !set_entity_system_ready || !destroy_entity || !reuse_entity ||
        !schema_lookup_count)
    {
        return 124;
    }
    reset_schema_entities();
    g_schema_system_interface = schema_system();
    g_game_resource_interface = game_resource();
    if (!g_schema_system_interface || !g_game_resource_interface)
    {
        return 125;
    }
    if (!ResetEngineAllocatorDiagnostics())
    {
        return 103;
    }
    if ((convar_service || convar_failed_load) &&
        !g_cvar.SeedInt32("sv_keels2_existing", 42))
    {
        return 74;
    }
    if ((convar_facade || convar_authoring) &&
        !g_cvar.SeedInt32("mp_limitteams", 2))
    {
        return 74;
    }
    std::array<void*, keels2::cs2::kUnregisterConCommandSlot + 1> foreign_cvar_vtable{};
    void** foreign_cvar_vptr{};
    if (wrong_cvar_provenance)
    {
        void* foreign_target = fake_tier0.Symbol("Msg");
        if (!foreign_target)
        {
            return 45;
        }
        foreign_cvar_vtable.fill(foreign_target);
        foreign_cvar_vptr = foreign_cvar_vtable.data();
        g_cvar_override = &foreign_cvar_vptr;
    }

    std::error_code error;
    std::filesystem::remove_all(fixture, error);
    if (missing_source2_server || missing_game_clients)
    {
#if defined(_WIN32)
        if (_putenv_s(
                "KEELS2_TEST_MISSING_SOURCE2_INTERFACE",
                missing_source2_server ? "server_after_bootstrap" : "game_clients") != 0)
#else
        if (setenv(
                "KEELS2_TEST_MISSING_SOURCE2_INTERFACE",
                missing_source2_server ? "server_after_bootstrap" : "game_clients",
                1) != 0)
#endif
        {
            return 42;
        }
    }
    if (reverse_unload)
    {
#if defined(_WIN32)
        if (_putenv_s("KEELS2_SHUTDOWN_TRACE_FILE", shutdown_trace.string().c_str()) != 0)
#else
        if (setenv("KEELS2_SHUTDOWN_TRACE_FILE", shutdown_trace.string().c_str(), 1) != 0)
#endif
        {
            return 39;
        }
    }
    if (lifecycle_failed_load)
    {
#if defined(_WIN32)
        if (_putenv_s("KEELS2_TEST_LIFECYCLE_FAIL_LOAD", "1") != 0)
#else
        if (setenv("KEELS2_TEST_LIFECYCLE_FAIL_LOAD", "1", 1) != 0)
#endif
        {
            return 55;
        }
    }
    if (lifecycle_pre_init)
    {
#if defined(_WIN32)
        if (_putenv_s("KEELS2_TEST_GAME_FRAME_DURING_INIT", "1") != 0)
#else
        if (setenv("KEELS2_TEST_GAME_FRAME_DURING_INIT", "1", 1) != 0)
#endif
        {
            return 63;
        }
    }
    if (failed_server_init)
    {
#if defined(_WIN32)
        if (_putenv_s("KEELS2_TEST_SERVER_INIT_FAILURE", "1") != 0)
#else
        if (setenv("KEELS2_TEST_SERVER_INIT_FAILURE", "1", 1) != 0)
#endif
        {
            return 64;
        }
    }
    if (missing_game_event_capture)
    {
#if defined(_WIN32)
        if (_putenv_s("KEELS2_TEST_SKIP_GAME_EVENT_LOAD", "1") != 0)
#else
        if (setenv("KEELS2_TEST_SKIP_GAME_EVENT_LOAD", "1", 1) != 0)
#endif
        {
            return 120;
        }
    }
    if (convar_failed_load)
    {
#if defined(_WIN32)
        if (_putenv_s("KEELS2_TEST_CONVAR_FAIL_LOAD", "1") != 0)
#else
        if (setenv("KEELS2_TEST_CONVAR_FAIL_LOAD", "1", 1) != 0)
#endif
        {
            return 75;
        }
    }
    const auto normal_proxy_path = fixture / "game" / "csgo" / "addons" / "keels2" / "bin" / platform / server_name;
    const auto proxy_path = invalid_layout ? fixture / "invalid" / "bin" / platform / server_name : normal_proxy_path;
    const auto host_path = normal_proxy_path.parent_path() / host_name;
    const auto adapter_source = host_source.parent_path() / adapter_name;
    const auto adapter_path = normal_proxy_path.parent_path() / adapter_name;
    const auto plugin_directory = fixture / "game" / "csgo" / "addons" / "keels2" / "plugins" / platform;
    const auto lifecycle_plugin_path =
        plugin_directory / (std::string("01_lifecycle_service") + plugin_extension);
    const auto authoring_concurrency_plugin_path =
        plugin_directory / (std::string("01_authoring_concurrency") + plugin_extension);
    const auto convar_plugin_path =
        plugin_directory / (std::string("01_convar_service") + plugin_extension);
    const auto convar_facade_plugin_path =
        plugin_directory / (std::string("01_convar_facade") + plugin_extension);
    const auto convar_authoring_plugin_path =
        plugin_directory / (std::string("01_convar_authoring") + plugin_extension);
    const auto plugin_runtime_service_path =
        plugin_directory / (std::string("01_plugin_runtime") + plugin_extension);
    const auto source2_callbacks_first_path =
        plugin_directory / (std::string("01_source2_callbacks_first") + plugin_extension);
    const auto source2_callbacks_second_path =
        plugin_directory / (std::string("02_source2_callbacks_second") + plugin_extension);
    const auto source2_callbacks_peer_path =
        plugin_directory / (std::string("03_source2_callbacks_peer") + plugin_extension);
    const auto schema_entity_service_path =
        plugin_directory / (std::string("01_schema_entity_service") + plugin_extension);
    const auto published_service_provider_path =
        plugin_directory / (std::string("01_published_service_provider") + plugin_extension);
    const auto published_service_consumer_path =
        plugin_directory / (std::string("02_published_service_consumer") + plugin_extension);
    const auto reload_retry_path =
        plugin_directory / (std::string("01_retry") + plugin_extension);
    const auto abi_v4_fixture_path =
        plugin_directory / (std::string("01_abi_v4_fixture") + plugin_extension);
    const auto real_server_path = fixture / "game" / "csgo" / "bin" / platform / server_name;
    const auto& proxy_source = unknown_build ? production_proxy_source : test_proxy_source;

    if (!CopyFile(proxy_source, proxy_path))
    {
        return 5;
    }
    if (!missing_server && !CopyFile(real_server_source, real_server_path))
    {
        return 6;
    }
    if (!missing_server && !unknown_build && !missing_host &&
        (!CopyFile(host_source, host_path) || !CopyFile(adapter_source, adapter_path)))
    {
        return 7;
    }
    if (missing_plugin)
    {
        std::filesystem::create_directories(plugin_directory, error);
        if (error)
        {
            return 8;
        }
    }
    if ((success || core_commands || plugin_lifecycle || command_removal ||
         plugin_index_compaction) &&
        !CopyFile(plugin_source, plugin_directory / (std::string("01_basic") + plugin_extension)))
    {
        return 9;
    }
    if (invalid_plugin &&
        !CopyFile(invalid_plugin_source, plugin_directory / (std::string("invalid") + plugin_extension)))
    {
        return 10;
    }
    if (failed_plugin_load &&
        !CopyFile(failing_plugin_source, plugin_directory / (std::string("failing") + plugin_extension)))
    {
        return 11;
    }
    if (duplicate_command &&
        (!CopyFile(plugin_source, plugin_directory / (std::string("01_basic") + plugin_extension)) ||
         !CopyFile(duplicate_command_source, plugin_directory / (std::string("02_duplicate_command") + plugin_extension))))
    {
        return 12;
    }
    if (duplicate_plugin_name &&
        (!CopyFile(plugin_source, plugin_directory / (std::string("01_basic") + plugin_extension)) ||
         !CopyFile(plugin_source, plugin_directory / (std::string("02_basic") + plugin_extension))))
    {
        return 13;
    }
    if ((reverse_unload || core_commands || plugin_index_compaction) &&
        (!CopyFile(lifecycle_first_source, plugin_directory / (std::string("02_lifecycle_first") + plugin_extension)) ||
         !CopyFile(lifecycle_second_source, plugin_directory / (std::string("03_lifecycle_second") + plugin_extension))))
    {
        return 14;
    }
    if (reserved_command &&
        !CopyFile(reserved_command_source, plugin_directory / (std::string("reserved") + plugin_extension)))
    {
        return 15;
    }
    if ((keelhook || keelhook_shutdown_retry) &&
        (!CopyFile(
            keelhook_target_source,
            plugin_directory / (std::string("01_keelhook_target") + plugin_extension)) ||
         !CopyFile(
            keelhook_peer_source,
            plugin_directory / (std::string("02_keelhook_peer") + plugin_extension))))
    {
        return 36;
    }
    if (source2_service &&
        !CopyFile(
            source2_service_source,
            plugin_directory / (std::string("01_source2_service") + plugin_extension)))
    {
        return 43;
    }
    if (schema_entity_service &&
        !CopyFile(schema_entity_service_source, schema_entity_service_path))
    {
        return 126;
    }
    if ((lifecycle_service || lifecycle_pre_init || lifecycle_failed_load) &&
        !CopyFile(
            lifecycle_service_source,
            lifecycle_plugin_path))
    {
        return 46;
    }
    if (authoring_concurrency &&
        !CopyFile(authoring_concurrency_source, authoring_concurrency_plugin_path))
    {
        return 68;
    }
    if ((convar_service || convar_failed_load) &&
        !CopyFile(convar_service_source, convar_plugin_path))
    {
        return 76;
    }
    if (convar_facade && !CopyFile(convar_facade_source, convar_facade_plugin_path))
    {
        return 92;
    }
    if (convar_authoring &&
        !CopyFile(convar_authoring_source, convar_authoring_plugin_path))
    {
        return 135;
    }
    if ((plugin_runtime_service || plugin_runtime_concurrency ||
            plugin_transition_shutdown_retry) &&
        (!CopyFile(plugin_runtime_service_source, plugin_runtime_service_path) ||
         !CopyFile(plugin_source,
             plugin_directory / (std::string("02_basic") + plugin_extension))))
    {
        return 93;
    }
    if (plugin_dependencies &&
        (!CopyFile(dependency_leaf_source,
             plugin_directory / (std::string("01_dependency_leaf") + plugin_extension)) ||
         !CopyFile(dependency_middle_source,
             plugin_directory / (std::string("02_dependency_middle") + plugin_extension)) ||
         !CopyFile(dependency_core_source,
             plugin_directory / (std::string("03_dependency_core") + plugin_extension)) ||
         !CopyFile(dependency_missing_source,
             plugin_directory / (std::string("04_dependency_missing") + plugin_extension)) ||
         !CopyFile(dependency_mismatch_source,
             plugin_directory / (std::string("05_dependency_mismatch") + plugin_extension)) ||
         !CopyFile(dependency_cycle_a_source,
             plugin_directory / (std::string("06_dependency_cycle_a") + plugin_extension)) ||
         !CopyFile(dependency_cycle_b_source,
             plugin_directory / (std::string("07_dependency_cycle_b") + plugin_extension)) ||
         !CopyFile(dependency_self_source,
             plugin_directory / (std::string("08_dependency_self") + plugin_extension)) ||
         !CopyFile(dependency_malformed_source,
             plugin_directory / (std::string("09_dependency_malformed") + plugin_extension))))
    {
        return 95;
    }
    if (source2_callbacks &&
        (!CopyFile(source2_callbacks_first_source, source2_callbacks_first_path) ||
         !CopyFile(source2_callbacks_second_source, source2_callbacks_second_path) ||
         !CopyFile(source2_callbacks_peer_source, source2_callbacks_peer_path)))
    {
        return 108;
    }
    if (published_services &&
        (!CopyFile(published_service_provider_source, published_service_provider_path) ||
         !CopyFile(published_service_consumer_source, published_service_consumer_path)))
    {
        return 142;
    }
    if (reload_retry && !CopyFile(failing_plugin_source, reload_retry_path))
    {
        return 144;
    }
    if (abi_v4_compatibility && !CopyFile(abi_v4_fixture_source, abi_v4_fixture_path))
    {
        return 146;
    }

    keels2::platform::DynamicLibrary proxy;
    if (!proxy.Open(proxy_path, loader_error))
    {
        return 16;
    }
    const auto factory = reinterpret_cast<KeelCreateInterfaceFn>(proxy.Symbol("CreateInterface"));
    if (!factory)
    {
        return 17;
    }

    int return_code = 0;
    void* config = factory("Source2ServerConfig001", &return_code);
    if (missing_server)
    {
        if (config || return_code != 1)
        {
            return 18;
        }
        return_code = 0;
        if (factory("Source2Server001", &return_code) || return_code != 1)
        {
            return 19;
        }
        const char* output = messages();
        return output && ValidateMessages(scenario, output) ? 0 : 20;
    }
    if (unknown_build && !VtableEntryBelongsTo(config, 0, real_server_path))
    {
        return 21;
    }

    g_expose_cvar = !missing_cvar;
    using ConnectFunction = bool (*)(void*, KeelCreateInterfaceFn);
    const auto connect = config ? VtableFunction<ConnectFunction>(config, 0) : nullptr;
    if (!config || return_code != 0 || !connect || !connect(config, &EngineFactory))
    {
        return 22;
    }

    void* server = factory("Source2Server001", &return_code);
    using InitFunction = int (*)(void*);
    const auto init = server ? VtableFunction<InitFunction>(server, 3) : nullptr;
    if (unknown_build && !VtableEntryBelongsTo(server, 3, real_server_path))
    {
        return 23;
    }
    if (!server || return_code != 0 || !init)
    {
        return 24;
    }
    const int init_result = init(server);
    const bool expected_init_failure = failed_server_init || missing_game_event_capture;
    if ((!expected_init_failure && init_result != 1) ||
        (expected_init_failure && init_result != 0))
    {
        return 24;
    }

    if (expected_init_failure)
    {
        using DisconnectFunction = void (*)(void*);
        const auto disconnect = VtableFunction<DisconnectFunction>(config, 1);
        if (!disconnect || g_cvar.register_count != 1 || g_cvar.ActiveCount() != 0 ||
            g_cvar.unregister_count != 1)
        {
            return 65;
        }
        disconnect(config);
#if defined(_WIN32)
        const int clear_result = _putenv_s(
            failed_server_init
                ? "KEELS2_TEST_SERVER_INIT_FAILURE"
                : "KEELS2_TEST_SKIP_GAME_EVENT_LOAD",
            "");
#else
        const int clear_result = unsetenv(
            failed_server_init
                ? "KEELS2_TEST_SERVER_INIT_FAILURE"
                : "KEELS2_TEST_SKIP_GAME_EVENT_LOAD");
#endif
        const char* output = messages();
        if (clear_result != 0 || !output || !ValidateMessages(scenario, output))
        {
            if (output)
            {
                std::fputs(output, stderr);
            }
            return 66;
        }
        return 0;
    }

    keels2::platform::DynamicLibrary lifecycle_fixture;
    using DispatchLifecycleFunction = void (*)();
    using LifecycleCallCountFunction = std::uint32_t (*)(std::uint32_t);
    using ResetLifecycleCallsFunction = void (*)();
    using DispatchClientConnectFunction = bool (*)();
    using DispatchClientCommandFunction = void (*)();
    using RejectionMessageFunction = const char* (*)();
    using OriginalCallCountFunction = std::uint32_t (*)();
    DispatchLifecycleFunction dispatch_lifecycle{};
    LifecycleCallCountFunction lifecycle_call_count{};
    ResetLifecycleCallsFunction reset_lifecycle_calls{};
    DispatchClientConnectFunction dispatch_client_connect{};
    DispatchClientCommandFunction dispatch_client_command{};
    RejectionMessageFunction rejection_message{};
    OriginalCallCountFunction client_connect_original_calls{};
    OriginalCallCountFunction client_command_original_calls{};
    using Source2VoidFunction = void (*)();
    using Source2BoolFunction = bool (*)();
    using Source2CountFunction = std::uint32_t (*)();
    using DispatchGameEventFunction = bool (*)(void*);
    keels2::platform::DynamicLibrary source2_callbacks_first;
    Source2VoidFunction source2_arm_block{};
    Source2BoolFunction source2_block_entered{};
    Source2VoidFunction source2_release_block{};
    Source2CountFunction source2_unload_count{};
    DispatchGameEventFunction dispatch_game_event{};
    Source2CountFunction game_event_load_count{};
    Source2CountFunction game_event_add_count{};
    Source2CountFunction game_event_remove_count{};
    Source2BoolFunction game_event_listener_active{};
    if (lifecycle_service || lifecycle_failed_load || authoring_concurrency ||
        source2_callbacks || convar_facade)
    {
        if (!lifecycle_fixture.Open(real_server_path, loader_error))
        {
            return 47;
        }
        dispatch_lifecycle = reinterpret_cast<DispatchLifecycleFunction>(
            lifecycle_fixture.Symbol("KeelTest_DispatchLifecycle"));
        lifecycle_call_count = reinterpret_cast<LifecycleCallCountFunction>(
            lifecycle_fixture.Symbol("KeelTest_LifecycleCallCount"));
        reset_lifecycle_calls = reinterpret_cast<ResetLifecycleCallsFunction>(
            lifecycle_fixture.Symbol("KeelTest_ResetLifecycleCalls"));
        if (!dispatch_lifecycle || !lifecycle_call_count || !reset_lifecycle_calls)
        {
            return 48;
        }
        reset_lifecycle_calls();
        if (source2_callbacks || convar_facade)
        {
            dispatch_client_connect = reinterpret_cast<DispatchClientConnectFunction>(
                lifecycle_fixture.Symbol("KeelTest_DispatchClientConnect"));
            dispatch_client_command = reinterpret_cast<DispatchClientCommandFunction>(
                lifecycle_fixture.Symbol("KeelTest_DispatchClientCommand"));
            rejection_message = reinterpret_cast<RejectionMessageFunction>(
                lifecycle_fixture.Symbol("KeelTest_RejectionMessage"));
            client_connect_original_calls = reinterpret_cast<OriginalCallCountFunction>(
                lifecycle_fixture.Symbol("KeelTest_ClientConnectOriginalCalls"));
            client_command_original_calls = reinterpret_cast<OriginalCallCountFunction>(
                lifecycle_fixture.Symbol("KeelTest_ClientCommandOriginalCalls"));
            dispatch_game_event = reinterpret_cast<DispatchGameEventFunction>(
                lifecycle_fixture.Symbol("KeelTest_DispatchGameEvent"));
            game_event_load_count = reinterpret_cast<Source2CountFunction>(
                lifecycle_fixture.Symbol("KeelTest_GameEventLoadCount"));
            game_event_add_count = reinterpret_cast<Source2CountFunction>(
                lifecycle_fixture.Symbol("KeelTest_GameEventAddCount"));
            game_event_remove_count = reinterpret_cast<Source2CountFunction>(
                lifecycle_fixture.Symbol("KeelTest_GameEventRemoveCount"));
            game_event_listener_active = reinterpret_cast<Source2BoolFunction>(
                lifecycle_fixture.Symbol("KeelTest_GameEventListenerActive"));
            if (!dispatch_client_connect || !dispatch_client_command || !rejection_message ||
                !client_connect_original_calls || !client_command_original_calls ||
                !dispatch_game_event || !game_event_load_count || !game_event_add_count ||
                !game_event_remove_count || !game_event_listener_active)
            {
                return 109;
            }
            if (source2_callbacks)
            {
                if (!source2_callbacks_first.Open(
                        RuntimePluginPath(plugin_directory, source2_callbacks_first_path),
                        loader_error))
                {
                    return 118;
                }
                source2_arm_block = reinterpret_cast<Source2VoidFunction>(
                    source2_callbacks_first.Symbol("KeelTest_Source2ArmBlock"));
                source2_block_entered = reinterpret_cast<Source2BoolFunction>(
                    source2_callbacks_first.Symbol("KeelTest_Source2BlockEntered"));
                source2_release_block = reinterpret_cast<Source2VoidFunction>(
                    source2_callbacks_first.Symbol("KeelTest_Source2ReleaseBlock"));
                source2_unload_count = reinterpret_cast<Source2CountFunction>(
                    source2_callbacks_first.Symbol("KeelTest_Source2UnloadCount"));
                if (!source2_arm_block || !source2_block_entered ||
                    !source2_release_block || !source2_unload_count)
                {
                    return 119;
                }
            }
        }
    }

    std::uint32_t expected_registrations{};
    std::size_t expected_active{};
    if (!unknown_build && !missing_host && !missing_cvar &&
        !missing_source2_server && !missing_game_clients && !wrong_cvar_provenance)
    {
        expected_registrations = 1;
        expected_active = 1;
        if (success || duplicate_command || duplicate_plugin_name || plugin_lifecycle ||
            command_removal || source2_service || schema_entity_service || convar_facade)
        {
            expected_registrations = 2;
            expected_active = 2;
        }
        else if (plugin_runtime_service || plugin_runtime_concurrency ||
            plugin_transition_shutdown_retry)
        {
            expected_registrations = 3;
            expected_active = 3;
        }
        else if (published_services)
        {
            expected_registrations = 3;
            expected_active = 3;
        }
        else if (reload_retry)
        {
            expected_registrations = 2;
            expected_active = 1;
        }
        else if (failed_plugin_load)
        {
            expected_registrations = 2;
            expected_active = 1;
        }
        else if (reverse_unload)
        {
            expected_registrations = 3;
            expected_active = 3;
        }
        else if (core_commands || plugin_index_compaction)
        {
            expected_registrations = 4;
            expected_active = 4;
        }
        else if (keelhook || keelhook_shutdown_retry)
        {
            expected_registrations = 6;
            expected_active = 6;
        }
    }
    if (g_cvar.register_count != expected_registrations || g_cvar.ActiveCount() != expected_active)
    {
        std::fputs(messages(), stderr);
        return 25;
    }
    if (expected_active != 0 && !g_cvar.HasActive("keel"))
    {
        return 26;
    }

    if (success)
    {
        if (!g_cvar.Dispatch({"keel_test", "first", "second"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "info", "1"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "info", "Basic"}))
        {
            return 27;
        }
    }
    if (missing_plugin || invalid_plugin || failed_plugin_load || duplicate_plugin_name)
    {
        if (!g_cvar.Dispatch({"keel", "plugins", "list"}))
        {
            return 28;
        }
    }
    if (core_commands)
    {
        const bool dispatched =
            g_cvar.Dispatch({"keel"}) &&
            g_cvar.Dispatch({"keel", "help", "plugins"}) &&
            g_cvar.Dispatch({"keel", "version"}) &&
            g_cvar.Dispatch({"keel", "game"}) &&
            g_cvar.Dispatch({"keel", "status"}) &&
            g_cvar.Dispatch({"keel", "credits"}) &&
            g_cvar.Dispatch({"keel", "plugins", "list"}) &&
            g_cvar.Dispatch({"keel", "plugins", "info", "1"}) &&
            g_cvar.Dispatch({"keel", "plugins", "info", "kEeLs2 bAsIc"}) &&
            g_cvar.Dispatch({"keel", "plugins", "info", "Basic"}) &&
            g_cvar.Dispatch({"keel", "plugins", "info", "lifecycle"}) &&
            g_cvar.Dispatch({"keel", "plugins", "cmds"}) &&
            g_cvar.Dispatch({"keel", "plugins", "cmds", "1"}) &&
            g_cvar.Dispatch({"keel", "inspect"}) &&
            g_cvar.Dispatch({"keel", "inspect", "hooks"}) &&
            g_cvar.Dispatch({"keel", "inspect", "interfaces"}) &&
            g_cvar.Dispatch({"keel", "inspect", "services"}) &&
            g_cvar.Dispatch({"keel", "inspect", "resources"}) &&
            g_cvar.Dispatch({"keel", "inspect", "profile"}) &&
            g_cvar.Dispatch({"keel", "nonsense"});
        if (!dispatched)
        {
            return 29;
        }
    }
    if (plugin_lifecycle)
    {
        const std::string plugin_filename = std::string("01_basic") + plugin_extension;
        if (!g_cvar.Dispatch({"keel", "plugins"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "load"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "info"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "1"}) ||
            g_cvar.HasActive("keel_test") || g_cvar.ActiveCount() != 1 ||
            g_cvar.unregister_count != 1 || g_cvar.Dispatch({"keel_test"}) ||
            !g_cvar.DispatchRetired({"keel_test", "stale_after_first_unload"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "list"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "load", "../01_basic"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "load", "01_basic.txt"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "load", "missing"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "load", "01_basic"}) ||
            g_cvar.register_count != 3 || g_cvar.ActiveCount() != 2 ||
            !g_cvar.HasActive("keel_test") ||
            !g_cvar.Dispatch({"keel", "plugins", "load", "01_basic"}) ||
            g_cvar.register_count != 3 ||
            !g_cvar.Dispatch({"keel", "plugins", "info", "1"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "cmds", "1"}) ||
            !g_cvar.Dispatch({"keel_test", "after_first_load"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "bAsIc"}) ||
            g_cvar.HasActive("keel_test") || g_cvar.ActiveCount() != 1 ||
            g_cvar.unregister_count != 2 ||
            !g_cvar.DispatchRetired({"keel_test", "stale_after_second_unload"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "999"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "load", plugin_filename.c_str()}) ||
            g_cvar.register_count != 4 || g_cvar.ActiveCount() != 2 ||
            !g_cvar.Dispatch({"keel", "plugins", "info", "1"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "list"}) ||
            !g_cvar.Dispatch({"keel_test", "after_second_load"}))
        {
            return 34;
        }
        expected_registrations = 4;
    }
    if (command_removal)
    {
        if (!g_cvar.Dispatch({"keel_test", "unregister"}) ||
            g_cvar.HasActive("keel_test") || g_cvar.ActiveCount() != 1 ||
            g_cvar.unregister_count != 1 || g_cvar.Dispatch({"keel_test"}) ||
            !g_cvar.DispatchRetired({"keel_test", "stale_after_early_removal"}))
        {
            return 40;
        }
    }
    if (plugin_index_compaction)
    {
        const std::string first_filename = std::string("02_lifecycle_first") + plugin_extension;
        if (!g_cvar.Dispatch({"keel", "plugins", "list"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "2"}) ||
            g_cvar.HasActive("lifecycle_first") || !g_cvar.HasActive("lifecycle_second") ||
            g_cvar.ActiveCount() != 3 || g_cvar.unregister_count != 1 ||
            !g_cvar.DispatchRetired({"lifecycle_first", "stale_after_compaction"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "list"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "info", "2"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "cmds", "2"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "load", first_filename.c_str()}) ||
            !g_cvar.HasActive("lifecycle_first") || !g_cvar.HasActive("lifecycle_second") ||
            g_cvar.ActiveCount() != 4 || g_cvar.register_count != 5 ||
            !g_cvar.Dispatch({"keel", "plugins", "list"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "info", "3"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "cmds", "3"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "2"}) ||
            g_cvar.HasActive("lifecycle_second") || !g_cvar.HasActive("lifecycle_first") ||
            g_cvar.ActiveCount() != 3 || g_cvar.unregister_count != 2 ||
            !g_cvar.Dispatch({"keel", "plugins", "list"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "info", "2"}))
        {
            return 35;
        }
        expected_registrations = 5;
    }
    if (source2_service)
    {
        if (!g_cvar.Dispatch({"s2_check"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "1"}) ||
            g_cvar.HasActive("s2_check") || g_cvar.ActiveCount() != 1 ||
            g_cvar.unregister_count != 1)
        {
            std::fputs(messages(), stderr);
            return 44;
        }
    }
    if (schema_entity_service)
    {
        set_entity_system_ready(true);
        if (!DispatchSource2LevelInit() ||
            !g_cvar.Dispatch({"keel_schema_entity_check", "initial"}))
        {
            std::fputs(messages(), stderr);
            return 127;
        }
        bool offthread_dispatched{};
        std::thread offthread([&] {
            offthread_dispatched =
                g_cvar.Dispatch({"keel_schema_entity_check", "offthread"});
        });
        offthread.join();
        if (!offthread_dispatched)
        {
            return 131;
        }
        destroy_entity();
        if (!g_cvar.Dispatch({"keel_schema_entity_check", "stale"}))
        {
            return 128;
        }
        reuse_entity();
        if (!g_cvar.Dispatch({"keel_schema_entity_check", "reuse"}))
        {
            return 129;
        }
        DispatchSource2LevelShutdown();
        if (!g_cvar.Dispatch({"keel", "plugins", "unload", "1"}) ||
            g_cvar.HasActive("keel_schema_entity_check") ||
            g_cvar.ActiveCount() != 1 || g_cvar.unregister_count != 1)
        {
            std::fputs(messages(), stderr);
            return 130;
        }
        if (schema_lookup_count() != 3)
        {
            return 134;
        }
        reset_schema_entities();
        if (!g_cvar.Dispatch({"keel", "plugins", "load", "01_schema_entity_service"}))
        {
            return 132;
        }
        set_entity_system_ready(true);
        if (!DispatchSource2LevelInit() ||
            !g_cvar.Dispatch({"keel_schema_entity_check", "initial"}) ||
            schema_lookup_count() != 2)
        {
            std::fputs(messages(), stderr);
            return 133;
        }
        expected_registrations = 3;
    }
    if (source2_callbacks)
    {
        const auto all_rejection = [](const char* message) {
            return message && std::strlen(message) == 255 &&
                std::all_of(message, message + 255, [](char value) { return value == 'A'; });
        };
        if (game_event_load_count() != 1 || game_event_add_count() != 1 ||
            !game_event_listener_active() || !DispatchSource2LevelInit() ||
            g_loop_init_calls != 1 || !dispatch_game_event(&g_game_event_instance) ||
            dispatch_client_connect() ||
            std::strcmp(rejection_message(), "peer rejection wins") != 0 ||
            client_connect_original_calls() != 0)
        {
            std::fputs(messages(), stderr);
            return 110;
        }
        dispatch_client_command();
        if (client_command_original_calls() != 0 ||
            !g_cvar.Dispatch({"keel", "plugins", "pause", "Source2 Callbacks Peer"}) ||
            dispatch_client_connect() || !all_rejection(rejection_message()) ||
            !g_cvar.Dispatch({"keel", "plugins", "pause", "Source2 Callbacks First"}) ||
            dispatch_client_connect() ||
            std::strcmp(rejection_message(), "second tie rejection") != 0)
        {
            std::fputs(messages(), stderr);
            return 111;
        }
        dispatch_client_command();
        if (client_command_original_calls() != 0 ||
            !g_cvar.Dispatch({"keel", "plugins", "pause", "Source2 Callbacks Second"}) ||
            !dispatch_client_connect() || rejection_message()[0] != '\0' ||
            client_connect_original_calls() != 1)
        {
            std::fputs(messages(), stderr);
            return 112;
        }
        dispatch_client_command();
        const std::size_t events_before_paused_dispatch =
            Count(messages(), "round_start");
        if (client_command_original_calls() != 1 ||
            !dispatch_game_event(&g_game_event_instance) ||
            Count(messages(), "round_start") != events_before_paused_dispatch ||
            !g_cvar.Dispatch({"keel", "plugins", "resume", "Source2 Callbacks Second"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "resume", "Source2 Callbacks First"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "resume", "Source2 Callbacks Peer"}) ||
            !dispatch_game_event(&g_game_event_instance))
        {
            std::fputs(messages(), stderr);
            return 113;
        }
        DispatchSource2LevelShutdown();
        if (g_loop_shutdown_calls != 1 ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "Source2 Callbacks Peer"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "Source2 Callbacks First"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "Source2 Callbacks Second"}))
        {
            std::fputs(messages(), stderr);
            return 114;
        }
        const std::size_t events_before_unloaded_dispatch =
            Count(messages(), "round_start");
        if (!dispatch_game_event(&g_game_event_instance) ||
            Count(messages(), "round_start") != events_before_unloaded_dispatch ||
            source2_unload_count() != 1)
        {
            std::fputs(messages(), stderr);
            return 115;
        }
        source2_callbacks_first.Close();
        if (!g_cvar.Dispatch({"keel", "plugins", "load", "01_source2_callbacks_first"}) ||
            !source2_callbacks_first.Open(
                RuntimePluginPath(plugin_directory, source2_callbacks_first_path),
                loader_error))
        {
            std::fputs(messages(), stderr);
            return 115;
        }
        source2_arm_block = reinterpret_cast<Source2VoidFunction>(
            source2_callbacks_first.Symbol("KeelTest_Source2ArmBlock"));
        source2_block_entered = reinterpret_cast<Source2BoolFunction>(
            source2_callbacks_first.Symbol("KeelTest_Source2BlockEntered"));
        source2_release_block = reinterpret_cast<Source2VoidFunction>(
            source2_callbacks_first.Symbol("KeelTest_Source2ReleaseBlock"));
        source2_unload_count = reinterpret_cast<Source2CountFunction>(
            source2_callbacks_first.Symbol("KeelTest_Source2UnloadCount"));
        if (!source2_arm_block || !source2_block_entered || !source2_release_block ||
            !source2_unload_count || game_event_add_count() != 1 ||
            !DispatchSource2LevelInit() ||
            g_loop_init_calls != 2 || !dispatch_game_event(&g_game_event_instance) ||
            dispatch_client_connect() || !all_rejection(rejection_message()))
        {
            std::fputs(messages(), stderr);
            return 115;
        }
        dispatch_client_command();
        DispatchSource2LevelShutdown();
        if (client_command_original_calls() != 2 || g_loop_shutdown_calls != 2 ||
            source2_unload_count() != 0)
        {
            std::fputs(messages(), stderr);
            return 116;
        }
        source2_arm_block();
        std::atomic<bool> level_init_completed{};
        bool level_init_result{};
        std::thread level_init_thread([&] {
            level_init_result = DispatchSource2LevelInit();
            level_init_completed.store(true, std::memory_order_release);
        });
        const auto block_deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(5);
        while (!source2_block_entered() && std::chrono::steady_clock::now() < block_deadline)
        {
            std::this_thread::yield();
        }
        if (!source2_block_entered())
        {
            source2_release_block();
            level_init_thread.join();
            return 120;
        }
        std::atomic<bool> unload_completed{};
        bool unload_result{};
        std::thread unload_thread([&] {
            unload_result = g_cvar.Dispatch(
                {"keel", "plugins", "unload", "Source2 Callbacks First"});
            unload_completed.store(true, std::memory_order_release);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        const bool drained_before_release = !unload_completed.load(std::memory_order_acquire) &&
            !level_init_completed.load(std::memory_order_acquire) &&
            source2_unload_count() == 0;
        source2_release_block();
        level_init_thread.join();
        unload_thread.join();
        if (!drained_before_release || !level_init_result || !unload_result ||
            source2_unload_count() != 1 || g_loop_init_calls != 3)
        {
            std::fputs(messages(), stderr);
            return 121;
        }
        DispatchSource2LevelShutdown();
        if (g_loop_shutdown_calls != 3)
        {
            return 122;
        }
        VtableFunction<void (*)(void*, void*)>(&g_loop_factory, 3)(
            &g_loop_factory,
            &g_loop);
        UnregisterSource2LoopMode();
        source2_callbacks_first.Close();
    }
    if (plugin_runtime_service)
    {
        const char* basic_marker =
            "KeelS2 0.9.0 is active. The basic native plugin is responding.";
        if (!g_cvar.Dispatch({"keel_runtime_probe"}) ||
            !g_cvar.Dispatch({"keel_test", "before_pause"}) ||
            Count(messages(), basic_marker) != 1 ||
            !g_cvar.Dispatch({"keel", "plugins", "pause", "2"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "list"}) ||
            !g_cvar.Dispatch({"keel_test", "while_paused"}) ||
            Count(messages(), basic_marker) != 1 ||
            !g_cvar.Dispatch({"keel", "plugins", "resume", "2"}) ||
            !g_cvar.Dispatch({"keel_test", "after_resume"}) ||
            Count(messages(), basic_marker) != 2 ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "2"}) ||
            !g_cvar.Dispatch({"keel_runtime_probe"}))
        {
            std::fputs(messages(), stderr);
            return 94;
        }
    }
    if (plugin_runtime_concurrency)
    {
        keels2::platform::DynamicLibrary runtime_plugin;
        if (!runtime_plugin.Open(
                RuntimePluginPath(plugin_directory, plugin_runtime_service_path),
                loader_error))
        {
            return 97;
        }
        using ArmBlockFunction = void (*)();
        using BlockEnteredFunction = KeelBool (*)();
        using ReleaseBlockFunction = void (*)();
        using CountFunction = std::uint32_t (*)();
        using PauseBasicFunction = KeelBool (*)();
        const auto arm_block = reinterpret_cast<ArmBlockFunction>(
            runtime_plugin.Symbol("KeelTest_PluginRuntimeArmBlock"));
        const auto block_entered = reinterpret_cast<BlockEnteredFunction>(
            runtime_plugin.Symbol("KeelTest_PluginRuntimeBlockEntered"));
        const auto release_block = reinterpret_cast<ReleaseBlockFunction>(
            runtime_plugin.Symbol("KeelTest_PluginRuntimeReleaseBlock"));
        const auto callback_count = reinterpret_cast<CountFunction>(
            runtime_plugin.Symbol("KeelTest_PluginRuntimeCallbackCount"));
        const auto unload_count = reinterpret_cast<CountFunction>(
            runtime_plugin.Symbol("KeelTest_PluginRuntimeUnloadCount"));
        const auto pause_basic = reinterpret_cast<PauseBasicFunction>(
            runtime_plugin.Symbol("KeelTest_PluginRuntimePauseBasic"));
        if (!arm_block || !block_entered || !release_block || !callback_count ||
            !unload_count || !pause_basic)
        {
            return 98;
        }
        arm_block();

        std::atomic<bool> pause_finished{};
        std::atomic<bool> pause_succeeded{};
        std::thread pause_thread([&] {
            pause_succeeded.store(pause_basic() == KEEL_TRUE, std::memory_order_release);
            pause_finished.store(true, std::memory_order_release);
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (block_entered() != KEEL_TRUE && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        if (block_entered() != KEEL_TRUE)
        {
            release_block();
            pause_thread.join();
            return 99;
        }
        if (!g_cvar.Dispatch({"keel", "plugins", "unload", "2"}) ||
            !Contains(messages(), "plugin transition is already active: KeelS2 Basic"))
        {
            release_block();
            pause_thread.join();
            return 103;
        }

        std::atomic<bool> unload_finished{};
        std::atomic<bool> unload_succeeded{};
        std::thread unload_thread([&] {
            unload_succeeded.store(
                g_cvar.Dispatch({"keel", "plugins", "unload", "1"}),
                std::memory_order_release);
            unload_finished.store(true, std::memory_order_release);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const bool unload_waited = !unload_finished.load(std::memory_order_acquire);
        const bool pause_waited = !pause_finished.load(std::memory_order_acquire);
        release_block();
        pause_thread.join();
        unload_thread.join();
        const std::uint32_t callbacks_after_unload = callback_count();
        const char* basic_marker =
            "KeelS2 0.9.0 is active. The basic native plugin is responding.";
        if (!pause_waited || !unload_waited || !pause_succeeded.load(std::memory_order_acquire) ||
            !unload_succeeded.load(std::memory_order_acquire) || unload_count() != 1 ||
            g_cvar.ActiveCount() != 2 || !g_cvar.Dispatch({"keel_test", "while_paused"}) ||
            Count(messages(), basic_marker) != 0 ||
            !g_cvar.Dispatch({"keel", "plugins", "resume", "1"}) ||
            !g_cvar.Dispatch({"keel_test", "after_resume"}) ||
            Count(messages(), basic_marker) != 1 ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "1"}) ||
            callback_count() != callbacks_after_unload)
        {
            std::fputs(messages(), stderr);
            return 100;
        }
        runtime_plugin.Close();
    }
    if (plugin_transition_shutdown_retry)
    {
        keels2::platform::DynamicLibrary runtime_plugin;
        if (!runtime_plugin.Open(
                RuntimePluginPath(plugin_directory, plugin_runtime_service_path),
                loader_error))
        {
            return 104;
        }
        using ArmBlockFunction = void (*)();
        using BlockEnteredFunction = KeelBool (*)();
        using ReleaseBlockFunction = void (*)();
        using CountFunction = std::uint32_t (*)();
        using PauseBasicFunction = KeelBool (*)();
        const auto arm_block = reinterpret_cast<ArmBlockFunction>(
            runtime_plugin.Symbol("KeelTest_PluginRuntimeArmBlock"));
        const auto block_entered = reinterpret_cast<BlockEnteredFunction>(
            runtime_plugin.Symbol("KeelTest_PluginRuntimeBlockEntered"));
        const auto release_block = reinterpret_cast<ReleaseBlockFunction>(
            runtime_plugin.Symbol("KeelTest_PluginRuntimeReleaseBlock"));
        const auto unload_count = reinterpret_cast<CountFunction>(
            runtime_plugin.Symbol("KeelTest_PluginRuntimeUnloadCount"));
        const auto pause_basic = reinterpret_cast<PauseBasicFunction>(
            runtime_plugin.Symbol("KeelTest_PluginRuntimePauseBasic"));
        using EarlyDisconnectFunction = void (*)(void*);
        const auto early_disconnect = VtableFunction<EarlyDisconnectFunction>(config, 1);
        if (!arm_block || !block_entered || !release_block || !unload_count ||
            !pause_basic || !early_disconnect)
        {
            return 105;
        }
        arm_block();

        std::atomic<bool> pause_finished{};
        std::atomic<bool> pause_succeeded{};
        std::thread pause_thread([&] {
            pause_succeeded.store(pause_basic() == KEEL_TRUE, std::memory_order_release);
            pause_finished.store(true, std::memory_order_release);
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (block_entered() != KEEL_TRUE && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        if (block_entered() != KEEL_TRUE ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "2"}))
        {
            release_block();
            pause_thread.join();
            return 106;
        }

        std::atomic<bool> disconnect_finished{};
        std::thread disconnect_thread([&] {
            early_disconnect(config);
            disconnect_finished.store(true, std::memory_order_release);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const bool disconnect_waited = !disconnect_finished.load(std::memory_order_acquire);
        const bool pause_waited = !pause_finished.load(std::memory_order_acquire);
        release_block();
        pause_thread.join();
        disconnect_thread.join();
        if (!pause_waited || !disconnect_waited ||
            !pause_succeeded.load(std::memory_order_acquire) || unload_count() != 1 ||
            g_cvar.ActiveCount() != 0 || g_cvar.unregister_count != expected_registrations)
        {
            std::fputs(messages(), stderr);
            return 107;
        }
        runtime_plugin.Close();
    }
    if (plugin_dependencies)
    {
        if (!g_cvar.Dispatch({"keel", "plugins", "list"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "pause", "Dependency Core"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "Dependency Core"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "pause", "Dependency Middle"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "pause", "Dependency Leaf"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "pause", "Dependency Middle"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "pause", "Dependency Core"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "resume", "Dependency Leaf"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "resume", "Dependency Middle"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "resume", "Dependency Core"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "resume", "Dependency Middle"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "resume", "Dependency Leaf"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "Dependency Core"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "list"}))
        {
            std::fputs(messages(), stderr);
            return 96;
        }
    }
    if (published_services)
    {
        if (!g_cvar.Dispatch({"keel", "plugins", "pause", "Published Service Provider"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "Published Service Provider"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "reload", "Published Service Provider"}) ||
            !g_cvar.Dispatch({"published_release"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "Published Service Provider"}) ||
            !g_cvar.Dispatch({"published_verify_gone"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "Published Service Consumer"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "load", "01_published_service_provider"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "load", "02_published_service_consumer"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "Published Service Consumer"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "Published Service Provider"}) ||
            g_cvar.ActiveCount() != 1)
        {
            std::fputs(messages(), stderr);
            return 143;
        }
        expected_registrations = 5;
    }
    if (reload_retry)
    {
        if (!CopyFile(plugin_source, reload_retry_path) ||
            !g_cvar.Dispatch({"keel", "plugins", "retry", "Failing Test Plugin"}) ||
            !g_cvar.HasActive("keel_test"))
        {
            std::fputs(messages(), stderr);
            return 145;
        }
        for (std::uint32_t cycle{}; cycle < 100; ++cycle)
        {
            if (!g_cvar.Dispatch({"keel", "plugins", "reload", "KeelS2 Basic"}) ||
                !g_cvar.Dispatch({"keel_test", "success_cycle"}))
            {
                std::fputs(messages(), stderr);
                return 146;
            }
        }
        if (!CopyFile(failing_plugin_source, reload_retry_path))
        {
            return 147;
        }
        for (std::uint32_t cycle{}; cycle < 5; ++cycle)
        {
            if (!g_cvar.Dispatch({"keel", "plugins", "reload", "KeelS2 Basic"}) ||
                !g_cvar.Dispatch({"keel_test", "rollback_cycle"}))
            {
                std::fputs(messages(), stderr);
                return 148;
            }
        }
        if (!CopyFile(plugin_source, reload_retry_path) ||
            !g_cvar.Dispatch({"keel", "plugins", "reload", "KeelS2 Basic"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "pause", "KeelS2 Basic"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "reload", "KeelS2 Basic"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "resume", "KeelS2 Basic"}) ||
            !g_cvar.Dispatch({"keel_test", "after_paused_reload"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "KeelS2 Basic"}) ||
            g_cvar.ActiveCount() != 1)
        {
            std::fputs(messages(), stderr);
            return 149;
        }
        expected_registrations = 115;
    }
    if (lifecycle_service)
    {
        keels2::platform::DynamicLibrary lifecycle_plugin;
        if (!lifecycle_plugin.Open(
                RuntimePluginPath(plugin_directory, lifecycle_plugin_path),
                loader_error))
        {
            return 58;
        }
        using LifecycleCallbackCountFunction = std::uint32_t (*)(std::uint32_t);
        const auto lifecycle_callback_count = reinterpret_cast<LifecycleCallbackCountFunction>(
            lifecycle_plugin.Symbol("KeelTest_LifecycleCallbackCount"));
        if (!lifecycle_callback_count)
        {
            return 59;
        }

        dispatch_lifecycle();
        for (std::uint32_t event = 1; event <= 7; ++event)
        {
            if (lifecycle_call_count(event) != 1 || lifecycle_callback_count(event) != 1)
            {
                return 49;
            }
        }
        if (!g_cvar.Dispatch({"keel", "plugins", "pause", "1"}))
        {
            return 50;
        }
        dispatch_lifecycle();
        for (std::uint32_t event = 1; event <= 7; ++event)
        {
            if (lifecycle_call_count(event) != 2 || lifecycle_callback_count(event) != 1)
            {
                return 51;
            }
        }
        if (!g_cvar.Dispatch({"keel", "plugins", "resume", "1"}))
        {
            return 52;
        }
        dispatch_lifecycle();
        for (std::uint32_t event = 1; event <= 7; ++event)
        {
            if (lifecycle_call_count(event) != 3 || lifecycle_callback_count(event) != 2)
            {
                return 53;
            }
        }
        lifecycle_plugin.Close();

        if (!g_cvar.Dispatch({"keel", "plugins", "unload", "1"}) ||
            g_cvar.ActiveCount() != 1 ||
            !g_cvar.Dispatch({"keel", "plugins", "load", "01_lifecycle_service"}))
        {
            std::fputs(messages(), stderr);
            return 54;
        }
        dispatch_lifecycle();
        for (std::uint32_t event = 1; event <= 7; ++event)
        {
            const std::uint32_t count = lifecycle_call_count(event);
            const std::uint32_t expected = event == KEELS2_LIFECYCLE_GAME_FRAME ? 5 : 4;
            if (count != expected)
            {
                std::fprintf(
                    stderr,
                    "lifecycle event %u reached %u calls after reload; expected %u\n",
                    event,
                    count,
                    expected);
                std::fputs(messages(), stderr);
                return 55;
            }
        }

        if (!lifecycle_plugin.Open(
                RuntimePluginPath(plugin_directory, lifecycle_plugin_path),
                loader_error))
        {
            return 58;
        }
        using ArmLifecycleBlockFunction = void (*)();
        using LifecycleBlockEnteredFunction = std::uint32_t (*)();
        using ReleaseLifecycleBlockFunction = void (*)();
        const auto arm_block = reinterpret_cast<ArmLifecycleBlockFunction>(
            lifecycle_plugin.Symbol("KeelTest_LifecycleArmBlock"));
        const auto block_entered = reinterpret_cast<LifecycleBlockEnteredFunction>(
            lifecycle_plugin.Symbol("KeelTest_LifecycleBlockEntered"));
        const auto release_block = reinterpret_cast<ReleaseLifecycleBlockFunction>(
            lifecycle_plugin.Symbol("KeelTest_LifecycleReleaseBlock"));
        if (!arm_block || !block_entered || !release_block)
        {
            return 59;
        }
        arm_block();
        lifecycle_plugin.Close();

        std::thread lifecycle_thread(dispatch_lifecycle);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (block_entered() != 1u && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        if (block_entered() != 1u)
        {
            release_block();
            lifecycle_thread.join();
            return 60;
        }

        std::atomic<bool> unload_finished{};
        std::atomic<bool> unload_succeeded{};
        std::thread unload_thread([&] {
            unload_succeeded.store(
                g_cvar.Dispatch({"keel", "plugins", "unload", "1"}),
                std::memory_order_release);
            unload_finished.store(true, std::memory_order_release);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const bool waited = !unload_finished.load(std::memory_order_acquire);
        release_block();
        lifecycle_thread.join();
        unload_thread.join();
        if (!waited || !unload_succeeded.load(std::memory_order_acquire) ||
            g_cvar.ActiveCount() != 1)
        {
            return 61;
        }
        for (std::uint32_t event = 1; event <= 7; ++event)
        {
            const std::uint32_t count = lifecycle_call_count(event);
            const std::uint32_t expected = event == KEELS2_LIFECYCLE_GAME_FRAME ? 6 : 5;
            if (count != expected)
            {
                std::fprintf(
                    stderr,
                    "lifecycle event %u reached %u calls during unload; expected %u\n",
                    event,
                    count,
                    expected);
                return 62;
            }
        }
        dispatch_lifecycle();
        for (std::uint32_t event = 1; event <= 7; ++event)
        {
            const std::uint32_t count = lifecycle_call_count(event);
            const std::uint32_t expected = event == KEELS2_LIFECYCLE_GAME_FRAME ? 7 : 6;
            if (count != expected)
            {
                std::fprintf(
                    stderr,
                    "lifecycle event %u reached %u calls after unload; expected %u\n",
                    event,
                    count,
                    expected);
                return 53;
            }
        }
    }
    if (authoring_concurrency)
    {
        keels2::platform::DynamicLibrary authoring_plugin;
        if (!authoring_plugin.Open(
                RuntimePluginPath(plugin_directory, authoring_concurrency_plugin_path),
                loader_error))
        {
            return 69;
        }
        using ArmBlockFunction = void (*)();
        using BlockEnteredFunction = KeelBool (*)();
        using ReleaseBlockFunction = void (*)();
        using CountFunction = std::uint32_t (*)();
        const auto arm_block = reinterpret_cast<ArmBlockFunction>(
            authoring_plugin.Symbol("KeelTest_AuthoringArmBlock"));
        const auto block_entered = reinterpret_cast<BlockEnteredFunction>(
            authoring_plugin.Symbol("KeelTest_AuthoringBlockEntered"));
        const auto release_block = reinterpret_cast<ReleaseBlockFunction>(
            authoring_plugin.Symbol("KeelTest_AuthoringReleaseBlock"));
        const auto callback_count = reinterpret_cast<CountFunction>(
            authoring_plugin.Symbol("KeelTest_AuthoringCallbackCount"));
        const auto unload_count = reinterpret_cast<CountFunction>(
            authoring_plugin.Symbol("KeelTest_AuthoringUnloadCount"));
        if (!arm_block || !block_entered || !release_block || !callback_count || !unload_count)
        {
            return 70;
        }

        arm_block();
        std::thread lifecycle_thread(dispatch_lifecycle);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (block_entered() != KEEL_TRUE && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        if (block_entered() != KEEL_TRUE || callback_count() != 1u)
        {
            release_block();
            lifecycle_thread.join();
            return 71;
        }

        std::atomic<bool> unload_started{};
        std::atomic<bool> unload_finished{};
        std::atomic<bool> unload_succeeded{};
        std::thread unload_thread([&] {
            unload_started.store(true, std::memory_order_release);
            unload_succeeded.store(
                g_cvar.Dispatch({"keel", "plugins", "unload", "1"}),
                std::memory_order_release);
            unload_finished.store(true, std::memory_order_release);
        });
        while (!unload_started.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const bool waited = !unload_finished.load(std::memory_order_acquire) &&
            unload_count() == 0u;
        release_block();
        lifecycle_thread.join();
        unload_thread.join();
        if (!waited || !unload_succeeded.load(std::memory_order_acquire) ||
            unload_count() != 1u || callback_count() != 1u || g_cvar.ActiveCount() != 1)
        {
            return 72;
        }

        dispatch_lifecycle();
        if (callback_count() != 1u)
        {
            return 73;
        }
        authoring_plugin.Close();
    }
    if (convar_service)
    {
        std::int32_t initial_value{};
        if (g_cvar.convar_register_count != 4 || g_cvar.convar_unregister_count != 0 ||
            g_cvar.convar_registration_string_count != 1 ||
            g_cvar.convar_invalid_registration_string_count != 0 ||
            g_cvar.convar_registration_reject_count != 0 ||
            EngineInvalidFreeCount() != 0 ||
            g_cvar.convar_queue_count != 0 || g_cvar.convar_filter_count != 4 ||
            g_cvar.convar_change_count != 4 || g_cvar.convar_global_change_count != 4 ||
            g_cvar.last_filter_slot != KEELS2_CONVAR_GLOBAL_SLOT ||
            g_cvar.last_change_slot != 0 ||
            g_cvar.last_global_slot != 0 || g_cvar.last_global_old != "7" ||
            g_cvar.last_global_new != "8" ||
            !g_cvar.ReadInt32("keels2_test_int", initial_value) || initial_value != 8 ||
            !g_cvar.LiveValueTailIntact("keels2_test_int") ||
            g_cvar.ActiveConVarCallbacks("keels2_test_int") != 1 ||
            !g_cvar.HasConVar("keels2_test_bool") ||
            !g_cvar.HasConVar("keels2_test_float") ||
            !g_cvar.HasConVar("keels2_test_string"))
        {
            return 77;
        }

        keels2::platform::DynamicLibrary convar_plugin;
        if (!convar_plugin.Open(
                RuntimePluginPath(plugin_directory, convar_plugin_path),
                loader_error))
        {
            return 78;
        }
        using VoidFunction = void (*)();
        using BoolFunction = KeelBool (*)();
        using CountFunction = std::uint32_t (*)();
        const auto arm_block = reinterpret_cast<VoidFunction>(
            convar_plugin.Symbol("KeelTest_ConVarArmBlock"));
        const auto block_entered = reinterpret_cast<BoolFunction>(
            convar_plugin.Symbol("KeelTest_ConVarBlockEntered"));
        const auto release_block = reinterpret_cast<VoidFunction>(
            convar_plugin.Symbol("KeelTest_ConVarReleaseBlock"));
        const auto callback_count = reinterpret_cast<CountFunction>(
            convar_plugin.Symbol("KeelTest_ConVarCallbackCount"));
        const auto invalid_count = reinterpret_cast<CountFunction>(
            convar_plugin.Symbol("KeelTest_ConVarInvalidCount"));
        const auto busy_count = reinterpret_cast<CountFunction>(
            convar_plugin.Symbol("KeelTest_ConVarBusyCount"));
        if (!arm_block || !block_entered || !release_block || !callback_count ||
            !invalid_count || !busy_count || callback_count() != 0 || invalid_count() != 0)
        {
            return 79;
        }
        std::int32_t deferred_value{};
        if (!g_cvar.SetInt32("keels2_test_int", 9) || callback_count() != 2 ||
            busy_count() != 1 || invalid_count() != 0 || g_cvar.convar_queue_count != 1 ||
            g_cvar.last_queue_slot != 0 ||
            !g_cvar.ReadInt32("keels2_test_int", deferred_value) || deferred_value != 11 ||
            !g_cvar.SetInt32("keels2_test_int", 10) || callback_count() != 3 ||
            invalid_count() != 0)
        {
            return 80;
        }

        arm_block();
        convar_plugin.Close();
        std::thread callback_thread([&] {
            static_cast<void>(g_cvar.SetInt32("keels2_test_int", 8));
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (block_entered() != KEEL_TRUE && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        if (block_entered() != KEEL_TRUE || callback_count() != 4 || invalid_count() != 0)
        {
            release_block();
            callback_thread.join();
            return 81;
        }

        std::atomic<bool> unload_started{};
        std::atomic<bool> unload_finished{};
        std::atomic<bool> unload_dispatched{};
        std::thread unload_thread([&] {
            unload_started.store(true, std::memory_order_release);
            unload_dispatched.store(
                g_cvar.Dispatch({"keel", "plugins", "unload", "1"}),
                std::memory_order_release);
            unload_finished.store(true, std::memory_order_release);
        });
        while (!unload_started.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const bool waited = !unload_finished.load(std::memory_order_acquire);
        release_block();
        callback_thread.join();
        unload_thread.join();
        if (!waited || !unload_dispatched.load(std::memory_order_acquire) ||
            g_cvar.ActiveConVarCallbacks("keels2_test_int") != 0 ||
            g_cvar.convar_unregister_count != 4 || !g_cvar.HasConVar("keels2_test_int") ||
            g_cvar.ActiveCount() != 1)
        {
            return 82;
        }

#if defined(_WIN32)
        if (_putenv_s("KEELS2_TEST_CONVAR_INCOMPATIBLE", "1") != 0)
#else
        if (setenv("KEELS2_TEST_CONVAR_INCOMPATIBLE", "1", 1) != 0)
#endif
        {
            return 83;
        }
        if (!g_cvar.Dispatch({"keel", "plugins", "load", "01_convar_service"}) ||
            g_cvar.convar_register_count != 4)
        {
            return 84;
        }
#if defined(_WIN32)
        if (_putenv_s("KEELS2_TEST_CONVAR_INCOMPATIBLE", "") != 0)
#else
        if (unsetenv("KEELS2_TEST_CONVAR_INCOMPATIBLE") != 0)
#endif
        {
            return 85;
        }

        g_cvar.RejectConVarRegistration("keels2_test_string");
        if (!g_cvar.Dispatch({"keel", "plugins", "load", "01_convar_service"}) ||
            g_cvar.convar_register_count != 7 ||
            g_cvar.convar_unregister_count != 7 ||
            g_cvar.convar_registration_string_count != 2 ||
            g_cvar.convar_invalid_registration_string_count != 0 ||
            g_cvar.convar_registration_reject_count != 1 ||
            g_cvar.ActiveConVarCallbacks("keels2_test_int") != 0 ||
            EngineInvalidFreeCount() != 0)
        {
            return 105;
        }
        if (!g_cvar.Dispatch({"keel", "plugins", "load", "01_convar_service"}) ||
            g_cvar.convar_register_count != 11 ||
            g_cvar.convar_registration_string_count != 3 ||
            g_cvar.convar_invalid_registration_string_count != 0 ||
            g_cvar.convar_registration_reject_count != 1 ||
            g_cvar.ActiveConVarCallbacks("keels2_test_int") != 1)
        {
            return 106;
        }

        if (!convar_plugin.Open(
                RuntimePluginPath(plugin_directory, convar_plugin_path),
                loader_error))
        {
            return 87;
        }
        const auto reloaded_callback_count = reinterpret_cast<CountFunction>(
            convar_plugin.Symbol("KeelTest_ConVarCallbackCount"));
        const auto reloaded_invalid_count = reinterpret_cast<CountFunction>(
            convar_plugin.Symbol("KeelTest_ConVarInvalidCount"));
        const auto reloaded_busy_count = reinterpret_cast<CountFunction>(
            convar_plugin.Symbol("KeelTest_ConVarBusyCount"));
        if (!reloaded_callback_count || !reloaded_invalid_count || !reloaded_busy_count ||
            !g_cvar.SetInt32("keels2_test_int", 9) || reloaded_callback_count() != 2 ||
            reloaded_busy_count() != 1 || reloaded_invalid_count() != 0 ||
            g_cvar.convar_queue_count != 2 || g_cvar.last_queue_slot != 0)
        {
            return 88;
        }
        convar_plugin.Close();
        if (!g_cvar.Dispatch({"keel", "plugins", "unload", "1"}) ||
            g_cvar.ActiveConVarCallbacks("keels2_test_int") != 0 ||
            g_cvar.convar_unregister_count != 11 || g_cvar.ActiveCount() != 1 ||
            g_cvar.convar_registration_string_count != 3 ||
            g_cvar.convar_invalid_registration_string_count != 0 ||
            EngineInvalidFreeCount() != 0)
        {
            return 89;
        }
    }
    if (convar_failed_load)
    {
        if (g_cvar.convar_register_count != 4 || g_cvar.convar_unregister_count != 4 ||
            g_cvar.convar_registration_string_count != 1 ||
            g_cvar.convar_invalid_registration_string_count != 0 ||
            g_cvar.convar_registration_reject_count != 0 ||
            EngineInvalidFreeCount() != 0 ||
            g_cvar.ActiveConVarCallbacks("keels2_test_int") != 0 ||
            !g_cvar.HasConVar("keels2_test_int") || g_cvar.ActiveCount() != 1)
        {
            return 90;
        }
#if defined(_WIN32)
        if (_putenv_s("KEELS2_TEST_CONVAR_FAIL_LOAD", "") != 0)
#else
        if (unsetenv("KEELS2_TEST_CONVAR_FAIL_LOAD") != 0)
#endif
        {
            return 91;
        }
    }
    if (convar_authoring)
    {
        keels2::platform::DynamicLibrary authoring_plugin;
        if (!authoring_plugin.Open(
                RuntimePluginPath(plugin_directory, convar_authoring_plugin_path),
                loader_error))
        {
            return 136;
        }
        using ValueFunction = std::uint32_t (*)(std::uint32_t);
        using SetFunction = int (*)(int);
        using RemoveFunction = std::uint32_t (*)(std::uint32_t);
        using StringFunction = std::uint32_t (*)(const char*);
        auto value = reinterpret_cast<ValueFunction>(
            authoring_plugin.Symbol("KeelTest_ConVarAuthoringValue"));
        auto set = reinterpret_cast<SetFunction>(
            authoring_plugin.Symbol("KeelTest_ConVarAuthoringSet"));
        auto remove_convar = reinterpret_cast<RemoveFunction>(
            authoring_plugin.Symbol("KeelTest_ConVarAuthoringRemove"));
        auto set_string = reinterpret_cast<StringFunction>(
            authoring_plugin.Symbol("KeelTest_ConVarAuthoringSetString"));
        auto string_equals = reinterpret_cast<StringFunction>(
            authoring_plugin.Symbol("KeelTest_ConVarAuthoringStringEquals"));
        std::int32_t integer_value{};
        float floating_value{};
        if (!value || !set || !remove_convar || !set_string || !string_equals ||
            value(0) != 1 || value(1) != 0 ||
            value(2) != 0 ||
            value(3) != 0 || value(4) != 0 || value(5) != 1 || value(6) != 11 ||
            value(7) != 0 || value(8) != 1 || value(9) != 0 || value(10) != 1 ||
            value(11) != 0 || value(12) != 1 || value(13) != 0 ||
            g_cvar.convar_register_count != 5 ||
            g_cvar.convar_unregister_count != 0 ||
            g_cvar.convar_registration_string_count != 1 ||
            g_cvar.convar_invalid_registration_string_count != 0 ||
            g_cvar.convar_registration_reject_count != 0 ||
            g_cvar.convar_queue_count != 0 || g_cvar.convar_filter_count != 2 ||
            g_cvar.convar_change_count != 2 || g_cvar.convar_global_change_count != 2 ||
            g_cvar.ActiveConVarCallbacks("keels2_authoring_int") != 1 ||
            !g_cvar.HasConVar("keels2_authoring_float") ||
            !g_cvar.HasConVar("keels2_authoring_bool") ||
            !g_cvar.HasConVar("keels2_authoring_string") ||
            !g_cvar.HasConVar("keels2_authoring_unbounded") ||
            !g_cvar.HasConVar("mp_limitteams") ||
            !g_cvar.ReadInt32("keels2_authoring_int", integer_value) ||
            integer_value != 11 ||
            !g_cvar.ReadFloat32("keels2_authoring_float", floating_value) ||
            floating_value != 2.5F || !g_cvar.LiveValueTailIntact("keels2_authoring_int") ||
            !g_cvar.LiveValueTailIntact("keels2_authoring_float") ||
            EngineInvalidFreeCount() != 0)
        {
            std::fputs(messages(), stderr);
            return 137;
        }

        if (!g_cvar.Dispatch({"keel", "plugins", "pause", "1"}) ||
            value(5) != 1 || value(6) != 0 || value(8) != 1 || value(10) != 1 ||
            value(12) != 1 || set(10) != -1 ||
            !g_cvar.SetInt32("keels2_authoring_int", 7) || value(2) != 0 ||
            !g_cvar.ReadInt32("keels2_authoring_int", integer_value) ||
            integer_value != 7 ||
            !g_cvar.Dispatch({"keel", "plugins", "resume", "1"}) ||
            value(5) != 1 || value(6) != 7 || value(8) != 1 || value(10) != 1 ||
            value(2) != 0 || value(3) != 0 ||
            g_cvar.ActiveConVarCallbacks("keels2_authoring_int") != 1 ||
            g_cvar.convar_unregister_count != 0)
        {
            std::fputs(messages(), stderr);
            return 138;
        }

        if (!g_cvar.Dispatch({"keel", "plugins", "unload", "1"}) ||
            value(0) != 1 || value(1) != 1 || value(2) != 0 || value(3) != 0 ||
            value(4) != 1 || value(5) != 0 || value(6) != 0 || value(7) != 0 ||
            value(8) != 0 || value(10) != 0 || value(11) != 0 || value(12) != 0 ||
            value(13) != 0 || set(5) != -1 || remove_convar(0) != 0 ||
            remove_convar(1) != 0 ||
            g_cvar.ActiveConVarCallbacks("keels2_authoring_int") != 0 ||
            g_cvar.convar_unregister_count != 5 || g_cvar.ActiveCount() != 1 ||
            !g_cvar.HasConVar("keels2_authoring_int"))
        {
            std::fputs(messages(), stderr);
            return 139;
        }

        authoring_plugin.Close();
        if (!g_cvar.Dispatch({"keel", "plugins", "load", "01_convar_authoring"}) ||
            !authoring_plugin.Open(
                RuntimePluginPath(plugin_directory, convar_authoring_plugin_path),
                loader_error))
        {
            std::fputs(messages(), stderr);
            return 140;
        }
        value = reinterpret_cast<ValueFunction>(
            authoring_plugin.Symbol("KeelTest_ConVarAuthoringValue"));
        set = reinterpret_cast<SetFunction>(
            authoring_plugin.Symbol("KeelTest_ConVarAuthoringSet"));
        remove_convar = reinterpret_cast<RemoveFunction>(
            authoring_plugin.Symbol("KeelTest_ConVarAuthoringRemove"));
        set_string = reinterpret_cast<StringFunction>(
            authoring_plugin.Symbol("KeelTest_ConVarAuthoringSetString"));
        string_equals = reinterpret_cast<StringFunction>(
            authoring_plugin.Symbol("KeelTest_ConVarAuthoringStringEquals"));
        if (!value || !set || !remove_convar || !set_string || !string_equals ||
            value(0) != 1 || value(1) != 0 || value(2) != 0 || value(3) != 0 ||
            value(5) != 1 || value(6) != 11 || value(7) != 0 || value(8) != 1 ||
            value(9) != 0 || value(10) != 1 || value(11) != 0 || value(12) != 1 ||
            value(13) != 0 ||
            g_cvar.convar_register_count != 10 ||
            g_cvar.convar_unregister_count != 5 ||
            g_cvar.convar_registration_string_count != 2 ||
            g_cvar.convar_invalid_registration_string_count != 0 ||
            g_cvar.ActiveConVarCallbacks("keels2_authoring_int") != 1 ||
            !g_cvar.ReadInt32("keels2_authoring_int", integer_value) ||
            integer_value != 11 ||
            !g_cvar.ReadFloat32("keels2_authoring_float", floating_value) ||
            floating_value != 2.5F || set(-100) != 1 || value(6) != 1 ||
            value(2) != 1 || value(13) != 1 || set(100) != 11 || value(6) != 11 ||
            value(2) != 2 || value(13) != 1 ||
            value(3) != 0 || remove_convar(0) != 1 || value(5) != 0 ||
            value(6) != 0 || set(10) != -1 ||
            g_cvar.ActiveConVarCallbacks("keels2_authoring_int") != 0 ||
            g_cvar.convar_unregister_count != 6 || remove_convar(1) != 1 ||
            value(8) != 0 || value(10) != 0 ||
            g_cvar.convar_unregister_count != 6)
        {
            std::fputs(messages(), stderr);
            return 140;
        }

        std::uint32_t worker_read_before{};
        std::uint32_t worker_set{};
        std::uint32_t worker_read_after{};
        std::thread authoring_worker([&] {
            worker_read_before = string_equals("keels2");
            worker_set = set_string("worker");
            worker_read_after = string_equals("worker");
        });
        authoring_worker.join();
        if (worker_read_before != 0 || worker_set != 1 || worker_read_after != 0 ||
            g_cvar.convar_queue_count != 1)
        {
            std::fputs(messages(), stderr);
            return 142;
        }
        g_cvar.DrainQueuedConVarValues();
        if (string_equals("worker") != 1 || value(2) != 2 || value(3) != 0 ||
            EngineInvalidFreeCount() != 0)
        {
            std::fputs(messages(), stderr);
            return 143;
        }

        if (!g_cvar.Dispatch({"keel", "plugins", "unload", "1"}) ||
            value(0) != 1 || value(1) != 1 || value(2) != 2 || value(3) != 0 ||
            value(4) != 1 || value(5) != 0 || value(6) != 0 || value(7) != 0 ||
            value(8) != 0 || value(10) != 0 || value(11) != 0 || value(12) != 0 ||
            value(13) != 1 || set(5) != -1 ||
            g_cvar.ActiveConVarCallbacks("keels2_authoring_int") != 0 ||
            g_cvar.convar_unregister_count != 10 || g_cvar.ActiveCount() != 1 ||
            EngineInvalidFreeCount() != 0)
        {
            std::fputs(messages(), stderr);
            return 141;
        }
        authoring_plugin.Close();
    }
    if (convar_facade)
    {
        std::int32_t integer_value{};
        float floating_value{};
        if (g_cvar.convar_register_count != 2 || g_cvar.convar_unregister_count != 0 ||
            g_cvar.convar_registration_string_count != 0 ||
            g_cvar.convar_invalid_registration_string_count != 0 ||
            g_cvar.convar_registration_reject_count != 0 ||
            EngineInvalidFreeCount() != 0 ||
            g_cvar.ActiveConVarCallbacks("keels2_sample_int") != 1 ||
            g_cvar.ActiveCount() != 2 ||
            !g_cvar.HasConVar("keels2_sample_float") ||
            !g_cvar.HasConVar("mp_limitteams") ||
            game_event_load_count() != 1 || game_event_add_count() != 1 ||
            !game_event_listener_active() || !DispatchSource2LevelInit() ||
            g_loop_init_calls != 1 || !dispatch_game_event(&g_game_event_instance) ||
            !dispatch_client_connect() || rejection_message()[0] != '\0' ||
            client_connect_original_calls() != 1)
        {
            std::fprintf(
                stderr,
                "sample precondition failed: registered=%u unregistered=%u strings=%u "
                "invalid_strings=%u rejects=%u invalid_frees=%u callbacks=%zu active=%zu\n",
                g_cvar.convar_register_count,
                g_cvar.convar_unregister_count,
                g_cvar.convar_registration_string_count,
                g_cvar.convar_invalid_registration_string_count,
                g_cvar.convar_registration_reject_count,
                EngineInvalidFreeCount(),
                g_cvar.ActiveConVarCallbacks("keels2_sample_int"),
                g_cvar.ActiveCount());
            std::fputs(messages(), stderr);
            return 93;
        }

        dispatch_client_command();
        dispatch_lifecycle();
        for (std::uint32_t event = 1; event <= 7; ++event)
        {
            if (lifecycle_call_count(event) != 1)
            {
                std::fputs(messages(), stderr);
                return 93;
            }
        }

        if (client_command_original_calls() != 1 ||
            Count(messages(), "[KeelS2 Source 2 Sample] event=round_start") != 1 ||
            Count(messages(), "[KeelS2 Source 2 Sample] GameFrame") != 1 ||
            !g_cvar.SetInt32("keels2_sample_int", 99) ||
            !g_cvar.Dispatch({"keel_sample", "bump"}) ||
            !g_cvar.ReadInt32("keels2_sample_int", integer_value) || integer_value != 100 ||
            !g_cvar.ReadFloat32("keels2_sample_float", floating_value) ||
            floating_value != 1.5F ||
            !g_cvar.Dispatch({"keel", "plugins", "pause", "1"}) ||
            !g_cvar.SetInt32("keels2_sample_int", 50) ||
            !g_cvar.Dispatch({"keel_sample", "bump"}) ||
            !dispatch_game_event(&g_game_event_instance) ||
            !g_cvar.ReadInt32("keels2_sample_int", integer_value) || integer_value != 50 ||
            !g_cvar.ReadFloat32("keels2_sample_float", floating_value) ||
            floating_value != 1.5F || g_cvar.convar_queue_count != 0 ||
            g_cvar.convar_filter_count != 2 ||
            Count(messages(), "[KeelS2 Source 2 Sample] caller=") != 1 ||
            Count(messages(), "[KeelS2 Source 2 Sample] event=round_start") != 1 ||
            !g_cvar.Dispatch({"keel", "plugins", "resume", "1"}) ||
            !g_cvar.SetInt32("keels2_sample_int", 100))
        {
            std::fputs(messages(), stderr);
            return 93;
        }

        const std::uint32_t changes_before_rejection = g_cvar.convar_change_count;
        const std::uint32_t globals_before_rejection = g_cvar.convar_global_change_count;
        g_cvar.RejectNextFilter();
        if (!g_cvar.Dispatch({"keel_sample", "bump"}) ||
            g_cvar.convar_queue_count != 0 || g_cvar.convar_filter_count != 4 ||
            g_cvar.convar_change_count != changes_before_rejection + 1 ||
            g_cvar.convar_global_change_count != globals_before_rejection + 1 ||
            !g_cvar.ReadInt32("keels2_sample_int", integer_value) || integer_value != 100 ||
            !g_cvar.ReadFloat32("keels2_sample_float", floating_value) ||
            floating_value != 1.75F ||
            !g_cvar.Dispatch({"keel_sample", "bump"}) ||
            g_cvar.convar_queue_count != 0 || g_cvar.convar_filter_count != 6 ||
            !g_cvar.ReadInt32("keels2_sample_int", integer_value) || integer_value != 0 ||
            !g_cvar.ReadFloat32("keels2_sample_float", floating_value) ||
            floating_value != 2.0F)
        {
            std::fputs(messages(), stderr);
            return 93;
        }

        for (int command = 0; command < 9; ++command)
        {
            if (!g_cvar.Dispatch({"keel_sample", "bump"}))
            {
                std::fputs(messages(), stderr);
                return 93;
            }
        }

        if (g_cvar.convar_queue_count != 0 || g_cvar.convar_filter_count != 24 ||
            !g_cvar.ReadInt32("keels2_sample_int", integer_value) || integer_value != 9 ||
            !g_cvar.ReadFloat32("keels2_sample_float", floating_value) ||
            floating_value != 4.0F ||
            !g_cvar.LiveValueTailIntact("keels2_sample_int") ||
            !g_cvar.LiveValueTailIntact("keels2_sample_float") ||
            Count(messages(), "[KeelS2 Source 2 Sample] caller=") != 12 ||
            Contains(messages(), "old=100 new=50") ||
            !Contains(messages(), "old=100 new=0") ||
            !Contains(messages(), "int=9 float=4 mp_limitteams=2"))
        {
            std::fputs(messages(), stderr);
            return 93;
        }

        DispatchSource2LevelShutdown();
        const std::size_t event_logs =
            Count(messages(), "[KeelS2 Source 2 Sample] event=round_start");
        const std::size_t frame_logs =
            Count(messages(), "[KeelS2 Source 2 Sample] GameFrame");
        const std::size_t connected_logs =
            Count(messages(), "[KeelS2 Source 2 Sample] ClientConnected");
        if (g_loop_shutdown_calls != 1 ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "1"}) ||
            g_cvar.ActiveConVarCallbacks("keels2_sample_int") != 0 ||
            g_cvar.convar_unregister_count != 2 || g_cvar.ActiveCount() != 1 ||
            !g_cvar.HasConVar("keels2_sample_int") ||
            !g_cvar.HasConVar("keels2_sample_float") ||
            g_cvar.Dispatch({"keel_sample"}) ||
            !dispatch_game_event(&g_game_event_instance))
        {
            std::fputs(messages(), stderr);
            return 93;
        }

        dispatch_lifecycle();
        if (Count(messages(), "[KeelS2 Source 2 Sample] event=round_start") != event_logs ||
            Count(messages(), "[KeelS2 Source 2 Sample] GameFrame") != frame_logs ||
            Count(messages(), "[KeelS2 Source 2 Sample] ClientConnected") != connected_logs ||
            !g_cvar.Dispatch({"keel", "plugins", "load", "01_convar_facade"}) ||
            g_cvar.convar_register_count != 4 ||
            g_cvar.convar_registration_string_count != 0 ||
            g_cvar.convar_invalid_registration_string_count != 0 ||
            EngineInvalidFreeCount() != 0 ||
            g_cvar.ActiveConVarCallbacks("keels2_sample_int") != 1 ||
            g_cvar.ActiveCount() != 2 ||
            game_event_add_count() != 1 || !game_event_listener_active() ||
            !g_cvar.ReadInt32("keels2_sample_int", integer_value) || integer_value != 9 ||
            !g_cvar.ReadFloat32("keels2_sample_float", floating_value) ||
            floating_value != 4.0F || !DispatchSource2LevelInit() ||
            g_loop_init_calls != 2 || !dispatch_game_event(&g_game_event_instance) ||
            !dispatch_client_connect() || rejection_message()[0] != '\0' ||
            client_connect_original_calls() != 2)
        {
            std::fputs(messages(), stderr);
            return 93;
        }

        dispatch_client_command();
        dispatch_lifecycle();
        if (client_command_original_calls() != 2 ||
            !g_cvar.Dispatch({"keel_sample"}) ||
            !g_cvar.ReadInt32("keels2_sample_int", integer_value) || integer_value != 9 ||
            !g_cvar.ReadFloat32("keels2_sample_float", floating_value) ||
            floating_value != 4.0F || g_cvar.convar_filter_count != 24 ||
            Count(messages(), "[KeelS2 Source 2 Sample] caller=") != 13 ||
            !g_cvar.Dispatch({"keel_sample", "invalid"}) ||
            !Contains(messages(), "usage: keel_sample [bump]") ||
            !g_cvar.Dispatch({"keel_sample", "bump"}) ||
            !g_cvar.ReadInt32("keels2_sample_int", integer_value) || integer_value != 10 ||
            !g_cvar.ReadFloat32("keels2_sample_float", floating_value) ||
            floating_value != 4.0F || g_cvar.convar_queue_count != 0 ||
            g_cvar.convar_filter_count != 26 ||
            Count(messages(), "[KeelS2 Source 2 Sample] caller=") != 14)
        {
            std::fputs(messages(), stderr);
            return 93;
        }

        DispatchSource2LevelShutdown();
        if (g_loop_shutdown_calls != 2 ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "1"}) ||
            g_cvar.ActiveConVarCallbacks("keels2_sample_int") != 0 ||
            g_cvar.convar_unregister_count != 4 || g_cvar.ActiveCount() != 1)
        {
            std::fputs(messages(), stderr);
            return 93;
        }
        expected_registrations = 3;
    }
    if (lifecycle_failed_load)
    {
        dispatch_lifecycle();
        for (std::uint32_t event = 1; event <= 7; ++event)
        {
            if (lifecycle_call_count(event) != 1)
            {
                return 56;
            }
        }
    }
    if (lifecycle_pre_init)
    {
#if defined(_WIN32)
        const int clear_result = _putenv_s("KEELS2_TEST_GAME_FRAME_DURING_INIT", "");
#else
        const int clear_result = unsetenv("KEELS2_TEST_GAME_FRAME_DURING_INIT");
#endif
        if (clear_result != 0)
        {
            return 67;
        }
    }
    if (keelhook)
    {
        if (!g_cvar.Dispatch({"keel", "plugins", "unload", "1"}) ||
            !g_cvar.HasActive("kh_run") ||
            !g_cvar.Dispatch({"kh_run"}))
        {
            std::fputs(messages(), stderr);
            return 37;
        }

        keels2::platform::DynamicLibrary keelhook_target;
        const auto keelhook_target_path =
            plugin_directory / ".runtime" / "1" /
            (std::string("01_keelhook_target") + plugin_extension);
        if (!keelhook_target.Open(keelhook_target_path, loader_error))
        {
            return 101;
        }
        using KeelHookTargetFunction = std::int32_t (*)(std::int32_t, std::int32_t);
        using KeelHookPauseTargetFunction = std::int32_t (*)(std::int32_t);
        using KeelHookLastArgumentFunction = std::int32_t (*)();
        using KeelHookPauseCountFunction = std::uint32_t (*)();
        using KeelHookPauseCleanupFunction = KeelBool (*)();
        const auto target = reinterpret_cast<KeelHookTargetFunction>(
            keelhook_target.Symbol("KeelHookFixtureTarget"));
        const auto pause_target = reinterpret_cast<KeelHookPauseTargetFunction>(
            keelhook_target.Symbol("KeelHookPauseFixtureTarget"));
        const auto last_left = reinterpret_cast<KeelHookLastArgumentFunction>(
            keelhook_target.Symbol("KeelTest_KeelHookLastLeft"));
        const auto last_right = reinterpret_cast<KeelHookLastArgumentFunction>(
            keelhook_target.Symbol("KeelTest_KeelHookLastRight"));
        const auto pause_calls = reinterpret_cast<KeelHookPauseCountFunction>(
            keelhook_target.Symbol("KeelTest_KeelHookPauseCalls"));
        const auto pause_cleanup = reinterpret_cast<KeelHookPauseCleanupFunction>(
            keelhook_target.Symbol("KeelTest_KeelHookPauseCleanup"));
        if (!target || !pause_target || !last_left || !last_right || !pause_calls ||
            !pause_cleanup || pause_calls() != 1 ||
            !g_cvar.Dispatch({"keel", "plugins", "pause", "1"}) ||
            target(2, 3) != 900 || last_left() != 2 || last_right() != 5 ||
            pause_target(5) != 15 || pause_calls() != 1 ||
            pause_cleanup() != KEEL_TRUE || pause_target(5) != 15 || pause_calls() != 1 ||
            !g_cvar.Dispatch({"keel", "plugins", "resume", "1"}) ||
            target(2, 3) != 900 || last_left() != 3 || last_right() != 5 ||
            pause_target(5) != 15 || pause_calls() != 1)
        {
            std::fputs(messages(), stderr);
            return 102;
        }
        keelhook_target.Close();

        if (!g_cvar.Dispatch({"keel", "plugins", "unload", "2"}) ||
            !g_cvar.Dispatch({"kh_after_peer"}) ||
            !g_cvar.Dispatch({"kh_restore_retry"}) ||
            !g_cvar.Dispatch({"kh_prepare_unload"}) ||
            !g_cvar.Dispatch({"keel", "plugins", "unload", "1"}) ||
            g_cvar.ActiveCount() != 1 || g_cvar.unregister_count != 5)
        {
            std::fputs(messages(), stderr);
            return 37;
        }
    }
    if (keelhook_shutdown_retry && !g_cvar.Dispatch({"kh_prepare_shutdown_retry"}))
    {
        return 38;
    }

    const auto repeated_init = VtableFunction<InitFunction>(server, 3);
    if (!repeated_init || repeated_init(server) != 1 || g_cvar.register_count != expected_registrations)
    {
        return 30;
    }

    using DisconnectFunction = void (*)(void*);
    const auto disconnect = VtableFunction<DisconnectFunction>(config, 1);
    if (!disconnect)
    {
        return 31;
    }
    disconnect(config);
    const auto repeated_disconnect = VtableFunction<DisconnectFunction>(config, 1);
    repeated_disconnect(config);

    if ((source2_callbacks || convar_facade) &&
        (game_event_remove_count() != 1 || game_event_listener_active()))
    {
        return 117;
    }

    if (lifecycle_service || lifecycle_failed_load)
    {
        dispatch_lifecycle();
        for (std::uint32_t event = 1; event <= 7; ++event)
        {
            const std::uint32_t expected = lifecycle_service
                ? (event == KEELS2_LIFECYCLE_GAME_FRAME ? 8 : 7)
                : 2;
            const std::uint32_t count = lifecycle_call_count(event);
            if (count != expected)
            {
                std::fprintf(
                    stderr,
                    "lifecycle event %u reached %u calls after shutdown; expected %u\n",
                    event,
                    count,
                    expected);
                return 54;
            }
        }
    }

    if (g_cvar.ActiveCount() != 0 || g_cvar.unregister_count != expected_registrations)
    {
        return 32;
    }

    if (reverse_unload)
    {
        std::ifstream trace(shutdown_trace, std::ios::binary);
        std::ostringstream content;
        if (trace)
        {
            content << trace.rdbuf();
        }
        const std::string expected =
            "bootstrap disconnect entered\n"
            "bootstrap disconnect lock acquired\n"
            "bootstrap disconnect patch restoration begin\n"
            "bootstrap disconnect patch restoration complete\n"
            "bootstrap host stop begin\n"
            "bootstrap StopHost entered\n"
            "bootstrap host stop export call begin\n"
            "host stop begin\n"
            "host resource release begin\n"
            "plugin service shutdown begin\n"
            "plugin service shutdown complete\n"
            "ConVar service shutdown begin\n"
            "ConVar service shutdown complete\n"
            "schema and entity service shutdown begin\n"
            "schema and entity service shutdown complete\n"
            "lifecycle service shutdown begin\n"
            "lifecycle service shutdown complete\n"
            "Source 2 callback service shutdown begin\n"
            "Source 2 callback service shutdown complete\n"
            "keelhook shutdown begin\n"
            "keelhook restoration completed\n"
            "adapter command release begin\n"
            "adapter command release complete\n"
            "host dispatch drain complete\n"
            "host command records cleared\n"
            "plugin unload loop begin\n"
            "plugin unload callback completed: Lifecycle Second\n"
            "plugin module released: Lifecycle Second\n"
            "plugin unload callback completed: Lifecycle First\n"
            "plugin module released: Lifecycle First\n"
            "plugin unload loop complete\n"
            "plugin load order cleared\n"
            "plugin records cleared\n"
            "published service registry released\n"
            "plugin service released\n"
            "Source 2 callback service released\n"
            "ConVar service released\n"
            "schema and entity service released\n"
            "lifecycle service released\n"
            "keelhook service released\n"
            "game adapter stop begin\n"
            "cs2 adapter stop begin\n"
            "cs2 active command release begin\n"
            "cs2 active command release complete\n"
            "cs2 active ConVar release begin\n"
            "cs2 active ConVar release complete\n"
            "cs2 retired command release begin\n"
            "cs2 retired command release complete\n"
            "cs2 retired ConVar release begin\n"
            "cs2 retired ConVar release complete\n"
            "cs2 interface invalidation begin\n"
            "cs2 interface invalidation complete\n"
            "cs2 lifecycle state reset begin\n"
            "cs2 lifecycle state reset complete\n"
            "cs2 adapter stop complete\n"
            "game adapter stop complete\n"
            "game adapter released\n"
            "host APIs cleared\n"
            "host state cleared\n"
            "host resource release complete\n"
            "host stopped\n"
            "bootstrap host stop export reported complete\n"
            "bootstrap host library close begin\n"
            "bootstrap host library close complete\n"
            "bootstrap StopHost complete\n"
            "bootstrap host stop returned\n"
            "bootstrap lifecycle marked complete\n"
            "bootstrap genuine disconnect begin\n"
            "bootstrap genuine disconnect complete\n"
            "bootstrap disconnect complete\n";
        const std::string actual = content.str();
#if defined(_WIN32)
        const int clear_result = _putenv_s("KEELS2_SHUTDOWN_TRACE_FILE", "");
#else
        const int clear_result = unsetenv("KEELS2_SHUTDOWN_TRACE_FILE");
#endif
        if (clear_result != 0)
        {
            return 40;
        }
        if (!trace || actual != expected)
        {
            std::fputs("shutdown trace mismatch\n", stderr);
            std::fputs(actual.c_str(), stderr);
            return 41;
        }
    }
    if (lifecycle_failed_load)
    {
#if defined(_WIN32)
        const int clear_result = _putenv_s("KEELS2_TEST_LIFECYCLE_FAIL_LOAD", "");
#else
        const int clear_result = unsetenv("KEELS2_TEST_LIFECYCLE_FAIL_LOAD");
#endif
        if (clear_result != 0)
        {
            return 57;
        }
    }

    const char* output = messages();
    if (!output)
    {
        return 33;
    }
    if (!ValidateMessages(scenario, output))
    {
        std::fputs(output, stderr);
        return 33;
    }
    if (keelhook)
    {
        const char* benchmark = std::strstr(output, "dispatch benchmark ns/call:");
        const char* end = benchmark ? std::strchr(benchmark, '\n') : nullptr;
        if (!benchmark || !end)
        {
            return 108;
        }
        std::fwrite(
            benchmark,
            1,
            static_cast<std::size_t>(end - benchmark),
            stdout);
        std::fputc('\n', stdout);
    }
    if (convar_service || convar_failed_load || convar_facade || convar_authoring)
    {
        g_cvar.Reset();
        if (EngineOutstandingAllocationCount() != 0 ||
            EngineAllocationCount() != EngineFreeCount() ||
            EngineInvalidFreeCount() != 0)
        {
            return 107;
        }
    }
    return 0;
}
