#include <keels2/source2_hooks.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <type_traits>

namespace docs
{
struct Coordinates
{
    std::int32_t x;
    float y;
};

struct Sample
{
    Coordinates coordinates;
    std::uint64_t markers[2];
};

struct Score
{
    std::int32_t points;
};

// Owning storage makes the distinction from a plain byte aggregate visible.
class OwnedNumber
{
public:
    OwnedNumber() : OwnedNumber(0) {}

    explicit OwnedNumber(int value) : number(std::make_unique<int>(value))
    {
        ++live;
    }

    OwnedNumber(const OwnedNumber& other) : OwnedNumber(other.Get()) {}

    OwnedNumber& operator=(const OwnedNumber& other) noexcept
    {
        *number = other.Get();
        return *this;
    }

    ~OwnedNumber() noexcept
    {
        --live;
    }

    int Get() const noexcept
    {
        return *number;
    }

    static inline int live{};

private:
    std::unique_ptr<int> number;
};
}

template <>
struct keels2::kh::AggregateTraits<docs::Coordinates>
{
    static consteval auto Fields()
    {
        return keels2::kh::Fields(
            keels2::kh::Field<std::int32_t>(offsetof(docs::Coordinates, x)),
            keels2::kh::Field<float>(offsetof(docs::Coordinates, y)));
    }
};

template <>
struct keels2::kh::AggregateTraits<docs::Sample>
{
    static consteval auto Fields()
    {
        return keels2::kh::Fields(
            keels2::kh::Field<docs::Coordinates>(offsetof(docs::Sample, coordinates)),
            keels2::kh::Field<std::uint64_t>(offsetof(docs::Sample, markers), 2));
    }
};

// An adapter explicitly maps a wrapper to its native scalar representation.
// The checked kh::Read/Write entry points validate the tag before calling it.
template <>
struct keels2::kh::ValueAdapter<docs::Score>
{
    static constexpr KeelHookValueType type = KH_VALUE_INT32;
    static docs::Score Read(const KeelHookValue& value) noexcept
    { return {value.scalar.int32}; }
    static docs::Score Fallback() noexcept
    {
        return {-1};
    }

    static bool Write(KeelHookValue& value, const docs::Score& input) noexcept
    {
        value.scalar.int32 = input.points;
        value.reserved = 0;
        return true;
    }
};

namespace docs
{
namespace kh = keels2::kh;

static_assert(kh::DescribedAggregate<Sample>);
static_assert(kh::ManagedObject<OwnedNumber>);
static_assert(kh::AdaptedValue<Score> && kh::AdaptedValue<CPlayerSlot>);
static_assert(kh::ValueTypeV<const Sample> == KH_VALUE_AGGREGATE);
static_assert(kh::ValueTypeV<Sample&> == KH_VALUE_POINTER);
static_assert(kh::ValueTypeV<OwnedNumber> == KH_VALUE_AGGREGATE);
static_assert(kh::ValueTypeV<Score> == KH_VALUE_INT32);
static_assert(kh::IntegralValueType<std::uint64_t>() == KH_VALUE_UINT64);
static_assert(kh::AggregateDescriptor<Sample&>() == nullptr);
static_assert(kh::ObjectDescriptor<OwnedNumber&>() == nullptr);
static_assert(kh::AggregateMetadata<Sample>::ValidFields());

bool AggregateValues()
{
    constexpr const auto* descriptor = kh::AggregateDescriptor<Sample>();
    static_assert(descriptor->fields[1].array_length == 2);
    static_assert(kh::AggregateMetadata<Sample>::fields.element_sizes[1] == sizeof(std::uint64_t));
    Sample storage{{7, 2.5F}, {10, 20}};
    KeelHookValue value{};
    value.type = kh::ValueTypeV<Sample>;
    value.scalar.aggregate = {&storage, sizeof(storage), 0};

    if (!kh::ValidValue<Sample>(value))
        return false;

    auto copy = kh::Read<Sample>(value);
    copy.coordinates.x += 3;

    if (!kh::Write(value, copy) || storage.coordinates.x != 10)
        return false;

    KeelHookValue reference{};
    reference.type = KH_VALUE_POINTER;

    if (!kh::WriteReference<std::int32_t&>(reference, storage.coordinates.x))
        return false;

    auto* borrowed = kh::ReadReference<std::int32_t&>(reference);

    if (!borrowed)
        return false;

    ++*borrowed;
    return storage.coordinates.x == 11 && storage.coordinates.y == 2.5F && storage.markers[1] == 20;
}

bool AdaptedValues()
{
    KeelHookValue value{};
    value.type = kh::ValueTypeV<Score>;

    if (!kh::Write(value, Score{12}) || kh::Read<Score>(value).points != 12)
        return false;

    value.type = KH_VALUE_FLOAT32;

    if (kh::ValidValue<Score>(value) || kh::Write(value, Score{3}) ||
        kh::Read<Score>(value).points != -1) return false;

    value.type = kh::ValueTypeV<CPlayerSlot>;

    if (!kh::Write(value, CPlayerSlot(2)) || kh::Read<CPlayerSlot>(value).Get() != 2)
        return false;

    if (!kh::Write(value, CSplitScreenSlot(1)) || kh::Read<CSplitScreenSlot>(value).Get() != 1)
        return false;

    value.reserved = 1;
    return kh::Read<CPlayerSlot>(value).Get() == -1 && kh::Read<CSplitScreenSlot>(value).Get() == -1;
}

bool CommandReferences()
{
    static_assert(kh::AdaptedValue<ConCommandRef>);
    static_assert(kh::ValueTypeV<ConCommandRef> == KH_VALUE_UINT64);
    KeelHookValue value{};
    value.type = kh::ValueTypeV<ConCommandRef>;
    const ConCommandRef reference(uint16(0x1234), 0x76543210);

    if (!kh::Write(value, reference) || value.scalar.uint64 != UINT64_C(0x7654321000001234))
        return false;

    const auto copied = kh::Read<ConCommandRef>(value);

    if (!copied.IsValidRef() || copied.GetAccessIndex() != 0x1234 ||
        copied.GetRegisteredIndex() != 0x76543210) return false;

    if (!kh::Write(value, ConCommandRef(uint16(7), -12345)) ||
        kh::Read<ConCommandRef>(value).GetRegisteredIndex() != -12345) return false;

    value.type = KH_VALUE_INT32;

    if (kh::ValidValue<ConCommandRef>(value) || kh::Write(value, reference) ||
        kh::Read<ConCommandRef>(value).IsValidRef()) return false;

    value.type = KH_VALUE_UINT64;
    value.reserved = 1;

    if (kh::ValidValue<ConCommandRef>(value) || kh::Write(value, reference) ||
        kh::Read<ConCommandRef>(value).IsValidRef()) return false;

    value.reserved = 0;
    return kh::Write(value, ConCommandRef{}) &&
        kh::ValidValue<ConCommandRef>(value) && !kh::Read<ConCommandRef>(value).IsValidRef();
}

// This helper owns raw storage for the standalone program. Real callbacks use
// the host's supplied storage and must not destroy host-owned frame objects.
class ObjectBuffer
{
public:
    ObjectBuffer()
    {
        value.type = KH_VALUE_AGGREGATE;
        value.scalar.aggregate = {storage.data(), sizeof(OwnedNumber), 0};
    }

    ObjectBuffer(const ObjectBuffer&) = delete;
    ObjectBuffer& operator=(const ObjectBuffer&) = delete;

    ~ObjectBuffer()
    {
        if (value.scalar.aggregate.reserved == KH_VALUE_OBJECT_CONSTRUCTED)
            kh::ObjectMetadata<OwnedNumber>::Destroy(value.scalar.aggregate.data);
    }

    KeelHookValue value{};

private:
    alignas(OwnedNumber) std::array<std::byte, sizeof(OwnedNumber)> storage{};
};

bool ObjectValues()
{
    constexpr const auto* operations = kh::ObjectDescriptor<OwnedNumber>();
    static_assert(operations->default_construct == &kh::ObjectMetadata<OwnedNumber>::DefaultConstruct);
    static_assert(operations->copy_construct == &kh::ObjectMetadata<OwnedNumber>::CopyConstruct);
    static_assert(operations->copy_assign == &kh::ObjectMetadata<OwnedNumber>::CopyAssign);
    OwnedNumber source(42);
    ObjectBuffer written, defaulted, copied;

    if (kh::ValidValue<OwnedNumber>(written.value))
        return false;
    // Write constructs empty storage and marks it, then assigns on later writes.
    if (!kh::Write(written.value, source) || !kh::ValidValue<OwnedNumber>(written.value))
        return false;

    if (!kh::Write(written.value, OwnedNumber(7)))
        return false;

    auto snapshot = kh::Read<OwnedNumber>(written.value);

    if (snapshot.Get() != 7)
        return false;

    if (!operations->default_construct(defaulted.value.scalar.aggregate.data))
        return false;

    defaulted.value.scalar.aggregate.reserved = KH_VALUE_OBJECT_CONSTRUCTED;

    if (!operations->copy_construct(copied.value.scalar.aggregate.data, &source))
        return false;

    copied.value.scalar.aggregate.reserved = KH_VALUE_OBJECT_CONSTRUCTED;

    if (!operations->copy_assign(defaulted.value.scalar.aggregate.data, &source))
        return false;

    return kh::Read<OwnedNumber>(defaulted.value).Get() == 42 &&
        kh::Read<OwnedNumber>(copied.value).Get() == 42;
}

class Primary
{
public:
    virtual int First(int value)
    {
        return value + 1;
    }

    int NonVirtual(int value)
    {
        return value + 2;
    }
};

class Secondary
{
public:
    virtual int Second(int value)
    {
        return value * 2;
    }
};

class Combined : public Primary, public Secondary {};

struct Observer
{
    // This callback body is compiled and its signature checked below. The
    // standalone executable does not register it with a host service.
    PluginResult Before(kh::Call<int>& call, int& value)
    {
        const KeelHookFrame* borrowed = call.Raw();
        last_target = borrowed ? borrowed->target : 0; // Copy the ID, never retain the frame.
        if (call.CurrentPhase() == kh::Phase::Pre)
            ++value;

        return plugin_continue;
    }

    void Wrong(double) {}

    KeelHookTargetHandle last_target{}; // An observation, not an owned target registration.
};
static_assert(std::is_same_v<kh::MethodClass<&Primary::First>, Primary>);
static_assert(std::is_same_v<kh::MethodSignature<&Primary::First>, int(int)>);
static_assert(std::is_same_v<kh::MethodClassOf<decltype(&Primary::First)>, Primary>);
static_assert(std::is_same_v<kh::MethodSignatureOf<decltype(&Primary::First)>, int(int)>);
static_assert(kh::CompatibleCallback<int(int), &Observer::Before>);
static_assert(kh::CompatibleMethodCallback<&Primary::First, &Observer::Before>);
static_assert(kh::CompatibleMethods<decltype(&Primary::First), decltype(&Observer::Before)>);
static_assert(!kh::CompatibleCallback<int(int), &Observer::Wrong>);

bool VirtualMembers()
{
    const auto first = kh::VirtualInfo<&Primary::First>();
    const auto index = kh::VirtualIndex<&Primary::First>();

    if (!first || !index || first->index != *index || *index != 0 ||
        kh::VirtualInfo<&Primary::NonVirtual>()) return false;

    Combined instance;
    const auto method = static_cast<int (Combined::*)(int)>(&Secondary::Second);
    const auto adjusted = kh::VirtualInfo(method);
    const auto adjusted_index = kh::VirtualIndex(method);
    const auto expected_adjustment = reinterpret_cast<std::uintptr_t>(static_cast<Secondary*>(&instance))
        - reinterpret_cast<std::uintptr_t>(&instance);

    return adjusted && adjusted_index && adjusted->index == *adjusted_index &&
        adjusted->this_adjustment == static_cast<std::int64_t>(expected_adjustment) &&
        adjusted->vtable_offset == 0 && (instance.*method)(7) == 14;
}
}

int main()
{
    if (!docs::AggregateValues())
    {
        std::fputs("Aggregate value example failed.\n", stderr);
        return 1;
    }

    if (!docs::AdaptedValues())
    {
        std::fputs("Adapted value example failed.\n", stderr);
        return 1;
    }

    if (!docs::CommandReferences())
    {
        std::fputs("Command reference example failed.\n", stderr);
        return 1;
    }

    if (!docs::ObjectValues() || docs::OwnedNumber::live != 0)
    {
        std::fputs("Object lifetime example failed.\n", stderr);
        return 1;
    }

    if (!docs::VirtualMembers())
    {
        std::fputs("Virtual member example failed.\n", stderr);
        return 1;
    }

    std::puts("KeelHook C++ values, object lifetimes and virtual members passed.");
    return 0;
}
