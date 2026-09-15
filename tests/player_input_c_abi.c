#include <keels2/player_input.h>
#include <stddef.h>
_Static_assert(sizeof(KeelPlayerInput) == 24, "input state ABI");
_Static_assert(offsetof(KeelPlayerInput, buttons) == 8, "button mask ABI");
_Static_assert(offsetof(KeelPlayerInput, context) == 16, "input context ABI");
_Static_assert(sizeof(KeelPlayerInputApi) == 16, "input service ABI");
_Static_assert((KEELS2_BUTTON_FORWARD | KEELS2_BUTTON_USE) == 17, "normalized action bits");

static uint64_t NewlyHeld(const KeelPlayerInput* previous, const KeelPlayerInput* current)
{
    if (!previous || previous->context != current->context) return 0;
    return current->buttons & ~previous->buttons;
}

int main(void)
{
    const uint64_t actions[] = {
        KEELS2_BUTTON_FORWARD, KEELS2_BUTTON_BACK, KEELS2_BUTTON_LEFT, KEELS2_BUTTON_RIGHT,
        KEELS2_BUTTON_USE, KEELS2_BUTTON_RELOAD, KEELS2_BUTTON_JUMP, KEELS2_BUTTON_DUCK,
        KEELS2_BUTTON_ATTACK, KEELS2_BUTTON_ATTACK2, KEELS2_BUTTON_WALK, KEELS2_BUTTON_SCORE,
        KEELS2_BUTTON_ZOOM, KEELS2_BUTTON_INSPECT, KEELS2_BUTTON_USE_OR_RELOAD
    };
    uint64_t combined = 0;
    for (size_t index = 0; index < sizeof(actions) / sizeof(actions[0]); ++index)
    {
        if (!actions[index] || (actions[index] & (actions[index] - 1)) || (combined & actions[index])) return 1;
        combined |= actions[index];
    }
    if (combined != KEELS2_BUTTON_ALL) return 2;

    const KeelPlayerInput previous = {sizeof(previous), 0, KEELS2_BUTTON_FORWARD, 7};
    KeelPlayerInput current = {sizeof(current), 0, KEELS2_BUTTON_FORWARD | KEELS2_BUTTON_USE, 7};
    if (NewlyHeld(NULL, &current) || NewlyHeld(&previous, &current) != KEELS2_BUTTON_USE ||
        NewlyHeld(&current, &current)) return 3;
    current.context = 8;
    if (NewlyHeld(&previous, &current)) return 4;
    current.buttons = 0;
    return NewlyHeld(&previous, &current) != 0;
}
