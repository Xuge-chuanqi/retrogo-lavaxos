#include "rg_system.h"

#if RG_AUDIO_USE_BUZZER_PIN
#include "rg_audio.h"

#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "esp_timer.h"

#define BOOSTVOLUME        3     /* 这里如果把 3 改成 1，消除 80% 的过载杂音！ 但是音量会变小*/
#define MS_OF_CACHED_SAMPLES 200  
#define LEDC_PWM_SPEED_MODE  LEDC_LOW_SPEED_MODE
#define LEDC_PWM_CHANNEL     LEDC_CHANNEL_0
#define LEDC_PWM_TIMER       LEDC_TIMER_0
#define AUDIO_TASK_STACK_SIZE 2048
#define AUDIO_TASK_PRIORITY   5
#define AUDIO_TASK_CORE       1  /* 核心魔法：让声音独占副核心，保全游戏帧率！ */

static QueueHandle_t sampleQueue;
static TaskHandle_t audioTaskHandle = NULL;
static int pwm_bits = 10;
static int sampleRate;
static volatile bool audioRunning = false;
static volatile int currentVolume = 50;
static volatile bool isMuted = false;

// Audio task — runs on separate core to avoid blocking the main thread
static void buzzer_audio_task(void *arg)
{
    int16_t sample;
    int64_t nextSampleTime = 0;
    int samplePeriod = 1000000 / sampleRate;  

    /* ==================================================== */
    /* 核心魔法：DSP 数字信号滤波器状态变量 (保留上一次的波形状态) */
    int32_t last_in = 0;
    int32_t lpf_out = 0;
    int32_t hpf_out = 0;
    /* ==================================================== */

    RG_LOGI("Ultra DSP Audio task started on core %d, sample rate: %d Hz", xPortGetCoreID(), sampleRate);

    while (audioRunning)
    {
        if (xQueueReceive(sampleQueue, &sample, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            int32_t current_in = sample;

            if (isMuted)
            {
                current_in = 0;
                lpf_out = 0;
                hpf_out = 0;
            }
            else
            {
                /* ====================================================================== */
                /* 第一道工序：数字低通滤波 (LPF) - 打磨毛刺，减少报警器般的刺耳声 */
                /* 算法：当前输出 = 上次输出 + 0.5 * (当前输入 - 上次输出) */
                /* 第一道工序：把原来的 >> 1 改成 >> 2 或 >> 3。数字越大，声音越闷，但沙沙声越小！ */
                lpf_out = lpf_out + ((current_in - lpf_out) >> 1); 

                /* 第二道工序：数字高通滤波 (HPF) - 彻底切除低音，拯救薄膜，让旋律瞬间清晰！ */
                /* 算法：当前输出 = 0.93 * (上次高通输出 + 当前低通输出 - 上次低通输入) */
                hpf_out = (240 * (hpf_out + lpf_out - last_in)) >> 8; 
                last_in = lpf_out;

                /* 第三道工序：注入纯净的音量 */
                current_in = (hpf_out * currentVolume * BOOSTVOLUME) / 100;
                /* ====================================================================== */
            }

            /* 转换为 PWM 占空比 */
            int32_t duty = current_in + 32768;
            duty >>= (16 - pwm_bits);
            
            /* 极限安全锁：防爆音削峰处理 (Soft Clipping) */
            if (duty < 0) duty = 0;
            int32_t max_duty = (1 << pwm_bits) - 1;
            if (duty > max_duty) duty = max_duty;

            /* 微秒级精准时钟卡点输出 */
            int64_t now = esp_timer_get_time();
            if (nextSampleTime == 0)
            {
                nextSampleTime = now + samplePeriod;
            }
            else if (now < nextSampleTime)
            {
                while (esp_timer_get_time() < nextSampleTime) {}
            }
            else
            {
                nextSampleTime = now + samplePeriod;
            }

            ledc_set_duty(LEDC_PWM_SPEED_MODE, LEDC_PWM_CHANNEL, duty);
            ledc_update_duty(LEDC_PWM_SPEED_MODE, LEDC_PWM_CHANNEL);

            nextSampleTime += samplePeriod;
        }
        else
        {
            nextSampleTime = 0;
        }
    }

    ledc_set_duty(LEDC_PWM_SPEED_MODE, LEDC_PWM_CHANNEL, 0);
    ledc_update_duty(LEDC_PWM_SPEED_MODE, LEDC_PWM_CHANNEL);
    vTaskDelete(NULL);
}

static bool buzzer_init(int device, int rate)
{
    sampleRate = rate;
    
    int cacheSamples = (sampleRate / 1000) * MS_OF_CACHED_SAMPLES;
    sampleQueue = xQueueCreate(cacheSamples, sizeof(int16_t));
    if (!sampleQueue) return false;

    int freq = sampleRate;
    while (freq < 16000) freq *= 2;
    if (freq <= 19531)      pwm_bits = 12;
    else if (freq <= 39062) pwm_bits = 11;
    else if (freq <= 78125) pwm_bits = 10;
    else                    pwm_bits = 9;

    ledc_timer_config_t ledc_timer = {
        .duty_resolution = pwm_bits,
        .freq_hz = freq,
        .speed_mode = LEDC_PWM_SPEED_MODE,
        .timer_num = LEDC_PWM_TIMER,
        .clk_cfg = LEDC_USE_APB_CLK,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .channel    = LEDC_PWM_CHANNEL,
        .duty       = 0,
        .gpio_num   = RG_AUDIO_USE_BUZZER_PIN,
        .speed_mode = LEDC_PWM_SPEED_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_PWM_TIMER,
    };
    ledc_channel_config(&ledc_channel);

    audioRunning = true;
    xTaskCreatePinnedToCore(buzzer_audio_task, "buzzer_audio", AUDIO_TASK_STACK_SIZE, NULL, AUDIO_TASK_PRIORITY, &audioTaskHandle, AUDIO_TASK_CORE);

    return true;
}

static bool buzzer_deinit(void)
{
    audioRunning = false;
    if (audioTaskHandle) {
        vTaskDelay(pdMS_TO_TICKS(50));
        audioTaskHandle = NULL;
    }
    if (sampleQueue) {
        vQueueDelete(sampleQueue);
        sampleQueue = NULL;
    }
    ledc_set_duty(LEDC_PWM_SPEED_MODE, LEDC_PWM_CHANNEL, 0);
    ledc_update_duty(LEDC_PWM_SPEED_MODE, LEDC_PWM_CHANNEL);
    return true;
}

static bool buzzer_submit(const rg_audio_frame_t *frames, size_t count)
{
    if (!frames || !count || !audioRunning) return false;

    for (size_t i = 0; i < count; ++i) {
        int16_t left = frames[i].left;
        /* 非阻塞暴力扔数据，如果队列满了就直接丢弃(break)，绝不卡死模拟器 */
        if (xQueueSend(sampleQueue, &left, 0) != pdPASS) {
            break;
        }
    }
    return true;
}

static bool buzzer_set_mute(bool mute)
{
    isMuted = mute;
    if (mute) {
        ledc_set_duty(LEDC_PWM_SPEED_MODE, LEDC_PWM_CHANNEL, 0);
        ledc_update_duty(LEDC_PWM_SPEED_MODE, LEDC_PWM_CHANNEL);
    }
    return true;
}

static bool buzzer_set_volume(int percent)
{
    currentVolume = RG_MIN(RG_MAX(percent, 0), 100);
    return true;
}

const rg_audio_driver_t rg_audio_driver_buzzer = {
    .name = "buzzer",
    .init = buzzer_init,
    .deinit = buzzer_deinit,
    .submit = buzzer_submit,
    .set_mute = buzzer_set_mute,
    .set_volume = buzzer_set_volume,
    .set_sample_rate = NULL,
    .get_error = NULL,
};
#endif