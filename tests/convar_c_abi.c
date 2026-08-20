#include <keels2/convar.h>

#include <stddef.h>

_Static_assert(sizeof(KeelConVarScalar) == 8, "KeelConVarScalar layout changed");
_Static_assert(sizeof(KeelConVarValue) == 16, "KeelConVarValue layout changed");
_Static_assert(sizeof(KeelConVarChange) == 56, "KeelConVarChange layout changed");
_Static_assert(sizeof(KeelConVarSpec) == 112, "KeelConVarSpec layout changed");
_Static_assert(sizeof(KeelConVarInfo) == 96, "KeelConVarInfo layout changed");
_Static_assert(sizeof(KeelConVarApi) == 56, "KeelConVarApi layout changed");
_Static_assert(offsetof(KeelConVarChange, old_value) == 24, "old value offset changed");
_Static_assert(offsetof(KeelConVarSpec, default_value) == 32, "default value offset changed");
_Static_assert(offsetof(KeelConVarSpec, callback) == 96, "callback offset changed");

static void Changed(const KeelConVarChange* change, void* user_data)
{
    (void)change;
    (void)user_data;
}

int main(void)
{
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
        &Changed,
        NULL
    };
    return spec.default_value.value.int32_value == 7 &&
        KEELS2_CONVAR_API_VERSION == 1u &&
        KEELS2_CONVAR_SERVICE_NAME[0] != '\0'
        ? 0
        : 1;
}
