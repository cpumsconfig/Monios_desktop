#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "osui_dll.h"

static void write_line(const char *text)
{
    fputs(text);
    fputs("\r\n");
}

static void write_uint(uint32_t value)
{
    char buffer[16];
    uint32_t index = 0;

    if (value == 0) {
        write(STDOUT_FILENO, "0", 1);
        return;
    }

    while (value > 0 && index < sizeof(buffer)) {
        buffer[index++] = (char) ('0' + (value % 10));
        value /= 10;
    }
    while (index > 0) {
        write(STDOUT_FILENO, &buffer[--index], 1);
    }
}

static void write_mouse_snapshot(const app_mouse_snapshot_t *snapshot)
{
    fputs("mouse x=");
    write_uint((uint32_t) snapshot->x_pixels);
    fputs(" y=");
    write_uint((uint32_t) snapshot->y_pixels);
    fputs(" buttons=");
    write_uint(snapshot->buttons);
    fputs(" wheel=");
    if (snapshot->wheel_delta < 0) {
        write(STDOUT_FILENO, "-", 1);
        write_uint((uint32_t) (-snapshot->wheel_delta));
    } else {
        write_uint((uint32_t) snapshot->wheel_delta);
    }
    fputs("\r\n");
}

int main(int argc, char **argv)
{
    char cwd[PATH_MAX_LEN];
    app_mouse_snapshot_t snapshot;
    const char *nav_items[] = { "Overview", "Drivers", "Settings" };
    const char *actions[] = { "Refresh", "Open", "More" };

    write_line("demo.exe started");
    if (app_getcwd(cwd, sizeof(cwd)) >= 0) {
        fputs("cwd: ");
        write_line(cwd);
    }

    fputs("argc: ");
    write_uint((uint32_t) argc);
    fputs("\r\n");
    for (int i = 0; i < argc; i++) {
        fputs("argv[");
        write_uint((uint32_t) i);
        fputs("]: ");
        write_line(argv[i]);
    }

    if (app_get_mouse(&snapshot) >= 0) {
        write_mouse_snapshot(&snapshot);
    }
    app_enter_graphics_mode();
    osui_canvas(0x00EAF0F6);
    osui_panel((osui_rect_t) { 112, 48, 800, 666 });
    osui_titlebar((osui_rect_t) { 112, 48, 800, 36 }, "OSUI Preview", true);
    osui_navrail((osui_rect_t) { 148, 104, 150, 556 }, nav_items, 3, 0);
    osui_label(318, 104, "MoniOS desktop controls", false);
    osui_label(318, 128, "A native OSUI surface for system apps.", true);
    osui_commandbar((osui_rect_t) { 318, 166, 558, 38 }, "System overview", actions, 3, 0);

    osui_card((osui_rect_t) { 318, 224, 270, 224 });
    osui_avatar((osui_rect_t) { 334, 242, 36, 36 }, "root", OSUI_STATE_PRIMARY);
    osui_label(380, 246, "Account", false);
    osui_badge((osui_rect_t) { 488, 242, 84, 24 }, "Online", OSUI_STATE_SUCCESS);
    osui_label(334, 274, "Email", true);
    osui_input((osui_rect_t) { 334, 294, 238, 30 }, "", "you@monios.local", true, false);
    osui_label(334, 338, "Password", true);
    osui_input((osui_rect_t) { 334, 358, 238, 30 }, "monios", "", false, true);
    osui_checkbox((osui_rect_t) { 334, 402, 190, 24 }, "Remember me", OSUI_STATE_CHECKED);

    osui_card((osui_rect_t) { 606, 224, 270, 224 });
    osui_label(622, 246, "System status", false);
    osui_chip((osui_rect_t) { 762, 242, 98, 24 }, "Stable", OSUI_STATE_PRIMARY);
    osui_label(622, 278, "Graphics", true);
    osui_label(730, 278, "framebuffer", false);
    osui_label(622, 306, "Theme", true);
    osui_label(730, 306, "MoniOS OSUI", false);
    osui_label(622, 334, "Progress", true);
    osui_slider((osui_rect_t) { 730, 328, 130, 24 }, 72, OSUI_STATE_SUCCESS);
    osui_toggle((osui_rect_t) { 622, 374, 238, 30 }, "Hardware acceleration", OSUI_STATE_CHECKED);
    osui_callout((osui_rect_t) { 622, 406, 238, 36 }, "Ready", "Services responding.", OSUI_STATE_SUCCESS);

    osui_card((osui_rect_t) { 318, 466, 270, 178 });
    osui_label(334, 488, "Selection", false);
    osui_radio((osui_rect_t) { 334, 518, 110, 24 }, "Balanced", OSUI_STATE_CHECKED);
    osui_radio((osui_rect_t) { 452, 518, 110, 24 }, "Saver", 0);
    osui_button_state((osui_rect_t) { 334, 562, 104, 34 }, "Apply", 0,
                      OSUI_STATE_PRIMARY | OSUI_STATE_FOCUSED);
    osui_button_state((osui_rect_t) { 450, 562, 104, 34 }, "Pressed", OSUI_BUTTON_GHOST,
                      OSUI_STATE_PRESSED);

    osui_card((osui_rect_t) { 606, 466, 270, 178 });
    osui_label(622, 488, "Recent activity", false);
    osui_list_item((osui_rect_t) { 622, 516, 238, 48 }, "Driver manager", "3 devices ready",
                   OSUI_STATE_SELECTED);
    osui_list_item((osui_rect_t) { 622, 570, 238, 48 }, "OSUI theme", "Native visual system",
                   OSUI_STATE_HOVERED);
    osui_statusbar((osui_rect_t) { 318, 650, 558, 28 }, "OSUI native theme", "Ready",
                   OSUI_STATE_SUCCESS);
    osui_present();
    write_line("demo rendered the OSUI control gallery");
    return 0;
}
