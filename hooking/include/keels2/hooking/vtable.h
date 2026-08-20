#ifndef KEELS2_HOOKING_VTABLE_H
#define KEELS2_HOOKING_VTABLE_H

#include <cstddef>
#include <memory>
#include <mutex>

namespace keels2::hooking
{

enum class VtableHookResult
{
    ok,
    invalid,
    conflict,
    protection_failure
};

VtableHookResult ResolveVtableSlot(
    void* instance,
    std::size_t index,
    void**& slot,
    void*& target) noexcept;

class SharedVtableHook final
{
public:
    static VtableHookResult Create(
        void** slot,
        void* replacement,
        std::unique_ptr<SharedVtableHook>& output);

    ~SharedVtableHook();
    SharedVtableHook(const SharedVtableHook&) = delete;
    SharedVtableHook& operator=(const SharedVtableHook&) = delete;

    VtableHookResult Enable() noexcept;
    VtableHookResult Disable() noexcept;
    bool Enabled() const noexcept;
    void* Original() const noexcept;
    void** Slot() const noexcept;

private:
    SharedVtableHook(void** slot, void* original, void* replacement) noexcept;

    void** slot_{};
    void* original_{};
    void* replacement_{};
    bool enabled_{};
};

class InstanceVtable final
{
public:
    static VtableHookResult Create(
        void* instance,
        std::size_t entry_count,
        std::shared_ptr<InstanceVtable>& output);

    ~InstanceVtable();
    InstanceVtable(const InstanceVtable&) = delete;
    InstanceVtable& operator=(const InstanceVtable&) = delete;

    VtableHookResult Enable(std::size_t index, void* replacement) noexcept;
    VtableHookResult Disable(std::size_t index, void* replacement) noexcept;
    bool Applied() const noexcept;
    bool Empty() const noexcept;
    bool Intact() const noexcept;
    std::size_t EntryCount() const noexcept;
    void* Instance() const noexcept;
    void* Original(std::size_t index) const noexcept;
    void** OriginalSlot(std::size_t index) const noexcept;

private:
    InstanceVtable(
        void* instance,
        void** original,
        std::size_t entry_count,
        std::size_t header_count,
        std::unique_ptr<void*[]> storage) noexcept;

    VtableHookResult Restore() noexcept;

    void* instance_{};
    void** original_{};
    void** shadow_{};
    std::size_t entry_count_{};
    std::unique_ptr<void*[]> storage_;
    std::unique_ptr<void*[]> replacements_;
    std::size_t enabled_count_{};
    mutable std::mutex mutex_;
};

}

#endif
