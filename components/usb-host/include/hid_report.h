#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Parsed mouse report, normalized across boot and report protocol layouts
typedef struct {
    union {
        struct {
            uint8_t button1  : 1;  // Left
            uint8_t button2  : 1;  // Right
            uint8_t button3  : 1;  // Middle
            uint8_t reserved : 5;
        };
        uint8_t val;
    } buttons;
    int16_t x_displacement;
    int16_t y_displacement;
    int8_t  scroll;  // Vertical wheel
    int8_t  tilt;    // Horizontal wheel
} hid_mouse_report_t;

/// @brief Parse a raw mouse input report
hid_mouse_report_t hid_parse_mouse_report(const uint8_t* data, int length);

/// @brief Handle a raw mouse report: parse it and inject the resulting BSP input events
void hid_mouse_handle_report(const uint8_t* data, int length);

#ifdef __cplusplus
}
#endif
