#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "lavaxvm.h"
#include "rg_system.h"
#include "rg_display.h"
#include "rg_surface.h"
#include "rg_utils.h"
#include "lodepng.h"
#include "ff.h"

#define LAVAX_DISPLAY_INTERVAL_US 66667

static rg_surface_t *display_surfaces[2];
static rg_surface_t *last_display_surface;
static uint16_t *app_mask_pixels;
static unsigned int app_mask_width;
static unsigned int app_mask_height;
static unsigned int display_surface_index;
static volatile int exit_requested;
static volatile int app_exit_requested;
static int app_exit_combo_down;
static int64_t hardware_tick_time;
static int64_t display_refresh_time;
static int64_t input_poll_time;
static int64_t statistics_tick_time;
static uint32_t app_mask_revision;

extern void LavaHardwareTick(unsigned int ticks);
extern void LavaHardwarePollInput(void);
extern void DmaRefresh(void);

static uint16_t gray_to_rgb565(int gray);

static int append_host_path(char *buffer, size_t size, size_t *length, const char *path)
{
    while (*path)
    {
        unsigned char first = (unsigned char)*path;

        if (first < 0x80)
        {
            if (*length + 1 >= size)
                return 0;
            buffer[(*length)++] = *path++;
        }
        else
        {
            unsigned int oem;
            unsigned int unicode;
            char encoded[4];
            size_t encoded_length;

            if (!path[1])
                return 0;
            oem = ((unsigned int)first << 8) | (unsigned char)path[1];
            unicode = ff_oem2uni(oem, 936);
            if (!unicode)
                return 0;
            encoded_length = rg_utf8_encode(encoded, (int)unicode);
            if (!encoded_length || *length + encoded_length >= size)
                return 0;
            memcpy(buffer + *length, encoded, encoded_length);
            *length += encoded_length;
            path += 2;
        }
    }
    buffer[*length] = 0;
    return 1;
}

const char *lavax_host_path(const char *path, char *buffer, size_t size)
{
    const char *relative = path;
    size_t length = 0;

    if (!path || !buffer || size == 0)
        return NULL;
    if (strncmp(relative, "fat:", 4) == 0)
        relative += 4;
    if (*relative != '/')
        return path;
    if (strncmp(relative, "/sd/", 4) == 0)
    {
        if (size < 1)
            return NULL;
    }
    else
    {
        if (size < 4)
            return NULL;
        memcpy(buffer, "/sd", 3);
        length = 3;
    }
    return append_host_path(buffer, size, &length, relative) ? buffer : NULL;
}

void lavax_platform_bind_display(void *surface0, void *surface1)
{
    display_surfaces[0] = (rg_surface_t *)surface0;
    display_surfaces[1] = (rg_surface_t *)surface1;
    display_surface_index = 0;
    last_display_surface = NULL;
}

void lavax_platform_redraw(void)
{
    if (last_display_surface)
        rg_display_submit(last_display_surface, 0);
}

static void clear_app_mask(void)
{
    free(app_mask_pixels);
    app_mask_pixels = NULL;
    app_mask_width = 0;
    app_mask_height = 0;
}

static int build_app_mask_path(const char *program_path, char *mask_path, size_t size)
{
    char host_buffer[256];
    const char *host_path = lavax_host_path(program_path, host_buffer, sizeof(host_buffer));
    const char *separator;
    const char *extension;
    size_t directory_length;
    size_t name_length;

    if (!host_path || !(separator = strrchr(host_path, '/')))
        return 0;
    extension = strrchr(separator + 1, '.');
    name_length = extension ? (size_t)(extension - separator - 1) : strlen(separator + 1);
    directory_length = (size_t)(separator - host_path + 1);
    if (!name_length || directory_length + name_length + sizeof(".png") > size)
        return 0;
    memcpy(mask_path, host_path, directory_length);
    memcpy(mask_path + directory_length, separator + 1, name_length);
    memcpy(mask_path + directory_length + name_length, ".png", sizeof(".png"));
    return 1;
}

void lavax_platform_set_app_path(const char *path)
{
    char mask_path[256];
    unsigned char *image = NULL;
    unsigned width = 0;
    unsigned height = 0;
    unsigned error;

    clear_app_mask();
    app_mask_revision++;
    if (!path || !build_app_mask_path(path, mask_path, sizeof(mask_path)))
        return;
    error = lodepng_decode32_file(&image, &width, &height, mask_path);
    if (error)
        return;
    if (!width || !height || width > 160 || height > 128)
    {
        free(image);
        RG_LOGW("LavaXOS: mask must be at most 160x128: %s", mask_path);
        return;
    }
    app_mask_pixels = malloc((size_t)width * height * sizeof(*app_mask_pixels));
    if (!app_mask_pixels)
    {
        free(image);
        RG_LOGW("LavaXOS: not enough memory for app mask: %s", mask_path);
        return;
    }
    for (size_t i = 0; i < (size_t)width * height; ++i)
    {
        int alpha = image[i * 4 + 3];
        int luminance = (image[i * 4] * 30 + image[i * 4 + 1] * 59 + image[i * 4 + 2] * 11) / 100;
        int gray = luminance < 70 ? 3 : luminance < 128 ? 2 : luminance < 192 ? 1 : 0;
        uint16_t foreground = gray_to_rgb565(gray);
        int fr = (foreground >> 11) & 31, fg = (foreground >> 5) & 63, fb = foreground & 31;
        app_mask_pixels[i] = (uint16_t)(((fr * alpha / 255) << 11) |
                                        ((fg * alpha / 255) << 5) |
                                        (fb * alpha / 255));
    }
    free(image);
    app_mask_width = width;
    app_mask_height = height;
    RG_LOGI("LavaXOS: loaded app mask %s (%ux%u)", mask_path, width, height);
}

static uint32_t hash_bytes(uint32_t hash, const uint8_t *data, size_t length)
{
    while (length--)
    {
        hash ^= *data++;
        hash *= 16777619u;
    }
    return hash;
}

static bool indexed_frame_changed(const uint8_t *pixels, int width, int height, int stride,
                                  const uint8_t *palette, int graph_mode,
                                  int canvas_width, int canvas_height, int display_scale,
                                  int mask_enabled)
{
    static uint32_t previous_hash;
    static int previous_width;
    static int previous_height;
    static int previous_stride;
    static int previous_graph_mode;
    static bool previous_valid;
    uint32_t hash = 2166136261u;

    for (int y = 0; y < height; ++y)
        hash = hash_bytes(hash, pixels + y * stride, (size_t)width);
    if (palette)
        hash = hash_bytes(hash, palette, 256 * 3);
    hash = hash_bytes(hash, (const uint8_t *)&graph_mode, sizeof(graph_mode));
    hash = hash_bytes(hash, (const uint8_t *)&canvas_width, sizeof(canvas_width));
    hash = hash_bytes(hash, (const uint8_t *)&canvas_height, sizeof(canvas_height));
    hash = hash_bytes(hash, (const uint8_t *)&display_scale, sizeof(display_scale));
    hash = hash_bytes(hash, (const uint8_t *)&mask_enabled, sizeof(mask_enabled));
    hash = hash_bytes(hash, (const uint8_t *)&app_mask_revision, sizeof(app_mask_revision));

    if (previous_valid && hash == previous_hash && width == previous_width &&
        height == previous_height && stride == previous_stride &&
        graph_mode == previous_graph_mode)
        return false;

    previous_hash = hash;
    previous_width = width;
    previous_height = height;
    previous_stride = stride;
    previous_graph_mode = graph_mode;
    previous_valid = true;
    return true;
}

static uint16_t gray_to_rgb565(int gray)
{
    int value = (3 - gray) * 85;
    return (uint16_t)(((value >> 3) << 11) | ((value >> 2) << 5) | (value >> 3));
}

static void calculate_display_viewport(int canvas_width, int canvas_height, int display_scale,
                                       int surface_width, int surface_height,
                                       int *target_x, int *target_y,
                                       int *target_width, int *target_height)
{
    int64_t scaled_width = (int64_t)canvas_width * display_scale;
    int64_t scaled_height = (int64_t)canvas_height * display_scale;

    if (scaled_width <= surface_width && scaled_height <= surface_height)
    {
        *target_width = (int)scaled_width;
        *target_height = (int)scaled_height;
    }
    else if (scaled_width * surface_height > scaled_height * surface_width)
    {
        *target_width = surface_width;
        *target_height = (int)(scaled_height * surface_width / scaled_width);
    }
    else
    {
        *target_height = surface_height;
        *target_width = (int)(scaled_width * surface_height / scaled_height);
    }

    if (*target_width < 1)
        *target_width = 1;
    if (*target_height < 1)
        *target_height = 1;
    *target_x = (surface_width - *target_width) / 2;
    *target_y = (surface_height - *target_height) / 2;
}

static void clear_display_surface(rg_surface_t *surface, int mask_enabled)
{
    int mask_x = app_mask_width < (unsigned)surface->width ?
        (surface->width - (int)app_mask_width) / 2 : 0;
    int mask_y = app_mask_height < (unsigned)surface->height ?
        (surface->height - (int)app_mask_height) / 2 : 0;

    for (int y = 0; y < surface->height; ++y)
    {
        uint16_t *row = (uint16_t *)((uint8_t *)surface->data + y * surface->stride);
        memset(row, 0, (size_t)surface->width * sizeof(uint16_t));
        if (mask_enabled && app_mask_pixels && y >= mask_y && y < mask_y + (int)app_mask_height)
        {
            const uint16_t *source = app_mask_pixels +
                (size_t)(y - mask_y) * app_mask_width;
            memcpy(row + mask_x, source, (size_t)app_mask_width * sizeof(*source));
        }
    }
}

void lavax_platform_present_indexed(const uint8_t *pixels, int width, int height, int stride,
                                    const uint8_t *palette, int graph_mode,
                                    int canvas_width, int canvas_height, int display_scale,
                                    int mask_enabled)
{
    static int logged_width;
    static int logged_height;
    static int logged_stride;
    static int logged_canvas_width;
    static int logged_canvas_height;
    static int logged_scale;
    rg_surface_t *display_surface;
    int target_x, target_y, target_width, target_height;
    int content_x, content_y;
    int x, y;

    if (!display_surfaces[0] || !display_surfaces[1] || !pixels ||
        width <= 0 || height <= 0 || stride < width)
        return;
    if (canvas_width <= 0)
        canvas_width = width;
    if (canvas_height <= 0)
        canvas_height = height;
    if (display_scale < 1)
        display_scale = 1;
    if (!indexed_frame_changed(pixels, width, height, stride, palette, graph_mode,
                               canvas_width, canvas_height, display_scale, mask_enabled))
        return;

    display_surface = display_surfaces[display_surface_index];
    calculate_display_viewport(canvas_width, canvas_height, display_scale,
                               display_surface->width, display_surface->height,
                               &target_x, &target_y, &target_width, &target_height);
    content_x = (canvas_width - width) / 2;
    content_y = (canvas_height - height) / 2;
    if (width != logged_width || height != logged_height || stride != logged_stride ||
        canvas_width != logged_canvas_width || canvas_height != logged_canvas_height ||
        display_scale != logged_scale)
    {
        RG_LOGI("LavaXOS: VM %dx%d stride:%d, canvas %dx%d scale:%d -> surface %dx%d, viewport %dx%d+%d+%d",
                width, height, stride, canvas_width, canvas_height, display_scale,
                display_surface->width, display_surface->height,
                target_width, target_height, target_x, target_y);
        logged_width = width;
        logged_height = height;
        logged_stride = stride;
        logged_canvas_width = canvas_width;
        logged_canvas_height = canvas_height;
        logged_scale = display_scale;
    }
    clear_display_surface(display_surface, mask_enabled);
    for (y = 0; y < target_height; y++)
    {
        int cy = y * canvas_height / target_height;
        int sy = cy - content_y;
        uint16_t *dst = (uint16_t *)((uint8_t *)display_surface->data +
                                     (target_y + y) * display_surface->stride) + target_x;
        for (x = 0; x < target_width; x++)
        {
            int cx = x * canvas_width / target_width;
            int sx = cx - content_x;
            int gray;
            int display_x;
            int display_y;

            if (sx < 0 || sx >= width || sy < 0 || sy >= height)
            {
                dst[x] = 0;
                continue;
            }
            uint8_t index = pixels[sy * stride + sx];
            int luminance = palette ?
                (palette[index * 3] * 30 + palette[index * 3 + 1] * 59 + palette[index * 3 + 2] * 11) / 100 :
                index;
            gray = luminance < 70 ? 3 : luminance < 128 ? 2 : luminance < 192 ? 1 : 0;
            display_x = target_x + x;
            display_y = target_y + y;
            if ((gray == 2 && ((display_x + display_y * 3) & 3) == 0) ||
                (gray == 1 && ((display_x * 3 + display_y) & 7) == 0))
                gray += gray == 2 ? -1 : 1;
            dst[x] = gray_to_rgb565(gray);
        }
    }
    rg_display_submit(display_surface, 0);
    last_display_surface = display_surface;
    display_surface_index ^= 1;
}

void lavax_platform_present_rgb555(const uint16_t *pixels, int width, int height, int stride,
                                   int canvas_width, int canvas_height, int display_scale,
                                   int mask_enabled)
{
    rg_surface_t *display_surface;
    int target_x, target_y, target_width, target_height;
    int content_x, content_y;
    int x, y;

    if (!display_surfaces[0] || !display_surfaces[1] || !pixels ||
        width <= 0 || height <= 0 || stride < width)
        return;
    if (canvas_width <= 0)
        canvas_width = width;
    if (canvas_height <= 0)
        canvas_height = height;
    if (display_scale < 1)
        display_scale = 1;
    display_surface = display_surfaces[display_surface_index];
    calculate_display_viewport(canvas_width, canvas_height, display_scale,
                               display_surface->width, display_surface->height,
                               &target_x, &target_y, &target_width, &target_height);
    content_x = (canvas_width - width) / 2;
    content_y = (canvas_height - height) / 2;
    clear_display_surface(display_surface, mask_enabled);
    for (y = 0; y < target_height; y++)
    {
        int cy = y * canvas_height / target_height;
        int sy = cy - content_y;
        uint16_t *dst = (uint16_t *)((uint8_t *)display_surface->data +
                                     (target_y + y) * display_surface->stride) + target_x;
        for (x = 0; x < target_width; x++)
        {
            int cx = x * canvas_width / target_width;
            int sx = cx - content_x;

            if (sx < 0 || sx >= width || sy < 0 || sy >= height)
            {
                dst[x] = 0;
                continue;
            }
            uint16_t color = pixels[sy * stride + sx];
            int r = (color >> 10) & 31, g = (color >> 5) & 31, b = color & 31;
            int luminance = (r * 30 + g * 59 + b * 11) * 255 / (31 * 100);
            int gray = luminance < 70 ? 3 : luminance < 128 ? 2 : luminance < 192 ? 1 : 0;
            dst[x] = gray_to_rgb565(gray);
        }
    }
    rg_display_submit(display_surface, 0);
    last_display_surface = display_surface;
    display_surface_index ^= 1;
}

void lavax_platform_poll(void)
{
    static unsigned int poll_divider;
    static int64_t task_yield_time;
    static uint32_t keys;
    int64_t now;

    if ((poll_divider++ & 31) != 0)
        return;
    now = rg_system_timer();

    if (!input_poll_time || now - input_poll_time >= 1000)
    {
        LavaHardwarePollInput();
        keys = rg_input_read_gamepad();
        input_poll_time = now;
    }

    if (!hardware_tick_time)
        hardware_tick_time = now;
    if (now > hardware_tick_time)
    {
        unsigned int ticks = (unsigned int)((now - hardware_tick_time) * 256 / 1000000);
        if (ticks)
        {
            LavaHardwareTick(ticks);
            hardware_tick_time += (int64_t)ticks * 1000000 / 256;
        }
    }
    if (!display_refresh_time || now - display_refresh_time >= LAVAX_DISPLAY_INTERVAL_US)
    {
        DmaRefresh();
        display_refresh_time = now;
    }

    if (!statistics_tick_time || now - statistics_tick_time >= 10000)
    {
        rg_system_tick(0);
        statistics_tick_time = now;
    }

    if ((keys & (RG_KEY_SELECT | RG_KEY_START)) == (RG_KEY_SELECT | RG_KEY_START))
    {
        if (!app_exit_combo_down)
            app_exit_requested = 1;
        app_exit_combo_down = 1;
    }
    else
    {
        app_exit_combo_down = 0;
    }

    if (rg_input_menu_released(keys))
        exit_requested = 1;

    if (!task_yield_time || now - task_yield_time >= 1000)
    {
        rg_task_yield();
        task_yield_time = now;
    }
}

int lavax_platform_should_exit(void)
{
    return exit_requested;
}

int lavax_platform_take_app_exit_request(void)
{
    int requested = app_exit_requested;
    app_exit_requested = 0;
    return requested;
}

int lavax_platform_app_exit_combo_down(void)
{
    return app_exit_combo_down;
}
