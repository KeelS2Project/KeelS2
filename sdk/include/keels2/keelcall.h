#ifndef KEELS2_KEELCALL_H
#define KEELS2_KEELCALL_H

#include <keels2/keelhook.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEELCALL_SERVICE_NAME "keels2.keelcall"
#define KEELCALL_API_VERSION 1u
#define KEELCALL_INVOKE_HOOKS 0x01u
#define KEELCALL_MAX_DEPTH 8u

/* Optional direct-call service. Targets are owned leases obtained through
 * KeelHook; this table does not change KeelHook API versions 3, 4 or 5.
 * Version 1 accepts detour targets with scalar arguments and scalar/void
 * returns only. Aggregates, objects, virtual targets and variadics return
 * UNSUPPORTED. Method prototypes include a non-null first pointer argument.
 * Native callers are responsible for the signature and every pointee's
 * validity, alignment and lifetime. No pointer memory is inspected here. */
typedef struct KeelCallApi
{
    uint32_t size;
    uint32_t api_version;
    /* Synchronous, game thread only, at most KEELCALL_MAX_DEPTH nested calls.
     * flags=0 enters the original function, bypassing its KeelHook callbacks;
     * INVOKE_HOOKS enters the current callback chain. Nested native calls may
     * still trigger other hooks. Arguments require exact types and reserved=0;
     * bool must be KEEL_FALSE/TRUE. Argument values are copied before dispatch.
     * result is required, including for void; it is cleared on failure. It may
     * alias an argument. Pointer results are borrowed from the native callee.
     * Calling plugin and target code stay retained until return. Target lease
     * release and callback add/remove return BUSY while this target is being
     * called through this service; callback enable/disable remains available.
     * ENGINE_FAILURE does not roll back native or callback side effects.
     * Invalid native signatures/pointers and native exceptions crossing the
     * ABI are outside the service contract. */
    KeelResult (*invoke)(KeelPluginHandle plugin, KeelHookTargetHandle target,
        uint32_t flags, const KeelHookValue* arguments, uint32_t argument_count,
        KeelHookValue* result);
} KeelCallApi;

#ifdef __cplusplus
}
#endif
#endif
