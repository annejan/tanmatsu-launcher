// USB HID gamepad support: turns directions and buttons into BSP navigation events.
//
// Gamepads do not agree on a report layout: one pad puts its stick in a hat switch, the next
// one reports X and Y and never touches the hat, and a DualShock 3 has its d-pad in its
// buttons. Rather than guess, the report descriptor is parsed on connect to find where X, Y,
// the hat switch and the buttons live, and reports are decoded using those bit offsets.

#include "hid_gamepad.h"
#include <string.h>
#include "bsp/input.h"
#include "esp_log.h"
#include "hid_layout.h"

static const char* TAG = "hid_gamepad";

// A DualShock 3 enumerates and hands out its report descriptor, but stays silent until
// the host asks it to start reporting. It has no hat switch either, its d-pad sits in
// buttons five through eight.
static const uint8_t dualshock3_enable_reporting[] = {0x42, 0x0c, 0x00, 0x00};

// Buttons of a DualShock 3, in the order it reports them
static const bsp_input_navigation_key_t dualshock3_button_map[] = {
    BSP_INPUT_NAVIGATION_KEY_SELECT,          // Select
    BSP_INPUT_NAVIGATION_KEY_JOYSTICK_PRESS,  // Left stick
    BSP_INPUT_NAVIGATION_KEY_JOYSTICK_PRESS,  // Right stick
    BSP_INPUT_NAVIGATION_KEY_START,           // Start
    BSP_INPUT_NAVIGATION_KEY_NONE,            // D-pad up
    BSP_INPUT_NAVIGATION_KEY_NONE,            // D-pad right
    BSP_INPUT_NAVIGATION_KEY_NONE,            // D-pad down
    BSP_INPUT_NAVIGATION_KEY_NONE,            // D-pad left
    BSP_INPUT_NAVIGATION_KEY_NONE,            // L2
    BSP_INPUT_NAVIGATION_KEY_NONE,            // R2
    BSP_INPUT_NAVIGATION_KEY_PGUP,            // L1
    BSP_INPUT_NAVIGATION_KEY_PGDN,            // R1
    BSP_INPUT_NAVIGATION_KEY_GAMEPAD_Y,       // Triangle
    BSP_INPUT_NAVIGATION_KEY_GAMEPAD_B,       // Circle, cancels
    BSP_INPUT_NAVIGATION_KEY_GAMEPAD_A,       // Cross, confirms
    BSP_INPUT_NAVIGATION_KEY_GAMEPAD_X,       // Square
    BSP_INPUT_NAVIGATION_KEY_HOME,            // PS button
};

static const hid_gamepad_quirk_t hid_gamepad_quirks[] = {
    {
        .vid                  = 0x054c,
        .pid                  = 0x0268,
        .name                 = "DualShock 3",
        .enable_report_id     = 0xf4,
        .enable_report        = dualshock3_enable_reporting,
        .enable_report_length = sizeof(dualshock3_enable_reporting),
        .dpad_first_button    = 4,
        .button_map           = dualshock3_button_map,
        .button_map_length    = sizeof(dualshock3_button_map) / sizeof(dualshock3_button_map[0]),
    },
};

const hid_gamepad_quirk_t* hid_gamepad_find_quirk(uint16_t vid, uint16_t pid) {
    for (size_t i = 0; i < sizeof(hid_gamepad_quirks) / sizeof(hid_gamepad_quirks[0]); i++) {
        if (hid_gamepad_quirks[i].vid == vid && hid_gamepad_quirks[i].pid == pid) {
            return &hid_gamepad_quirks[i];
        }
    }
    return NULL;
}

typedef struct {
    hid_layout_t layout;

    bool     dpad_is_buttons;  // Four of the buttons are a d-pad rather than face buttons
    uint16_t dpad_first;       // Index of the first of those, they run up, right, down, left

    const bsp_input_navigation_key_t* button_map;  // Navigation key per button, NULL to take them in order
    size_t                            button_map_length;
} hid_gamepad_layout_t;

static hid_gamepad_layout_t layout;

// Navigation keys the buttons map to when a gamepad has no button map of its own
static const bsp_input_navigation_key_t button_keys[] = {
    BSP_INPUT_NAVIGATION_KEY_GAMEPAD_A,
    BSP_INPUT_NAVIGATION_KEY_GAMEPAD_B,
    BSP_INPUT_NAVIGATION_KEY_GAMEPAD_X,
    BSP_INPUT_NAVIGATION_KEY_GAMEPAD_Y,
    BSP_INPUT_NAVIGATION_KEY_START,
    BSP_INPUT_NAVIGATION_KEY_SELECT,
};

#define BUTTON_KEY_COUNT (sizeof(button_keys) / sizeof(button_keys[0]))

// Buttons of one gamepad that can hold a navigation key, the rest is ignored
#define MAX_BUTTON_KEYS 20

// Everything a report turns into: the four directions followed by the buttons
#define NAVIGATION_KEY_COUNT (4 + MAX_BUTTON_KEYS)

static void inject_navigation(bsp_input_navigation_key_t key, bool state) {
    bsp_input_event_t event = {
        .type                      = INPUT_EVENT_TYPE_NAVIGATION,
        .args_navigation.key       = key,
        .args_navigation.modifiers = 0,
        .args_navigation.state     = state,
    };
    bsp_input_inject_event(&event);
}



void hid_gamepad_disconnect(void) {
    memset(&layout, 0, sizeof(layout));
}

bool hid_gamepad_connect(const uint8_t* report_descriptor, size_t length, uint16_t vid, uint16_t pid) {
    hid_gamepad_disconnect();

    if (!hid_layout_parse(report_descriptor, length, &layout.layout)) {
        ESP_LOGW(TAG, "Nothing usable in the report descriptor, ignoring this device");
        return false;
    }

    bool absolute_axes = (layout.layout.x.present && !layout.layout.x.relative) ||
                         (layout.layout.y.present && !layout.layout.y.relative);
    if (!absolute_axes && !layout.layout.hat.present) {
        ESP_LOGW(TAG, "No usable directions in the report descriptor, ignoring this device");
        hid_gamepad_disconnect();
        return false;
    }

    const hid_gamepad_quirk_t* quirk = hid_gamepad_find_quirk(vid, pid);
    if (quirk != NULL) {
        if (quirk->dpad_first_button != HID_GAMEPAD_NO_DPAD_BUTTONS && layout.layout.buttons.present &&
            layout.layout.button_count > quirk->dpad_first_button + 3) {
            layout.dpad_is_buttons = true;
            layout.dpad_first      = (uint16_t)quirk->dpad_first_button;
            ESP_LOGI(TAG, "%s: buttons %d to %d are a d-pad", quirk->name, quirk->dpad_first_button + 1,
                     quirk->dpad_first_button + 4);
        }
        layout.button_map        = quirk->button_map;
        layout.button_map_length = quirk->button_map_length;
    }

    ESP_LOGI(TAG, "Gamepad layout: report id %d, x %d, y %d, hat %d, %d buttons at %d", layout.layout.report_id,
             layout.layout.x.present ? layout.layout.x.bit_offset : -1,
             layout.layout.y.present ? layout.layout.y.bit_offset : -1,
             layout.layout.hat.present ? layout.layout.hat.bit_offset : -1, layout.layout.button_count,
             layout.layout.buttons.present ? layout.layout.buttons.bit_offset : -1);

    return true;
}

void hid_gamepad_handle_report(const uint8_t* data, int length) {
    if (!layout.layout.valid || !hid_layout_strip_report_id(&layout.layout, &data, &length)) {
        return;
    }

    bool                       left = false, right = false, up = false, down = false;
    bsp_input_navigation_key_t keys[NAVIGATION_KEY_COUNT] = {
        BSP_INPUT_NAVIGATION_KEY_LEFT,
        BSP_INPUT_NAVIGATION_KEY_RIGHT,
        BSP_INPUT_NAVIGATION_KEY_UP,
        BSP_INPUT_NAVIGATION_KEY_DOWN,
    };
    bool states[NAVIGATION_KEY_COUNT] = {0};

    hid_layout_axis_directions(data, length, &layout.layout.x, &left, &right);
    hid_layout_axis_directions(data, length, &layout.layout.y, &up, &down);

    if (layout.layout.hat.present) {
        // Eight directions clockwise starting at up, anything else means centered
        int32_t hat = hid_layout_read(data, length, &layout.layout.hat) - layout.layout.hat.logical_min;
        up          = up || (hat == 0 || hat == 1 || hat == 7);
        right       = right || (hat == 1 || hat == 2 || hat == 3);
        down        = down || (hat == 3 || hat == 4 || hat == 5);
        left        = left || (hat == 5 || hat == 6 || hat == 7);
    }

    size_t next_key = 4;
    size_t unmapped = 0;
    for (uint16_t b = 0; b < layout.layout.button_count; b++) {
        bool pressed = hid_layout_read_button(data, length, &layout.layout, b);

        if (layout.dpad_is_buttons && b >= layout.dpad_first && b < layout.dpad_first + 4) {
            switch (b - layout.dpad_first) {
                case 0:
                    up = up || pressed;
                    break;
                case 1:
                    right = right || pressed;
                    break;
                case 2:
                    down = down || pressed;
                    break;
                default:
                    left = left || pressed;
                    break;
            }
            continue;
        }

        bsp_input_navigation_key_t key;
        if (layout.button_map != NULL) {
            // The quirk table knows which button is which on this gamepad
            key = b < layout.button_map_length ? layout.button_map[b] : BSP_INPUT_NAVIGATION_KEY_NONE;
        } else {
            // Gamepads disagree about which button is which, so they are taken in the order reported
            key = unmapped < BUTTON_KEY_COUNT ? button_keys[unmapped] : BSP_INPUT_NAVIGATION_KEY_NONE;
            unmapped++;
        }

        if (key == BSP_INPUT_NAVIGATION_KEY_NONE || next_key >= NAVIGATION_KEY_COUNT) {
            continue;
        }

        keys[next_key]   = key;
        states[next_key] = pressed;
        next_key++;
    }

    states[0] = left;
    states[1] = right;
    states[2] = up;
    states[3] = down;

    // Only send events on state changes, gamepads report their full state continuously
    static uint32_t prev_state = 0;
    uint32_t        state      = 0;

    for (size_t i = 0; i < NAVIGATION_KEY_COUNT; i++) {
        if (states[i]) {
            state |= (1 << i);
        }
        bool was = prev_state & (1 << i);
        if (was != states[i]) {
            inject_navigation(keys[i], states[i]);
            ESP_LOGD(TAG, "Navigation key %d = %d", keys[i], states[i]);
        }
    }

    prev_state = state;
}
