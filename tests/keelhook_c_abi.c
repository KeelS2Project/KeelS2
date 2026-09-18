#include <keels2/keelhook.h>
#include <keels2/keelcall.h>

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(KeelCallApi) == 16, "direct call API x64 layout");
_Static_assert(offsetof(KeelCallApi, invoke) == 8, "direct call entry offset");

/* This executable owns its storage. A real callback borrows frame storage only
 * until dispatch returns; copying a KeelHookValue does not copy its pointee. */
struct Coordinates
{
    int32_t x;
    float y;
};

struct Sample
{
    struct Coordinates coordinates;
    uint64_t markers[2];
};

static int CheckValues(void)
{
    const KeelHookAggregateField coordinate_fields[] = {
        {sizeof(KeelHookAggregateField), KH_VALUE_INT32,
            offsetof(struct Coordinates, x), 1, NULL},
        {sizeof(KeelHookAggregateField), KH_VALUE_FLOAT32,
            offsetof(struct Coordinates, y), 1, NULL}
    };
    const KeelHookAggregate coordinates = {
        sizeof(KeelHookAggregate), sizeof(struct Coordinates),
        2, 0, coordinate_fields
    };
    const KeelHookAggregateField sample_fields[] = {
        {sizeof(KeelHookAggregateField), KH_VALUE_AGGREGATE,
            offsetof(struct Sample, coordinates), 1, &coordinates},
        {sizeof(KeelHookAggregateField), KH_VALUE_UINT64,
            offsetof(struct Sample, markers), 2, NULL}
    };
    const KeelHookAggregate sample = {
        sizeof(KeelHookAggregate), sizeof(struct Sample), 2, 0, sample_fields
    };
    struct Sample storage = {{7, 2.5f}, {10, 20}};
    struct Sample copied;
    KeelHookValue value = {0};
    KeelHookValue scalar = {0};
    scalar.type = KH_VALUE_INT32;
    scalar.scalar.int32 = 7;
    if (scalar.reserved != 0 || scalar.scalar.int32 != 7)
    {
        return 0;
    }

    value.type = KH_VALUE_AGGREGATE;
    value.scalar.aggregate.data = &storage;
    value.scalar.aggregate.size = sizeof(storage);
    value.scalar.aggregate.reserved = 0; /* Plain bytes, no object lifecycle. */
    if (value.type != KH_VALUE_AGGREGATE || value.reserved != 0 ||
        !value.scalar.aggregate.data || value.scalar.aggregate.size != sizeof(copied) ||
        value.scalar.aggregate.reserved != 0)
    {
        return 0;
    }
    memcpy(&copied, value.scalar.aggregate.data, sizeof(copied));
    copied.coordinates.x += scalar.scalar.int32;
    memcpy(value.scalar.aggregate.data, &copied, sizeof(copied));

    /* The pointer representation also carries native lvalue references.
     * Here storage stays alive for every use of the borrowed pointer. */
    scalar.type = KH_VALUE_POINTER;
    scalar.scalar.pointer = &storage.coordinates.x;
    *(int32_t*)scalar.scalar.pointer += 1;
    return storage.coordinates.x == 15 && storage.coordinates.y == 2.5f &&
        storage.markers[1] == 20 && sample.byte_size <= KEELHOOK_MAX_AGGREGATE_SIZE &&
        sample.fields[0].aggregate == &coordinates && sample.fields[1].array_length == 2;
}

static KeelBool DefaultConstruct(void* destination)
{
    if (!destination)
    {
        return KEEL_FALSE;
    }
    *(int*)destination = 0;
    return KEEL_TRUE;
}

static KeelBool CopyConstruct(void* destination, const void* source)
{
    if (!destination || !source)
    {
        return KEEL_FALSE;
    }
    *(int*)destination = *(const int*)source;
    return KEEL_TRUE;
}

static KeelBool CopyAssign(void* destination, const void* source)
{
    return CopyConstruct(destination, source);
}

static void Destroy(void* value)
{
    (void)value;
}

/* int makes the C callback example self-contained. Native C++ objects need
 * callbacks implementing their actual constructors, assignment and destructor;
 * do not substitute these int operations for a non-trivial native type. */
static int CheckObject(const KeelHookObject* object)
{
    int storage = 0;
    int copied = 0;
    const int source = 17;
    int valid;
    KeelHookValue value = {0};
    value.type = KH_VALUE_AGGREGATE;
    value.scalar.aggregate.data = &storage;
    value.scalar.aggregate.size = object->byte_size;
    if (!object->default_construct(value.scalar.aggregate.data))
    {
        return 0;
    }
    value.scalar.aggregate.reserved = KH_VALUE_OBJECT_CONSTRUCTED;
    valid = object->copy_assign(value.scalar.aggregate.data, &source) && storage == 17;
    if (object->copy_construct(&copied, value.scalar.aggregate.data))
    {
        valid = valid && copied == 17;
        object->destroy(&copied);
    }
    else
    {
        valid = 0;
    }
    object->destroy(value.scalar.aggregate.data);
    value.scalar.aggregate.reserved = 0;
    return valid;
}

static KeelHookAction Callback(KeelHookFrame* frame, void* user_data)
{
    (void)frame;
    (void)user_data;
    return KH_ACTION_CONTINUE;
}

int main(void)
{
    const KeelHookObject object = {
        sizeof(KeelHookObject),
        sizeof(int),
        _Alignof(int),
        0,
        "c.fixture.object",
        &DefaultConstruct,
        &CopyConstruct,
        &CopyAssign,
        &Destroy
    };
    const KeelHookValueType argument_types[] = {KH_VALUE_AGGREGATE, KH_VALUE_POINTER};
    const KeelHookAggregate* argument_aggregates[] = {NULL, NULL};
    const KeelHookObject* argument_objects[] = {&object, NULL};
    const KeelHookPrototype prototype = {
        sizeof(KeelHookPrototype),
        KH_CALL_NATIVE,
        KH_VALUE_AGGREGATE,
        2,
        argument_types,
        NULL,
        argument_aggregates,
        2,
        KH_PROTOTYPE_VAFMT,
        &object,
        argument_objects
    };
    const KeelHookCallbackSpec callback = {
        sizeof(KeelHookCallbackSpec),
        KH_PHASE_BOTH,
        0,
        0,
        &Callback,
        NULL
    };
    const KeelHookVirtualTargetSpec virtual_target = {
        sizeof(KeelHookVirtualTargetSpec),
        KH_MECHANISM_VIRTUAL,
        0,
        0,
        0,
        0,
        (void*)&object,
        NULL,
        8,
        16
    };
    return CheckValues() && CheckObject(&object) &&
        KEELHOOK_API_VERSION_3 == 3u && KEELHOOK_API_VERSION_4 == 4u &&
        KEELHOOK_API_VERSION == 5u && prototype.return_object == &object &&
        prototype.argument_objects[0] == &object &&
        prototype.flags == KH_PROTOTYPE_VAFMT && callback.callback == &Callback &&
        virtual_target.this_adjustment == 8 && virtual_target.vtable_offset == 16
        ? 0
        : 1;
}
