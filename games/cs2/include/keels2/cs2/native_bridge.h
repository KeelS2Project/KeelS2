#ifndef KEELS2_CS2_NATIVE_BRIDGE_H
#define KEELS2_CS2_NATIVE_BRIDGE_H

#include <keels2/plugin.h>
#include <keels2/schema.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*KeelCs2GameEventCallback)(void* event, const char* name, void* user_data);

typedef struct KeelCs2SchemaField
{
    void* declaring_class;
    int32_t offset;
    uint32_t value_size;
    uint32_t value_alignment;
    KeelSchemaValueType value_type;
} KeelCs2SchemaField;

typedef struct KeelCs2EntityIdentity
{
    int32_t index;
    uint32_t source2_handle;
} KeelCs2EntityIdentity;

void* KeelCs2_CreateGameEventListener(
    void* manager,
    KeelCs2GameEventCallback callback,
    void* user_data);
void KeelCs2_DestroyGameEventListener(void* listener);
uint32_t KeelCs2_ListenForGameEvent(void* listener, const char* name);
uint32_t KeelCs2_WriteRejectionMessage(void* buffer, const char* message, uint32_t length);
KeelResult KeelCs2_ResolveSchemaField(
    void* schema_system,
    const char* module_name,
    const char* class_name,
    const char* field_name,
    KeelSchemaValueType value_type,
    KeelCs2SchemaField* field);
void* KeelCs2_ReadGameEntitySystem(void* game_resource_service, uint32_t offset);
KeelResult KeelCs2_FindEntityByIndex(
    void* entity_system,
    int32_t index,
    KeelCs2EntityIdentity* entity);
KeelResult KeelCs2_FindEntityBySource2Handle(
    void* entity_system,
    uint32_t source2_handle,
    KeelCs2EntityIdentity* entity);
KeelResult KeelCs2_ValidateEntity(
    void* entity_system,
    const KeelCs2EntityIdentity* entity);
KeelResult KeelCs2_ReadEntityField(
    void* entity_system,
    const KeelCs2EntityIdentity* entity,
    const KeelCs2SchemaField* field,
    void* value,
    uint32_t value_size);

#ifdef __cplusplus
}
#endif

#endif
