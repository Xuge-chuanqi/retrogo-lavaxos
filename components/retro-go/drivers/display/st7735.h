// ST7735 SPI display driver for Retro-Go. Based on ili9341.h but with correct init sequence
// and timing differences: COLMOD=0x05, different power/gamma registers, longer delays.

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <driver/ledc.h>

static spi_device_handle_t spi_dev;
static QueueHandle_t spi_transactions;
static QueueHandle_t spi_buffers;

#define SPI_TRANSACTION_COUNT (10)
#define SPI_BUFFER_COUNT      (5)
#define SPI_BUFFER_LENGTH     (LCD_BUFFER_LENGTH * 2)

#define ST7735_CMD(cmd, data...)                    \
    {                                                \
        const uint8_t c = cmd, x[] = {data};         \
        spi_queue_transaction(&c, 1, 0);             \
        if (sizeof(x))                               \
            spi_queue_transaction(&x, sizeof(x), 1); \
    }

static inline uint16_t *spi_take_buffer(void)
{
    uint16_t *buffer;
    if (xQueueReceive(spi_buffers, &buffer, pdMS_TO_TICKS(2500)) != pdTRUE)
        RG_PANIC("display");
    return buffer;
}

static inline void spi_give_buffer(uint16_t *buffer)
{
    xQueueSend(spi_buffers, &buffer, portMAX_DELAY);
}

static inline void spi_queue_transaction(const void *data, size_t length, uint32_t type)
{
    if (!data || !length)
        return;

    spi_transaction_t *t;
    xQueueReceive(spi_transactions, &t, portMAX_DELAY);

    *t = (spi_transaction_t){
        .tx_buffer = NULL,
        .length = length * 8,
        .user = (void *)type,
        .flags = 0,
    };

    if (type & 2)
        t->tx_buffer = data;
    else if (length < 5)
    {
        memcpy(t->tx_data, data, length);
        t->flags = SPI_TRANS_USE_TXDATA;
    }
    else
    {
        t->tx_buffer = memcpy(spi_take_buffer(), data, length);
        t->user = (void *)(type | 2);
    }

    if (spi_device_queue_trans(spi_dev, t, pdMS_TO_TICKS(2500)) != ESP_OK)
        RG_PANIC("display");
}

IRAM_ATTR
static void spi_pre_transfer_cb(spi_transaction_t *t)
{
    gpio_set_level(RG_GPIO_LCD_DC, (int)t->user & 1);
}

IRAM_ATTR
static void spi_task(void *arg)
{
    spi_transaction_t *t;

    while (spi_device_get_trans_result(spi_dev, &t, portMAX_DELAY) == ESP_OK)
    {
        if ((int)t->user & 2)
            spi_give_buffer((uint16_t *)t->tx_buffer);
        xQueueSend(spi_transactions, &t, portMAX_DELAY);
    }
}

static void spi_init(void)
{
    spi_transactions = xQueueCreate(SPI_TRANSACTION_COUNT, sizeof(spi_transaction_t *));
    spi_buffers = xQueueCreate(SPI_BUFFER_COUNT, sizeof(uint16_t *));

    while (uxQueueSpacesAvailable(spi_transactions))
    {
        void *trans = malloc(sizeof(spi_transaction_t));
        xQueueSend(spi_transactions, &trans, portMAX_DELAY);
    }

    while (uxQueueSpacesAvailable(spi_buffers))
    {
        void *buffer = rg_alloc(SPI_BUFFER_LENGTH, MEM_DMA);
        xQueueSend(spi_buffers, &buffer, portMAX_DELAY);
    }

    const spi_bus_config_t buscfg = {
        .miso_io_num = RG_GPIO_LCD_MISO,
        .mosi_io_num = RG_GPIO_LCD_MOSI,
        .sclk_io_num = RG_GPIO_LCD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    const spi_device_interface_config_t devcfg = {
        .clock_speed_hz = RG_SCREEN_SPEED,
        .mode = 0,
        .spics_io_num = RG_GPIO_LCD_CS,
        .queue_size = SPI_TRANSACTION_COUNT,
        .pre_cb = &spi_pre_transfer_cb,
        .flags = SPI_DEVICE_NO_DUMMY,
    };

    esp_err_t ret;

    ret = spi_bus_initialize(RG_SCREEN_HOST, &buscfg, SPI_DMA_CH_AUTO);
    RG_ASSERT(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE, "spi_bus_initialize failed.");

    ret = spi_bus_add_device(RG_SCREEN_HOST, &devcfg, &spi_dev);
    RG_ASSERT(ret == ESP_OK, "spi_bus_add_device failed.");

    rg_task_create("rg_spi", &spi_task, NULL, 1.5 * 1024, RG_TASK_PRIORITY_7, 1);
}

static void spi_deinit(void)
{
    if (spi_bus_remove_device(spi_dev) == ESP_OK)
        spi_bus_free(RG_SCREEN_HOST);
    else
        RG_LOGE("Failed to properly terminate SPI driver!");
}

static void lcd_set_backlight(float percent)
{
#if defined(RG_GPIO_LCD_BCKL)
    float level = RG_MIN(RG_MAX(percent / 100.f, 0), 1.f);
    int error_code = ledc_set_fade_time_and_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0x1FFF * level, 50, 0);
    if (error_code)
        RG_LOGW("backlight set failed: %d", error_code);
#else
    (void)percent;
#endif
}

#ifndef RG_SCREEN_COL_OFFSET
#define RG_SCREEN_COL_OFFSET 0
#endif
#ifndef RG_SCREEN_ROW_OFFSET
#define RG_SCREEN_ROW_OFFSET 0
#endif

static void lcd_set_window(int left, int top, int width, int height)
{
    int right = left + width - 1;
    int bottom = top + height - 1;

    left += RG_SCREEN_COL_OFFSET;
    right += RG_SCREEN_COL_OFFSET;
    top += RG_SCREEN_ROW_OFFSET;
    bottom += RG_SCREEN_ROW_OFFSET;

    if (left < 0 || top < 0 || right >= display.screen.real_width + RG_SCREEN_COL_OFFSET || bottom >= display.screen.real_height + RG_SCREEN_ROW_OFFSET)
        RG_LOGW("Bad lcd window (x0=%d, y0=%d, x1=%d, y1=%d)\n", left, top, right, bottom);

    ST7735_CMD(0x2A, left >> 8, left & 0xff, right >> 8, right & 0xff);
    ST7735_CMD(0x2B, top >> 8, top & 0xff, bottom >> 8, bottom & 0xff);
    ST7735_CMD(0x2C);
}

static inline uint16_t *lcd_get_buffer(size_t length)
{
    return spi_take_buffer();
}

static inline void lcd_send_buffer(uint16_t *buffer, size_t length)
{
    if (length > 0)
        spi_queue_transaction(buffer, length * sizeof(*buffer), 3);
    else
        spi_give_buffer(buffer);
}

static void lcd_sync(void)
{
}

static void lcd_init(void)
{
#ifdef RG_GPIO_LCD_BCKL
    ledc_timer_config(&(ledc_timer_config_t){
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
    });
    ledc_channel_config(&(ledc_channel_config_t){
        .gpio_num = RG_GPIO_LCD_BCKL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    #ifdef RG_GPIO_LCD_BCKL_INVERT
        .flags.output_invert = 1,
    #endif
    });
    ledc_fade_func_install(0);
#endif

    spi_init();

    gpio_set_direction(RG_GPIO_LCD_DC, GPIO_MODE_OUTPUT);
    gpio_set_level(RG_GPIO_LCD_DC, 1);

#if defined(RG_GPIO_LCD_RST)
    gpio_set_direction(RG_GPIO_LCD_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(RG_GPIO_LCD_RST, 0);
    rg_usleep(10 * 1000);
    gpio_set_level(RG_GPIO_LCD_RST, 1);
    rg_usleep(150 * 1000);
#else
    ST7735_CMD(0x01); // SWRESET
    rg_usleep(150 * 1000);
#endif

    ST7735_CMD(0x11); // SLPOUT
    rg_usleep(150 * 1000);

    // Init sequence from XUEERSI_ESP32_Board-master
    ST7735_CMD(0xB1, 0x01, 0x2C, 0x2D); // FRMCTR1
    ST7735_CMD(0xB2, 0x01, 0x2C, 0x2D); // FRMCTR2
    ST7735_CMD(0xB3, 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D); // FRMCTR3
    ST7735_CMD(0xB4, 0x07);             // INVCTR
    ST7735_CMD(0xC0, 0xA2, 0x02, 0x84); // PWCTR1
    ST7735_CMD(0xC1, 0xC5);             // PWCTR2
    ST7735_CMD(0xC2, 0x0A, 0x00);       // PWCTR3
    ST7735_CMD(0xC3, 0x8A, 0x2A);       // PWCTR4
    ST7735_CMD(0xC4, 0x8A, 0xEE);       // PWCTR5
    ST7735_CMD(0xC5, 0x0E);             // VMCTR1
#ifdef RG_SCREEN_MADCTL
    ST7735_CMD(0x36, RG_SCREEN_MADCTL); // MADCTL
#else
    ST7735_CMD(0x36, 0x00);
#endif
    ST7735_CMD(0x3A, 0x05);             // COLMOD: RGB565
    ST7735_CMD(0xE0, 0x0F, 0x1A, 0x0F, 0x18, 0x2F, 0x28, 0x20, 0x22, 0x1F, 0x1B, 0x23, 0x37, 0x00, 0x07, 0x02, 0x10);
    ST7735_CMD(0xE1, 0x0F, 0x1B, 0x0F, 0x17, 0x33, 0x2C, 0x29, 0x2E, 0x30, 0x30, 0x39, 0x3F, 0x00, 0x07, 0x03, 0x10);
    ST7735_CMD(0x13); // NORON
    rg_usleep(10 * 1000);
    ST7735_CMD(0x29); // DISPON

    RG_LOGI("ST7735 display initialized (160x128)\n");
}

static void lcd_deinit(void)
{
#ifdef RG_SCREEN_DEINIT
    RG_SCREEN_DEINIT();
#endif
    spi_deinit();
}

const rg_display_driver_t rg_display_driver_st7735 = {
    .name = "st7735",
};
