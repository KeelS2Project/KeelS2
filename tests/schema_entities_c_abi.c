#include <keels2/entities.h>
#include <keels2/entity_input.h>
#include <keels2/entity_outputs.h>
#include <keels2/entity_construction.h>
#include <keels2/entity_tools.h>
#include <keels2/entity_access.h>
#include <keels2/entity_hook_data.h>
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

_Static_assert(sizeof(KeelDamageInfo) == 56, "damage snapshot ABI");
_Static_assert(sizeof(KeelDamageEdit) == 40, "damage edit ABI");
_Static_assert(sizeof(KeelEntityHookDataApi) == 32, "entity hook data table ABI");
_Static_assert(offsetof(KeelDamageInfo, force) == 32, "damage force ABI");
_Static_assert(offsetof(KeelEntityHookDataApi, weapon_matches) == 24, "weapon match ABI");

_Static_assert(sizeof(KeelEntityTeleport) == 44, "teleport ABI");
_Static_assert(offsetof(KeelEntityTeleport, velocity) == 32, "teleport velocity ABI");
_Static_assert(sizeof(KeelEntityToolsApi) == 40, "entity tools API ABI");
_Static_assert(offsetof(KeelEntityToolsApi, remove) == 32, "entity remove ABI");

_Static_assert(sizeof(KeelEntityConstructionApi) == 72, "construction API ABI");
_Static_assert(offsetof(KeelEntityConstructionApi, create) == 16, "construction create ABI");
_Static_assert(offsetof(KeelEntityConstructionApi, visit) == 64, "construction visit ABI");
_Static_assert(sizeof(KeelEntityKeyValue) == 48, "keyvalue ABI");
_Static_assert(offsetof(KeelEntityKeyValue, string_value) == 16, "keyvalue string ABI");

_Static_assert(sizeof(KeelEntityInputValue) == 40, "input value ABI");
_Static_assert(sizeof(KeelEntityInputRequest) == 88, "input request ABI");
_Static_assert(offsetof(KeelEntityInputRequest, value) == 40, "input payload ABI");
_Static_assert(offsetof(KeelEntityInputRequest, delay) == 80, "input delay ABI");
_Static_assert(sizeof(KeelEntityInputApi) == 24, "input table ABI");
_Static_assert(offsetof(KeelEntityInputApi, dispatch) == 16, "input dispatch ABI");

_Static_assert(sizeof(KeelEntityOutputValue) == 4136, "output snapshot ABI");
_Static_assert(sizeof(KeelEntityOutputEvent) == 4872, "output event ABI");
_Static_assert(offsetof(KeelEntityOutputEvent, value) == 736, "output value ABI");
_Static_assert(sizeof(KeelEntityOutputSpec) == 56, "output filter ABI");
_Static_assert(offsetof(KeelEntityOutputSpec, callback) == 40, "output callback ABI");
_Static_assert(sizeof(KeelEntityOutputsApi) == 32, "output API ABI");

int main(void)
{
    KeelSchemaFieldSpec field = {0};
    KeelEntityInfo entity = {0};
    return field.size == 0 && entity.size == 0 ? 0 : 1;
}
