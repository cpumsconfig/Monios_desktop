#include "common.h"
#include "graphics.h"
#include "gui.h"
#include "osui.h"
#include "spinlock.h"

static gui_info_t g_gui_info;
/* Protects the GUI status snapshot against concurrent UI/ioctl callers. */
static spinlock_t g_gui_lock = SPINLOCK_INITIALIZER;

void gui_refresh(void)
{
    uint32_t osui_widgets = osui_component_count();
    uint64_t flags = 0;

    spin_lock_irqsave(&g_gui_lock, &flags);
    g_gui_info.wm_ready = graphics_active();
    g_gui_info.app_framework_ready = graphics_active() && osui_widgets > 0;
    g_gui_info.widgets_registered = osui_widgets;
    g_gui_info.windows = graphics_window_count();
    g_gui_info.focused = graphics_focused_window_index();
    strcpy(g_gui_info.status,
           g_gui_info.app_framework_ready ? "gui: wm/app framework active" :
           (graphics_active() ? "gui: wm ready; osui pending" : "gui: framework ready"));
    spin_unlock_irqrestore(&g_gui_lock, flags);
}

void gui_init(void)
{
    memset(&g_gui_info, 0, sizeof(g_gui_info));
    g_gui_info.initialized = true;
    gui_refresh();
}

const gui_info_t *gui_info(void)
{
    gui_refresh();
    return &g_gui_info;
}

const char *gui_status(void)
{
    gui_refresh();
    return g_gui_info.status;
}
