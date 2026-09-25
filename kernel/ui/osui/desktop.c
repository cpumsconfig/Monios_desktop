#include "graphics.h"
#include "kernel.h"
#include "osui.h"

void osui_desktop_tick(void)
{
    if (graphics_active()) {
        graphics_periodic_update(timer_ticks());
    }
}
