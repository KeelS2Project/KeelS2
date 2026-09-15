#include <keels2/pause.h>
#include <stddef.h>

_Static_assert(KEELS2_PLUGIN_ABI_VERSION == 4, "plugin ABI");
_Static_assert(KEELS2_PAUSE_API_VERSION == 1, "pause service version");
_Static_assert(sizeof(KeelPauseApi) == 16, "pause API size");
_Static_assert(offsetof(KeelPauseApi, set_prepare_callback) == 8, "callback registration offset");

static KeelBool ready(void* data) { return data ? KEEL_TRUE : KEEL_FALSE; }
int main(void) {
    KeelPreparePauseCallback callback = &ready;
    return callback((void*)&callback) == KEEL_TRUE && callback(NULL) == KEEL_FALSE ? 0 : 1;
}
