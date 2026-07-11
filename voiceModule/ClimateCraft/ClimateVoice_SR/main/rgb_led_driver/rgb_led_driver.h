#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rgb_led_init(void);
void rgb_led_set_color(uint8_t r, uint8_t g, uint8_t b);
void rgb_led_off(void);

#ifdef __cplusplus
}
#endif
