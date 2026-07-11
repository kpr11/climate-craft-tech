#include "mic_speech.h"

#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "model_path.h"
#include "esp_process_sdkconfig.h"
#include "esp_mn_speech_commands.h"

#include "bsp_board.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

static const char *TAG = "App/Speech";

#define VOICE_SESSION_TIMEOUT_US   (60LL * 1000000LL)

int wakeup_flag = 0;
static esp_afe_sr_iface_t *afe_handle = NULL;
static volatile int task_flag = 0;
static volatile bool feed_paused = false;
srmodel_list_t *models = NULL;
static esp_sr_event_callback_t spench_callback = NULL;

void Speech_set_feed_paused(bool paused)
{
    feed_paused = paused;
}

static int64_t voice_session_deadline = 0;

static void voice_session_extend(void)
{
    voice_session_deadline = esp_timer_get_time() + VOICE_SESSION_TIMEOUT_US;
}

static bool voice_session_expired(void)
{
    return esp_timer_get_time() > voice_session_deadline;
}

void feed_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int nch = afe_handle->get_feed_channel_num(afe_data);
    int feed_channel = esp_get_feed_channel();

    assert(nch == feed_channel);

    int16_t *i2s_buff = heap_caps_malloc(
        audio_chunksize * sizeof(int16_t) * feed_channel,
        MALLOC_CAP_SPIRAM
    );

    assert(i2s_buff);

    esp_task_wdt_add(NULL);
    int feed_log_counter = 0;

    while (task_flag)
    {
        esp_get_feed_data(true, i2s_buff, audio_chunksize * sizeof(int16_t) * feed_channel);

        if (!feed_paused)
        {
            afe_handle->feed(afe_data, i2s_buff);
        }

        esp_task_wdt_reset();

        if (++feed_log_counter >= 100)
        {
            feed_log_counter = 0;
            int16_t peak = 0;
            for (int i = 0; i < audio_chunksize * feed_channel; i++)
            {
                int16_t v = i2s_buff[i] < 0 ? -i2s_buff[i] : i2s_buff[i];
                if (v > peak) peak = v;
            }
            ESP_LOGI(TAG, "Mic input peak level: %d / 32767", peak);
        }
    }

    if (i2s_buff)
    {
        free(i2s_buff);
        i2s_buff = NULL;
    }

    vTaskDelete(NULL);
}

void detect_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;

    int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);

    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    printf("multinet:%s\n", mn_name);

    esp_mn_iface_t *multinet = esp_mn_handle_from_name(mn_name);
    model_iface_data_t *model_data = multinet->create(mn_name, 3000);

    int mu_chunksize = multinet->get_samp_chunksize(model_data);

    esp_mn_commands_alloc(multinet, model_data);
    esp_mn_commands_add(0, "move up");
    esp_mn_commands_add(1, "move down");
    esp_mn_commands_add(2, "stop now");
    esp_mn_commands_update();

    multinet->set_det_threshold(model_data, 0.17);

    assert(mu_chunksize == afe_chunksize);

    multinet->print_active_speech_commands(model_data);

    esp_task_wdt_add(NULL);

    while (task_flag)
    {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);

        if (!res || res->ret_value == ESP_FAIL)
        {
            printf("fetch error!\n");
            break;
        }

        if (res->wakeup_state == WAKENET_DETECTED)
        {
            multinet->clean(model_data);
        }

        if (res->raw_data_channels == 1 && res->wakeup_state == WAKENET_DETECTED)
        {
            wakeup_flag = 1;
            voice_session_extend();
            afe_handle->disable_wakenet(afe_data);

            esp_sr_evt_data_t evtdata;
            evtdata.awaken_channel = 0;

            if (spench_callback != NULL)
            {
                spench_callback(ESP_SR_EVT_AWAKEN, evtdata, NULL);
            }
        }
        else if (res->raw_data_channels > 1 && res->wakeup_state == WAKENET_CHANNEL_VERIFIED)
        {
            esp_sr_evt_data_t evtdata;
            evtdata.awaken_channel = res->trigger_channel_id;

            if (spench_callback != NULL)
            {
                spench_callback(ESP_SR_EVT_AWAKEN, evtdata, NULL);
            }

            wakeup_flag = 1;
            voice_session_extend();
            afe_handle->disable_wakenet(afe_data);
            multinet->clean(model_data);
        }

        esp_task_wdt_reset();

        if (wakeup_flag == 1)
        {
            if (voice_session_expired())
            {
                ESP_LOGI(TAG, "Voice session expired. Going back to wake-word mode.");

                afe_handle->enable_wakenet(afe_data);
                wakeup_flag = 0;

                esp_sr_evt_data_t evtdata;
                if (spench_callback != NULL)
                {
                    spench_callback(ESP_SR_EVT_CMD_TIMEOUT, evtdata, NULL);
                }

                continue;
            }

            esp_mn_state_t mn_state = multinet->detect(model_data, res->data);

            if (mn_state == ESP_MN_STATE_DETECTING)
            {
                continue;
            }

            if (mn_state == ESP_MN_STATE_DETECTED)
            {
                esp_mn_results_t *mn_result = multinet->get_results(model_data);

                ESP_LOGI(TAG, "====================================");
                ESP_LOGI(TAG, "COMMAND DETECTED");
                ESP_LOGI(TAG, "Command ID : %d", mn_result->command_id[0]);
                ESP_LOGI(TAG, "Phrase ID  : %d", mn_result->phrase_id[0]);
                ESP_LOGI(TAG, "Text       : %s", mn_result->string);
                ESP_LOGI(TAG, "====================================");

                voice_session_extend();

                esp_sr_evt_data_t evtdata;
                evtdata.sr_cmd = mn_result->command_id[0];

                if (spench_callback != NULL)
                {
                    spench_callback(ESP_SR_EVT_CMD, evtdata, NULL);
                }

                multinet->clean(model_data);
                continue;
            }

            if (mn_state == ESP_MN_STATE_TIMEOUT)
            {
                if (!voice_session_expired())
                {
                    ESP_LOGI(TAG, "Command timeout, but session still active.");
                    multinet->clean(model_data);
                    continue;
                }

                ESP_LOGI(TAG, "Command timeout. Going back to wake-word mode.");

                afe_handle->enable_wakenet(afe_data);
                wakeup_flag = 0;

                esp_sr_evt_data_t evtdata;
                if (spench_callback != NULL)
                {
                    spench_callback(ESP_SR_EVT_CMD_TIMEOUT, evtdata, NULL);
                }

                continue;
            }
        }

        esp_task_wdt_reset();
    }

    if (model_data)
    {
        multinet->destroy(model_data);
        model_data = NULL;
    }

    printf("detect exit\n");
    vTaskDelete(NULL);
}

void Speech_Init(void)
{
    models = esp_srmodel_init("model");

afe_config_t *afe_config = afe_config_init(
    esp_get_input_format(),
    models,
    AFE_TYPE_SR,
    AFE_MODE_LOW_COST
);

afe_config->ns_init = false;
afe_config->vad_init = true;
afe_config->aec_init = false;
afe_config->se_init = false;

    afe_handle = esp_afe_handle_from_config(afe_config);
    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);

    afe_config_free(afe_config);

    task_flag = 1;

    xTaskCreatePinnedToCore(&detect_Task, "detect", 8 * 1024, (void *)afe_data, 5, NULL, 1);
    xTaskCreatePinnedToCore(&feed_Task, "feed", 8 * 1024, (void *)afe_data, 5, NULL, 0);
}

esp_err_t Speech_register_callback(esp_sr_event_callback_t callback)
{
    if (!callback)
    {
        return ESP_ERR_INVALID_ARG;
    }

    spench_callback = callback;
    return ESP_OK;
}