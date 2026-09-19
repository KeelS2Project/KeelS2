#include <keels2/convar.h>
#include <keels2/convar_access.h>
#include <keels2/convar_observe.h>

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(KeelConVarScalar) == 8, "KeelConVarScalar layout changed");
_Static_assert(sizeof(KeelConVarValue) == 16, "KeelConVarValue layout changed");
_Static_assert(sizeof(KeelConVarChange) == 56, "KeelConVarChange layout changed");
_Static_assert(sizeof(KeelConVarSpec) == 112, "KeelConVarSpec layout changed");
_Static_assert(sizeof(KeelConVarInfo) == 96, "KeelConVarInfo layout changed");
_Static_assert(sizeof(KeelConVarApi) == 56, "KeelConVarApi layout changed");
_Static_assert(offsetof(KeelConVarChange, old_value) == 24, "old value offset changed");
_Static_assert(offsetof(KeelConVarSpec, default_value) == 32, "default value offset changed");
_Static_assert(offsetof(KeelConVarSpec, callback) == 96, "callback offset changed");

typedef struct TextChange
{
    char previous[32];
    char current[32];
    unsigned calls;
} TextChange;

static void Changed(const KeelConVarChange* change, void* user_data)
{
    TextChange* saved = user_data;

    if (!saved || !change || change->size != sizeof(*change) ||
        change->old_value.size != sizeof(KeelConVarValue) ||
        change->new_value.size != sizeof(KeelConVarValue) ||
        change->old_value.type != KEELS2_CONVAR_STRING ||
        change->new_value.type != KEELS2_CONVAR_STRING ||
        !change->old_value.value.string_value || !change->new_value.value.string_value)
        return;

    const size_t previous_size = strlen(change->old_value.value.string_value) + 1;
    const size_t current_size = strlen(change->new_value.value.string_value) + 1;

    if (previous_size > sizeof(saved->previous) || current_size > sizeof(saved->current))
        return;

    memcpy(saved->previous, change->old_value.value.string_value, previous_size);
    memcpy(saved->current, change->new_value.value.string_value, current_size);
    ++saved->calls;
}

_Static_assert(sizeof(KeelConVarAccessApi) == 16, "native ConVar access layout changed");
_Static_assert(offsetof(KeelConVarAccessApi, invoke) == 8, "native ConVar invoke offset changed");
_Static_assert(sizeof(KeelConVarObserveApi) == 16, "ConVar observer layout changed");
_Static_assert(offsetof(KeelConVarObserveApi, observe) == 8, "ConVar observer offset changed");

int main(void)
{
    char previous[] = "Ready";
    char current[] = "Updated";
    const KeelConVarValue values[] = {
        {sizeof(KeelConVarValue), KEELS2_CONVAR_BOOL, {.boolean_value = KEEL_TRUE}},
        {sizeof(KeelConVarValue), KEELS2_CONVAR_INT32, {.int32_value = 7}},
        {sizeof(KeelConVarValue), KEELS2_CONVAR_FLOAT32, {.float32_value = 1.5f}},
        {sizeof(KeelConVarValue), KEELS2_CONVAR_STRING, {.string_value = previous}}
    };
    KeelConVarSpec spec = {
        sizeof(KeelConVarSpec),
        KEELS2_CONVAR_INT32,
        "keels2_c_abi",
        "C ABI fixture",
        KEELS2_CVAR_FLAG_RELEASE,
        {sizeof(KeelConVarValue), KEELS2_CONVAR_INT32, {.int32_value = 7}},
        KEEL_TRUE,
        0,
        {sizeof(KeelConVarValue), KEELS2_CONVAR_INT32, {.int32_value = 1}},
        KEEL_TRUE,
        0,
        {sizeof(KeelConVarValue), KEELS2_CONVAR_INT32, {.int32_value = 11}},
        NULL,
        NULL
    };
    TextChange saved = {0};
    KeelConVarChange change = {
        .size = sizeof(KeelConVarChange),
        .slot = KEELS2_CONVAR_GLOBAL_SLOT,
        .convar = 1,
        .name = "keels2_c_text",
        .old_value = values[3],
        .new_value = {sizeof(KeelConVarValue), KEELS2_CONVAR_STRING, {.string_value = current}}
    };
    KeelConVarChangeCallback callback = &Changed;
    callback(&change, &saved);
    previous[0] = 'r';
    current[0] = 'u';
    return spec.default_value.value.int32_value == 7 &&
        spec.has_minimum == KEEL_TRUE && spec.minimum_value.value.int32_value == 1 &&
        spec.has_maximum == KEEL_TRUE && spec.maximum_value.value.int32_value == 11 &&
        values[0].value.boolean_value == KEEL_TRUE &&
        values[1].value.int32_value == 7 && values[2].value.float32_value == 1.5f &&
        strcmp(values[3].value.string_value, "ready") == 0 &&
        saved.calls == 1 && strcmp(saved.previous, "Ready") == 0 &&
        strcmp(saved.current, "Updated") == 0 &&
        KEELS2_CONVAR_API_VERSION == 1u &&
        KEELS2_CONVAR_SERVICE_NAME[0] != '\0'
        ? 0
        : 1;
}
