#include "rgb_led_driver.h"

#include "led_strip.h"
#include "bsp_board.h"

static led_strip_handle_t led_strip;

void rgb_led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO_PIN,
        .max_leds = LED_STRIP_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };

    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    led_strip_clear(led_strip);
}

void rgb_led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < LED_STRIP_LED_COUNT; i++)
    {
        led_strip_set_pixel(led_strip, i, r, g, b);
    }

    led_strip_refresh(led_strip);
}

void rgb_led_off(void)
{
    led_strip_clear(led_strip);
}
