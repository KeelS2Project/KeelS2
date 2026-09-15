#include <keels2/lifecycle.h>

static void Callback(const KeelLifecycleEvent* event, void* user_data)
{
    (void)event;
    (void)user_data;
}

int main(void)
{
    KeelLifecycleSubscriptionSpec spec = {
        sizeof(KeelLifecycleSubscriptionSpec),
        KEELS2_LIFECYCLE_GAME_FRAME,
        0,
        &Callback,
        0
    };
    KeelLifecycleEvent event = {
        sizeof(KeelLifecycleEvent),
        spec.event,
        sizeof(KeelLifecycleGameFrame),
        0,
        0
    };
    return event.type == KEELS2_LIFECYCLE_GAME_FRAME &&
        KEELS2_LIFECYCLE_API_VERSION == 1u &&
        KEELS2_LIFECYCLE_SERVICE_NAME[0] != '\0'
        ? 0
        : 1;
}
