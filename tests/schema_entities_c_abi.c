#include <keels2/entities.h>
#include <keels2/schema.h>
#include <keels2/player_actions.h>

#include <stddef.h>

_Static_assert(sizeof(KeelSchemaFieldSpec) == 32, "KeelSchemaFieldSpec ABI");
_Static_assert(sizeof(KeelSchemaFieldInfo) == 64, "KeelSchemaFieldInfo ABI");
_Static_assert(sizeof(KeelSchemaApi) == 32, "KeelSchemaApi ABI");
_Static_assert(sizeof(KeelEntityInfo) == 24, "KeelEntityInfo ABI");
_Static_assert(sizeof(KeelEntitiesApi) == 56, "KeelEntitiesApi ABI");
_Static_assert(sizeof(KeelPlayerAction) == 24, "KeelPlayerAction ABI");
_Static_assert(sizeof(KeelPlayerActionsApi) == 16, "KeelPlayerActionsApi ABI");
_Static_assert(offsetof(KeelPlayerAction, damage) == 20, "KeelPlayerAction damage ABI");

int main(void)
{
    KeelSchemaFieldSpec field = {0};
    KeelEntityInfo entity = {0};
    return field.size == 0 && entity.size == 0 ? 0 : 1;
}
