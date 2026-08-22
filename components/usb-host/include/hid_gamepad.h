#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bsp/input.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Value of dpad_first_button for gamepads whose buttons are all just buttons
#define HID_GAMEPAD_NO_DPAD_BUTTONS (-1)

/// @brief Everything known about a gamepad that its report descriptor does not tell us
///
/// Add a row to hid_gamepad_quirks in hid_gamepad.c to support another gamepad.
typedef struct {
    uint16_t    vid;
    uint16_t    pid;
    const char* name;

    /// Feature report that makes the gamepad start sending input reports, NULL when it needs no nudge
    uint8_t        enable_report_id;
    const uint8_t* enable_report;
    size_t         enable_report_length;

    /// Index of the first of four buttons that act as a d-pad, in the order up, right, down, left
    int dpad_first_button;

    /// Navigation key per button, indexed by button number, BSP_INPUT_NAVIGATION_KEY_NONE to ignore one.
    /// Without this the buttons are taken in the order the gamepad reports them.
    const bsp_input_navigation_key_t* button_map;
    size_t                            button_map_length;
} hid_gamepad_quirk_t;

/// @brief Look up the quirks of a gamepad, NULL when it needs none
const hid_gamepad_quirk_t* hid_gamepad_find_quirk(uint16_t vid, uint16_t pid);

/// @brief Learn the report layout of a connected gamepad from its report descriptor
///
/// @return true when the descriptor describes something usable
bool hid_gamepad_connect(const uint8_t* report_descriptor, size_t length, uint16_t vid, uint16_t pid);

/// @brief Forget the connected gamepad and release every key it was holding
void hid_gamepad_disconnect(void);

/// @brief Handle a raw gamepad report: decode it and inject the resulting BSP input events
void hid_gamepad_handle_report(const uint8_t* data, int length);

#ifdef __cplusplus
}
#endif
