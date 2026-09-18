#include <keels2/entities.h>
#include <keels2/entity_access.h>
#include <keels2/schema.h>
#include <keels2/player_actions.h>
#include <keels2/entity_writes.h>
#include <keels2/player_management.h>
#include <keels2/round_control.h>
#include <keels2/player_statistics.h>

#include <stddef.h>

_Static_assert(sizeof(KeelSchemaFieldSpec) == 32, "KeelSchemaFieldSpec ABI");
_Static_assert(sizeof(KeelSchemaFieldInfo) == 64, "KeelSchemaFieldInfo ABI");
_Static_assert(sizeof(KeelSchemaApi) == 32, "KeelSchemaApi ABI");
_Static_assert(sizeof(KeelEntityInfo) == 24, "KeelEntityInfo ABI");
_Static_assert(sizeof(KeelEntitiesApi) == 56, "KeelEntitiesApi ABI");
_Static_assert(sizeof(KeelPlayerAction) == 24, "KeelPlayerAction ABI");
_Static_assert(sizeof(KeelPlayerActionsApi) == 16, "KeelPlayerActionsApi ABI");
_Static_assert(offsetof(KeelPlayerAction, damage) == 20, "KeelPlayerAction damage ABI");

_Static_assert(sizeof(KeelEntityWritesApi) == 24, "KeelEntityWritesApi ABI");
_Static_assert(offsetof(KeelEntityWritesApi, write_field) == 16, "KeelEntityWritesApi write ABI");
_Static_assert(sizeof(KeelPlayerManagementApi) == 24, "KeelPlayerManagementApi ABI");
_Static_assert(sizeof(KeelPlayerManagementAction) == 16, "KeelPlayerManagementAction ABI");

_Static_assert(sizeof(KeelRoundTermination) == 20, "KeelRoundTermination ABI");
_Static_assert(offsetof(KeelRoundTermination, delay) == 8, "round delay ABI");
_Static_assert(sizeof(KeelRoundControlApi) == 24, "KeelRoundControlApi ABI");
_Static_assert(offsetof(KeelRoundControlApi, terminate) == 16, "round terminate ABI");

_Static_assert(sizeof(KeelPlayerStatisticsApi) == 32, "KeelPlayerStatisticsApi ABI");
_Static_assert(offsetof(KeelPlayerStatisticsApi, read) == 16, "statistics read ABI");
_Static_assert(offsetof(KeelPlayerStatisticsApi, write) == 24, "statistics write ABI");

_Static_assert(sizeof(KeelEntityAccessSpec) == 24, "entity access spec ABI");
_Static_assert(sizeof(KeelEntityAccessApi) == 16, "entity access table ABI");
_Static_assert(sizeof(KeelEntityCaptureApi) == 16, "entity capture table ABI");
_Static_assert(offsetof(KeelEntityCaptureApi, capture) == 8, "entity capture function ABI");
_Static_assert(offsetof(KeelEntityAccessSpec, class_name) == 16, "entity access class ABI");

int main(void)
{
    KeelSchemaFieldSpec field = {0};
    KeelEntityInfo entity = {0};
    return field.size == 0 && entity.size == 0 ? 0 : 1;
}
