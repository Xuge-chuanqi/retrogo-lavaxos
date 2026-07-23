// Target definition
#define RG_TARGET_NAME              "ONE"
#define RG_DEFAULT_TIMEZONE         "UTC+8"
#define RG_LANG_DEFAULT             RG_LANG_ZH
#define RG_PREVIEW_DEFAULT          PREVIEW_MODE_NONE
#define RG_FONT_DEFAULT             RG_FONT_PINGFANG_ZH_12

// Storage
#define RG_STORAGE_ROOT             "/sd"
#define RG_STORAGE_SDSPI_HOST       SPI3_HOST
#define RG_STORAGE_SDSPI_SPEED      SDMMC_FREQ_DEFAULT

// Audio
#define RG_AUDIO_USE_INT_DAC        0
#define RG_AUDIO_USE_EXT_DAC        1
#define RG_AUDIO_FORCE_EXT_DAC      1

// Video: UC1611S 160x128 logical, 128x160 physical 2bpp parallel LCD
#define RG_SCREEN_DRIVER            3
#define RG_SCREEN_BACKLIGHT         1
#define RG_SCREEN_WIDTH             160
#define RG_SCREEN_HEIGHT            128
#define RG_SCREEN_ROTATE            0
#define RG_SCREEN_VISIBLE_AREA      {0, 0, 0, 0}
#define RG_SCREEN_SAFE_AREA         {0, 0, 0, 0}

// Input: 74HC165 plus direct system keys
#define RG_RECOVERY_BTN              RG_KEY_MENU
#define RG_GAMEPAD_SERIAL_BITS       8
#define RG_GAMEPAD_SERIAL_MAP {\
    {RG_KEY_LEFT,   .num = 0, .level = 1},\
    {RG_KEY_DOWN,   .num = 1, .level = 1},\
    {RG_KEY_UP,     .num = 2, .level = 1},\
    {RG_KEY_RIGHT,  .num = 3, .level = 1},\
    {RG_KEY_B,      .num = 4, .level = 1},\
    {RG_KEY_A,      .num = 5, .level = 1},\
    {RG_KEY_Y,      .num = 6, .level = 1},\
    {RG_KEY_X,      .num = 7, .level = 1},\
}
#define RG_GAMEPAD_GPIO_MAP {\
    {RG_KEY_MENU,   .num = GPIO_NUM_12, .pullup = 1, .level = 0},\
    {RG_KEY_SELECT, .num = GPIO_NUM_21, .pullup = 1, .level = 0},\
    {RG_KEY_START,  .num = GPIO_NUM_0,  .pullup = 1, .level = 0},\
}

// Battery
#define RG_BATTERY_DRIVER           1
#define RG_BATTERY_ADC_UNIT         ADC_UNIT_1
#define RG_BATTERY_ADC_CHANNEL      ADC_CHANNEL_0
#define RG_BATTERY_CALC_PERCENT(raw) (((raw) * 2.f - 3000.f) / (4200.f - 3000.f) * 100.f)
#define RG_BATTERY_CALC_VOLTAGE(raw) ((raw) * 2.f * 0.001f)

// I2C BUS and PCF8563 hardware RTC
#define RG_GPIO_I2C_SDA             GPIO_NUM_16
#define RG_GPIO_I2C_SCL             GPIO_NUM_4
#define RG_RTC_PCF8563              1

// LCD parallel bus
#define RG_GPIO_LCD_WR              GPIO_NUM_11
// LCD RD is held inactive by hardware; GPIO12 is the direct M/Menu key.
#define RG_GPIO_LCD_BCKL            GPIO_NUM_13
#define RG_GPIO_LCD_RST             GPIO_NUM_47
#define RG_GPIO_LCD_CS              GPIO_NUM_48
#define RG_GPIO_LCD_DC              GPIO_NUM_45
#define RG_GPIO_LCD_D0              GPIO_NUM_10
#define RG_GPIO_LCD_D1              GPIO_NUM_9
#define RG_GPIO_LCD_D2              GPIO_NUM_3
#define RG_GPIO_LCD_D3              GPIO_NUM_17
#define RG_GPIO_LCD_D4              GPIO_NUM_18
#define RG_GPIO_LCD_D5              GPIO_NUM_8
#define RG_GPIO_LCD_D6              GPIO_NUM_19
#define RG_GPIO_LCD_D7              GPIO_NUM_20

// SD card SPI
#define RG_GPIO_SDSPI_MISO          GPIO_NUM_15
#define RG_GPIO_SDSPI_MOSI          GPIO_NUM_6
#define RG_GPIO_SDSPI_CLK           GPIO_NUM_7
#define RG_GPIO_SDSPI_CS            GPIO_NUM_5

// 74HC165 gamepad
#define RG_GPIO_GAMEPAD_DATA        GPIO_NUM_38
#define RG_GPIO_GAMEPAD_LATCH       GPIO_NUM_39
#define RG_GPIO_GAMEPAD_CLOCK       GPIO_NUM_2

// External I2S DAC
#define RG_GPIO_SND_I2S_DATA        GPIO_NUM_40
#define RG_GPIO_SND_I2S_BCK         GPIO_NUM_41
#define RG_GPIO_SND_I2S_WS          GPIO_NUM_42
#define RG_GPIO_SND_AMP_ENABLE      GPIO_NUM_46

#define RG_UPDATER_ENABLE           1
#define RG_UPDATER_APPLICATION      RG_APP_FACTORY
#define RG_UPDATER_DOWNLOAD_LOCATION RG_STORAGE_ROOT "/one/firmware"
