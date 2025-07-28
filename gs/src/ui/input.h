#ifndef GS_UI_INPUT_H
#define GS_UI_INPUT_H
typedef enum {
    KEYPAD_BUTTON_B = 0,
    KEYPAD_BUTTON_A,
    KEYPAD_BUTTON_X,
    KEYPAD_BUTTON_Y,
    KEYPAD_BUTTON_LB,
    KEYPAD_BUTTON_RB,
    KEYPAD_BUTTON_LT,
    KEYPAD_BUTTON_RT,
    KEYPAD_BUTTON_SELECT,
    KEYPAD_BUTTON_START,
    KEYPAD_BUTTON_UNKNOWN1, // Placeholder for unknown button
    KEYPAD_BUTTON_L3,
    KEYPAD_BUTTON_R3,
    KEYPAD_BUTTON_UP,
    KEYPAD_BUTTON_DOWN,
    KEYPAD_BUTTON_LEFT,
    KEYPAD_BUTTON_RIGHT
} keypad_button_t;

int ui_keypad_init(void);
void ui_keypad_deinit(void);


#endif // GS_UI_INPUT_H