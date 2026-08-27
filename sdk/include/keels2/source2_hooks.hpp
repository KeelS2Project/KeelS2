#ifndef KEELS2_SOURCE2_HOOKS_HPP
#define KEELS2_SOURCE2_HOOKS_HPP

#include <keels2/keelhook.hpp>
#include <keels2/source2_sdk.hpp>

#include <cstdint>
#include <type_traits>

namespace keels2::kh
{

template <>
struct ValueAdapter<CPlayerSlot>
{
    static_assert(sizeof(CPlayerSlot) == sizeof(std::int32_t));
    static_assert(std::is_standard_layout_v<CPlayerSlot>);
    static_assert(std::is_trivially_copyable_v<CPlayerSlot>);

    static constexpr KeelHookValueType type = KH_VALUE_INT32;

    static CPlayerSlot Read(const KeelHookValue& value) noexcept
    {
        return CPlayerSlot(value.scalar.int32);
    }

    static CPlayerSlot Fallback() noexcept
    {
        return CPlayerSlot(-1);
    }

    static bool Write(KeelHookValue& value, const CPlayerSlot& input) noexcept
    {
        value.scalar.int32 = input.Get();
        value.reserved = 0;
        return true;
    }
};

template <>
struct ValueAdapter<CSplitScreenSlot>
{
    static_assert(sizeof(CSplitScreenSlot) == sizeof(std::int32_t));
    static_assert(std::is_standard_layout_v<CSplitScreenSlot>);
    static_assert(std::is_trivially_copyable_v<CSplitScreenSlot>);

    static constexpr KeelHookValueType type = KH_VALUE_INT32;

    static CSplitScreenSlot Read(const KeelHookValue& value) noexcept
    {
        return CSplitScreenSlot(value.scalar.int32);
    }

    static CSplitScreenSlot Fallback() noexcept
    {
        return CSplitScreenSlot(-1);
    }

    static bool Write(KeelHookValue& value, const CSplitScreenSlot& input) noexcept
    {
        value.scalar.int32 = input.Get();
        value.reserved = 0;
        return true;
    }
};

}

#endif
