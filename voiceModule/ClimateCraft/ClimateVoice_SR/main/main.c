#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "bsp_board.h"
#include "tca9555_driver.h"
#include "rgb_led_driver.h"
#include "mic_speech.h"
#include "welcome.h"
#include "moving_up.h"
#include "moving_down.h"
#include "yes.h"
#include "stopped.h"
#include "going_to_sleep.h"

#define RELAY_UP      GPIO_NUM_4
#define RELAY_DOWN    GPIO_NUM_5

#define RELAY_ON      0
#define RELAY_OFF     1

static const char *TAG = "VOICE";

static void relay_stop(bool announce)
{
    gpio_set_level(RELAY_UP, RELAY_OFF);
    gpio_set_level(RELAY_DOWN, RELAY_OFF);

    ESP_LOGI(TAG, "STOP - Both Relays OFF");

    if (announce)
    {
        Speech_set_feed_paused(true);
        esp_audio_play(stopped_pcm, (int)stopped_pcm_len, portMAX_DELAY);
        Speech_set_feed_paused(false);
    }
}

static void relay_up(void)
{
    /* Always switch the opposite relay OFF first. */
    gpio_set_level(RELAY_DOWN, RELAY_OFF);
    gpio_set_level(RELAY_UP, RELAY_ON);

    ESP_LOGI(TAG, "UP - Relay1 ON");

    Speech_set_feed_paused(true);
    esp_audio_play(moving_up_pcm, (int)moving_up_pcm_len, portMAX_DELAY);
    Speech_set_feed_paused(false);
}

static void relay_down(void)
{
    /* Always switch the opposite relay OFF first. */
    gpio_set_level(RELAY_UP, RELAY_OFF);
    gpio_set_level(RELAY_DOWN, RELAY_ON);

    ESP_LOGI(TAG, "DOWN - Relay2 ON");

    Speech_set_feed_paused(true);
    esp_audio_play(moving_down_pcm, (int)moving_down_pcm_len, portMAX_DELAY);
    Speech_set_feed_paused(false);
}

static void Speech_event_callback(esp_sr_rec_event_t event,
                                  esp_sr_evt_data_t evt_data,
                                  void *user_data)
{
    (void)user_data;

    switch (event)
    {
        case ESP_SR_EVT_AWAKEN:
            ESP_LOGI(TAG, "Voice Activated - Listening");
            rgb_led_set_color(0, 0, 255);
            Speech_set_feed_paused(true);
            esp_audio_play(yes_pcm, (int)yes_pcm_len, portMAX_DELAY);
            Speech_set_feed_paused(false);
            break;

        case ESP_SR_EVT_CMD:
            ESP_LOGI(TAG, "Command ID = %d", evt_data.sr_cmd);

            switch (evt_data.sr_cmd)
            {
                case 0:
                    rgb_led_set_color(0, 255, 0);
                    relay_up();
                    rgb_led_set_color(0, 0, 255);
                    break;

                case 1:
                    rgb_led_set_color(255, 0, 0);
                    relay_down();
                    rgb_led_set_color(0, 0, 255);
                    break;

                case 2:
                    rgb_led_set_color(255, 160, 0);
                    relay_stop(true);
                    rgb_led_set_color(0, 0, 255);
                    break;

                case 3:
                    relay_down();
                    break;

                default:
                    rgb_led_set_color(255, 160, 0);
                    relay_stop(true);
                    rgb_led_set_color(0, 0, 255);
                    break;
            }
            break;

        case ESP_SR_EVT_CMD_TIMEOUT:
            ESP_LOGI(TAG, "Voice Session Timeout");
            rgb_led_off();
            Speech_set_feed_paused(true);
            esp_audio_play(going_to_sleep_pcm, (int)going_to_sleep_pcm_len, portMAX_DELAY);
            Speech_set_feed_paused(false);
            break;

        default:
            break;
    }
}

void app_main(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask =
            (1ULL << RELAY_UP) |
            (1ULL << RELAY_DOWN),

        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));

    rgb_led_init();
    rgb_led_off();

    /* Keep both motor relays OFF during startup. */
    relay_stop(false);

    /*
     * Initialize microphone, speaker codec and I2S.
     * Audio format used by the project:
     * 16 kHz, 2 channels, 16-bit input to the board driver.
     */
    ESP_ERROR_CHECK(esp_board_init(16000, 2, 16));

    tca9555_driver_init();

    /* Give the speaker codec a short time to become ready. */
    vTaskDelay(pdMS_TO_TICKS(300));

    ESP_LOGI(TAG, "Playing startup voice...");

    esp_err_t audio_result = esp_audio_play(
        welcome_pcm,
        (int)welcome_pcm_len,
        portMAX_DELAY
    );

    if (audio_result == ESP_OK)
    {
        ESP_LOGI(TAG, "Startup voice played successfully");
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Audio playback failed: %s",
            esp_err_to_name(audio_result)
        );
    }

    /* Brief pause before activating the microphones. */
    vTaskDelay(pdMS_TO_TICKS(300));

    relay_stop(false);

    Speech_Init();

    ESP_ERROR_CHECK(
        Speech_register_callback(Speech_event_callback)
    );

    ESP_LOGI(TAG, "ClimateCraft Voice Controller Ready");
    ESP_LOGI(TAG, "ID0 = Relay1 UP");
    ESP_LOGI(TAG, "ID1 = Relay2 DOWN");
    ESP_LOGI(TAG, "ID2 = STOP");
}