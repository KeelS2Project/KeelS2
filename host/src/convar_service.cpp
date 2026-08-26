#include "convar_service.h"

#include "host.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cctype>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace keels2::host
{

namespace
{

constexpr std::uint64_t kPublicFlags =
    KEELS2_CVAR_FLAG_PROTECTED |
    KEELS2_CVAR_FLAG_SINGLE_PLAYER_ONLY |
    KEELS2_CVAR_FLAG_ARCHIVE |
    KEELS2_CVAR_FLAG_NOTIFY |
    KEELS2_CVAR_FLAG_UNLOGGED |
    KEELS2_CVAR_FLAG_REPLICATED |
    KEELS2_CVAR_FLAG_CHEAT |
    KEELS2_CVAR_FLAG_RELEASE |
    KEELS2_CVAR_FLAG_NOT_CONNECTED |
    KEELS2_CVAR_FLAG_SERVER_CANNOT_QUERY;

struct StoredValue
{
    KeelBool boolean_value{};
    std::int32_t int32_value{};
    float float32_value{};
    std::string string_value;
};

StoredValue StoreValue(const KeelConVarValue& value)
{
    StoredValue stored;
    switch (value.type)
    {
        case KEELS2_CONVAR_BOOL:
            stored.boolean_value = value.value.boolean_value;
            break;
        case KEELS2_CONVAR_INT32:
            stored.int32_value = value.value.int32_value;
            break;
        case KEELS2_CONVAR_FLOAT32:
            stored.float32_value = value.value.float32_value;
            break;
        case KEELS2_CONVAR_STRING:
            stored.string_value = value.value.string_value ? value.value.string_value : "";
            break;
        default:
            break;
    }
    return stored;
}

bool EqualValue(
    const StoredValue& stored,
    const KeelConVarValue& value,
    KeelConVarType type) noexcept
{
    switch (type)
    {
        case KEELS2_CONVAR_BOOL:
            return stored.boolean_value == value.value.boolean_value;
        case KEELS2_CONVAR_INT32:
            return stored.int32_value == value.value.int32_value;
        case KEELS2_CONVAR_FLOAT32:
            return std::bit_cast<std::uint32_t>(stored.float32_value) ==
                std::bit_cast<std::uint32_t>(value.value.float32_value);
        case KEELS2_CONVAR_STRING:
            return value.value.string_value && stored.string_value == value.value.string_value;
        default:
            return false;
    }
}

}

struct ConVarService::Definition
{
    explicit Definition(const KeelConVarSpec& spec)
        : name(spec.name),
          description(spec.description ? spec.description : ""),
          flags(spec.flags | KEELS2_CVAR_FLAG_RELEASE),
          type(spec.type),
          default_value(StoreValue(spec.default_value)),
          has_minimum(spec.has_minimum == KEEL_TRUE),
          minimum_value(has_minimum ? StoreValue(spec.minimum_value) : StoredValue{}),
          has_maximum(spec.has_maximum == KEEL_TRUE),
          maximum_value(has_maximum ? StoreValue(spec.maximum_value) : StoredValue{})
    {
    }

    std::string name;
    std::string description;
    std::uint64_t flags{};
    KeelConVarType type{};
    StoredValue default_value;
    bool has_minimum{};
    StoredValue minimum_value;
    bool has_maximum{};
    StoredValue maximum_value;
    KeelConVarHandle active{};
};

struct ConVarService::Record
{
    ConVarService* service{};
    KeelConVarHandle handle{};
    KeelPluginHandle owner{};
    GameConVarHandle game_handle{};
    std::string name;
    std::string definition_key;
    KeelConVarChangeCallback callback{};
    KeelSource2ConVarChangeCallback native_callback{};
    void* user_data{};
    bool created{};
    std::atomic<bool> enabled{};
    std::atomic<std::uint32_t> active{};
    std::atomic<std::uint32_t> release_state{};
};

std::atomic<ConVarService*> ConVarService::active_{};
thread_local std::array<const ConVarService::Record*, 64> ConVarService::callback_stack_{};
thread_local std::size_t ConVarService::callback_depth_{};

ConVarService::ConVarService(Host& host, GameAdapter& adapter)
    : host_(host), adapter_(adapter)
{
    ConVarService* expected{};
    if (!active_.compare_exchange_strong(expected, this, std::memory_order_acq_rel))
    {
        throw std::runtime_error("ConVar service already exists");
    }
    api_ = {
        sizeof(KeelConVarApi),
        KEELS2_CONVAR_API_VERSION,
        &CreateEntry,
        &FindEntry,
        &ReleaseEntry,
        &ReadEntry,
        &QueueSetEntry,
        &DescribeEntry
    };
}

ConVarService::~ConVarService()
{
    active_.store(nullptr, std::memory_order_release);
    static_cast<void>(Shutdown());
}

const KeelConVarApi& ConVarService::Api() const noexcept
{
    return api_;
}

void ConVarService::Activate(KeelPluginHandle plugin)
{
    std::scoped_lock lock(registry_mutex_);
    if (shutting_down_)
    {
        return;
    }
    for (const auto& [handle, record] : records_)
    {
        static_cast<void>(handle);
        if (record->owner == plugin && record->release_state.load(std::memory_order_acquire) == 0)
        {
            record->enabled.store(true, std::memory_order_release);
        }
    }
}

KeelResult ConVarService::Deactivate(KeelPluginHandle plugin)
{
    std::vector<std::shared_ptr<Record>> owned;
    {
        std::scoped_lock lock(registry_mutex_);
        if (IsCurrentOwner(plugin))
        {
            return KEEL_RESULT_BUSY;
        }
        for (const auto& [handle, record] : records_)
        {
            static_cast<void>(handle);
            if (record->owner == plugin)
            {
                record->enabled.store(false, std::memory_order_release);
                owned.push_back(record);
            }
        }
    }
    for (const auto& record : owned)
    {
        WaitForZero(record->active);
    }
    return KEEL_RESULT_OK;
}

KeelResult ConVarService::ReleasePlugin(KeelPluginHandle plugin)
{
    const KeelResult deactivated = Deactivate(plugin);
    if (deactivated != KEEL_RESULT_OK)
    {
        return deactivated;
    }
    std::vector<std::shared_ptr<Record>> owned;
    {
        std::scoped_lock lock(registry_mutex_);
        for (const auto& [handle, record] : records_)
        {
            static_cast<void>(handle);
            if (record->owner == plugin)
            {
                owned.push_back(record);
            }
        }
    }
    for (const auto& record : owned)
    {
        const KeelResult released = ReleaseRecord(record);
        if (released != KEEL_RESULT_OK && released != KEEL_RESULT_NOT_FOUND)
        {
            return released;
        }
    }
    return KEEL_RESULT_OK;
}

bool ConVarService::Shutdown()
{
    std::vector<std::shared_ptr<Record>> records;
    {
        std::scoped_lock lock(registry_mutex_);
        if (shutdown_complete_)
        {
            return true;
        }
        shutting_down_ = true;
        records.reserve(records_.size());
        bool current{};
        for (const auto& [handle, record] : records_)
        {
            static_cast<void>(handle);
            record->enabled.store(false, std::memory_order_release);
            records.push_back(record);
            current = current || IsCurrentRecord(record.get());
        }
        if (current)
        {
            return false;
        }
    }
    for (const auto& record : records)
    {
        WaitForZero(record->active);
        if (ReleaseRecord(record) != KEEL_RESULT_OK)
        {
            return false;
        }
    }
    std::scoped_lock lock(registry_mutex_);
    records_.clear();
    definitions_.clear();
    shutdown_complete_ = true;
    return true;
}

KeelResult ConVarService::CreateEntry(
    KeelPluginHandle plugin,
    const KeelConVarSpec* spec,
    KeelConVarHandle* convar)
{
    ConVarService* service = active_.load(std::memory_order_acquire);
    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return service->Create(plugin, spec, convar);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while creating a ConVar");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult ConVarService::FindEntry(
    KeelPluginHandle plugin,
    const char* name,
    KeelConVarType expected_type,
    KeelConVarHandle* convar)
{
    ConVarService* service = active_.load(std::memory_order_acquire);
    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return service->Find(plugin, name, expected_type, convar);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while finding a ConVar");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult ConVarService::ReleaseEntry(KeelPluginHandle plugin, KeelConVarHandle convar)
{
    ConVarService* service = active_.load(std::memory_order_acquire);
    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return service->Release(plugin, convar);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while releasing a ConVar");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult ConVarService::ReadEntry(
    KeelPluginHandle plugin,
    KeelConVarHandle convar,
    std::int32_t slot,
    KeelConVarValue* value)
{
    ConVarService* service = active_.load(std::memory_order_acquire);
    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return service->Read(plugin, convar, slot, value);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while reading a ConVar");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult ConVarService::QueueSetEntry(
    KeelPluginHandle plugin,
    KeelConVarHandle convar,
    std::int32_t slot,
    const KeelConVarValue* value)
{
    ConVarService* service = active_.load(std::memory_order_acquire);
    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return service->QueueSet(plugin, convar, slot, value);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while setting a ConVar value");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult ConVarService::DescribeEntry(
    KeelPluginHandle plugin,
    KeelConVarHandle convar,
    KeelConVarInfo* info)
{
    ConVarService* service = active_.load(std::memory_order_acquire);
    if (!service)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return service->Describe(plugin, convar, info);
    }
    catch (...)
    {
        service->host_.Write(KEEL_LOG_ERROR, "exception while describing a ConVar");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

void ConVarService::ChangeEntry(
    std::int32_t slot,
    const KeelConVarValue& new_value,
    const KeelConVarValue& old_value,
    void* user_data)
{
    auto* record = static_cast<Record*>(user_data);
    ConVarService* service = active_.load(std::memory_order_acquire);
    if (record && service && record->service == service)
    {
        service->Dispatch(*record, slot, new_value, old_value);
    }
}

void ConVarService::NativeChangeEntry(
    void* convar,
    std::int32_t slot,
    const void* new_value,
    const void* old_value,
    void* user_data)
{
    auto* record = static_cast<Record*>(user_data);
    ConVarService* service = active_.load(std::memory_order_acquire);
    if (record && service && record->service == service)
    {
        service->DispatchNative(*record, convar, slot, new_value, old_value);
    }
}

KeelResult ConVarService::Create(
    KeelPluginHandle plugin,
    const KeelConVarSpec* spec,
    KeelConVarHandle* output)
{
    return CreateImpl(
        plugin,
        spec,
        spec ? spec->callback : nullptr,
        nullptr,
        spec ? spec->user_data : nullptr,
        output,
        nullptr);
}

KeelResult ConVarService::CreateNative(
    KeelPluginHandle plugin,
    const KeelConVarSpec* spec,
    KeelSource2ConVarChangeCallback callback,
    void* user_data,
    KeelConVarHandle* output,
    void** native_convar)
{
    if (native_convar)
    {
        *native_convar = nullptr;
    }
    if (!spec || spec->callback || spec->user_data)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    return CreateImpl(
        plugin,
        spec,
        nullptr,
        callback,
        user_data,
        output,
        native_convar);
}

KeelResult ConVarService::CreateImpl(
    KeelPluginHandle plugin,
    const KeelConVarSpec* spec,
    KeelConVarChangeCallback callback,
    KeelSource2ConVarChangeCallback native_callback,
    void* user_data,
    KeelConVarHandle* output,
    void** native_convar)
{
    if (!spec || !output)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *output = 0;
    if (!ValidDefinition(*spec))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }

    std::scoped_lock host_lock(host_.state_mutex_);
    PluginRecord* owner = host_.PluginByHandle(plugin);
    if (!host_.accepting_resources_ || !owner || !owner->accepting_resources)
    {
        return KEEL_RESULT_NOT_READY;
    }

    std::scoped_lock registry_lock(registry_mutex_);
    if (shutting_down_)
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (next_convar_ == 0)
    {
        host_.Write(KEEL_LOG_ERROR, "ConVar handle space is exhausted");
        return KEEL_RESULT_ENGINE_FAILURE;
    }

    const std::string key = NormalizeName(spec->name);
    auto definition = definitions_.find(key);
    if (definition != definitions_.end())
    {
        if (definition->second.active != 0)
        {
            return KEEL_RESULT_ALREADY_EXISTS;
        }
        if (!EqualDefinition(definition->second, *spec))
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
    }
    else
    {
        GameConVarHandle existing{};
        std::string error;
        const KeelResult found = adapter_.FindConVar(
            spec->name,
            spec->type,
            existing,
            nullptr,
            error);
        if (found == KEEL_RESULT_OK)
        {
            adapter_.ReleaseConVar(existing);
            return KEEL_RESULT_ALREADY_EXISTS;
        }
        if (found != KEEL_RESULT_NOT_FOUND)
        {
            if (!error.empty())
            {
                host_.Write(KEEL_LOG_ERROR, error);
            }
            return found;
        }
    }

    auto record = std::make_shared<Record>();
    record->service = this;
    record->handle = next_convar_++;
    record->owner = plugin;
    record->name = spec->name;
    record->definition_key = key;
    record->callback = callback;
    record->native_callback = native_callback;
    record->user_data = user_data;
    record->created = true;
    const KeelConVarHandle handle = record->handle;
    bool inserted_definition{};
    if (definition == definitions_.end())
    {
        const auto inserted = definitions_.emplace(key, Definition(*spec));
        definition = inserted.first;
        inserted_definition = true;
    }

    std::string error;
    const KeelResult created = adapter_.CreateConVar(
        *spec,
        callback ? &ChangeEntry : nullptr,
        native_callback ? &NativeChangeEntry : nullptr,
        record.get(),
        record->game_handle,
        native_convar,
        error);
    if (created != KEEL_RESULT_OK)
    {
        if (inserted_definition)
        {
            definitions_.erase(definition);
        }
        if (!error.empty())
        {
            host_.Write(KEEL_LOG_ERROR, error);
        }
        return created;
    }

    try
    {
        records_.emplace(handle, record);
    }
    catch (...)
    {
        adapter_.ReleaseConVar(record->game_handle);
        if (inserted_definition)
        {
            definitions_.erase(definition);
        }
        throw;
    }

    definition->second.active = handle;
    record->enabled.store(
        owner->state == PluginState::loaded && !owner->loading,
        std::memory_order_release);
    *output = handle;
    return KEEL_RESULT_OK;
}

KeelResult ConVarService::Find(
    KeelPluginHandle plugin,
    const char* name,
    KeelConVarType expected_type,
    KeelConVarHandle* output)
{
    return FindImpl(plugin, name, expected_type, output, nullptr);
}

KeelResult ConVarService::FindNative(
    KeelPluginHandle plugin,
    const char* name,
    KeelConVarType expected_type,
    KeelConVarHandle* output,
    void** native_convar)
{
    if (native_convar)
    {
        *native_convar = nullptr;
    }
    return FindImpl(plugin, name, expected_type, output, native_convar);
}

KeelResult ConVarService::FindImpl(
    KeelPluginHandle plugin,
    const char* name,
    KeelConVarType expected_type,
    KeelConVarHandle* output,
    void** native_convar)
{
    if (!output)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *output = 0;
    if (!ValidLookupName(name) || !ValidType(expected_type))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }

    std::scoped_lock host_lock(host_.state_mutex_);
    PluginRecord* owner = host_.PluginByHandle(plugin);
    if (!host_.accepting_resources_ || !owner || !owner->accepting_resources)
    {
        return KEEL_RESULT_NOT_READY;
    }

    std::scoped_lock registry_lock(registry_mutex_);
    if (shutting_down_ || next_convar_ == 0)
    {
        return KEEL_RESULT_NOT_READY;
    }
    auto record = std::make_shared<Record>();
    record->service = this;
    record->handle = next_convar_++;
    record->owner = plugin;
    record->name = name;
    const KeelConVarHandle handle = record->handle;

    std::string error;
    const KeelResult found = adapter_.FindConVar(
        name,
        expected_type,
        record->game_handle,
        native_convar,
        error);
    if (found != KEEL_RESULT_OK)
    {
        if (!error.empty() && found != KEEL_RESULT_NOT_FOUND)
        {
            host_.Write(KEEL_LOG_ERROR, error);
        }
        return found;
    }
    try
    {
        records_.emplace(handle, record);
    }
    catch (...)
    {
        adapter_.ReleaseConVar(record->game_handle);
        throw;
    }
    record->enabled.store(
        owner->state == PluginState::loaded && !owner->loading,
        std::memory_order_release);
    *output = handle;
    return KEEL_RESULT_OK;
}

KeelResult ConVarService::Release(KeelPluginHandle plugin, KeelConVarHandle convar)
{
    std::unique_lock host_lock(host_.state_mutex_);
    PluginRecord* owner = host_.PluginByHandle(plugin);
    if (!owner || !owner->accepting_resources)
    {
        return KEEL_RESULT_NOT_READY;
    }
    const auto record = OwnedRecord(plugin, convar);
    host_lock.unlock();
    return record ? ReleaseRecord(record) : KEEL_RESULT_NOT_FOUND;
}

KeelResult ConVarService::ReleaseNative(
    KeelPluginHandle plugin,
    KeelConVarHandle convar)
{
    return Release(plugin, convar);
}

KeelResult ConVarService::Read(
    KeelPluginHandle plugin,
    KeelConVarHandle convar,
    std::int32_t slot,
    KeelConVarValue* value)
{
    if (!value || value->size != sizeof(KeelConVarValue) ||
        slot != KEELS2_CONVAR_GLOBAL_SLOT)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if (!adapter_.IsGameThread())
    {
        return KEEL_RESULT_WRONG_THREAD;
    }
    std::scoped_lock host_lock(host_.state_mutex_);
    PluginRecord* owner = host_.PluginByHandle(plugin);
    if (!host_.accepting_resources_ || !owner || !owner->accepting_resources)
    {
        return KEEL_RESULT_NOT_READY;
    }
    const auto record = OwnedRecord(plugin, convar);
    if (!record || record->release_state.load(std::memory_order_acquire) != 0)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    return adapter_.ReadConVar(record->game_handle, slot, *value);
}

KeelResult ConVarService::QueueSet(
    KeelPluginHandle plugin,
    KeelConVarHandle convar,
    std::int32_t slot,
    const KeelConVarValue* value)
{
    if (!value || slot != KEELS2_CONVAR_GLOBAL_SLOT)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::unique_lock host_lock(host_.state_mutex_);
    PluginRecord* owner = host_.PluginByHandle(plugin);
    if (!host_.accepting_resources_ || !owner || !owner->accepting_resources)
    {
        return KEEL_RESULT_NOT_READY;
    }
    const auto record = OwnedRecord(plugin, convar);
    if (!record || record->release_state.load(std::memory_order_acquire) != 0)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    KeelConVarInfo info{};
    info.size = sizeof(info);
    const KeelResult described = adapter_.DescribeConVar(record->game_handle, info);
    if (described != KEEL_RESULT_OK)
    {
        return described;
    }
    if (!ValidValue(*value, info.type))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    record->active.fetch_add(1, std::memory_order_acq_rel);
    host_lock.unlock();
    struct ActiveScope
    {
        ~ActiveScope()
        {
            ConVarService::LeaveActive(active);
        }

        std::atomic<std::uint32_t>& active;
    } active_scope{record->active};
    return adapter_.QueueConVarSet(record->game_handle, slot, *value);
}

KeelResult ConVarService::Describe(
    KeelPluginHandle plugin,
    KeelConVarHandle convar,
    KeelConVarInfo* info)
{
    if (!info || info->size != sizeof(KeelConVarInfo))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock host_lock(host_.state_mutex_);
    PluginRecord* owner = host_.PluginByHandle(plugin);
    if (!host_.accepting_resources_ || !owner || !owner->accepting_resources)
    {
        return KEEL_RESULT_NOT_READY;
    }
    const auto record = OwnedRecord(plugin, convar);
    if (!record || record->release_state.load(std::memory_order_acquire) != 0)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    const KeelResult described = adapter_.DescribeConVar(record->game_handle, *info);
    if (described == KEEL_RESULT_OK)
    {
        info->flags &= kPublicFlags;
    }
    return described;
}

void ConVarService::Dispatch(
    Record& record,
    std::int32_t slot,
    const KeelConVarValue& new_value,
    const KeelConVarValue& old_value)
{
    record.active.fetch_add(1, std::memory_order_acq_rel);
    if (!record.enabled.load(std::memory_order_acquire) || !record.callback ||
        record.release_state.load(std::memory_order_acquire) != 0)
    {
        LeaveActive(record.active);
        return;
    }
    if (callback_depth_ == callback_stack_.size())
    {
        LeaveActive(record.active);
        host_.Write(KEEL_LOG_ERROR, "ConVar callback recursion limit reached");
        return;
    }
    callback_stack_[callback_depth_++] = &record;
    const KeelConVarChange change{
        sizeof(KeelConVarChange),
        slot,
        record.handle,
        record.name.c_str(),
        old_value,
        new_value
    };
    try
    {
        record.callback(&change, record.user_data);
    }
    catch (...)
    {
        host_.Write(KEEL_LOG_ERROR, "plugin threw during a ConVar callback");
    }
    callback_stack_[--callback_depth_] = nullptr;
    LeaveActive(record.active);
}

void ConVarService::DispatchNative(
    Record& record,
    void* convar,
    std::int32_t slot,
    const void* new_value,
    const void* old_value)
{
    record.active.fetch_add(1, std::memory_order_acq_rel);
    if (!record.enabled.load(std::memory_order_acquire) || !record.native_callback ||
        !convar || !new_value || !old_value ||
        record.release_state.load(std::memory_order_acquire) != 0)
    {
        LeaveActive(record.active);
        return;
    }
    if (callback_depth_ == callback_stack_.size())
    {
        LeaveActive(record.active);
        host_.Write(KEEL_LOG_ERROR, "ConVar callback recursion limit reached");
        return;
    }
    callback_stack_[callback_depth_++] = &record;
    try
    {
        record.native_callback(convar, slot, new_value, old_value, record.user_data);
    }
    catch (...)
    {
        host_.Write(KEEL_LOG_ERROR, "plugin threw during a ConVar callback");
    }
    callback_stack_[--callback_depth_] = nullptr;
    LeaveActive(record.active);
}

KeelResult ConVarService::ReleaseRecord(const std::shared_ptr<Record>& record)
{
    if (!record)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    if (IsCurrentRecord(record.get()))
    {
        return KEEL_RESULT_BUSY;
    }
    std::uint32_t expected{};
    if (!record->release_state.compare_exchange_strong(
            expected,
            1,
            std::memory_order_acq_rel))
    {
        while (expected == 1)
        {
            record->release_state.wait(expected, std::memory_order_acquire);
            expected = record->release_state.load(std::memory_order_acquire);
        }
        return expected == 2 ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }
    record->enabled.store(false, std::memory_order_release);
    WaitForZero(record->active);
    adapter_.ReleaseConVar(record->game_handle);
    WaitForZero(record->active);
    {
        std::scoped_lock lock(registry_mutex_);
        const auto current = records_.find(record->handle);
        if (current != records_.end() && current->second == record)
        {
            records_.erase(current);
        }
        if (record->created)
        {
            const auto definition = definitions_.find(record->definition_key);
            if (definition != definitions_.end() && definition->second.active == record->handle)
            {
                definition->second.active = 0;
            }
        }
    }
    record->release_state.store(2, std::memory_order_release);
    record->release_state.notify_all();
    return KEEL_RESULT_OK;
}

std::shared_ptr<ConVarService::Record> ConVarService::OwnedRecord(
    KeelPluginHandle plugin,
    KeelConVarHandle convar) const
{
    if (convar == 0)
    {
        return {};
    }
    std::scoped_lock lock(registry_mutex_);
    const auto record = records_.find(convar);
    return record != records_.end() && record->second->owner == plugin
        ? record->second
        : std::shared_ptr<Record>{};
}

bool ConVarService::ValidType(KeelConVarType type) noexcept
{
    return type == KEELS2_CONVAR_BOOL || type == KEELS2_CONVAR_INT32 ||
        type == KEELS2_CONVAR_FLOAT32 || type == KEELS2_CONVAR_STRING;
}

bool ConVarService::ValidLookupName(const char* name) noexcept
{
    if (!name)
    {
        return false;
    }
    std::size_t length{};
    for (const unsigned char* character = reinterpret_cast<const unsigned char*>(name);
         *character;
         ++character)
    {
        const bool letter = (*character >= 'a' && *character <= 'z') ||
            (*character >= 'A' && *character <= 'Z');
        if (!letter && !(*character >= '0' && *character <= '9') && *character != '_')
        {
            return false;
        }
        if (++length > 63)
        {
            return false;
        }
    }
    return length != 0;
}

bool ConVarService::ValidValue(const KeelConVarValue& value, KeelConVarType type) noexcept
{
    if (value.size != sizeof(KeelConVarValue) || value.type != type)
    {
        return false;
    }
    if (type == KEELS2_CONVAR_BOOL)
    {
        return value.value.boolean_value == KEEL_FALSE || value.value.boolean_value == KEEL_TRUE;
    }
    if (type == KEELS2_CONVAR_FLOAT32)
    {
        return std::isfinite(value.value.float32_value);
    }
    if (type == KEELS2_CONVAR_STRING)
    {
        return value.value.string_value && std::strlen(value.value.string_value) <= 4095;
    }
    return type == KEELS2_CONVAR_INT32;
}

bool ConVarService::ValidDefinition(const KeelConVarSpec& spec) noexcept
{
    if (spec.size != sizeof(KeelConVarSpec) || !Host::ValidCommandName(spec.name) ||
        !Host::ValidMetadataText(spec.description, 255, true) || !ValidType(spec.type) ||
        (spec.flags & ~kPublicFlags) != 0 || !ValidValue(spec.default_value, spec.type) ||
        (spec.has_minimum != KEEL_FALSE && spec.has_minimum != KEEL_TRUE) ||
        (spec.has_maximum != KEEL_FALSE && spec.has_maximum != KEEL_TRUE) ||
        spec.reserved_minimum != 0 || spec.reserved_maximum != 0)
    {
        return false;
    }
    const bool has_minimum = spec.has_minimum == KEEL_TRUE;
    const bool has_maximum = spec.has_maximum == KEEL_TRUE;
    if ((spec.type == KEELS2_CONVAR_BOOL || spec.type == KEELS2_CONVAR_STRING) &&
        (has_minimum || has_maximum))
    {
        return false;
    }
    if ((has_minimum && !ValidValue(spec.minimum_value, spec.type)) ||
        (has_maximum && !ValidValue(spec.maximum_value, spec.type)))
    {
        return false;
    }
    if (spec.type == KEELS2_CONVAR_INT32)
    {
        const std::int32_t value = spec.default_value.value.int32_value;
        const std::int32_t minimum = has_minimum
            ? spec.minimum_value.value.int32_value
            : std::numeric_limits<std::int32_t>::min();
        const std::int32_t maximum = has_maximum
            ? spec.maximum_value.value.int32_value
            : std::numeric_limits<std::int32_t>::max();
        return minimum <= maximum && value >= minimum && value <= maximum;
    }
    if (spec.type == KEELS2_CONVAR_FLOAT32)
    {
        const float value = spec.default_value.value.float32_value;
        const float minimum = has_minimum
            ? spec.minimum_value.value.float32_value
            : -std::numeric_limits<float>::max();
        const float maximum = has_maximum
            ? spec.maximum_value.value.float32_value
            : std::numeric_limits<float>::max();
        return minimum <= maximum && value >= minimum && value <= maximum;
    }
    return true;
}

bool ConVarService::EqualDefinition(
    const Definition& definition,
    const KeelConVarSpec& spec) noexcept
{
    const bool has_minimum = spec.has_minimum == KEEL_TRUE;
    const bool has_maximum = spec.has_maximum == KEEL_TRUE;
    return definition.type == spec.type && definition.name == spec.name &&
        definition.description == (spec.description ? spec.description : "") &&
        definition.flags == (spec.flags | KEELS2_CVAR_FLAG_RELEASE) &&
        definition.has_minimum == has_minimum && definition.has_maximum == has_maximum &&
        EqualValue(definition.default_value, spec.default_value, spec.type) &&
        (!has_minimum || EqualValue(definition.minimum_value, spec.minimum_value, spec.type)) &&
        (!has_maximum || EqualValue(definition.maximum_value, spec.maximum_value, spec.type));
}

std::string ConVarService::NormalizeName(const char* name)
{
    std::string normalized = name ? name : "";
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return normalized;
}

bool ConVarService::IsCurrentOwner(KeelPluginHandle plugin) noexcept
{
    for (std::size_t index{}; index < callback_depth_; ++index)
    {
        if (callback_stack_[index] && callback_stack_[index]->owner == plugin)
        {
            return true;
        }
    }
    return false;
}

bool ConVarService::IsCurrentRecord(const Record* record) noexcept
{
    return std::find(
        callback_stack_.begin(),
        callback_stack_.begin() + static_cast<std::ptrdiff_t>(callback_depth_),
        record) != callback_stack_.begin() + static_cast<std::ptrdiff_t>(callback_depth_);
}

void ConVarService::LeaveActive(std::atomic<std::uint32_t>& active) noexcept
{
    if (active.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        active.notify_all();
    }
}

void ConVarService::WaitForZero(std::atomic<std::uint32_t>& active) noexcept
{
    std::uint32_t value = active.load(std::memory_order_acquire);
    while (value != 0)
    {
        active.wait(value, std::memory_order_acquire);
        value = active.load(std::memory_order_acquire);
    }
}

}
