#include <keels2/keelhook.h>

#include <stddef.h>

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
    return KEELHOOK_API_VERSION_3 == 3u && KEELHOOK_API_VERSION_4 == 4u &&
        KEELHOOK_API_VERSION == 5u && prototype.return_object == &object &&
        prototype.argument_objects[0] == &object &&
        prototype.flags == KH_PROTOTYPE_VAFMT && callback.callback == &Callback &&
        virtual_target.this_adjustment == 8 && virtual_target.vtable_offset == 16
        ? 0
        : 1;
}
