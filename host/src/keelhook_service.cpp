#include "keelhook_service.h"

#include "host.h"

#include <keels2/hooking/vtable.h>
#include <keels2/platform/loaded_module.h>

#include <dyncall.h>
#include <dyncall_args.h>
#include <dyncall_callback.h>
#include <safetyhook/inline_hook.hpp>
#include <safetyhook/os.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace keels2::host
{

namespace
{

std::filesystem::path NormalizePath(const std::filesystem::path& path)
{
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : normalized;
}

bool EqualPath(const std::filesystem::path& left, const std::filesystem::path& right)
{
#if defined(_WIN32)
    const std::string first = left.string();
    const std::string second = right.string();
    return first.size() == second.size() &&
        std::equal(first.begin(), first.end(), second.begin(), [](unsigned char a, unsigned char b) {
            return std::tolower(a) == std::tolower(b);
        });
#else
    return left == right;
#endif
}

bool CopyText(const char* value, std::size_t maximum, bool allow_empty, std::string& output)
{
    if (!value)
    {
        return false;
    }
    std::size_t length{};
    while (length <= maximum && value[length] != '\0')
    {
        ++length;
    }
    if (length > maximum || (!allow_empty && length == 0))
    {
        return false;
    }
    output.assign(value, length);
    return true;
}

bool IsValueType(KeelHookValueType type, bool allow_void)
{
    return (allow_void && type == KH_VALUE_VOID) ||
        (type >= KH_VALUE_BOOL && type <= KH_VALUE_AGGREGATE);
}

bool IsScalarValueType(KeelHookValueType type)
{
    return type >= KH_VALUE_BOOL && type <= KH_VALUE_FLOAT64;
}

std::size_t ScalarByteSize(KeelHookValueType type)
{
    switch (type)
    {
        case KH_VALUE_BOOL:
        case KH_VALUE_INT8:
        case KH_VALUE_UINT8: return 1;
        case KH_VALUE_INT16:
        case KH_VALUE_UINT16: return 2;
        case KH_VALUE_INT32:
        case KH_VALUE_UINT32:
        case KH_VALUE_FLOAT32: return 4;
        case KH_VALUE_INT64:
        case KH_VALUE_UINT64:
        case KH_VALUE_POINTER:
        case KH_VALUE_FLOAT64: return 8;
        default: return 0;
    }
}

int HexDigit(unsigned char value)
{
    if (value >= '0' && value <= '9')
    {
        return value - '0';
    }
    value = static_cast<unsigned char>(std::tolower(value));
    return value >= 'a' && value <= 'f'
        ? value - 'a' + 10
        : -1;
}

bool ApplyOffset(void* address, std::int64_t offset, void*& result)
{
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    if (offset >= 0)
    {
        const auto amount = static_cast<std::uint64_t>(offset);
        if (amount > std::numeric_limits<std::uintptr_t>::max() - value)
        {
            return false;
        }
        result = reinterpret_cast<void*>(value + static_cast<std::uintptr_t>(amount));
        return true;
    }
    const std::uint64_t amount = static_cast<std::uint64_t>(-(offset + 1)) + 1;
    if (amount > value)
    {
        return false;
    }
    result = reinterpret_cast<void*>(value - static_cast<std::uintptr_t>(amount));
    return true;
}

template <typename Type>
Type BitCopy(const auto& value)
{
    static_assert(sizeof(Type) == sizeof(value));
    Type result{};
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

}

class KeelHookService::Implementation final
{
public:
    Implementation(KeelHookService& service, std::string profile)
        : service_(service), service_profile_(std::move(profile))
    {
        const auto callback_entry = &CallbackDispatch;
        platform::LoadedModule module;
        std::string error;
        if (platform::FindLoadedModuleForAddress(
                BitCopy<void*>(callback_entry),
                module,
                error) != platform::ModuleLookup::found)
        {
            throw std::runtime_error("KeelHook callback module could not be inspected");
        }
        for (const auto& range : module.ranges)
        {
            if (range.executable)
            {
                entry_hazards_.push_back({
                    reinterpret_cast<std::uint8_t*>(const_cast<std::byte*>(range.address)),
                    range.size
                });
            }
        }
        if (entry_hazards_.empty())
        {
            throw std::runtime_error("KeelHook callback module has no executable range");
        }
        Implementation* expected{};
        if (!active_.compare_exchange_strong(expected, this, std::memory_order_acq_rel))
        {
            throw std::runtime_error("KeelHook service already exists");
        }
        api_ = {
            sizeof(KeelHookApi),
            KEELHOOK_API_VERSION,
            &ResolveTargetEntry,
            &ReleaseTargetEntry,
            &AddCallbackEntry,
            &RemoveCallbackEntry,
            &ResolveVirtualTargetEntry
        };
    }

    ~Implementation()
    {
        active_.store(nullptr, std::memory_order_release);
        Shutdown();
    }

    const KeelHookApi& Api() const noexcept
    {
        return api_;
    }

    void Authorize(KeelPluginHandle plugin, const std::filesystem::path& path, bool active)
    {
        std::scoped_lock lock(registry_mutex_);
        if (!shutting_down_)
        {
            const auto normalized = NormalizePath(path);
            const auto [iterator, inserted] = owners_.try_emplace(
                plugin,
                OwnerState{normalized, true, active});
            if (!inserted && EqualPath(iterator->second.path, normalized))
            {
                iterator->second.accepting = true;
                iterator->second.active = iterator->second.active || active;
            }
        }
    }

    void Activate(KeelPluginHandle plugin)
    {
        std::scoped_lock lock(registry_mutex_);
        const auto owner = owners_.find(plugin);
        if (owner == owners_.end() || shutting_down_)
        {
            return;
        }
        owner->second.accepting = true;
        owner->second.active = true;
        for (const auto& [handle, callback] : callbacks_)
        {
            static_cast<void>(handle);
            if (callback->owner == plugin)
            {
                callback->enabled.store(true, std::memory_order_release);
            }
        }
    }

    KeelResult Deactivate(KeelPluginHandle plugin)
    {
        std::vector<std::shared_ptr<CallbackRecord>> callbacks;
        {
            std::scoped_lock lock(registry_mutex_);
            const auto owner = owners_.find(plugin);
            if (owner == owners_.end())
            {
                return KEEL_RESULT_OK;
            }
            if (IsCurrentOwner(plugin))
            {
                return KEEL_RESULT_BUSY;
            }
            owner->second.accepting = false;
            owner->second.active = false;
            for (const auto& [handle, callback] : callbacks_)
            {
                static_cast<void>(handle);
                if (callback->owner == plugin)
                {
                    callback->enabled.store(false, std::memory_order_release);
                    callbacks.push_back(callback);
                }
            }
        }
        for (const auto& callback : callbacks)
        {
            WaitForZero(callback->active);
        }
        return KEEL_RESULT_OK;
    }

    KeelResult ReleasePlugin(KeelPluginHandle plugin)
    {
        CollectPhysical();
        struct ReleaseOperation
        {
            std::shared_ptr<TargetRecord> target;
            std::vector<std::shared_ptr<CallbackRecord>> callbacks;
            bool leased{};
            bool disable{};
            bool restored{true};
        };
        std::vector<ReleaseOperation> operations;
        std::filesystem::path owner_path;
        {
            std::scoped_lock lock(registry_mutex_);
            const auto owner = owners_.find(plugin);
            if (owner == owners_.end())
            {
                return KEEL_RESULT_OK;
            }
            if (IsCurrentOwner(plugin))
            {
                return KEEL_RESULT_BUSY;
            }
            owner_path = owner->second.path;
            for (const auto& [handle, target] : targets_)
            {
                static_cast<void>(handle);
                const bool owned_lease = target->leases.contains(plugin);
                std::vector<std::shared_ptr<CallbackRecord>> owned_callbacks;
                for (const auto& callback : target->callbacks)
                {
                    if (callback->owner == plugin)
                    {
                        owned_callbacks.push_back(callback);
                    }
                }
                if (TargetTouchesModule(*target, owner_path))
                {
                    const bool other_lease = std::any_of(
                        target->leases.begin(),
                        target->leases.end(),
                        [plugin](KeelPluginHandle lease) { return lease != plugin; });
                    const bool other_callback = std::any_of(
                        target->callbacks.begin(),
                        target->callbacks.end(),
                        [plugin](const auto& callback) { return callback->owner != plugin; });
                    if (other_lease || other_callback)
                    {
                        return KEEL_RESULT_BUSY;
                    }
                }
                if (!owned_lease && owned_callbacks.empty())
                {
                    continue;
                }
                if (target->transition || IsCurrentTarget(target.get()))
                {
                    return KEEL_RESULT_BUSY;
                }
                const std::size_t remaining_callbacks = target->callbacks.size() - owned_callbacks.size();
                operations.push_back({
                    target,
                    std::move(owned_callbacks),
                    owned_lease,
                    remaining_callbacks == 0 && !target->callbacks.empty(),
                    true
                });
            }

            std::sort(operations.begin(), operations.end(), [](const auto& left, const auto& right) {
                return left.target->handle < right.target->handle;
            });

            owner->second.accepting = false;
            for (auto& operation : operations)
            {
                operation.target->transition = true;
                for (const auto& callback : operation.callbacks)
                {
                    callback->enabled.store(false, std::memory_order_release);
                }
            }
        }

        for (auto& operation : operations)
        {
            if (operation.disable)
            {
                operation.restored = DisablePhysical(*operation.target);
            }
            for (const auto& callback : operation.callbacks)
            {
                WaitForZero(callback->active);
            }
            if (operation.disable && !operation.restored)
            {
                operation.restored = DisablePhysical(*operation.target);
            }
            if (operation.restored && TargetTouchesModule(*operation.target, owner_path))
            {
                WaitForZero(operation.target->active);
            }
        }

        bool restored = true;
        {
            std::scoped_lock lock(registry_mutex_);
            for (auto& operation : operations)
            {
                auto& target = operation.target;
                if (operation.restored)
                {
                    if (operation.leased)
                    {
                        target->leases.erase(plugin);
                    }
                    for (const auto& callback : operation.callbacks)
                    {
                        callbacks_.erase(callback->handle);
                        target->callbacks.erase(
                            std::remove(target->callbacks.begin(), target->callbacks.end(), callback),
                            target->callbacks.end());
                    }
                }
                else
                {
                    restored = false;
                }
                target->transition = false;
            }
            if (restored)
            {
                owners_.erase(plugin);
            }
            PruneTargetsLocked();
        }
        CollectPhysical();
        return restored ? KEEL_RESULT_OK : KEEL_RESULT_ENGINE_FAILURE;
    }

    bool Shutdown()
    {
        std::vector<std::shared_ptr<CallbackRecord>> callbacks;
        std::vector<std::shared_ptr<TargetRecord>> targets;
        {
            std::scoped_lock lock(registry_mutex_);
            if (shutdown_complete_)
            {
                return true;
            }
            if (!shutting_down_)
            {
                shutting_down_ = true;
                owners_.clear();
                callbacks.reserve(callbacks_.size());
                for (auto& [handle, callback] : callbacks_)
                {
                    static_cast<void>(handle);
                    callback->enabled.store(false, std::memory_order_release);
                    callbacks.push_back(callback);
                }
                callbacks_.clear();
                for (auto& [handle, target] : targets_)
                {
                    static_cast<void>(handle);
                    target->callbacks.clear();
                    target->leases.clear();
                    target->transition = true;
                }
            }
            targets.reserve(targets_.size() + retired_targets_.size());
            for (auto& [handle, target] : targets_)
            {
                static_cast<void>(handle);
                targets.push_back(target);
            }
            targets.insert(targets.end(), retired_targets_.begin(), retired_targets_.end());
            std::sort(targets.begin(), targets.end(), [](const auto& left, const auto& right) {
                return left->handle < right->handle;
            });
        }

        std::vector<bool> disabled;
        disabled.reserve(targets.size());
        bool restored = true;
        for (const auto& target : targets)
        {
            const bool result = DisablePhysical(*target);
            disabled.push_back(result);
            restored = result && restored;
        }
        for (const auto& callback : callbacks)
        {
            WaitForZero(callback->active);
        }
        for (std::size_t index{}; index < targets.size(); ++index)
        {
            if (disabled[index])
            {
                WaitForZero(targets[index]->active);
                restored = DestroyPhysical(*targets[index]) && restored;
            }
        }

        {
            std::scoped_lock lock(registry_mutex_);
            if (restored)
            {
                targets_.clear();
                targets_by_key_.clear();
                instance_tables_.clear();
                retired_targets_.clear();
                shutdown_complete_ = true;
            }
        }
        return restored;
    }

private:
    struct AggregateData
    {
        std::uint32_t byte_size{};
        std::string identity;
        std::shared_ptr<DCaggr> native;
        std::vector<std::shared_ptr<AggregateData>> children;
    };

    struct PrototypeData
    {
        KeelHookCallingConvention calling_convention{};
        KeelHookValueType return_type{};
        std::vector<KeelHookValueType> arguments;
        std::shared_ptr<AggregateData> return_aggregate;
        std::vector<std::shared_ptr<AggregateData>> argument_aggregates;
        std::vector<DCaggr*> callback_aggregates;
        std::string callback_signature;
        std::string aggregate_identity;
        bool method{};

        bool operator==(const PrototypeData& other) const
        {
            return calling_convention == other.calling_convention &&
                return_type == other.return_type && arguments == other.arguments &&
                aggregate_identity == other.aggregate_identity && method == other.method;
        }
    };

    struct TargetKey
    {
        std::uintptr_t primary{};
        std::uintptr_t secondary{};
        KeelHookMechanism mechanism{};

        bool operator==(const TargetKey&) const = default;
    };

    struct TargetKeyHash
    {
        std::size_t operator()(const TargetKey& key) const noexcept
        {
            const auto primary = std::hash<std::uintptr_t>{}(key.primary);
            const auto secondary = std::hash<std::uintptr_t>{}(key.secondary);
            const auto mechanism = std::hash<std::uint32_t>{}(key.mechanism);
            const auto first = primary ^
                (secondary + 0x9e3779b9u + (primary << 6u) + (primary >> 2u));
            return first ^ (mechanism + 0x9e3779b9u + (first << 6u) + (first >> 2u));
        }
    };

    struct TargetRecord;

    struct InstanceTableRecord
    {
        std::shared_ptr<hooking::InstanceVtable> table;
        std::filesystem::path module_path;
        platform::LoadedModulePin module_pin;
    };

    struct CallbackRecord
    {
        KeelHookCallbackHandle handle{};
        KeelPluginHandle owner{};
        std::weak_ptr<TargetRecord> target;
        std::uint32_t phases{};
        std::int32_t priority{};
        std::uint64_t sequence{};
        KeelHookCallback callback{};
        void* user_data{};
        std::atomic<bool> enabled{true};
        std::atomic<std::uint32_t> active{};
    };

    struct TargetRecord
    {
        Implementation* service{};
        KeelHookTargetHandle handle{};
        TargetKey key;
        void* address{};
        std::filesystem::path module_path;
        platform::LoadedModulePin module_pin;
        std::filesystem::path storage_module_path;
        platform::LoadedModulePin storage_module_pin;
        PrototypeData prototype;
        std::unordered_set<KeelPluginHandle> leases;
        std::vector<std::shared_ptr<CallbackRecord>> callbacks;
        bool transition{};
        std::mutex physical_mutex;
        std::unique_ptr<safetyhook::InlineHook> hook;
        std::unique_ptr<hooking::SharedVtableHook> virtual_hook;
        std::shared_ptr<InstanceTableRecord> instance_table;
        void** virtual_slot{};
        std::uint32_t virtual_index{};
        bool instance_enabled{};
        DCCallback* closure{};
        bool restore_failure_reported{};
        bool quiescence_failure_reported{};
        std::atomic<void*> trampoline{};
        std::atomic<std::uint32_t> active{};
    };

    struct OwnerState
    {
        std::filesystem::path path;
        bool accepting{};
        bool active{};
    };

    struct ResolvedTarget
    {
        void* address{};
        platform::LoadedModule module;
        platform::LoadedModulePin pin;
        platform::LoadedModule storage_module;
        platform::LoadedModulePin storage_pin;
        std::shared_ptr<InstanceTableRecord> instance_table;
        void** virtual_slot{};
        std::uint32_t virtual_index{};
    };

    static KeelResult ResolveTargetEntry(
        KeelPluginHandle plugin,
        const KeelHookTargetSpec* spec,
        const KeelHookPrototype* prototype,
        KeelHookTargetHandle* target)
    {
        Implementation* instance = active_.load(std::memory_order_acquire);
        if (!instance)
        {
            return KEEL_RESULT_NOT_READY;
        }
        try
        {
            return instance->ResolveTarget(plugin, spec, prototype, target);
        }
        catch (...)
        {
            instance->Log("internal exception while resolving a target");
            return KEEL_RESULT_ENGINE_FAILURE;
        }
    }

    static KeelResult ReleaseTargetEntry(KeelPluginHandle plugin, KeelHookTargetHandle target)
    {
        Implementation* instance = active_.load(std::memory_order_acquire);
        if (!instance)
        {
            return KEEL_RESULT_NOT_READY;
        }
        try
        {
            return instance->ReleaseTarget(plugin, target);
        }
        catch (...)
        {
            instance->Log("internal exception while releasing a target");
            return KEEL_RESULT_ENGINE_FAILURE;
        }
    }

    static KeelResult AddCallbackEntry(
        KeelPluginHandle plugin,
        KeelHookTargetHandle target,
        const KeelHookCallbackSpec* spec,
        KeelHookCallbackHandle* callback)
    {
        Implementation* instance = active_.load(std::memory_order_acquire);
        if (!instance)
        {
            return KEEL_RESULT_NOT_READY;
        }
        try
        {
            return instance->AddCallback(plugin, target, spec, callback);
        }
        catch (...)
        {
            instance->Log("internal exception while adding a callback");
            return KEEL_RESULT_ENGINE_FAILURE;
        }
    }

    static KeelResult RemoveCallbackEntry(KeelPluginHandle plugin, KeelHookCallbackHandle callback)
    {
        Implementation* instance = active_.load(std::memory_order_acquire);
        if (!instance)
        {
            return KEEL_RESULT_NOT_READY;
        }
        try
        {
            return instance->RemoveCallback(plugin, callback);
        }
        catch (...)
        {
            instance->Log("internal exception while removing a callback");
            return KEEL_RESULT_ENGINE_FAILURE;
        }
    }

    static KeelResult ResolveVirtualTargetEntry(
        KeelPluginHandle plugin,
        const KeelHookVirtualTargetSpec* spec,
        const KeelHookPrototype* prototype,
        KeelHookTargetHandle* target)
    {
        Implementation* instance = active_.load(std::memory_order_acquire);
        if (!instance)
        {
            return KEEL_RESULT_NOT_READY;
        }
        try
        {
            return instance->ResolveVirtualTarget(plugin, spec, prototype, target);
        }
        catch (...)
        {
            instance->Log("internal exception while resolving a virtual target");
            return KEEL_RESULT_ENGINE_FAILURE;
        }
    }

    KeelResult ResolveTarget(
        KeelPluginHandle plugin,
        const KeelHookTargetSpec* spec,
        const KeelHookPrototype* prototype,
        KeelHookTargetHandle* output)
    {
        CollectPhysical();
        if (!spec || spec->size != sizeof(KeelHookTargetSpec) || !prototype || !output)
        {
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        *output = 0;
        {
            std::scoped_lock lock(registry_mutex_);
            if (!OwnerReadyLocked(plugin))
            {
                return KEEL_RESULT_NOT_READY;
            }
        }

        PrototypeData canonical;
        std::string error;
        const KeelResult prototype_result = CanonicalPrototype(
            *prototype,
            (spec->flags & KH_TARGET_METHOD) != 0,
            canonical,
            error);
        if (prototype_result != KEEL_RESULT_OK)
        {
            Log(error);
            return prototype_result;
        }
        ResolvedTarget resolved;
        const KeelResult resolution_result = ResolveSpec(*spec, resolved, error);
        if (resolution_result != KEEL_RESULT_OK)
        {
            Log(error);
            return resolution_result;
        }

        const TargetKey key{
            reinterpret_cast<std::uintptr_t>(resolved.address),
            0,
            spec->mechanism
        };
        std::scoped_lock lock(registry_mutex_);
        if (!OwnerReadyLocked(plugin))
        {
            return KEEL_RESULT_NOT_READY;
        }
        return RegisterTargetLocked(plugin, key, resolved, canonical, output);
    }

    KeelResult ResolveVirtualTarget(
        KeelPluginHandle plugin,
        const KeelHookVirtualTargetSpec* spec,
        const KeelHookPrototype* prototype,
        KeelHookTargetHandle* output)
    {
        CollectPhysical();
        if (!spec || spec->size != sizeof(KeelHookVirtualTargetSpec) || !prototype || !output)
        {
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        *output = 0;
        if (!spec->instance || spec->flags != 0 || spec->reserved != 0 ||
            (spec->mechanism != KH_MECHANISM_VIRTUAL &&
                spec->mechanism != KH_MECHANISM_VIRTUAL_INSTANCE) ||
            (spec->mechanism == KH_MECHANISM_VIRTUAL && spec->table_size != 0) ||
            (spec->mechanism == KH_MECHANISM_VIRTUAL_INSTANCE &&
                (spec->table_size == 0 || spec->index >= spec->table_size || spec->table_size > 4096)))
        {
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        if (spec->profile)
        {
            std::string profile;
            if (!CopyText(spec->profile, 512, false, profile))
            {
                return KEEL_RESULT_INVALID_ARGUMENT;
            }
            if (profile != service_profile_)
            {
                Log("virtual target compatibility profile does not match the running server");
                return KEEL_RESULT_INCOMPATIBLE;
            }
        }

        PrototypeData canonical;
        std::string error;
        const KeelResult prototype_result = CanonicalPrototype(*prototype, true, canonical, error);
        if (prototype_result != KEEL_RESULT_OK)
        {
            Log(error);
            return prototype_result;
        }
        if (canonical.arguments.empty() || canonical.arguments.front() != KH_VALUE_POINTER)
        {
            Log("virtual target prototype must begin with an object pointer");
            return KEEL_RESULT_INVALID_ARGUMENT;
        }

        if (spec->mechanism == KH_MECHANISM_VIRTUAL)
        {
            {
                std::scoped_lock lock(registry_mutex_);
                if (!OwnerReadyLocked(plugin))
                {
                    return KEEL_RESULT_NOT_READY;
                }
                if (instance_tables_.contains(spec->instance))
                {
                    Log("shared and per-instance virtual targets cannot manage the same object");
                    return KEEL_RESULT_BUSY;
                }
            }
            ResolvedTarget resolved;
            void* original{};
            if (hooking::ResolveVtableSlot(
                    spec->instance,
                    spec->index,
                    resolved.virtual_slot,
                    original) != hooking::VtableHookResult::ok)
            {
                Log("virtual target object or slot is invalid");
                return KEEL_RESULT_INVALID_ARGUMENT;
            }
            resolved.address = original;
            resolved.virtual_index = spec->index;
            const TargetKey key{
                reinterpret_cast<std::uintptr_t>(resolved.virtual_slot),
                0,
                spec->mechanism
            };
            {
                std::scoped_lock lock(registry_mutex_);
                if (!OwnerReadyLocked(plugin))
                {
                    return KEEL_RESULT_NOT_READY;
                }
                if (instance_tables_.contains(spec->instance) ||
                    VirtualScopeConflictLocked(resolved.virtual_slot, spec->mechanism))
                {
                    Log("shared and per-instance virtual targets cannot overlap");
                    return KEEL_RESULT_BUSY;
                }
                const auto existing = targets_by_key_.find(key);
                if (existing != targets_by_key_.end())
                {
                    const auto& target = existing->second;
                    if (!target->transition)
                    {
                        const void* expected = target->address;
                        if (target->virtual_hook && target->virtual_hook->Enabled())
                        {
                            expected = target->closure;
                        }
                        if (original != expected)
                        {
                            Log("shared virtual slot no longer matches the registered target");
                            return KEEL_RESULT_INCOMPATIBLE;
                        }
                    }
                    return RegisterTargetLocked(
                        plugin,
                        key,
                        resolved,
                        canonical,
                        output);
                }
            }
            const auto code_lookup = platform::FindLoadedModuleForAddress(
                original,
                resolved.module,
                error);
            if (code_lookup != platform::ModuleLookup::found)
            {
                Log(error);
                return ModuleResult(code_lookup);
            }
            if (!platform::IsExecutableAddress(resolved.module, original) ||
                !resolved.pin.Acquire(resolved.module, error))
            {
                Log("virtual target does not resolve to pinned executable code");
                return KEEL_RESULT_INVALID_ARGUMENT;
            }
            const auto storage_lookup = platform::FindLoadedModuleForAddress(
                resolved.virtual_slot,
                resolved.storage_module,
                error);
            if (storage_lookup != platform::ModuleLookup::found)
            {
                Log(error);
                return ModuleResult(storage_lookup);
            }
            if (!resolved.storage_pin.Acquire(resolved.storage_module, error))
            {
                Log(error);
                return KEEL_RESULT_ENGINE_FAILURE;
            }
            std::scoped_lock lock(registry_mutex_);
            if (!OwnerReadyLocked(plugin))
            {
                return KEEL_RESULT_NOT_READY;
            }
            if (instance_tables_.contains(spec->instance) ||
                VirtualScopeConflictLocked(resolved.virtual_slot, spec->mechanism))
            {
                Log("shared and per-instance virtual targets cannot overlap");
                return KEEL_RESULT_BUSY;
            }
            return RegisterTargetLocked(plugin, key, resolved, canonical, output);
        }

        std::scoped_lock lock(registry_mutex_);
        if (!OwnerReadyLocked(plugin))
        {
            return KEEL_RESULT_NOT_READY;
        }
        std::shared_ptr<InstanceTableRecord> table_record;
        bool created{};
        const auto known = instance_tables_.find(spec->instance);
        if (known != instance_tables_.end())
        {
            table_record = known->second;
            if (table_record->table->EntryCount() != spec->table_size ||
                !table_record->table->Intact())
            {
                Log("per-instance virtual table is incompatible or no longer intact");
                return KEEL_RESULT_INCOMPATIBLE;
            }
        }
        else
        {
            std::shared_ptr<hooking::InstanceVtable> table;
            if (hooking::InstanceVtable::Create(
                    spec->instance,
                    spec->table_size,
                    table) != hooking::VtableHookResult::ok)
            {
                Log("per-instance virtual table could not be cloned");
                return KEEL_RESULT_INVALID_ARGUMENT;
            }
            platform::LoadedModule storage_module;
            const auto storage_lookup = platform::FindLoadedModuleForAddress(
                table->OriginalSlot(spec->index),
                storage_module,
                error);
            if (storage_lookup != platform::ModuleLookup::found)
            {
                Log(error);
                return ModuleResult(storage_lookup);
            }
            auto record = std::make_shared<InstanceTableRecord>();
            record->table = std::move(table);
            record->module_path = storage_module.path;
            if (!record->module_pin.Acquire(storage_module, error))
            {
                Log(error);
                return KEEL_RESULT_ENGINE_FAILURE;
            }
            table_record = std::move(record);
            created = true;
        }

        ResolvedTarget resolved;
        resolved.address = table_record->table->Original(spec->index);
        resolved.virtual_slot = table_record->table->OriginalSlot(spec->index);
        resolved.virtual_index = spec->index;
        resolved.instance_table = table_record;
        const auto code_lookup = platform::FindLoadedModuleForAddress(
            resolved.address,
            resolved.module,
            error);
        if (code_lookup != platform::ModuleLookup::found)
        {
            Log(error);
            return ModuleResult(code_lookup);
        }
        if (!platform::IsExecutableAddress(resolved.module, resolved.address) ||
            !resolved.pin.Acquire(resolved.module, error))
        {
            Log("per-instance virtual target does not resolve to pinned executable code");
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        if (VirtualScopeConflictLocked(resolved.virtual_slot, spec->mechanism))
        {
            Log("shared and per-instance virtual targets cannot overlap");
            return KEEL_RESULT_BUSY;
        }
        const TargetKey key{
            reinterpret_cast<std::uintptr_t>(spec->instance),
            spec->index,
            spec->mechanism
        };
        const KeelResult result = RegisterTargetLocked(plugin, key, resolved, canonical, output);
        if (result == KEEL_RESULT_OK && created)
        {
            instance_tables_.emplace(spec->instance, std::move(table_record));
        }
        return result;
    }

    KeelResult RegisterTargetLocked(
        KeelPluginHandle plugin,
        const TargetKey& key,
        ResolvedTarget& resolved,
        PrototypeData& canonical,
        KeelHookTargetHandle* output)
    {
        const auto existing = targets_by_key_.find(key);
        if (existing != targets_by_key_.end())
        {
            if (existing->second->transition)
            {
                return KEEL_RESULT_BUSY;
            }
            if (!(existing->second->prototype == canonical))
            {
                Log("target is already managed with an incompatible prototype");
                return KEEL_RESULT_INCOMPATIBLE;
            }
            existing->second->leases.insert(plugin);
            *output = existing->second->handle;
            return KEEL_RESULT_OK;
        }
        if (next_target_ == 0)
        {
            Log("target handle space is exhausted");
            return KEEL_RESULT_ENGINE_FAILURE;
        }

        auto target = std::make_shared<TargetRecord>();
        target->service = this;
        target->handle = next_target_++;
        target->key = key;
        target->address = resolved.address;
        target->module_path = resolved.module.path;
        target->module_pin = std::move(resolved.pin);
        target->storage_module_path = resolved.storage_module.path;
        target->storage_module_pin = std::move(resolved.storage_pin);
        target->instance_table = resolved.instance_table;
        target->virtual_slot = resolved.virtual_slot;
        target->virtual_index = resolved.virtual_index;
        target->prototype = std::move(canonical);
        target->leases.insert(plugin);
        targets_.emplace(target->handle, target);
        targets_by_key_.emplace(target->key, target);
        *output = target->handle;
        return KEEL_RESULT_OK;
    }

    bool VirtualScopeConflictLocked(void** slot, KeelHookMechanism mechanism) const
    {
        for (const auto& [handle, target] : targets_)
        {
            static_cast<void>(handle);
            if (target->virtual_slot != slot)
            {
                continue;
            }
            if ((mechanism == KH_MECHANISM_VIRTUAL &&
                    target->key.mechanism == KH_MECHANISM_VIRTUAL_INSTANCE) ||
                (mechanism == KH_MECHANISM_VIRTUAL_INSTANCE &&
                    target->key.mechanism == KH_MECHANISM_VIRTUAL))
            {
                return true;
            }
        }
        return false;
    }

    KeelResult ReleaseTarget(KeelPluginHandle plugin, KeelHookTargetHandle handle)
    {
        CollectPhysical();
        {
            std::scoped_lock lock(registry_mutex_);
            if (!OwnerExistsLocked(plugin))
            {
                return KEEL_RESULT_NOT_READY;
            }
            const auto iterator = targets_.find(handle);
            if (iterator == targets_.end() || !iterator->second->leases.contains(plugin))
            {
                return KEEL_RESULT_NOT_FOUND;
            }
            const auto& target = iterator->second;
            if (target->transition)
            {
                return KEEL_RESULT_BUSY;
            }
            if (std::any_of(target->callbacks.begin(), target->callbacks.end(), [plugin](const auto& callback) {
                    return callback->owner == plugin;
                }))
            {
                return KEEL_RESULT_BUSY;
            }
            target->leases.erase(plugin);
            PruneTargetsLocked();
        }
        CollectPhysical();
        return KEEL_RESULT_OK;
    }

    KeelResult AddCallback(
        KeelPluginHandle plugin,
        KeelHookTargetHandle target_handle,
        const KeelHookCallbackSpec* spec,
        KeelHookCallbackHandle* output)
    {
        CollectPhysical();
        if (!spec || spec->size != sizeof(KeelHookCallbackSpec) || !output || !spec->callback ||
            spec->reserved != 0 || spec->phases == 0 || (spec->phases & ~KH_PHASE_BOTH) != 0)
        {
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        *output = 0;
        std::shared_ptr<TargetRecord> target;
        std::shared_ptr<CallbackRecord> callback;
        bool first{};
        {
            std::scoped_lock lock(registry_mutex_);
            if (!OwnerReadyLocked(plugin))
            {
                return KEEL_RESULT_NOT_READY;
            }
            const auto target_iterator = targets_.find(target_handle);
            if (target_iterator == targets_.end() || !target_iterator->second->leases.contains(plugin))
            {
                return KEEL_RESULT_NOT_FOUND;
            }
            target = target_iterator->second;
            if (target->transition)
            {
                return KEEL_RESULT_BUSY;
            }
            if (next_callback_ == 0 || next_sequence_ == 0)
            {
                Log("callback handle space is exhausted");
                return KEEL_RESULT_ENGINE_FAILURE;
            }
            first = target->callbacks.empty();
            if (first)
            {
                target->transition = true;
            }
            callback = std::make_shared<CallbackRecord>();
            callback->handle = next_callback_++;
            callback->owner = plugin;
            callback->target = target;
            callback->phases = spec->phases;
            callback->priority = spec->priority;
            callback->sequence = next_sequence_++;
            callback->callback = spec->callback;
            callback->user_data = spec->user_data;
            callback->enabled.store(owners_.at(plugin).active, std::memory_order_release);
            target->callbacks.push_back(callback);
            callbacks_.emplace(callback->handle, callback);
        }

        if (first)
        {
            std::string error;
            if (!EnablePhysical(*target, error))
            {
                callback->enabled.store(false, std::memory_order_release);
                std::scoped_lock lock(registry_mutex_);
                callbacks_.erase(callback->handle);
                target->callbacks.erase(
                    std::remove(target->callbacks.begin(), target->callbacks.end(), callback),
                    target->callbacks.end());
                target->transition = false;
                Log(error);
                return KEEL_RESULT_ENGINE_FAILURE;
            }
            std::scoped_lock lock(registry_mutex_);
            target->transition = false;
        }
        *output = callback->handle;
        return KEEL_RESULT_OK;
    }

    KeelResult RemoveCallback(KeelPluginHandle plugin, KeelHookCallbackHandle handle)
    {
        CollectPhysical();
        std::shared_ptr<CallbackRecord> callback;
        std::shared_ptr<TargetRecord> target;
        bool last{};
        bool was_enabled{};
        {
            std::scoped_lock lock(registry_mutex_);
            if (!OwnerExistsLocked(plugin))
            {
                return KEEL_RESULT_NOT_READY;
            }
            const auto iterator = callbacks_.find(handle);
            if (iterator == callbacks_.end() || iterator->second->owner != plugin)
            {
                return KEEL_RESULT_NOT_FOUND;
            }
            callback = iterator->second;
            target = callback->target.lock();
            if (!target || target->transition)
            {
                return KEEL_RESULT_BUSY;
            }
            was_enabled = callback->enabled.load(std::memory_order_acquire);
            callback->enabled.store(false, std::memory_order_release);
            target->transition = true;
            last = target->callbacks.size() == 1;
        }

        bool restored = true;
        if (last)
        {
            restored = DisablePhysical(*target);
        }
        if (!restored)
        {
            std::scoped_lock lock(registry_mutex_);
            callback->enabled.store(was_enabled, std::memory_order_release);
            target->transition = false;
            return KEEL_RESULT_ENGINE_FAILURE;
        }
        if (!IsCurrentCallback(callback.get()))
        {
            WaitForZero(callback->active);
        }
        {
            std::scoped_lock lock(registry_mutex_);
            callbacks_.erase(callback->handle);
            target->callbacks.erase(
                std::remove(target->callbacks.begin(), target->callbacks.end(), callback),
                target->callbacks.end());
            target->transition = false;
            PruneTargetsLocked();
        }
        CollectPhysical();
        return KEEL_RESULT_OK;
    }

    struct AggregateBuildContext
    {
        std::unordered_map<const KeelHookAggregate*, std::shared_ptr<AggregateData>> completed;
        std::unordered_set<const KeelHookAggregate*> active;
        std::size_t count{};
    };

    static char AggregateFieldCharacter(KeelHookValueType type)
    {
        return type == KH_VALUE_BOOL ? DC_SIGCHAR_UCHAR : SignatureCharacter(type);
    }

    KeelResult CanonicalAggregate(
        const KeelHookAggregate* input,
        std::uint32_t depth,
        AggregateBuildContext& context,
        std::shared_ptr<AggregateData>& output,
        std::string& error) const
    {
        if (!input || depth > KEELHOOK_MAX_AGGREGATE_DEPTH)
        {
            error = "aggregate descriptor nesting is invalid";
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        if (const auto completed = context.completed.find(input); completed != context.completed.end())
        {
            output = completed->second;
            return KEEL_RESULT_OK;
        }
        if (!context.active.insert(input).second ||
            ++context.count > KEELHOOK_MAX_AGGREGATE_DESCRIPTORS ||
            input->size != sizeof(KeelHookAggregate) || input->flags != 0 ||
            input->byte_size == 0 || input->byte_size > KEELHOOK_MAX_AGGREGATE_SIZE ||
            input->field_count == 0 || input->field_count > KEELHOOK_MAX_AGGREGATE_FIELDS ||
            !input->fields)
        {
            error = "aggregate descriptor is invalid or recursive";
            return KEEL_RESULT_INVALID_ARGUMENT;
        }

        auto aggregate = std::make_shared<AggregateData>();
        aggregate->byte_size = input->byte_size;
        aggregate->identity = std::to_string(input->byte_size) + "{";
        aggregate->children.reserve(input->field_count);
        std::vector<std::shared_ptr<AggregateData>> nested(input->field_count);
        std::uint32_t previous_offset{};
        for (std::uint32_t index{}; index < input->field_count; ++index)
        {
            const KeelHookAggregateField& field = input->fields[index];
            if (field.size != sizeof(KeelHookAggregateField) || field.array_length == 0 ||
                field.offset >= input->byte_size || (index != 0 && field.offset < previous_offset))
            {
                context.active.erase(input);
                error = "aggregate field descriptor is invalid";
                return KEEL_RESULT_INVALID_ARGUMENT;
            }
            previous_offset = field.offset;
            std::size_t field_size{};
            if (field.type == KH_VALUE_AGGREGATE)
            {
                const KeelResult result = CanonicalAggregate(
                    field.aggregate,
                    depth + 1,
                    context,
                    nested[index],
                    error);
                if (result != KEEL_RESULT_OK)
                {
                    context.active.erase(input);
                    return result;
                }
                field_size = nested[index]->byte_size;
                aggregate->children.push_back(nested[index]);
            }
            else if (IsScalarValueType(field.type) && !field.aggregate)
            {
                field_size = ScalarByteSize(field.type);
            }
            else
            {
                context.active.erase(input);
                error = "aggregate field type is invalid";
                return KEEL_RESULT_INVALID_ARGUMENT;
            }
            const std::size_t available = input->byte_size - field.offset;
            if (field_size == 0 || field.array_length > available / field_size)
            {
                context.active.erase(input);
                error = "aggregate field exceeds its containing object";
                return KEEL_RESULT_INVALID_ARGUMENT;
            }
            aggregate->identity += std::to_string(field.type) + ":" +
                std::to_string(field.offset) + ":" + std::to_string(field.array_length) + ":";
            if (nested[index])
            {
                aggregate->identity += nested[index]->identity;
            }
            aggregate->identity.push_back(';');
        }
        aggregate->identity.push_back('}');

        aggregate->native = std::shared_ptr<DCaggr>(
            dcNewAggr(input->field_count, input->byte_size),
            [](DCaggr* value) {
                if (value)
                {
                    dcFreeAggr(value);
                }
            });
        if (!aggregate->native)
        {
            context.active.erase(input);
            error = "aggregate adapter allocation failed";
            return KEEL_RESULT_ENGINE_FAILURE;
        }
        for (std::uint32_t index{}; index < input->field_count; ++index)
        {
            const KeelHookAggregateField& field = input->fields[index];
            if (nested[index])
            {
                dcAggrField(
                    aggregate->native.get(),
                    DC_SIGCHAR_AGGREGATE,
                    static_cast<DCint>(field.offset),
                    field.array_length,
                    nested[index]->native.get());
            }
            else
            {
                dcAggrField(
                    aggregate->native.get(),
                    AggregateFieldCharacter(field.type),
                    static_cast<DCint>(field.offset),
                    field.array_length);
            }
        }
        dcCloseAggr(aggregate->native.get());
        context.active.erase(input);
        context.completed.emplace(input, aggregate);
        output = std::move(aggregate);
        return KEEL_RESULT_OK;
    }

    KeelResult CanonicalPrototype(
        const KeelHookPrototype& input,
        bool method,
        PrototypeData& output,
        std::string& error) const
    {
        if (input.size != sizeof(KeelHookPrototype) || input.calling_convention != KH_CALL_NATIVE ||
            input.flags != 0 || input.fixed_argument_count != input.argument_count ||
            !IsValueType(input.return_type, true) || input.argument_count > KEELHOOK_MAX_ARGUMENTS ||
            (input.argument_count != 0 && !input.argument_types))
        {
            error = "prototype is invalid or unsupported";
            return input.calling_convention == KH_CALL_NATIVE
                ? KEEL_RESULT_INVALID_ARGUMENT
                : KEEL_RESULT_UNSUPPORTED;
        }
        output = {};
        output.calling_convention = input.calling_convention;
        output.return_type = input.return_type;
        output.method = method;
        if (input.argument_count != 0)
        {
            output.arguments.assign(input.argument_types, input.argument_types + input.argument_count);
        }
        if (std::any_of(output.arguments.begin(), output.arguments.end(), [](KeelHookValueType type) {
                return !IsValueType(type, false);
            }))
        {
            error = "prototype contains an unsupported argument type";
            return KEEL_RESULT_UNSUPPORTED;
        }
        if (method && (output.arguments.empty() || output.arguments.front() != KH_VALUE_POINTER))
        {
            error = "method prototype must begin with an object pointer";
            return KEEL_RESULT_INVALID_ARGUMENT;
        }

        const bool aggregate_return = input.return_type == KH_VALUE_AGGREGATE;
        const bool aggregate_argument = std::find(
            output.arguments.begin(),
            output.arguments.end(),
            KH_VALUE_AGGREGATE) != output.arguments.end();
        if (aggregate_return != (input.return_aggregate != nullptr) ||
            (aggregate_argument && !input.argument_aggregates))
        {
            error = "prototype aggregate descriptors do not match its value types";
            return KEEL_RESULT_INVALID_ARGUMENT;
        }

        AggregateBuildContext aggregate_context;
        if (aggregate_return)
        {
            const KeelResult result = CanonicalAggregate(
                input.return_aggregate,
                1,
                aggregate_context,
                output.return_aggregate,
                error);
            if (result != KEEL_RESULT_OK)
            {
                return result;
            }
        }
        output.argument_aggregates.resize(output.arguments.size());
        for (std::size_t index{}; index < output.arguments.size(); ++index)
        {
            const KeelHookAggregate* descriptor = input.argument_aggregates
                ? input.argument_aggregates[index]
                : nullptr;
            if ((output.arguments[index] == KH_VALUE_AGGREGATE) != (descriptor != nullptr))
            {
                error = "prototype argument aggregate descriptors are inconsistent";
                return KEEL_RESULT_INVALID_ARGUMENT;
            }
            if (!descriptor)
            {
                continue;
            }
            const KeelResult result = CanonicalAggregate(
                descriptor,
                1,
                aggregate_context,
                output.argument_aggregates[index],
                error);
            if (result != KEEL_RESULT_OK)
            {
                return result;
            }
        }

        output.callback_signature.reserve(output.arguments.size() + 5);
        if (method)
        {
            output.callback_signature.push_back(DC_SIGCHAR_CC_PREFIX);
            output.callback_signature.push_back(DC_SIGCHAR_CC_THISCALL);
        }
        output.aggregate_identity = aggregate_return
            ? "R" + output.return_aggregate->identity
            : "R-";
        for (std::size_t index{}; index < output.arguments.size(); ++index)
        {
            output.callback_signature.push_back(SignatureCharacter(output.arguments[index]));
            if (output.argument_aggregates[index])
            {
                output.callback_aggregates.push_back(output.argument_aggregates[index]->native.get());
                output.aggregate_identity += "A" + output.argument_aggregates[index]->identity;
            }
            else
            {
                output.aggregate_identity += "A-";
            }
        }
        output.callback_signature.push_back(DC_SIGCHAR_ENDARG);
        output.callback_signature.push_back(SignatureCharacter(output.return_type));
        if (output.return_aggregate)
        {
            output.callback_aggregates.push_back(output.return_aggregate->native.get());
        }
        return KEEL_RESULT_OK;
    }

    KeelResult ResolveSpec(
        const KeelHookTargetSpec& spec,
        ResolvedTarget& resolved,
        std::string& error) const
    {
        if ((spec.flags & ~KH_TARGET_METHOD) != 0 || spec.reserved != 0)
        {
            error = "target specification contains unsupported flags";
            return KEEL_RESULT_UNSUPPORTED;
        }
        if (spec.mechanism != KH_MECHANISM_DETOUR)
        {
            error = "target mechanism is unsupported";
            return KEEL_RESULT_UNSUPPORTED;
        }

        void* base_address{};
        if (spec.source == KH_TARGET_ADDRESS)
        {
            if (!spec.address || spec.occurrence != 0 || spec.symbol || spec.pattern || spec.profile)
            {
                error = "direct target address is invalid";
                return KEEL_RESULT_INVALID_ARGUMENT;
            }
            base_address = spec.address;
            if (spec.module)
            {
                std::string module_name;
                if (!CopyText(spec.module, 4096, false, module_name))
                {
                    error = "module selector is invalid";
                    return KEEL_RESULT_INVALID_ARGUMENT;
                }
                const auto lookup = platform::FindLoadedModule(module_name, resolved.module, error);
                if (lookup != platform::ModuleLookup::found)
                {
                    return ModuleResult(lookup);
                }
                if (!resolved.pin.Acquire(resolved.module, error))
                {
                    return KEEL_RESULT_ENGINE_FAILURE;
                }
            }
            else
            {
                const auto lookup = platform::FindLoadedModuleForAddress(base_address, resolved.module, error);
                if (lookup != platform::ModuleLookup::found)
                {
                    return ModuleResult(lookup);
                }
                if (!resolved.pin.Acquire(resolved.module, error))
                {
                    return KEEL_RESULT_ENGINE_FAILURE;
                }
            }
        }
        else if (spec.source == KH_TARGET_SYMBOL)
        {
            if (spec.occurrence != 0 || spec.address || spec.pattern || spec.profile)
            {
                error = "symbol target occurrence must be zero";
                return KEEL_RESULT_INVALID_ARGUMENT;
            }
            std::string module_name;
            std::string symbol;
            if (!CopyText(spec.module, 4096, false, module_name) ||
                !CopyText(spec.symbol, 512, false, symbol))
            {
                error = "symbol target is incomplete";
                return KEEL_RESULT_INVALID_ARGUMENT;
            }
            const auto lookup = platform::FindLoadedModule(module_name, resolved.module, error);
            if (lookup != platform::ModuleLookup::found)
            {
                return ModuleResult(lookup);
            }
            if (!resolved.pin.Acquire(resolved.module, error))
            {
                return KEEL_RESULT_ENGINE_FAILURE;
            }
            base_address = platform::FindLoadedSymbol(resolved.module, symbol, error);
            if (!base_address)
            {
                return KEEL_RESULT_NOT_FOUND;
            }
        }
        else if (spec.source == KH_TARGET_PATTERN)
        {
            if (spec.address || spec.symbol)
            {
                error = "pattern target contains fields for another resolver";
                return KEEL_RESULT_INVALID_ARGUMENT;
            }
            std::string module_name;
            std::string profile;
            std::string pattern;
            if (!CopyText(spec.module, 4096, false, module_name) ||
                !CopyText(spec.profile, 512, false, profile) ||
                !CopyText(spec.pattern, 16384, false, pattern))
            {
                error = "pattern target is incomplete";
                return KEEL_RESULT_INVALID_ARGUMENT;
            }
            if (profile != service_profile_)
            {
                error = "pattern target compatibility profile does not match the running server";
                return KEEL_RESULT_INCOMPATIBLE;
            }
            const auto lookup = platform::FindLoadedModule(module_name, resolved.module, error);
            if (lookup != platform::ModuleLookup::found)
            {
                return ModuleResult(lookup);
            }
            if (!resolved.pin.Acquire(resolved.module, error))
            {
                return KEEL_RESULT_ENGINE_FAILURE;
            }
            std::vector<std::optional<std::uint8_t>> bytes;
            if (!ParsePattern(pattern, bytes))
            {
                error = "pattern syntax is invalid";
                return KEEL_RESULT_INVALID_ARGUMENT;
            }
            const KeelResult scan_result = ScanPattern(resolved.module, bytes, spec.occurrence, base_address);
            if (scan_result != KEEL_RESULT_OK)
            {
                error = scan_result == KEEL_RESULT_AMBIGUOUS
                    ? "pattern target is ambiguous"
                    : "pattern target was not found";
                return scan_result;
            }
        }
        else
        {
            error = "target source is unsupported";
            return KEEL_RESULT_UNSUPPORTED;
        }

        if (!ApplyOffset(base_address, spec.offset, resolved.address) ||
            !platform::IsExecutableAddress(resolved.module, resolved.address))
        {
            error = "resolved target is not an executable address in the requested module";
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        return KEEL_RESULT_OK;
    }

    static bool ParsePattern(
        std::string_view text,
        std::vector<std::optional<std::uint8_t>>& bytes)
    {
        std::size_t position{};
        while (position < text.size())
        {
            while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])))
            {
                ++position;
            }
            if (position == text.size())
            {
                break;
            }
            const std::size_t start = position;
            while (position < text.size() && !std::isspace(static_cast<unsigned char>(text[position])))
            {
                ++position;
            }
            const auto token = text.substr(start, position - start);
            if (token == "?" || token == "??")
            {
                bytes.emplace_back(std::nullopt);
                continue;
            }
            if (token.size() != 2)
            {
                return false;
            }
            const int high = HexDigit(static_cast<unsigned char>(token[0]));
            const int low = HexDigit(static_cast<unsigned char>(token[1]));
            if (high < 0 || low < 0)
            {
                return false;
            }
            bytes.emplace_back(static_cast<std::uint8_t>((high << 4) | low));
            if (bytes.size() > 4096)
            {
                return false;
            }
        }
        return !bytes.empty();
    }

    static KeelResult ScanPattern(
        const platform::LoadedModule& module,
        const std::vector<std::optional<std::uint8_t>>& pattern,
        std::uint32_t occurrence,
        void*& address)
    {
        struct ScanRange
        {
            std::uintptr_t begin{};
            std::uintptr_t end{};
        };
        std::vector<ScanRange> ranges;
        for (const auto& range : module.ranges)
        {
            if (!range.readable || !range.executable || range.size < pattern.size())
            {
                continue;
            }
            const auto begin = reinterpret_cast<std::uintptr_t>(range.address);
            if (range.size > std::numeric_limits<std::uintptr_t>::max() - begin)
            {
                continue;
            }
            ranges.push_back({begin, begin + range.size});
        }
        std::sort(ranges.begin(), ranges.end(), [](const auto& left, const auto& right) {
            return left.begin < right.begin;
        });
        std::vector<ScanRange> merged;
        for (const auto& range : ranges)
        {
            if (merged.empty() || range.begin > merged.back().end)
            {
                merged.push_back(range);
            }
            else
            {
                merged.back().end = std::max(merged.back().end, range.end);
            }
        }

        std::uint32_t matches{};
        void* selected{};
        for (const auto& range : merged)
        {
            const auto* bytes = reinterpret_cast<const std::byte*>(range.begin);
            const auto size = static_cast<std::size_t>(range.end - range.begin);
            for (std::size_t position{}; position <= size - pattern.size(); ++position)
            {
                bool matched = true;
                for (std::size_t index{}; index < pattern.size(); ++index)
                {
                    if (pattern[index] &&
                        static_cast<std::uint8_t>(bytes[position + index]) != *pattern[index])
                    {
                        matched = false;
                        break;
                    }
                }
                if (!matched)
                {
                    continue;
                }
                ++matches;
                if (occurrence == 0)
                {
                    if (matches > 1)
                    {
                        return KEEL_RESULT_AMBIGUOUS;
                    }
                    selected = const_cast<std::byte*>(bytes + position);
                }
                else if (matches == occurrence)
                {
                    address = const_cast<std::byte*>(bytes + position);
                    return KEEL_RESULT_OK;
                }
            }
        }
        if (occurrence == 0 && matches == 1)
        {
            address = selected;
            return KEEL_RESULT_OK;
        }
        return KEEL_RESULT_NOT_FOUND;
    }

    bool EnablePhysical(TargetRecord& target, std::string& error)
    {
        std::scoped_lock lock(target.physical_mutex);
        if (PhysicalEnabled(target))
        {
            return true;
        }
        if (!target.closure)
        {
            target.closure = target.prototype.callback_aggregates.empty()
                ? dcbNewCallback(
                    target.prototype.callback_signature.c_str(),
                    &CallbackDispatch,
                    &target)
                : dcbNewCallback2(
                    target.prototype.callback_signature.c_str(),
                    &CallbackDispatch,
                    &target,
                    target.prototype.callback_aggregates.data());
            if (!target.closure)
            {
                error = "could not allocate a native callback closure";
                return false;
            }
        }
        if (target.key.mechanism == KH_MECHANISM_DETOUR)
        {
            if (!target.hook)
            {
                auto hook = safetyhook::InlineHook::create(
                    target.address,
                    static_cast<void*>(target.closure),
                    safetyhook::InlineHook::StartDisabled);
                if (!hook)
                {
                    error = "could not relocate target prologue, backend error " +
                        std::to_string(hook.error().type);
                    dcbFreeCallback(target.closure);
                    target.closure = nullptr;
                    return false;
                }
                target.hook = std::make_unique<safetyhook::InlineHook>(std::move(*hook));
                target.trampoline.store(target.hook->original<void*>(), std::memory_order_release);
            }
            const auto enabled = target.hook->enable();
            if (!enabled)
            {
                error = "could not activate target detour, backend error " +
                    std::to_string(enabled.error().type);
                target.trampoline.store(nullptr, std::memory_order_release);
                target.hook.reset();
                dcbFreeCallback(target.closure);
                target.closure = nullptr;
                return false;
            }
            return true;
        }

        target.trampoline.store(target.address, std::memory_order_release);
        if (target.key.mechanism == KH_MECHANISM_VIRTUAL)
        {
            if (!target.virtual_hook)
            {
                const auto created = hooking::SharedVtableHook::Create(
                    target.virtual_slot,
                    static_cast<void*>(target.closure),
                    target.virtual_hook);
                if (created != hooking::VtableHookResult::ok ||
                    !target.virtual_hook || target.virtual_hook->Original() != target.address)
                {
                    error = "shared virtual slot changed before activation";
                    target.virtual_hook.reset();
                    target.trampoline.store(nullptr, std::memory_order_release);
                    dcbFreeCallback(target.closure);
                    target.closure = nullptr;
                    return false;
                }
            }
            if (target.virtual_hook->Enable() != hooking::VtableHookResult::ok)
            {
                error = "shared virtual slot could not be activated";
                target.trampoline.store(nullptr, std::memory_order_release);
                target.virtual_hook.reset();
                dcbFreeCallback(target.closure);
                target.closure = nullptr;
                return false;
            }
            return true;
        }

        if (target.key.mechanism == KH_MECHANISM_VIRTUAL_INSTANCE && target.instance_table &&
            target.instance_table->table->Enable(
                target.virtual_index,
                static_cast<void*>(target.closure)) == hooking::VtableHookResult::ok)
        {
            target.instance_enabled = true;
            return true;
        }
        error = "per-instance virtual slot could not be activated";
        target.trampoline.store(nullptr, std::memory_order_release);
        dcbFreeCallback(target.closure);
        target.closure = nullptr;
        return false;
    }

    bool DisablePhysical(TargetRecord& target)
    {
        std::scoped_lock lock(target.physical_mutex);
        if (!PhysicalEnabled(target))
        {
            target.restore_failure_reported = false;
            return true;
        }
        bool restored{};
        std::uint32_t backend_error{};
        if (target.key.mechanism == KH_MECHANISM_DETOUR)
        {
            const auto hazards = PhysicalHazards(target);
            const auto disabled = target.hook->disable(hazards);
            restored = disabled.has_value();
            if (!restored)
            {
                backend_error = disabled.error().type;
            }
        }
        else if (target.key.mechanism == KH_MECHANISM_VIRTUAL)
        {
            restored = target.virtual_hook->Disable() == hooking::VtableHookResult::ok;
        }
        else if (target.key.mechanism == KH_MECHANISM_VIRTUAL_INSTANCE && target.instance_table)
        {
            restored = target.instance_table->table->Disable(
                target.virtual_index,
                static_cast<void*>(target.closure)) == hooking::VtableHookResult::ok;
            if (restored)
            {
                target.instance_enabled = false;
            }
        }
        if (!restored)
        {
            if (!target.restore_failure_reported)
            {
                const std::string suffix = backend_error == 0
                    ? std::string{}
                    : ", backend error " + std::to_string(backend_error);
                Log("could not restore a physical target" + suffix);
                target.restore_failure_reported = true;
            }
            return false;
        }
        target.restore_failure_reported = false;
        return true;
    }

    static bool PhysicalEnabled(const TargetRecord& target)
    {
        if (target.key.mechanism == KH_MECHANISM_DETOUR)
        {
            return target.hook && target.hook->enabled();
        }
        if (target.key.mechanism == KH_MECHANISM_VIRTUAL)
        {
            return target.virtual_hook && target.virtual_hook->Enabled();
        }
        return target.key.mechanism == KH_MECHANISM_VIRTUAL_INSTANCE && target.instance_enabled;
    }

    static DCsigchar CallbackDispatch(
        DCCallback*,
        DCArgs* native_arguments,
        DCValue* native_result,
        void* user_data)
    {
        auto* target = static_cast<TargetRecord*>(user_data);
        if (!target || !target->service)
        {
            return DC_SIGCHAR_VOID;
        }
        return target->service->Dispatch(*target, native_arguments, native_result);
    }

    struct alignas(KEELHOOK_MAX_AGGREGATE_ALIGNMENT) AggregateStorage final
    {
        std::byte* data() noexcept
        {
            return bytes.data();
        }

        const std::byte* data() const noexcept
        {
            return bytes.data();
        }

        void fill(std::byte value) noexcept
        {
            bytes.fill(value);
        }

        std::array<std::byte, KEELHOOK_MAX_AGGREGATE_SIZE> bytes{};
    };
    using ArgumentAggregateStorage =
        std::array<AggregateStorage, KEELHOOK_MAX_ARGUMENTS>;

    DCsigchar Dispatch(TargetRecord& target, DCArgs* native_arguments, DCValue* native_result) noexcept
    {
        const char return_character = SignatureCharacter(target.prototype.return_type);
        std::array<KeelHookValue, KEELHOOK_MAX_ARGUMENTS> arguments{};
        ArgumentAggregateStorage argument_storage{};
        ExtractArguments(target.prototype, native_arguments, arguments, argument_storage);
        AggregateStorage result_storage{};
        KeelHookValue result{};
        InitializeValue(
            target.prototype.return_type,
            target.prototype.return_aggregate,
            result,
            result_storage);
        target.active.fetch_add(1, std::memory_order_acq_rel);
        if (target_depth_ >= target_stack_.size())
        {
            result = CallOriginal(target, arguments, result_storage);
            StoreNativeResult(target.prototype, result, native_arguments, native_result);
            LeaveActive(target.active);
            return return_character;
        }
        target_stack_[target_depth_++] = &target;

        bool original_called{};
        bool superseded{};
        try
        {
            std::vector<std::shared_ptr<CallbackRecord>> callbacks;
            {
                std::scoped_lock lock(registry_mutex_);
                callbacks = target.callbacks;
            }
            std::stable_sort(callbacks.begin(), callbacks.end(), [](const auto& left, const auto& right) {
                return left->priority != right->priority
                    ? left->priority > right->priority
                    : left->sequence < right->sequence;
            });

            std::array<KeelHookValue, KEELHOOK_MAX_ARGUMENTS> before_arguments{};
            ArgumentAggregateStorage before_argument_storage{};
            KeelHookValue before_result{};
            AggregateStorage before_result_storage{};
            bool overridden{};
            KeelHookFrame frame{
                sizeof(KeelHookFrame),
                KH_PHASE_PRE,
                target.handle,
                static_cast<std::uint32_t>(target.prototype.arguments.size()),
                0,
                arguments.data(),
                result
            };
            for (const auto& callback : callbacks)
            {
                if ((callback->phases & KH_PHASE_PRE) == 0)
                {
                    continue;
                }
                SnapshotValues(
                    target.prototype,
                    arguments,
                    argument_storage,
                    result,
                    result_storage,
                    before_arguments,
                    before_argument_storage,
                    before_result,
                    before_result_storage);
                const KeelHookAction action = InvokeCallback(*callback, frame);
                ValidateFrame(
                    frame,
                    target,
                    KH_PHASE_PRE,
                    0,
                    arguments,
                    argument_storage,
                    before_arguments,
                    before_argument_storage,
                    result_storage,
                    before_result,
                    before_result_storage);
                if (action == KH_ACTION_CONTINUE)
                {
                    RestoreResult(
                        target.prototype,
                        result,
                        result_storage,
                        before_result,
                        before_result_storage);
                }
                else if (action == KH_ACTION_OVERRIDE)
                {
                    if (!superseded)
                    {
                        result = frame.result;
                        overridden = true;
                    }
                    else
                    {
                        RestoreResult(
                            target.prototype,
                            result,
                            result_storage,
                            before_result,
                            before_result_storage);
                    }
                }
                else if (action == KH_ACTION_SUPERSEDE)
                {
                    result = frame.result;
                    overridden = true;
                    superseded = true;
                }
                else
                {
                    RestoreArguments(
                        target.prototype,
                        arguments,
                        argument_storage,
                        before_arguments,
                        before_argument_storage);
                    RestoreResult(
                        target.prototype,
                        result,
                        result_storage,
                        before_result,
                        before_result_storage);
                    Log("callback returned an invalid action");
                }
                frame.result = result;
            }

            if (!superseded)
            {
                AggregateStorage original_storage{};
                const KeelHookValue original = CallOriginal(target, arguments, original_storage);
                original_called = true;
                if (!overridden)
                {
                    CopyValue(
                        target.prototype.return_type,
                        target.prototype.return_aggregate,
                        original,
                        original_storage,
                        result,
                        result_storage);
                }
                frame.flags = KH_FRAME_ORIGINAL_CALLED;
            }
            frame.phase = KH_PHASE_POST;
            frame.result = result;
            for (auto iterator = callbacks.rbegin(); iterator != callbacks.rend(); ++iterator)
            {
                const auto& callback = *iterator;
                if ((callback->phases & KH_PHASE_POST) == 0)
                {
                    continue;
                }
                SnapshotValues(
                    target.prototype,
                    arguments,
                    argument_storage,
                    result,
                    result_storage,
                    before_arguments,
                    before_argument_storage,
                    before_result,
                    before_result_storage);
                const KeelHookAction action = InvokeCallback(*callback, frame);
                ValidateFrame(
                    frame,
                    target,
                    KH_PHASE_POST,
                    original_called ? KH_FRAME_ORIGINAL_CALLED : 0,
                    arguments,
                    argument_storage,
                    before_arguments,
                    before_argument_storage,
                    result_storage,
                    before_result,
                    before_result_storage);
                if (action == KH_ACTION_OVERRIDE)
                {
                    result = frame.result;
                }
                else
                {
                    if (action != KH_ACTION_CONTINUE)
                    {
                        RestoreArguments(
                            target.prototype,
                            arguments,
                            argument_storage,
                            before_arguments,
                            before_argument_storage);
                        Log("post callback returned an invalid action");
                    }
                    RestoreResult(
                        target.prototype,
                        result,
                        result_storage,
                        before_result,
                        before_result_storage);
                }
                frame.result = result;
            }
        }
        catch (...)
        {
            Log("internal exception during callback dispatch");
            if (!original_called && !superseded)
            {
                result = CallOriginal(target, arguments, result_storage);
            }
        }

        StoreNativeResult(target.prototype, result, native_arguments, native_result);
        --target_depth_;
        target_stack_[target_depth_] = nullptr;
        LeaveActive(target.active);
        return return_character;
    }

    KeelHookAction InvokeCallback(CallbackRecord& callback, KeelHookFrame& frame) noexcept
    {
        callback.active.fetch_add(1, std::memory_order_acq_rel);
        if (!callback.enabled.load(std::memory_order_acquire))
        {
            LeaveActive(callback.active);
            return KH_ACTION_CONTINUE;
        }
        if (callback_depth_ >= callback_stack_.size())
        {
            LeaveActive(callback.active);
            Log("callback recursion limit was reached");
            return KH_ACTION_CONTINUE;
        }
        callback_stack_[callback_depth_++] = &callback;
        KeelHookAction action = KH_ACTION_CONTINUE;
        try
        {
            action = callback.callback(&frame, callback.user_data);
        }
        catch (...)
        {
            Log("plugin threw from a KeelHook callback");
        }
        --callback_depth_;
        callback_stack_[callback_depth_] = nullptr;
        LeaveActive(callback.active);
        return action;
    }

    static void BindValue(
        KeelHookValueType type,
        const std::shared_ptr<AggregateData>& aggregate,
        KeelHookValue& output,
        AggregateStorage& storage)
    {
        output = {};
        output.type = type;
        if (type == KH_VALUE_AGGREGATE && aggregate)
        {
            output.scalar.aggregate.data = storage.data();
            output.scalar.aggregate.size = aggregate->byte_size;
        }
    }

    static void InitializeValue(
        KeelHookValueType type,
        const std::shared_ptr<AggregateData>& aggregate,
        KeelHookValue& output,
        AggregateStorage& storage)
    {
        storage.fill(std::byte{});
        BindValue(type, aggregate, output, storage);
    }

    static bool RuntimeValueValid(
        KeelHookValueType type,
        const std::shared_ptr<AggregateData>& aggregate,
        const KeelHookValue& value,
        const AggregateStorage& storage)
    {
        if (value.type != type || value.reserved != 0)
        {
            return false;
        }
        if (type != KH_VALUE_AGGREGATE)
        {
            return true;
        }
        return aggregate && value.scalar.aggregate.data == storage.data() &&
            value.scalar.aggregate.size == aggregate->byte_size &&
            value.scalar.aggregate.reserved == 0;
    }

    static void CopyValue(
        KeelHookValueType type,
        const std::shared_ptr<AggregateData>& aggregate,
        const KeelHookValue& source,
        const AggregateStorage& source_storage,
        KeelHookValue& output,
        AggregateStorage& output_storage)
    {
        if (type != KH_VALUE_AGGREGATE)
        {
            output = source;
            return;
        }
        BindValue(type, aggregate, output, output_storage);
        if (aggregate)
        {
            std::memcpy(output_storage.data(), source_storage.data(), aggregate->byte_size);
        }
    }

    static void SnapshotValues(
        const PrototypeData& prototype,
        const std::array<KeelHookValue, KEELHOOK_MAX_ARGUMENTS>& arguments,
        const ArgumentAggregateStorage& argument_storage,
        const KeelHookValue& result,
        const AggregateStorage& result_storage,
        std::array<KeelHookValue, KEELHOOK_MAX_ARGUMENTS>& output_arguments,
        ArgumentAggregateStorage& output_argument_storage,
        KeelHookValue& output_result,
        AggregateStorage& output_result_storage)
    {
        for (std::size_t index{}; index < prototype.arguments.size(); ++index)
        {
            CopyValue(
                prototype.arguments[index],
                prototype.argument_aggregates[index],
                arguments[index],
                argument_storage[index],
                output_arguments[index],
                output_argument_storage[index]);
        }
        CopyValue(
            prototype.return_type,
            prototype.return_aggregate,
            result,
            result_storage,
            output_result,
            output_result_storage);
    }

    static void RestoreArguments(
        const PrototypeData& prototype,
        std::array<KeelHookValue, KEELHOOK_MAX_ARGUMENTS>& arguments,
        ArgumentAggregateStorage& argument_storage,
        const std::array<KeelHookValue, KEELHOOK_MAX_ARGUMENTS>& before_arguments,
        const ArgumentAggregateStorage& before_argument_storage)
    {
        for (std::size_t index{}; index < prototype.arguments.size(); ++index)
        {
            CopyValue(
                prototype.arguments[index],
                prototype.argument_aggregates[index],
                before_arguments[index],
                before_argument_storage[index],
                arguments[index],
                argument_storage[index]);
        }
    }

    static void RestoreResult(
        const PrototypeData& prototype,
        KeelHookValue& result,
        AggregateStorage& result_storage,
        const KeelHookValue& before_result,
        const AggregateStorage& before_result_storage)
    {
        CopyValue(
            prototype.return_type,
            prototype.return_aggregate,
            before_result,
            before_result_storage,
            result,
            result_storage);
    }

    static void ValidateFrame(
        KeelHookFrame& frame,
        const TargetRecord& target,
        KeelHookPhase phase,
        std::uint32_t flags,
        std::array<KeelHookValue, KEELHOOK_MAX_ARGUMENTS>& arguments,
        ArgumentAggregateStorage& argument_storage,
        const std::array<KeelHookValue, KEELHOOK_MAX_ARGUMENTS>& before_arguments,
        const ArgumentAggregateStorage& before_argument_storage,
        AggregateStorage& result_storage,
        const KeelHookValue& before_result,
        const AggregateStorage& before_result_storage)
    {
        for (std::size_t index{}; index < target.prototype.arguments.size(); ++index)
        {
            if (!RuntimeValueValid(
                    target.prototype.arguments[index],
                    target.prototype.argument_aggregates[index],
                    arguments[index],
                    argument_storage[index]))
            {
                CopyValue(
                    target.prototype.arguments[index],
                    target.prototype.argument_aggregates[index],
                    before_arguments[index],
                    before_argument_storage[index],
                    arguments[index],
                    argument_storage[index]);
            }
        }
        if (!RuntimeValueValid(
                target.prototype.return_type,
                target.prototype.return_aggregate,
                frame.result,
                result_storage))
        {
            CopyValue(
                target.prototype.return_type,
                target.prototype.return_aggregate,
                before_result,
                before_result_storage,
                frame.result,
                result_storage);
        }
        frame.size = sizeof(KeelHookFrame);
        frame.phase = phase;
        frame.target = target.handle;
        frame.argument_count = static_cast<std::uint32_t>(target.prototype.arguments.size());
        frame.flags = flags;
        frame.arguments = arguments.data();
    }

    static void ExtractArguments(
        const PrototypeData& prototype,
        DCArgs* source,
        std::array<KeelHookValue, KEELHOOK_MAX_ARGUMENTS>& output,
        ArgumentAggregateStorage& storage)
    {
        for (std::size_t index{}; index < prototype.arguments.size(); ++index)
        {
            auto& value = output[index];
            InitializeValue(
                prototype.arguments[index],
                prototype.argument_aggregates[index],
                value,
                storage[index]);
            switch (value.type)
            {
                case KH_VALUE_BOOL: value.scalar.boolean = dcbArgBool(source) ? KEEL_TRUE : KEEL_FALSE; break;
                case KH_VALUE_INT8: value.scalar.int8 = dcbArgChar(source); break;
                case KH_VALUE_UINT8: value.scalar.uint8 = dcbArgUChar(source); break;
                case KH_VALUE_INT16: value.scalar.int16 = dcbArgShort(source); break;
                case KH_VALUE_UINT16: value.scalar.uint16 = dcbArgUShort(source); break;
                case KH_VALUE_INT32: value.scalar.int32 = dcbArgInt(source); break;
                case KH_VALUE_UINT32: value.scalar.uint32 = dcbArgUInt(source); break;
                case KH_VALUE_INT64: value.scalar.int64 = dcbArgLongLong(source); break;
                case KH_VALUE_UINT64: value.scalar.uint64 = dcbArgULongLong(source); break;
                case KH_VALUE_POINTER: value.scalar.pointer = dcbArgPointer(source); break;
                case KH_VALUE_FLOAT32: value.scalar.float32 = dcbArgFloat(source); break;
                case KH_VALUE_FLOAT64: value.scalar.float64 = dcbArgDouble(source); break;
                case KH_VALUE_AGGREGATE: dcbArgAggr(source, storage[index].data()); break;
                default: break;
            }
        }
    }

    static void AddNativeArgument(
        DCCallVM* machine,
        const KeelHookValue& value,
        const std::shared_ptr<AggregateData>& aggregate)
    {
        switch (value.type)
        {
            case KH_VALUE_BOOL: dcArgBool(machine, value.scalar.boolean != KEEL_FALSE); break;
            case KH_VALUE_INT8: dcArgChar(machine, value.scalar.int8); break;
            case KH_VALUE_UINT8: dcArgChar(machine, BitCopy<DCchar>(value.scalar.uint8)); break;
            case KH_VALUE_INT16: dcArgShort(machine, value.scalar.int16); break;
            case KH_VALUE_UINT16: dcArgShort(machine, BitCopy<DCshort>(value.scalar.uint16)); break;
            case KH_VALUE_INT32: dcArgInt(machine, value.scalar.int32); break;
            case KH_VALUE_UINT32: dcArgInt(machine, BitCopy<DCint>(value.scalar.uint32)); break;
            case KH_VALUE_INT64: dcArgLongLong(machine, value.scalar.int64); break;
            case KH_VALUE_UINT64: dcArgLongLong(machine, BitCopy<DClonglong>(value.scalar.uint64)); break;
            case KH_VALUE_POINTER: dcArgPointer(machine, value.scalar.pointer); break;
            case KH_VALUE_FLOAT32: dcArgFloat(machine, value.scalar.float32); break;
            case KH_VALUE_FLOAT64: dcArgDouble(machine, value.scalar.float64); break;
            case KH_VALUE_AGGREGATE:
                dcArgAggr(machine, aggregate->native.get(), value.scalar.aggregate.data);
                break;
            default: break;
        }
    }

    KeelHookValue CallOriginal(
        TargetRecord& target,
        const std::array<KeelHookValue, KEELHOOK_MAX_ARGUMENTS>& arguments,
        AggregateStorage& result_storage) noexcept
    {
        KeelHookValue result{};
        InitializeValue(
            target.prototype.return_type,
            target.prototype.return_aggregate,
            result,
            result_storage);
        void* trampoline = target.trampoline.load(std::memory_order_acquire);
        DCCallVM* machine = trampoline ? dcNewCallVM(4096) : nullptr;
        if (!machine)
        {
            Log("original target could not be invoked");
            return result;
        }
        dcMode(machine, target.prototype.method ? DC_CALL_C_DEFAULT_THIS : DC_CALL_C_DEFAULT);
        if (target.prototype.return_aggregate)
        {
            dcBeginCallAggr(machine, target.prototype.return_aggregate->native.get());
        }
        for (std::size_t index{}; index < target.prototype.arguments.size(); ++index)
        {
            AddNativeArgument(machine, arguments[index], target.prototype.argument_aggregates[index]);
        }
        switch (target.prototype.return_type)
        {
            case KH_VALUE_VOID: dcCallVoid(machine, trampoline); break;
            case KH_VALUE_BOOL: result.scalar.boolean = dcCallBool(machine, trampoline) ? KEEL_TRUE : KEEL_FALSE; break;
            case KH_VALUE_INT8: result.scalar.int8 = dcCallChar(machine, trampoline); break;
            case KH_VALUE_UINT8: result.scalar.uint8 = BitCopy<std::uint8_t>(dcCallChar(machine, trampoline)); break;
            case KH_VALUE_INT16: result.scalar.int16 = dcCallShort(machine, trampoline); break;
            case KH_VALUE_UINT16: result.scalar.uint16 = BitCopy<std::uint16_t>(dcCallShort(machine, trampoline)); break;
            case KH_VALUE_INT32: result.scalar.int32 = dcCallInt(machine, trampoline); break;
            case KH_VALUE_UINT32: result.scalar.uint32 = BitCopy<std::uint32_t>(dcCallInt(machine, trampoline)); break;
            case KH_VALUE_INT64: result.scalar.int64 = dcCallLongLong(machine, trampoline); break;
            case KH_VALUE_UINT64: result.scalar.uint64 = BitCopy<std::uint64_t>(dcCallLongLong(machine, trampoline)); break;
            case KH_VALUE_POINTER: result.scalar.pointer = dcCallPointer(machine, trampoline); break;
            case KH_VALUE_FLOAT32: result.scalar.float32 = dcCallFloat(machine, trampoline); break;
            case KH_VALUE_FLOAT64: result.scalar.float64 = dcCallDouble(machine, trampoline); break;
            case KH_VALUE_AGGREGATE:
                dcCallAggr(
                    machine,
                    trampoline,
                    target.prototype.return_aggregate->native.get(),
                    result_storage.data());
                break;
            default: break;
        }
        if (dcGetError(machine) != DC_ERROR_NONE)
        {
            Log("native call adapter reported an error");
        }
        dcFree(machine);
        return result;
    }

    static void StoreNativeResult(
        const PrototypeData& prototype,
        const KeelHookValue& value,
        DCArgs* arguments,
        DCValue* output)
    {
        if (!output)
        {
            return;
        }
        std::memset(output, 0, sizeof(*output));
        switch (value.type)
        {
            case KH_VALUE_BOOL: output->B = value.scalar.boolean != KEEL_FALSE; break;
            case KH_VALUE_INT8: output->c = value.scalar.int8; break;
            case KH_VALUE_UINT8: output->C = value.scalar.uint8; break;
            case KH_VALUE_INT16: output->s = value.scalar.int16; break;
            case KH_VALUE_UINT16: output->S = value.scalar.uint16; break;
            case KH_VALUE_INT32: output->i = value.scalar.int32; break;
            case KH_VALUE_UINT32: output->I = value.scalar.uint32; break;
            case KH_VALUE_INT64: output->l = value.scalar.int64; break;
            case KH_VALUE_UINT64: output->L = value.scalar.uint64; break;
            case KH_VALUE_POINTER: output->p = value.scalar.pointer; break;
            case KH_VALUE_FLOAT32: output->f = value.scalar.float32; break;
            case KH_VALUE_FLOAT64: output->d = value.scalar.float64; break;
            case KH_VALUE_AGGREGATE:
                if (prototype.return_aggregate && arguments && value.scalar.aggregate.data)
                {
                    dcbReturnAggr(arguments, output, value.scalar.aggregate.data);
                }
                break;
            default: break;
        }
    }

    static char SignatureCharacter(KeelHookValueType type)
    {
        switch (type)
        {
            case KH_VALUE_VOID: return DC_SIGCHAR_VOID;
            case KH_VALUE_BOOL: return DC_SIGCHAR_BOOL;
            case KH_VALUE_INT8: return DC_SIGCHAR_CHAR;
            case KH_VALUE_UINT8: return DC_SIGCHAR_UCHAR;
            case KH_VALUE_INT16: return DC_SIGCHAR_SHORT;
            case KH_VALUE_UINT16: return DC_SIGCHAR_USHORT;
            case KH_VALUE_INT32: return DC_SIGCHAR_INT;
            case KH_VALUE_UINT32: return DC_SIGCHAR_UINT;
            case KH_VALUE_INT64: return DC_SIGCHAR_LONGLONG;
            case KH_VALUE_UINT64: return DC_SIGCHAR_ULONGLONG;
            case KH_VALUE_POINTER: return DC_SIGCHAR_POINTER;
            case KH_VALUE_FLOAT32: return DC_SIGCHAR_FLOAT;
            case KH_VALUE_FLOAT64: return DC_SIGCHAR_DOUBLE;
            case KH_VALUE_AGGREGATE: return DC_SIGCHAR_AGGREGATE;
            default: return DC_SIGCHAR_VOID;
        }
    }

    static KeelResult ModuleResult(platform::ModuleLookup lookup)
    {
        if (lookup == platform::ModuleLookup::ambiguous)
        {
            return KEEL_RESULT_AMBIGUOUS;
        }
        if (lookup == platform::ModuleLookup::not_found)
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        return KEEL_RESULT_ENGINE_FAILURE;
    }

    bool OwnerReadyLocked(KeelPluginHandle plugin) const
    {
        const auto owner = owners_.find(plugin);
        return !shutting_down_ && owner != owners_.end() && owner->second.accepting;
    }

    bool OwnerExistsLocked(KeelPluginHandle plugin) const
    {
        return !shutting_down_ && owners_.contains(plugin);
    }

    static bool IsCurrentOwner(KeelPluginHandle plugin)
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

    static bool IsCurrentCallback(const CallbackRecord* callback)
    {
        return std::find(
            callback_stack_.begin(),
            callback_stack_.begin() + static_cast<std::ptrdiff_t>(callback_depth_),
            callback) != callback_stack_.begin() + static_cast<std::ptrdiff_t>(callback_depth_);
    }

    static bool IsCurrentTarget(const TargetRecord* target)
    {
        return std::find(
            target_stack_.begin(),
            target_stack_.begin() + static_cast<std::ptrdiff_t>(target_depth_),
            target) != target_stack_.begin() + static_cast<std::ptrdiff_t>(target_depth_);
    }

    static bool TargetTouchesModule(
        const TargetRecord& target,
        const std::filesystem::path& path)
    {
        return EqualPath(target.module_path, path) ||
            (!target.storage_module_path.empty() && EqualPath(target.storage_module_path, path)) ||
            (target.instance_table && EqualPath(target.instance_table->module_path, path));
    }

    static void WaitForZero(std::atomic<std::uint32_t>& value)
    {
        std::uint32_t current = value.load(std::memory_order_acquire);
        while (current != 0)
        {
            value.wait(current, std::memory_order_acquire);
            current = value.load(std::memory_order_acquire);
        }
    }

    static void LeaveActive(std::atomic<std::uint32_t>& value)
    {
        if (value.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            value.notify_all();
        }
    }

    void PruneTargetsLocked()
    {
        for (auto iterator = targets_.begin(); iterator != targets_.end();)
        {
            const auto& target = iterator->second;
            if (!target->transition && target->leases.empty() && target->callbacks.empty())
            {
                targets_by_key_.erase(target->key);
                retired_targets_.push_back(target);
                iterator = targets_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }

    void CollectPhysical()
    {
        if (target_depth_ != 0)
        {
            return;
        }
        std::vector<std::shared_ptr<TargetRecord>> candidates;
        {
            std::scoped_lock lock(registry_mutex_);
            for (const auto& [handle, target] : targets_)
            {
                static_cast<void>(handle);
                if (!target->transition && target->callbacks.empty())
                {
                    candidates.push_back(target);
                }
            }
            candidates.insert(candidates.end(), retired_targets_.begin(), retired_targets_.end());
            std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
                return left->handle < right->handle;
            });
        }
        for (const auto& target : candidates)
        {
            if (target->active.load(std::memory_order_acquire) == 0)
            {
                DestroyPhysical(*target);
            }
        }
        std::scoped_lock lock(registry_mutex_);
        retired_targets_.erase(
            std::remove_if(retired_targets_.begin(), retired_targets_.end(), [](const auto& target) {
                std::scoped_lock physical_lock(target->physical_mutex);
                return !target->hook && !target->virtual_hook &&
                    !target->instance_enabled && !target->closure;
            }),
            retired_targets_.end());
        for (auto iterator = instance_tables_.begin(); iterator != instance_tables_.end();)
        {
            const auto& record = iterator->second;
            const bool active = std::any_of(targets_.begin(), targets_.end(), [&](const auto& entry) {
                return entry.second->instance_table == record;
            });
            const bool retired = std::any_of(
                retired_targets_.begin(),
                retired_targets_.end(),
                [&](const auto& target) { return target->instance_table == record; });
            if (!active && !retired && record->table->Empty())
            {
                iterator = instance_tables_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }

    bool DestroyPhysical(TargetRecord& target)
    {
        std::scoped_lock lock(target.physical_mutex);
        if (target.active.load(std::memory_order_acquire) != 0 ||
            PhysicalEnabled(target))
        {
            return false;
        }
        if (target.hook)
        {
            const auto hazards = PhysicalHazards(target);
            const auto quiesced = target.hook->quiesce(hazards);
            if (!quiesced)
            {
                if (!target.quiescence_failure_reported)
                {
                    Log("native callback retirement is waiting for a safe thread state");
                    target.quiescence_failure_reported = true;
                }
                return false;
            }
        }
        else if (target.closure)
        {
            const auto hazards = PhysicalHazards(target);
            const std::span<const safetyhook::IpMapping> mappings;
            if (!safetyhook::trap_threads(
                    reinterpret_cast<std::uint8_t*>(target.address),
                    1,
                    mappings,
                    hazards,
                    [] {}))
            {
                if (!target.quiescence_failure_reported)
                {
                    Log("native callback retirement is waiting for a safe thread state");
                    target.quiescence_failure_reported = true;
                }
                return false;
            }
        }
        target.quiescence_failure_reported = false;
        target.trampoline.store(nullptr, std::memory_order_release);
        target.hook.reset();
        target.virtual_hook.reset();
        target.instance_enabled = false;
        if (target.closure)
        {
            dcbFreeCallback(target.closure);
            target.closure = nullptr;
        }
        return true;
    }

    std::vector<safetyhook::IpRange> PhysicalHazards(const TargetRecord& target) const
    {
        std::vector<safetyhook::IpRange> hazards = entry_hazards_;
        if (target.closure)
        {
            hazards.push_back({reinterpret_cast<std::uint8_t*>(target.closure), 24});
        }
        return hazards;
    }

    void Log(std::string_view message) const noexcept
    {
        try
        {
            service_.Log(KEEL_LOG_ERROR, "KeelHook: " + std::string(message));
        }
        catch (...)
        {
        }
    }

    KeelHookService& service_;
    KeelHookApi api_{};
    mutable std::mutex registry_mutex_;
    bool shutting_down_{};
    bool shutdown_complete_{};
    KeelHookTargetHandle next_target_{1};
    KeelHookCallbackHandle next_callback_{1};
    std::uint64_t next_sequence_{1};
    std::unordered_map<KeelPluginHandle, OwnerState> owners_;
    std::unordered_map<KeelHookTargetHandle, std::shared_ptr<TargetRecord>> targets_;
    std::unordered_map<TargetKey, std::shared_ptr<TargetRecord>, TargetKeyHash> targets_by_key_;
    std::unordered_map<void*, std::shared_ptr<InstanceTableRecord>> instance_tables_;
    std::unordered_map<KeelHookCallbackHandle, std::shared_ptr<CallbackRecord>> callbacks_;
    std::vector<std::shared_ptr<TargetRecord>> retired_targets_;
    std::vector<safetyhook::IpRange> entry_hazards_;
    std::string service_profile_;

    inline static std::atomic<Implementation*> active_{};
    inline static thread_local std::array<const CallbackRecord*, 128> callback_stack_{};
    inline static thread_local std::size_t callback_depth_{};
    inline static thread_local std::array<const TargetRecord*, 64> target_stack_{};
    inline static thread_local std::size_t target_depth_{};
};

KeelHookService::KeelHookService(Host& host)
    : host_(host),
      implementation_(std::make_unique<Implementation>(*this, host.compatibility_profile_))
{
}

KeelHookService::~KeelHookService() = default;

const KeelHookApi& KeelHookService::Api() const noexcept
{
    return implementation_->Api();
}

void KeelHookService::Authorize(
    KeelPluginHandle plugin,
    const std::filesystem::path& path,
    bool active)
{
    implementation_->Authorize(plugin, path, active);
}

void KeelHookService::Activate(KeelPluginHandle plugin)
{
    implementation_->Activate(plugin);
}

KeelResult KeelHookService::Deactivate(KeelPluginHandle plugin)
{
    return implementation_->Deactivate(plugin);
}

KeelResult KeelHookService::ReleasePlugin(KeelPluginHandle plugin)
{
    return implementation_->ReleasePlugin(plugin);
}

bool KeelHookService::Shutdown()
{
    return implementation_->Shutdown();
}

void KeelHookService::Log(KeelLogLevel level, const std::string& message)
{
    host_.Write(level, message);
}

}
