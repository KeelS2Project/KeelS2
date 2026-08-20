#ifndef KEELS2_CS2_NATIVE_BRIDGE_H
#define KEELS2_CS2_NATIVE_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*KeelCs2GameEventCallback)(void* event, const char* name, void* user_data);

void* KeelCs2_CreateGameEventListener(
    void* manager,
    KeelCs2GameEventCallback callback,
    void* user_data);
void KeelCs2_DestroyGameEventListener(void* listener);
uint32_t KeelCs2_ListenForGameEvent(void* listener, const char* name);
uint32_t KeelCs2_WriteRejectionMessage(void* buffer, const char* message, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif
