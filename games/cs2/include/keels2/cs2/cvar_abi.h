#ifndef KEELS2_CS2_CVAR_ABI_H
#define KEELS2_CS2_CVAR_ABI_H

#include <cstddef>
#include <cstdint>

namespace keels2::cs2
{

inline constexpr const char* kCvarInterfaceVersion = "VEngineCvar007";
inline constexpr std::uint64_t kReleaseCommandFlag = 1ull << 19;
inline constexpr std::uint64_t kReferenceFlag = 1ull << 10;
inline constexpr std::uint64_t kPerUserFlag = 1ull << 15;
inline constexpr std::uint64_t kPerformingCallbacksFlag = 1ull << 18;
inline constexpr std::uint32_t kFindConVarSlot = 11;
inline constexpr std::uint32_t kCallChangeCallbackSlot = 14;
inline constexpr std::uint32_t kCallFilterCallbackSlot = 16;
inline constexpr std::uint32_t kCallGlobalChangeCallbacksSlot = 23;
inline constexpr std::uint32_t kRegisterConVarSlot = 38;
inline constexpr std::uint32_t kUnregisterConVarSlot = 39;
inline constexpr std::uint32_t kGetConVarDataSlot = 41;
inline constexpr std::uint32_t kRegisterConCommandSlot = 42;
inline constexpr std::uint32_t kUnregisterConCommandSlot = 43;
inline constexpr std::uint32_t kQueueThreadSetValueSlot = 45;
inline constexpr std::uint32_t kCommandSize = 1616;
inline constexpr std::uint32_t kCommandArgumentCountOffset = 1080;
inline constexpr std::uint32_t kCommandArgumentValuesOffset = 1088;
inline constexpr std::uint32_t kCommandMaximumArguments = 64;
inline constexpr std::uint32_t kConVarDataTypeOffset = 40;
inline constexpr std::uint32_t kConVarDataFlagsOffset = 48;
inline constexpr std::uint32_t kConVarDataCustomDataOffset = 80;
inline constexpr std::uint32_t kConVarDataValueOffset = 88;
inline constexpr std::uint32_t kConVarObjectDataOffset = 8;
inline constexpr std::uint32_t kConVarValueInfoChangeProviderOffset = 56;
inline constexpr std::uint32_t kConVarValueInfoCustomDataOffset = 88;

enum class ConVarType : std::int16_t
{
    invalid = -1,
    boolean = 0,
    int16 = 1,
    uint16 = 2,
    int32 = 3,
    uint32 = 4,
    int64 = 5,
    uint64 = 6,
    float32 = 7,
    float64 = 8,
    string = 9,
    color = 10,
    vector2 = 11,
    vector3 = 12,
    vector4 = 13,
    qangle = 14,
    vector_world_space = 15,
    maximum = 16
};

union alignas(8) ConVarValue
{
    bool boolean;
    std::int16_t int16;
    std::uint16_t uint16;
    std::int32_t int32;
    std::uint32_t uint32;
    std::int64_t int64;
    std::uint64_t uint64;
    float float32;
    double float64;
    char* string;
    std::byte storage[16];
};

class ICommandCallback
{
public:
    virtual void CommandCallback(const void* context, const void* command) = 0;
};

union CommandCallbackPointer
{
    void* function;
    ICommandCallback* interface_pointer;
};

struct CommandCallbackInfo
{
    CommandCallbackPointer callback;
    bool is_interface : 1;
    bool is_void_callback : 1;
    bool is_contextless : 1;
};

struct CompletionCallbackInfo
{
    void* callback;
    bool is_interface;
};

class ConVarRef
{
public:
    ConVarRef() : access_index_(0xffffu), padding_(0), registered_index_(0) {}
    ConVarRef(std::uint16_t access_index, std::int32_t registered_index)
        : access_index_(access_index), padding_(0), registered_index_(registered_index) {}

    bool IsValid() const
    {
        return access_index_ != 0xffffu;
    }

    std::uint16_t AccessIndex() const
    {
        return access_index_;
    }

    std::int32_t RegisteredIndex() const
    {
        return registered_index_;
    }

private:
    std::uint16_t access_index_;
    std::uint16_t padding_;
    std::int32_t registered_index_;
};

struct ConVarObject;

using GenericChangeCallback = void (*)(
    ConVarObject* reference,
    std::int32_t split_screen_slot,
    const ConVarValue* new_value,
    const ConVarValue* old_value);
using GenericChangeProvider = void (*)(
    ConVarObject* reference,
    std::int32_t split_screen_slot,
    const ConVarValue* new_value,
    const ConVarValue* old_value,
    void* unknown,
    GenericChangeCallback callback);
using GenericFilterCallback = bool (*)(
    ConVarObject* reference,
    std::int32_t split_screen_slot,
    const ConVarValue* new_value,
    const ConVarValue* old_value);
using GenericFilterProvider = bool (*)(
    ConVarObject* reference,
    std::int32_t split_screen_slot,
    const ConVarValue* new_value,
    const ConVarValue* old_value,
    void* unknown,
    GenericFilterCallback callback);
using CustomDataProvider = void* (*)();

struct ConVarValueInfo
{
    std::int32_t version;
    bool has_default;
    bool has_minimum;
    bool has_maximum;
    std::byte default_value[sizeof(ConVarValue)];
    std::byte minimum_value[sizeof(ConVarValue)];
    std::byte maximum_value[sizeof(ConVarValue)];
    GenericChangeProvider change_provider;
    GenericChangeCallback change_callback;
    GenericFilterProvider filter_provider;
    GenericFilterCallback filter_callback;
    CustomDataProvider custom_data;
    ConVarType type;
    CompletionCallbackInfo completion;
};

struct CvarCreationBase
{
    const char* name;
    const char* help;
    std::uint64_t flags;
};

struct ConVarCreation : CvarCreationBase
{
    ConVarValueInfo value_info;
};

struct ConVarData
{
    const char* name;
    ConVarValue* default_value;
    ConVarValue* minimum_value;
    ConVarValue* maximum_value;
    const char* help;
    ConVarType type;
    std::int16_t version;
    std::uint32_t times_changed;
    std::uint64_t flags;
    std::uint32_t callback_index;
    std::uint32_t filter_callback_index;
    std::uint32_t completion_callback_index;
    std::int32_t game_info_flags;
    std::int32_t user_info_byte_index;
    std::uint32_t padding;
    CustomDataProvider custom_data;
    std::byte values[sizeof(ConVarValue)];
};

struct ConVarObject
{
    ConVarRef reference;
    ConVarData* data;
};

struct CommandCreation : CvarCreationBase
{
    CommandCallbackInfo callback_info;
    CompletionCallbackInfo completion_info;
};

class CommandRef
{
public:
    CommandRef() : access_index_(0xffffu), registered_index_(0) {}
    CommandRef(std::uint16_t access_index, std::int32_t registered_index)
        : access_index_(access_index), registered_index_(registered_index) {}

    bool IsValid() const
    {
        return access_index_ != 0xffffu;
    }

    std::uint16_t AccessIndex() const
    {
        return access_index_;
    }

    std::int32_t RegisteredIndex() const
    {
        return registered_index_;
    }

private:
    std::uint16_t access_index_;
    std::int32_t registered_index_;
};

class CvarInterface
{
public:
    virtual void Slot00() = 0;
    virtual void Slot01() = 0;
    virtual void Slot02() = 0;
    virtual void Slot03() = 0;
    virtual void Slot04() = 0;
    virtual void Slot05() = 0;
    virtual void Slot06() = 0;
    virtual void Slot07() = 0;
    virtual void Slot08() = 0;
    virtual void Slot09() = 0;
    virtual void Slot10() = 0;
    virtual ConVarRef FindConVar(const char* name, bool allow_defensive) = 0;
    virtual void Slot12() = 0;
    virtual void Slot13() = 0;
    virtual void CallChangeCallback(
        ConVarRef reference,
        std::int32_t split_screen_slot,
        const ConVarValue* new_value,
        const ConVarValue* old_value,
        void* unknown) = 0;
    virtual void Slot15() = 0;
    virtual bool CallFilterCallback(
        ConVarRef reference,
        std::int32_t split_screen_slot,
        const ConVarValue* new_value,
        const ConVarValue* old_value,
        void* unknown) = 0;
    virtual void Slot17() = 0;
    virtual void Slot18() = 0;
    virtual void Slot19() = 0;
    virtual void Slot20() = 0;
    virtual void Slot21() = 0;
    virtual void Slot22() = 0;
    virtual void CallGlobalChangeCallbacks(
        ConVarObject* reference,
        std::int32_t split_screen_slot,
        const char* new_value,
        const char* old_value,
        void* unknown) = 0;
    virtual void Slot24() = 0;
    virtual void Slot25() = 0;
    virtual void Slot26() = 0;
    virtual void Slot27() = 0;
    virtual void Slot28() = 0;
    virtual void Slot29() = 0;
    virtual void Slot30() = 0;
    virtual void Slot31() = 0;
    virtual void Slot32() = 0;
    virtual void Slot33() = 0;
    virtual void Slot34() = 0;
    virtual void Slot35() = 0;
    virtual void Slot36() = 0;
    virtual void Slot37() = 0;
    virtual void RegisterConVar(
        const ConVarCreation& setup,
        std::uint64_t additional_flags,
        ConVarRef* reference,
        ConVarData** data) = 0;
    virtual void UnregisterConVarCallbacks(ConVarRef reference) = 0;
    virtual void Slot40() = 0;
    virtual ConVarData* GetConVarData(ConVarRef reference) = 0;
    virtual CommandRef RegisterConCommand(const CommandCreation& setup, std::uint64_t additional_flags) = 0;
    virtual void UnregisterConCommandCallbacks(CommandRef command) = 0;
    virtual void* GetConCommandData(CommandRef command) = 0;
    virtual void QueueThreadSetValue(
        ConVarObject* reference,
        std::int32_t split_screen_slot,
        void* unknown,
        ConVarValue* value) = 0;
};

static_assert(sizeof(ConVarValue) == 16);
static_assert(alignof(ConVarValue) == 8);
static_assert(sizeof(CommandCallbackInfo) == 16);
static_assert(sizeof(CompletionCallbackInfo) == 16);
static_assert(sizeof(ConVarRef) == 8);
static_assert(offsetof(ConVarValueInfo, default_value) == 7);
static_assert(offsetof(ConVarValueInfo, change_provider) == kConVarValueInfoChangeProviderOffset);
static_assert(offsetof(ConVarValueInfo, custom_data) == kConVarValueInfoCustomDataOffset);
static_assert(offsetof(ConVarValueInfo, type) == 96);
static_assert(offsetof(ConVarValueInfo, completion) == 104);
static_assert(sizeof(ConVarValueInfo) == 120);
static_assert(sizeof(CvarCreationBase) == 24);
static_assert(sizeof(ConVarCreation) == 144);
static_assert(offsetof(ConVarData, type) == kConVarDataTypeOffset);
static_assert(offsetof(ConVarData, flags) == kConVarDataFlagsOffset);
static_assert(offsetof(ConVarData, custom_data) == kConVarDataCustomDataOffset);
static_assert(offsetof(ConVarData, values) == kConVarDataValueOffset);
static_assert(sizeof(ConVarData) == 104);
static_assert(offsetof(ConVarObject, data) == kConVarObjectDataOffset);
static_assert(sizeof(ConVarObject) == 16);
static_assert(sizeof(CommandCreation) == 56);
static_assert(sizeof(CommandRef) == 8);

}

#endif
