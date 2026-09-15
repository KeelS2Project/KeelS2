#include <keels2/source2_callbacks.h>

static KeelBool Callback(const KeelSource2CallbackEvent* event, void* user_data)
{
    return event && user_data ? KEEL_TRUE : KEEL_FALSE;
}

int main(void)
{
    KeelSource2SubscriptionSpec spec = {0};
    spec.size = sizeof(spec);
    spec.type = KEELS2_SOURCE2_GAME_EVENT;
    spec.game_event = "round_start";
    spec.callback = &Callback;
    spec.user_data = &spec;

    KeelSource2CallbacksApi api = {0};
    api.size = sizeof(api);
    api.api_version = KEELS2_SOURCE2_CALLBACKS_API_VERSION;
    return spec.callback != 0 && api.size == sizeof(KeelSource2CallbacksApi) ? 0 : 1;
}
