#include <driver/gpio.h>
#if CONFIG_IDF_TARGET_ESP32S3
#include <soc/soc.h>
#include <soc/usb_serial_jtag_reg.h>
#endif

#define UC1611S_WIDTH  128
#define UC1611S_HEIGHT 160
#define UC1611S_PAGES  (UC1611S_HEIGHT / 4)
#define UC1611S_SW_WIDTH  160
#define UC1611S_SW_HEIGHT 128

static uint16_t lcd_buffer[LCD_BUFFER_LENGTH];
static uint8_t gray_fb[UC1611S_SW_HEIGHT][UC1611S_SW_WIDTH];
static uint8_t drv_fb[UC1611S_PAGES][UC1611S_WIDTH];
static uint8_t sent_fb[UC1611S_PAGES][UC1611S_WIDTH];
static bool sent_fb_valid;
static int window_left;
static int window_top;
static int window_width;
static int window_height;
static int window_cursor;
static rg_mutex_t *lcd_sync_lock;

static const gpio_num_t lcd_data_pins[8] = {
    RG_GPIO_LCD_D0, RG_GPIO_LCD_D1, RG_GPIO_LCD_D2, RG_GPIO_LCD_D3,
    RG_GPIO_LCD_D4, RG_GPIO_LCD_D5, RG_GPIO_LCD_D6, RG_GPIO_LCD_D7,
};

static inline void lcd_write_bus(uint8_t value)
{
    for (int i = 0; i < 8; ++i)
        gpio_set_level(lcd_data_pins[i], (value >> i) & 1);

    gpio_set_level(RG_GPIO_LCD_WR, 0);
    rg_usleep(1);
    gpio_set_level(RG_GPIO_LCD_WR, 1);
}

static inline void lcd_write_command(uint8_t command)
{
    gpio_set_level(RG_GPIO_LCD_DC, 0);
    gpio_set_level(RG_GPIO_LCD_CS, 0);
    lcd_write_bus(command);
    gpio_set_level(RG_GPIO_LCD_CS, 1);
}

static inline void lcd_write_data(uint8_t data)
{
    gpio_set_level(RG_GPIO_LCD_DC, 1);
    gpio_set_level(RG_GPIO_LCD_CS, 0);
    lcd_write_bus(data);
    gpio_set_level(RG_GPIO_LCD_CS, 1);
}

static void lcd_set_contrast(uint8_t contrast)
{
    if (lcd_sync_lock && !rg_mutex_take(lcd_sync_lock, -1))
        return;

    lcd_write_command(0x81);
    lcd_write_data(contrast);

    if (lcd_sync_lock)
        rg_mutex_give(lcd_sync_lock);
}

static inline void uc1611s_set_page(uint8_t page)
{
    lcd_write_command(0x70 | ((page >> 4) & 0x0F));
    lcd_write_command(0x60 | (page & 0x0F));
}

static inline void uc1611s_set_column(uint8_t column)
{
    lcd_write_command(0x04 | ((column >> 8) & 0x03));
    lcd_write_data(column & 0xFF);
}

static inline uint8_t rgb565_to_luma(uint16_t color)
{
    int r = (color >> 11) & 0x1F;
    int g = (color >> 5) & 0x3F;
    int b = color & 0x1F;
    r = (r * 255) / 31;
    g = (g * 255) / 63;
    b = (b * 255) / 31;
    return (uint8_t)((r * 30 + g * 59 + b * 11) / 100);
}

static inline uint8_t rgb565_to_gray2(uint16_t color)
{
    uint8_t luma = rgb565_to_luma(color);
    if (luma < 64) //0-63
        return 3;
    if (luma < 112) //64-127 
        return 2;
    if (luma < 192) //128-191
        return 1;
    return 0;
}

static inline bool uc1611s_map_pixel(int x, int y, uint8_t *dx, uint8_t *dy)
{
    if (x < 0 || y < 0 || x >= UC1611S_SW_WIDTH || y >= UC1611S_SW_HEIGHT)
        return false;

    *dx = UC1611S_WIDTH - 1 - y;
    *dy = x + 1;
    if (*dy >= UC1611S_HEIGHT)
        *dy = UC1611S_HEIGHT - 1;
    return true;
}

static void lcd_set_backlight(float percent)
{
#if defined(RG_GPIO_LCD_BCKL)
    gpio_set_level(RG_GPIO_LCD_BCKL, percent > 0 ? 1 : 0);
#else
    (void)percent;
#endif
}

static void lcd_set_window(int left, int top, int width, int height)
{
    if (left < 0 || top < 0 || left + width > UC1611S_SW_WIDTH || top + height > UC1611S_SW_HEIGHT)
        RG_LOGW("Bad lcd window (x=%d, y=%d, w=%d, h=%d)\n", left, top, width, height);

    window_left = left;
    window_top = top;
    window_width = width;
    window_height = height;
    window_cursor = 0;
}

static inline uint16_t *lcd_get_buffer(size_t length)
{
    (void)length;
    return lcd_buffer;
}

static inline void lcd_send_buffer(uint16_t *buffer, size_t length)
{
    if (window_width <= 0 || window_height <= 0 || length == 0)
        return;

    size_t max_length = (size_t)window_width * window_height;
    if (length > max_length)
        length = max_length;

    for (size_t i = 0; i < length; ++i)
    {
        int pixel = window_cursor++;
        int x = window_left + (pixel % window_width);
        int y = window_top + (pixel / window_width);

        if (x < 0 || y < 0 || x >= UC1611S_SW_WIDTH || y >= UC1611S_SW_HEIGHT)
            continue;

        gray_fb[y][x] = rgb565_to_gray2(buffer[i]) & 0x03;
    }
}

static void lcd_sync(void)
{
    if (lcd_sync_lock && !rg_mutex_take(lcd_sync_lock, -1))
        return;

    memset(drv_fb, 0, sizeof(drv_fb));

    for (uint16_t y = 0; y < UC1611S_SW_HEIGHT; ++y)
    {
        for (uint16_t x = 0; x < UC1611S_SW_WIDTH; ++x)
        {
            uint8_t dx, dy;
            uc1611s_map_pixel(x, y, &dx, &dy);

            uint8_t gray = gray_fb[y][x] & 0x03;
            uint8_t page = dy / 4;
            uint8_t shift = (dy % 4) * 2;
            drv_fb[page][dx] &= ~(0x03 << shift);
            drv_fb[page][dx] |= ((gray & 0x03) << shift);
        }
    }

    for (uint8_t page = 0; page < UC1611S_PAGES; ++page)
    {
        uint16_t col = 0;

        while (col < UC1611S_WIDTH)
        {
            uint16_t first;

            while (col < UC1611S_WIDTH && sent_fb_valid &&
                   drv_fb[page][col] == sent_fb[page][col])
                ++col;
            if (col >= UC1611S_WIDTH)
                break;

            first = col;
            while (col < UC1611S_WIDTH &&
                   (!sent_fb_valid || drv_fb[page][col] != sent_fb[page][col]))
                ++col;

            uc1611s_set_page(page);
            uc1611s_set_column((uint8_t)first);
            for (uint16_t changed_col = first; changed_col < col; ++changed_col)
            {
                lcd_write_command(0x01);
                lcd_write_data(drv_fb[page][changed_col]);
                sent_fb[page][changed_col] = drv_fb[page][changed_col];
            }
        }
    }
    sent_fb_valid = true;

    if (lcd_sync_lock)
        rg_mutex_give(lcd_sync_lock);
}

static void lcd_init(void)
{
    lcd_sync_lock = rg_mutex_create();
    if (!lcd_sync_lock)
        RG_LOGE("Failed to create UC1611S display lock\n");

    uint64_t lcd_pin_mask =
        (1ULL << RG_GPIO_LCD_WR) |
        (1ULL << RG_GPIO_LCD_CS) |
        (1ULL << RG_GPIO_LCD_DC);

#ifdef RG_GPIO_LCD_RD
    lcd_pin_mask |= 1ULL << RG_GPIO_LCD_RD;
#endif
#ifdef RG_GPIO_LCD_RST
    lcd_pin_mask |= 1ULL << RG_GPIO_LCD_RST;
#endif
#ifdef RG_GPIO_LCD_BCKL
    lcd_pin_mask |= 1ULL << RG_GPIO_LCD_BCKL;
#endif
#ifdef RG_GPIO_LCD_CTRL
    lcd_pin_mask |= 1ULL << RG_GPIO_LCD_CTRL;
#endif

    for (int i = 0; i < 8; ++i)
        lcd_pin_mask |= 1ULL << lcd_data_pins[i];

#if CONFIG_IDF_TARGET_ESP32S3
    bool usb_pad_before = REG_GET_BIT(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE) != 0;
#endif

    gpio_config_t lcd_gpio_config = {
        .pin_bit_mask = lcd_pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&lcd_gpio_config));

#if CONFIG_IDF_TARGET_ESP32S3
    bool usb_pad_after = REG_GET_BIT(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE) != 0;
    RG_LOGI("LCD GPIO configured: mask=0x%llx, USB pad before=%d after=%d, D6=%d D7=%d\n",
            (unsigned long long)lcd_pin_mask, usb_pad_before, usb_pad_after,
            RG_GPIO_LCD_D6, RG_GPIO_LCD_D7);
#else
    RG_LOGI("LCD GPIO configured: mask=0x%llx\n", (unsigned long long)lcd_pin_mask);
#endif

#ifdef RG_GPIO_LCD_BCKL
    gpio_set_level(RG_GPIO_LCD_BCKL, 0);
#endif
#ifdef RG_GPIO_LCD_CTRL
    gpio_set_level(RG_GPIO_LCD_CTRL, 1);
#endif

    gpio_set_level(RG_GPIO_LCD_WR, 1);
    gpio_set_level(RG_GPIO_LCD_CS, 1);
    gpio_set_level(RG_GPIO_LCD_DC, 1);
#ifdef RG_GPIO_LCD_RD
    gpio_set_level(RG_GPIO_LCD_RD, 1);
#endif

    for (int i = 0; i < 8; ++i)
        gpio_set_level(lcd_data_pins[i], 0);

#ifdef RG_GPIO_LCD_RST
    gpio_set_level(RG_GPIO_LCD_RST, 1);
    rg_usleep(10 * 1000);
    gpio_set_level(RG_GPIO_LCD_RST, 0);
    rg_usleep(20 * 1000);
    gpio_set_level(RG_GPIO_LCD_RST, 1);
    rg_usleep(60 * 1000);
#endif

    lcd_write_command(0xE1);
    lcd_write_data(0xE2);
    rg_usleep(20 * 1000);

    lcd_write_command(0x27);
    lcd_write_command(0x2D);

    lcd_set_contrast(0xA8); // ?A0

    lcd_write_command(0x89);
    lcd_write_command(0xEA);

    lcd_write_command(0xF1);
    lcd_write_data(0x9F);

    lcd_write_command(0xC4);
    lcd_write_command(0x94);

    lcd_write_command(0x04);
    lcd_write_data(0x00);

    lcd_write_command(0x60);
    lcd_write_command(0x70);

    lcd_write_command(0xC9);
    lcd_write_data(0xAF);

    lcd_write_command(0xA4);
    lcd_write_command(0xA9);

    RG_LOGI("UC1611S display initialized\n");

    memset(gray_fb, 0, sizeof(gray_fb));
    memset(drv_fb, 0, sizeof(drv_fb));
    memset(sent_fb, 0, sizeof(sent_fb));
    sent_fb_valid = false;
}

static void lcd_deinit(void)
{
   
    lcd_write_command(0xAE); // Display off
#ifdef RG_GPIO_LCD_BCKL
    gpio_set_level(RG_GPIO_LCD_BCKL, 0);
#endif
#ifdef RG_GPIO_LCD_CTRL
    gpio_set_level(RG_GPIO_LCD_CTRL, 0);
#endif
    if (lcd_sync_lock)
    {
        rg_mutex_free(lcd_sync_lock);
        lcd_sync_lock = NULL;
    }
}

const rg_display_driver_t rg_display_driver_uc1611s = {
    .name = "uc1611s",
};
