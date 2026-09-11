#ifndef KEELS2_SOURCE2_HOOKS_HPP
#define KEELS2_SOURCE2_HOOKS_HPP

#include <keels2/keelhook.hpp>
#include <keels2/source2_sdk.hpp>

#include <bit>
#include <cstdint>
#include <type_traits>

namespace keels2::kh
{

template <>
struct ValueAdapter<ConCommandRef>
{
    static_assert(sizeof(ConCommandRef) == sizeof(std::uint64_t));
    static_assert(alignof(ConCommandRef) == alignof(std::int32_t));
    static_assert(std::is_standard_layout_v<ConCommandRef>);
    static_assert(std::is_trivially_copyable_v<ConCommandRef>);

    static constexpr KeelHookValueType type = KH_VALUE_UINT64;

    static ConCommandRef Read(const KeelHookValue& value) noexcept
    {
        return std::bit_cast<ConCommandRef>(value.scalar.uint64);
    }

    static ConCommandRef Fallback() noexcept
    {
        return {};
    }

    static bool Write(KeelHookValue& value, const ConCommandRef& input) noexcept
    {
        value.scalar.uint64 = std::bit_cast<std::uint64_t>(input);
        value.reserved = 0;
        return true;
    }
};

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
