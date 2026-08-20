#include <keels2/hooking/vtable.h>

#include <safetyhook/common.hpp>
#include <safetyhook/os.hpp>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace keels2::hooking
{

namespace
{

constexpr std::size_t HeaderCount()
{
#if SAFETYHOOK_ABI_MSVC
    return 1;
#elif SAFETYHOOK_ABI_ITANIUM
    return 2;
#else
    return 0;
#endif
}

bool Aligned(const void* address)
{
    return reinterpret_cast<std::uintptr_t>(address) % alignof(void*) == 0;
}

bool Accessible(const void* address, std::size_t size, bool write)
{
    if (!address || size == 0)
    {
        return false;
    }
#if SAFETYHOOK_OS_WINDOWS
    return write
        ? safetyhook::vm_is_writable(
            reinterpret_cast<std::uint8_t*>(const_cast<void*>(address)),
            size)
        : safetyhook::vm_is_readable(
            reinterpret_cast<std::uint8_t*>(const_cast<void*>(address)),
            size);
#else
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    if (size > std::numeric_limits<std::uintptr_t>::max() - begin)
    {
        return false;
    }
    const auto end = begin + size;
    for (auto cursor = begin; cursor < end;)
    {
        const auto info = safetyhook::vm_query(reinterpret_cast<std::uint8_t*>(cursor));
        if (!info || info->is_free || !info->access.read || (write && !info->access.write))
        {
            return false;
        }
        const auto region = reinterpret_cast<std::uintptr_t>(info->address);
        if (info->size > std::numeric_limits<std::uintptr_t>::max() - region)
        {
            return false;
        }
        const auto next = region + info->size;
        if (cursor < region || next <= cursor)
        {
            return false;
        }
        cursor = next < end ? next : end;
    }
    return true;
#endif
}

VtableHookResult Exchange(void** location, void* expected, void* desired, bool protect)
{
    if (!location || !Aligned(location) || !Accessible(location, sizeof(void*), !protect))
    {
        return VtableHookResult::invalid;
    }

    std::uint32_t old_protection{};
    if (protect)
    {
        auto info = safetyhook::vm_query(reinterpret_cast<std::uint8_t*>(location));
        if (!info || info->is_free || !info->access.read)
        {
            return VtableHookResult::invalid;
        }
        auto access = info->access;
        access.write = true;
        const auto changed = safetyhook::vm_protect(
            reinterpret_cast<std::uint8_t*>(location),
            sizeof(void*),
            access);
        if (!changed)
        {
            return VtableHookResult::protection_failure;
        }
        old_protection = *changed;
    }

    std::atomic_ref<void*> cell(*location);
    void* observed = expected;
    const bool exchanged = cell.compare_exchange_strong(
        observed,
        desired,
        std::memory_order_acq_rel,
        std::memory_order_acquire);

    if (protect)
    {
        const auto restored = safetyhook::vm_protect(
            reinterpret_cast<std::uint8_t*>(location),
            sizeof(void*),
            old_protection);
        if (!restored)
        {
            if (exchanged)
            {
                void* rollback = desired;
                cell.compare_exchange_strong(
                    rollback,
                    expected,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire);
                static_cast<void>(safetyhook::vm_protect(
                    reinterpret_cast<std::uint8_t*>(location),
                    sizeof(void*),
                    old_protection));
            }
            return VtableHookResult::protection_failure;
        }
    }
    return exchanged ? VtableHookResult::ok : VtableHookResult::conflict;
}

void* Load(void** location)
{
    std::atomic_ref<void*> cell(*location);
    return cell.load(std::memory_order_acquire);
}

}

VtableHookResult ResolveVtableSlot(
    void* instance,
    std::size_t index,
    void**& slot,
    void*& target) noexcept
{
    slot = nullptr;
    target = nullptr;
    if (!instance || !Aligned(instance) || !Accessible(instance, sizeof(void*), false) ||
        index > std::numeric_limits<std::size_t>::max() / sizeof(void*))
    {
        return VtableHookResult::invalid;
    }
    auto** table = static_cast<void**>(Load(static_cast<void**>(instance)));
    if (!table || reinterpret_cast<std::uintptr_t>(table) >
            std::numeric_limits<std::uintptr_t>::max() - index * sizeof(void*))
    {
        return VtableHookResult::invalid;
    }
    slot = table + index;
    if (!Aligned(slot) || !Accessible(slot, sizeof(void*), false))
    {
        slot = nullptr;
        return VtableHookResult::invalid;
    }
    target = Load(slot);
    if (!target)
    {
        slot = nullptr;
        return VtableHookResult::invalid;
    }
    return VtableHookResult::ok;
}

SharedVtableHook::SharedVtableHook(void** slot, void* original, void* replacement) noexcept
    : slot_(slot), original_(original), replacement_(replacement)
{
}

VtableHookResult SharedVtableHook::Create(
    void** slot,
    void* replacement,
    std::unique_ptr<SharedVtableHook>& output)
{
    output.reset();
    if (!slot || !replacement || !Aligned(slot) || !Accessible(slot, sizeof(void*), false))
    {
        return VtableHookResult::invalid;
    }
    void* original = Load(slot);
    if (!original)
    {
        return VtableHookResult::invalid;
    }
    output.reset(new SharedVtableHook(slot, original, replacement));
    return VtableHookResult::ok;
}

SharedVtableHook::~SharedVtableHook()
{
    static_cast<void>(Disable());
}

VtableHookResult SharedVtableHook::Enable() noexcept
{
    if (enabled_)
    {
        return VtableHookResult::ok;
    }
    const auto result = Exchange(slot_, original_, replacement_, true);
    if (result == VtableHookResult::ok)
    {
        enabled_ = true;
    }
    return result;
}

VtableHookResult SharedVtableHook::Disable() noexcept
{
    if (!enabled_)
    {
        return VtableHookResult::ok;
    }
    const auto result = Exchange(slot_, replacement_, original_, true);
    if (result == VtableHookResult::ok)
    {
        enabled_ = false;
    }
    return result;
}

bool SharedVtableHook::Enabled() const noexcept
{
    return enabled_;
}

void* SharedVtableHook::Original() const noexcept
{
    return original_;
}

void** SharedVtableHook::Slot() const noexcept
{
    return slot_;
}

InstanceVtable::InstanceVtable(
    void* instance,
    void** original,
    std::size_t entry_count,
    std::size_t header_count,
    std::unique_ptr<void*[]> storage) noexcept
    : instance_(instance),
      original_(original),
      shadow_(storage.get() + header_count),
      entry_count_(entry_count),
      storage_(std::move(storage)),
      replacements_(new (std::nothrow) void*[entry_count]{})
{
}

VtableHookResult InstanceVtable::Create(
    void* instance,
    std::size_t entry_count,
    std::shared_ptr<InstanceVtable>& output)
{
    output.reset();
    if (!instance || entry_count == 0 || !Aligned(instance) ||
        !Accessible(instance, sizeof(void*), true))
    {
        return VtableHookResult::invalid;
    }
    auto** original = static_cast<void**>(Load(static_cast<void**>(instance)));
    const std::size_t header_count = HeaderCount();
    if (!original || reinterpret_cast<std::uintptr_t>(original) < header_count * sizeof(void*))
    {
        return VtableHookResult::invalid;
    }
    if (entry_count > std::numeric_limits<std::size_t>::max() - header_count)
    {
        return VtableHookResult::invalid;
    }
    const std::size_t total = entry_count + header_count;
    if (total > std::numeric_limits<std::size_t>::max() / sizeof(void*))
    {
        return VtableHookResult::invalid;
    }
    void** begin = original - header_count;
    if (!Aligned(begin) || !Accessible(begin, total * sizeof(void*), false))
    {
        return VtableHookResult::invalid;
    }
    auto storage = std::unique_ptr<void*[]>(new (std::nothrow) void*[total]);
    if (!storage)
    {
        return VtableHookResult::protection_failure;
    }
    std::memcpy(storage.get(), begin, total * sizeof(void*));
    auto created = std::shared_ptr<InstanceVtable>(new InstanceVtable(
        instance,
        original,
        entry_count,
        header_count,
        std::move(storage)));
    if (!created->replacements_)
    {
        return VtableHookResult::protection_failure;
    }
    output = std::move(created);
    return VtableHookResult::ok;
}

InstanceVtable::~InstanceVtable()
{
    if (Restore() != VtableHookResult::ok && Applied())
    {
        static_cast<void>(storage_.release());
    }
}

VtableHookResult InstanceVtable::Enable(std::size_t index, void* replacement) noexcept
{
    std::scoped_lock lock(mutex_);
    if (index >= entry_count_ || !replacement || replacements_[index])
    {
        return VtableHookResult::invalid;
    }
    if (!Accessible(instance_, sizeof(void*), true))
    {
        return VtableHookResult::invalid;
    }
    auto** object = static_cast<void**>(instance_);
    if (enabled_count_ == 0)
    {
        shadow_[index] = replacement;
        const auto applied = Exchange(object, original_, shadow_, false);
        if (applied != VtableHookResult::ok)
        {
            shadow_[index] = original_[index];
            return applied;
        }
    }
    else
    {
        if (Load(object) != shadow_)
        {
            return VtableHookResult::conflict;
        }
        const auto replaced = Exchange(&shadow_[index], original_[index], replacement, false);
        if (replaced != VtableHookResult::ok)
        {
            return replaced;
        }
    }
    replacements_[index] = replacement;
    ++enabled_count_;
    return VtableHookResult::ok;
}

VtableHookResult InstanceVtable::Disable(std::size_t index, void* replacement) noexcept
{
    std::scoped_lock lock(mutex_);
    if (index >= entry_count_ || !replacement || replacements_[index] != replacement)
    {
        return VtableHookResult::invalid;
    }
    if (!Accessible(instance_, sizeof(void*), true))
    {
        return VtableHookResult::invalid;
    }
    auto** object = static_cast<void**>(instance_);
    if (Load(object) != shadow_)
    {
        return VtableHookResult::conflict;
    }
    const auto restored = Exchange(&shadow_[index], replacement, original_[index], false);
    if (restored != VtableHookResult::ok)
    {
        return restored;
    }
    if (enabled_count_ == 1)
    {
        const auto removed = Exchange(object, shadow_, original_, false);
        if (removed != VtableHookResult::ok)
        {
            static_cast<void>(Exchange(&shadow_[index], original_[index], replacement, false));
            return removed;
        }
    }
    replacements_[index] = nullptr;
    --enabled_count_;
    return VtableHookResult::ok;
}

VtableHookResult InstanceVtable::Restore() noexcept
{
    std::scoped_lock lock(mutex_);
    if (enabled_count_ == 0)
    {
        return VtableHookResult::ok;
    }
    if (!Accessible(instance_, sizeof(void*), true))
    {
        return VtableHookResult::invalid;
    }
    auto** object = static_cast<void**>(instance_);
    const auto restored = Exchange(object, shadow_, original_, false);
    if (restored != VtableHookResult::ok)
    {
        return restored;
    }
    for (std::size_t index{}; index < entry_count_; ++index)
    {
        shadow_[index] = original_[index];
        replacements_[index] = nullptr;
    }
    enabled_count_ = 0;
    return VtableHookResult::ok;
}

bool InstanceVtable::Applied() const noexcept
{
    std::scoped_lock lock(mutex_);
    return enabled_count_ != 0;
}

bool InstanceVtable::Empty() const noexcept
{
    std::scoped_lock lock(mutex_);
    return enabled_count_ == 0;
}

bool InstanceVtable::Intact() const noexcept
{
    std::scoped_lock lock(mutex_);
    if (!Accessible(instance_, sizeof(void*), false))
    {
        return false;
    }
    auto** object = static_cast<void**>(instance_);
    return Load(object) == (enabled_count_ == 0 ? original_ : shadow_);
}

std::size_t InstanceVtable::EntryCount() const noexcept
{
    return entry_count_;
}

void* InstanceVtable::Instance() const noexcept
{
    return instance_;
}

void* InstanceVtable::Original(std::size_t index) const noexcept
{
    return index < entry_count_ ? original_[index] : nullptr;
}

void** InstanceVtable::OriginalSlot(std::size_t index) const noexcept
{
    return index < entry_count_ ? &original_[index] : nullptr;
}

}
