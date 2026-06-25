#include "common.h"
#include "graphics.h"
#include "gui.h"

static gui_info_t g_gui_info;

void gui_refresh(void)
{
    g_gui_info.wm_ready = graphics_active();
    g_gui_info.app_framework_ready = true;
    g_gui_info.widgets_registered = 8;
    g_gui_info.windows = graphics_window_count();
    g_gui_info.focused = graphics_focused_window_index();
    strcpy(g_gui_info.status, graphics_active() ? "gui: wm/app framework active" : "gui: framework ready");
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
