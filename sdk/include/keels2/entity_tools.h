#ifndef KEELS2_ENTITY_TOOLS_H
#define KEELS2_ENTITY_TOOLS_H
#include <keels2/entities.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KEELS2_ENTITY_TOOLS_SERVICE_NAME "keels2.entity_tools"
#define KEELS2_ENTITY_TOOLS_API_VERSION 1u
#define KEELS2_ENTITY_TOOL_TELEPORT 0x01u
#define KEELS2_ENTITY_TOOL_SET_MODEL 0x02u
#define KEELS2_ENTITY_TOOL_REMOVE 0x04u
#define KEELS2_TELEPORT_POSITION 0x01u
#define KEELS2_TELEPORT_ANGLES 0x02u
#define KEELS2_TELEPORT_VELOCITY 0x04u
#define KEELS2_ENTITY_MODEL_MAX_BYTES 511u

typedef struct KeelEntityTeleport
{
    uint32_t size;
    uint32_t flags;
    float position[3];
    float angles[3];
    float velocity[3];
} KeelEntityTeleport;

typedef struct KeelEntityToolsApi
{
    uint32_t size;
    uint32_t api_version;
    /* Main-thread only; owned handles must belong to the caller and remain
     * current in this map. Capabilities describe the reviewed game binding,
     * not whether every entity supports each operation. Output zero on failure. */
    KeelResult (*capabilities)(KeelPluginHandle plugin, uint32_t* capabilities);
    /* At least one flag is required. Selected vectors must be finite; omitted
     * vectors are ignored. Angles are pitch/yaw/roll in degrees. Dispatches the
     * actual entity class's teleport implementation. */
    KeelResult (*teleport)(KeelPluginHandle plugin, KeelEntityHandle entity, const KeelEntityTeleport* request);
    /* Nonempty NUL-terminated engine model asset name, <=511 bytes, no control
     * characters. Requires a model entity. Availability/precaching is governed
     * by the game; success means the engine setter was invoked. */
    KeelResult (*set_model)(KeelPluginHandle plugin, KeelEntityHandle entity, const char* model);
    /* Requests engine removal, which may be deferred. Does not release the
     * owned handle; subsequent validation fails when removal invalidates it.
     * All calls may run game callbacks. Failure after a callback is not a
     * transaction rollback, and success does not guarantee the entity survives. */
    KeelResult (*remove)(KeelPluginHandle plugin, KeelEntityHandle entity);
} KeelEntityToolsApi;
#ifdef __cplusplus
}
#endif
#endif
