#include <stdlib.h>
#include "rg_system.h"
#include "rg_display.h"
#include "rg_gui.h"
#include "rg_input.h"
#include "rg_utils.h"
#include "lavaxvm.h"

static rg_surface_t *surfaces[2];
static void *vm_ram;
static void *local_ram;
static bool quit_requested;

static void event_handler(int event, void *arg)
{
    (void)arg;
    if (event == RG_EVENT_REDRAW)
        lavax_platform_redraw();
    else if (event == RG_EVENT_SHUTDOWN)
        quit_requested = true;
}

void app_main(void)
{
    const rg_handlers_t handlers = {.event = event_handler};
    const size_t vm_ram_size = lavaxvm_vm_ram_size();
    const size_t local_ram_size = lavaxvm_local_ram_size();
    rg_system_init(32000, &handlers, NULL);
    rg_system_tick(0);
    RG_LOGI("LavaXOS: allocating display and VM memory");
    surfaces[0] = rg_surface_create(RG_SCREEN_WIDTH, RG_SCREEN_HEIGHT, RG_PIXEL_565_LE, MEM_FAST);
    surfaces[1] = rg_surface_create(RG_SCREEN_WIDTH, RG_SCREEN_HEIGHT, RG_PIXEL_565_LE, MEM_FAST);
    vm_ram = rg_alloc(vm_ram_size, MEM_SLOW | MEM_8BIT);
    local_ram = rg_alloc(local_ram_size, MEM_SLOW | MEM_8BIT);
    if (!surfaces[0] || !surfaces[1] || !vm_ram || !local_ram)
    {
        rg_gui_alert("LavaXOS memory allocation failed", NULL);
        rg_system_exit();
        return;
    }
    RG_LOGI("LavaXOS: memory ready, binding display");
    lavax_platform_bind_display(surfaces[0], surfaces[1]);
    RG_LOGI("LavaXOS: initializing VM");
    if (!lavaxvm_init(vm_ram, vm_ram_size, local_ram, local_ram_size))
    {
        RG_LOGE("LavaXOS: VM initialization failed");
        rg_gui_alert(lavaxvm_last_error() ? lavaxvm_last_error() : "LavaXOS startup failed", NULL);
        rg_system_exit();
        return;
    }
    RG_LOGI("LavaXOS: VM initialized, loading Shell.sys");
    rg_system_tick(0);
    if (!lavaxvm_load_shell())
    {
        RG_LOGE("LavaXOS: Shell loading failed");
        rg_gui_alert(lavaxvm_last_error() ? lavaxvm_last_error() : "LavaXOS startup failed", NULL);
        rg_system_exit();
        return;
    }
    RG_LOGI("LavaXOS: Shell loaded, starting VM");
    rg_system_tick(0);
    lavaxvm_run();

    if (!quit_requested)
        rg_gui_game_menu();

    lavaxvm_shutdown();
    rg_surface_free(surfaces[0]);
    rg_surface_free(surfaces[1]);
    free(vm_ram);
    free(local_ram);
    rg_system_switch_app("launcher", "launcher", NULL, RG_BOOT_ONCE);
}
