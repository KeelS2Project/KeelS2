#include <keels2/entities.h>
#include <keels2/schema.h>

#include <stddef.h>

_Static_assert(sizeof(KeelSchemaFieldSpec) == 32, "KeelSchemaFieldSpec ABI");
_Static_assert(sizeof(KeelSchemaFieldInfo) == 64, "KeelSchemaFieldInfo ABI");
_Static_assert(sizeof(KeelSchemaApi) == 32, "KeelSchemaApi ABI");
_Static_assert(sizeof(KeelEntityInfo) == 24, "KeelEntityInfo ABI");
_Static_assert(sizeof(KeelEntitiesApi) == 56, "KeelEntitiesApi ABI");

int main(void)
{
    KeelSchemaFieldSpec field = {0};
    KeelEntityInfo entity = {0};
    return field.size == 0 && entity.size == 0 ? 0 : 1;
}
