#ifndef KEELS2_KEELHOOK_H
#define KEELS2_KEELHOOK_H

#include <keels2/plugin.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELHOOK_SERVICE_NAME "keels2.keelhook"
#define KEELHOOK_API_VERSION_3 3u
#define KEELHOOK_API_VERSION_4 4u
#define KEELHOOK_API_VERSION 5u
#define KEELHOOK_MAX_ARGUMENTS 32u
#define KEELHOOK_MAX_AGGREGATE_SIZE 64u
#define KEELHOOK_MAX_AGGREGATE_ALIGNMENT 16u
#define KEELHOOK_MAX_AGGREGATE_FIELDS 64u
#define KEELHOOK_MAX_AGGREGATE_DEPTH 8u
#define KEELHOOK_MAX_AGGREGATE_DESCRIPTORS 256u
#define KEELHOOK_MAX_OBJECT_IDENTITY 512u
#define KEELHOOK_VAFMT_BUFFER_SIZE 4096u

typedef uint32_t KeelHookValueType;
typedef uint32_t KeelHookCallingConvention;
typedef uint32_t KeelHookTargetSource;
typedef uint32_t KeelHookMechanism;
typedef uint32_t KeelHookPhase;
typedef uint32_t KeelHookAction;
typedef uint64_t KeelHookTargetHandle;
typedef uint64_t KeelHookCallbackHandle;

#define KH_VALUE_VOID 0u
#define KH_VALUE_BOOL 1u
#define KH_VALUE_INT8 2u
#define KH_VALUE_UINT8 3u
#define KH_VALUE_INT16 4u
#define KH_VALUE_UINT16 5u
#define KH_VALUE_INT32 6u
#define KH_VALUE_UINT32 7u
#define KH_VALUE_INT64 8u
#define KH_VALUE_UINT64 9u
#define KH_VALUE_POINTER 10u
#define KH_VALUE_FLOAT32 11u
#define KH_VALUE_FLOAT64 12u
#define KH_VALUE_AGGREGATE 13u

#define KH_CALL_NATIVE 1u

#define KH_TARGET_ADDRESS 1u
#define KH_TARGET_SYMBOL 2u
#define KH_TARGET_PATTERN 3u
#define KH_TARGET_PROFILE 4u

#define KH_MECHANISM_DETOUR 1u
#define KH_MECHANISM_VIRTUAL 2u
#define KH_MECHANISM_VIRTUAL_INSTANCE 3u

#define KH_TARGET_METHOD 1u

#define KH_PROTOTYPE_VAFMT 1u

#define KH_VALUE_OBJECT_CONSTRUCTED 1u

#define KH_PHASE_PRE 1u
#define KH_PHASE_POST 2u
#define KH_PHASE_BOTH (KH_PHASE_PRE | KH_PHASE_POST)

#define KH_ACTION_CONTINUE 0u
#define KH_ACTION_OVERRIDE 1u
#define KH_ACTION_SUPERSEDE 2u

#define KH_FRAME_ORIGINAL_CALLED 1u
#define KH_FRAME_RECALLED 2u

typedef union KeelHookScalar
{
    KeelBool boolean;
    int8_t int8;
    uint8_t uint8;
    int16_t int16;
    uint16_t uint16;
    int32_t int32;
    uint32_t uint32;
    int64_t int64;
    uint64_t uint64;
    void* pointer;
    float float32;
    double float64;
    struct
    {
        void* data;
        uint32_t size;
        uint32_t reserved;
    } aggregate;
} KeelHookScalar;

typedef struct KeelHookValue
{
    KeelHookValueType type;
    uint32_t reserved;
    KeelHookScalar scalar;
} KeelHookValue;

typedef struct KeelHookAggregate KeelHookAggregate;

typedef struct KeelHookAggregateField
{
    uint32_t size;
    KeelHookValueType type;
    uint32_t offset;
    uint32_t array_length;
    const KeelHookAggregate* aggregate;
} KeelHookAggregateField;

struct KeelHookAggregate
{
    uint32_t size;
    uint32_t byte_size;
    uint32_t field_count;
    uint32_t flags;
    const KeelHookAggregateField* fields;
};

typedef KeelBool (*KeelHookObjectDefaultConstruct)(void* destination);
typedef KeelBool (*KeelHookObjectCopyConstruct)(void* destination, const void* source);
typedef KeelBool (*KeelHookObjectCopyAssign)(void* destination, const void* source);
typedef void (*KeelHookObjectDestroy)(void* value);

typedef struct KeelHookObject
{
    uint32_t size;
    uint32_t byte_size;
    uint32_t alignment;
    uint32_t flags;
    const char* identity;
    KeelHookObjectDefaultConstruct default_construct;
    KeelHookObjectCopyConstruct copy_construct;
    KeelHookObjectCopyAssign copy_assign;
    KeelHookObjectDestroy destroy;
} KeelHookObject;

typedef struct KeelHookPrototypeV4
{
    uint32_t size;
    KeelHookCallingConvention calling_convention;
    KeelHookValueType return_type;
    uint32_t argument_count;
    const KeelHookValueType* argument_types;
    const KeelHookAggregate* return_aggregate;
    const KeelHookAggregate* const* argument_aggregates;
    uint32_t fixed_argument_count;
    uint32_t flags;
} KeelHookPrototypeV4;

typedef struct KeelHookPrototype
{
    uint32_t size;
    KeelHookCallingConvention calling_convention;
    KeelHookValueType return_type;
    uint32_t argument_count;
    const KeelHookValueType* argument_types;
    const KeelHookAggregate* return_aggregate;
    const KeelHookAggregate* const* argument_aggregates;
    uint32_t fixed_argument_count;
    uint32_t flags;
    const KeelHookObject* return_object;
    const KeelHookObject* const* argument_objects;
} KeelHookPrototype;

typedef struct KeelHookTargetSpec
{
    uint32_t size;
    KeelHookTargetSource source;
    KeelHookMechanism mechanism;
    uint32_t flags;
    const char* module;
    const char* symbol;
    const char* pattern;
    const char* profile;
    void* address;
    int64_t offset;
    uint32_t occurrence;
    uint32_t reserved;
} KeelHookTargetSpec;

typedef struct KeelHookVirtualTargetSpecV4
{
    uint32_t size;
    KeelHookMechanism mechanism;
    uint32_t flags;
    uint32_t index;
    uint32_t table_size;
    uint32_t reserved;
    void* instance;
    const char* profile;
} KeelHookVirtualTargetSpecV4;

typedef struct KeelHookVirtualTargetSpec
{
    uint32_t size;
    KeelHookMechanism mechanism;
    uint32_t flags;
    uint32_t index;
    uint32_t table_size;
    uint32_t reserved;
    void* instance;
    const char* profile;
    int64_t this_adjustment;
    int64_t vtable_offset;
} KeelHookVirtualTargetSpec;

typedef struct KeelHookFrame
{
    uint32_t size;
    KeelHookPhase phase;
    KeelHookTargetHandle target;
    uint32_t argument_count;
    uint32_t flags;
    KeelHookValue* arguments;
    KeelHookValue result;
} KeelHookFrame;

typedef KeelHookAction (*KeelHookCallback)(KeelHookFrame* frame, void* user_data);

typedef struct KeelHookCallbackSpec
{
    uint32_t size;
    uint32_t phases;
    int32_t priority;
    uint32_t reserved;
    KeelHookCallback callback;
    void* user_data;
} KeelHookCallbackSpec;

typedef struct KeelHookApiV3
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*resolve_target)(
        KeelPluginHandle plugin,
        const KeelHookTargetSpec* spec,
        const KeelHookPrototypeV4* prototype,
        KeelHookTargetHandle* target);
    KeelResult (*release_target)(KeelPluginHandle plugin, KeelHookTargetHandle target);
    KeelResult (*add_callback)(
        KeelPluginHandle plugin,
        KeelHookTargetHandle target,
        const KeelHookCallbackSpec* spec,
        KeelHookCallbackHandle* callback);
    KeelResult (*remove_callback)(KeelPluginHandle plugin, KeelHookCallbackHandle callback);
    KeelResult (*resolve_virtual_target)(
        KeelPluginHandle plugin,
        const KeelHookVirtualTargetSpecV4* spec,
        const KeelHookPrototypeV4* prototype,
        KeelHookTargetHandle* target);
} KeelHookApiV3;

typedef struct KeelHookApiV4
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*resolve_target)(
        KeelPluginHandle plugin,
        const KeelHookTargetSpec* spec,
        const KeelHookPrototypeV4* prototype,
        KeelHookTargetHandle* target);
    KeelResult (*release_target)(KeelPluginHandle plugin, KeelHookTargetHandle target);
    KeelResult (*add_callback)(
        KeelPluginHandle plugin,
        KeelHookTargetHandle target,
        const KeelHookCallbackSpec* spec,
        KeelHookCallbackHandle* callback);
    KeelResult (*remove_callback)(KeelPluginHandle plugin, KeelHookCallbackHandle callback);
    KeelResult (*resolve_virtual_target)(
        KeelPluginHandle plugin,
        const KeelHookVirtualTargetSpecV4* spec,
        const KeelHookPrototypeV4* prototype,
        KeelHookTargetHandle* target);
    KeelResult (*call_original)(KeelPluginHandle plugin, KeelHookFrame* frame);
    KeelResult (*recall)(KeelPluginHandle plugin, KeelHookFrame* frame);
    KeelResult (*set_callback_enabled)(
        KeelPluginHandle plugin,
        KeelHookCallbackHandle callback,
        KeelBool enabled);
} KeelHookApiV4;

typedef struct KeelHookApi
{
    uint32_t size;
    uint32_t api_version;
    KeelResult (*resolve_target)(
        KeelPluginHandle plugin,
        const KeelHookTargetSpec* spec,
        const KeelHookPrototype* prototype,
        KeelHookTargetHandle* target);
    KeelResult (*release_target)(KeelPluginHandle plugin, KeelHookTargetHandle target);
    KeelResult (*add_callback)(
        KeelPluginHandle plugin,
        KeelHookTargetHandle target,
        const KeelHookCallbackSpec* spec,
        KeelHookCallbackHandle* callback);
    KeelResult (*remove_callback)(KeelPluginHandle plugin, KeelHookCallbackHandle callback);
    KeelResult (*resolve_virtual_target)(
        KeelPluginHandle plugin,
        const KeelHookVirtualTargetSpec* spec,
        const KeelHookPrototype* prototype,
        KeelHookTargetHandle* target);
    KeelResult (*call_original)(KeelPluginHandle plugin, KeelHookFrame* frame);
    KeelResult (*recall)(KeelPluginHandle plugin, KeelHookFrame* frame);
    KeelResult (*set_callback_enabled)(
        KeelPluginHandle plugin,
        KeelHookCallbackHandle callback,
        KeelBool enabled);
} KeelHookApi;

#ifdef __cplusplus
}
#endif

#endif
