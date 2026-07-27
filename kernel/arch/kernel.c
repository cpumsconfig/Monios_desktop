#include "common.h"
#include "acpi.h"
#include "aac.h"
#include "audio.h"
#include "bluetooth.h"
#include "browser.h"
#include "bsod.h"
#include "cmos.h"
#include "cpu.h"
#include "cdrom.h"
#include "console.h"
#include "device.h"
#include "dma.h"
#include "driver_manager.h"
#include "exec.h"
#include "extfs.h"
#include "file.h"
#include "futex.h"
#include "gop.h"
#include "heap.h"
#include "hid.h"
#include "iic.h"
#include "i2c.h"
#include "i3c.h"
#include "spi.h"
#include "tpm.h"
#include "mcb.h"
#include "md.h"
#include "input.h"
#include "interrupt.h"
#include "installer.h"
#include "ipc.h"
#include "ipv6.h"
#include "kernel.h"
#include "kernel_layout.h"
#include "keyboard.h"
#include "lazyalloc.h"
#include "graphics.h"
#include "font.h"
#include "bios.h"
#include "bitmap.h"
#include "buddy.h"
#include "memory.h"
#include "mmu.h"
#include "mouse.h"
#include "lwip.h"
#include "net.h"
#include "ntfs.h"
#include "pcb.h"
#include "pci.h"
#include "pool.h"
#include "prsys.h"
#include "registry.h"
#include "rtc.h"
#include "scheduler.h"
#include "session.h"
#include "gdb_stub.h"
#include "crash_dump.h"
#include "ftrace.h"
#include "frame.h"
#include "fs_cache.h"
#include "shell.h"
#include "signal.h"
#include "schedopt.h"
#include "smp.h"
#include "storage_ext.h"
#include "syscall.h"
#include "task.h"
#include "terminal.h"
#include "ui.h"
#include "tls.h"
#include "http.h"
#include "usb_ext.h"
#include "vma.h"
#include "vmext.h"
#include "wifi.h"
#include "gui.h"
#include "gpu.h"
#include "power.h"
#include "opp.h"
#include "od.h"
#include "path.h"

static bool g_graphics_mode_requested;
static bool g_graphics_mode_toggle_requested;
static keyboard_status_t g_keyboard_status;
static uint8_t g_last_mouse_buttons;
static bool g_shutdown_requested;
static bool g_reboot_requested;
static bool g_sleep_requested;
static bool g_installer_boot_media;

static void u32_to_2dec(char *dst, uint32_t value)
{
    dst[0] = (char) ('0' + ((value / 10) % 10));
    dst[1] = (char) ('0' + (value % 10));
}

static void u64_to_dec(char *buf, uint64_t value)
{
    char temp[21];
    uint32_t i = 0;
    uint32_t j = 0;

    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    while (value > 0) {
        temp[i++] = (char) ('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) {
        buf[j++] = temp[--i];
    }
    buf[j] = '\0';
}

static void build_uptime_prefix(char *buf)
{
    uint64_t ticks = timer_ticks();
    uint32_t hz = timer_hz();
    uint64_t total_seconds = hz == 0 ? 0 : (ticks / hz);
    uint32_t hours = (uint32_t) ((total_seconds / 3600) % 100);
    uint32_t minutes = (uint32_t) ((total_seconds / 60) % 60);
    uint32_t seconds = (uint32_t) (total_seconds % 60);

    buf[0] = '[';
    u32_to_2dec(&buf[1], hours);
    buf[3] = ':';
    u32_to_2dec(&buf[4], minutes);
    buf[6] = ':';
    u32_to_2dec(&buf[7], seconds);
    buf[9] = ']';
    buf[10] = ' ';
    buf[11] = '\0';
}

static void serial_init(void)
{
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x01);
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);
}

static void serial_write_char(char ch)
{
    while ((inb(0x3FD) & 0x20) == 0) {
    }
    outb(0x3F8, (uint8_t) ch);
}

void serial_write(const char *str)
{
    while (*str) {
        serial_write_char(*str++);
    }
}

static bool g_log_quiet;

void log_write(const char *str)
{
    char prefix[12];

    if (g_log_quiet) {
        return;
    }
    build_uptime_prefix(prefix);
    serial_write(prefix);
    serial_write(str);
    serial_write_char('\n');
}

void log_write_event(const char *tag, const char *detail)
{
    char prefix[12];

    if (g_log_quiet) {
        return;
    }
    build_uptime_prefix(prefix);
    serial_write(prefix);
    serial_write(tag);
    serial_write(": ");
    serial_write(detail);
    serial_write_char('\n');
}

void log_write_bool_event(const char *tag, bool enabled)
{
    if (enabled) {
        log_write_event(tag, "on");
    } else {
        log_write_event(tag, "off");
    }
}

void kernel_log_hex_u32(const char *label, uint32_t value)
{
    char line[64];
    static const char hex[] = "0123456789ABCDEF";
    uint32_t label_len;

    if (label == NULL) {
        return;
    }
    label_len = (uint32_t) strlen(label);
    if (label_len + 10 >= sizeof(line)) {
        line[0] = '\0';
        return;
    }
    memcpy(line, label, label_len);
    line[label_len++] = '0';
    line[label_len++] = 'x';
    for (uint8_t i = 0; i < 8; i++) {
        line[label_len + i] = hex[(value >> ((7 - i) * 4)) & 0xF];
    }
    line[label_len + 8] = '\0';
    log_write(line);
}

void kernel_request_shutdown(void)
{
    g_shutdown_requested = true;
}

void kernel_request_reboot(void)
{
    g_reboot_requested = true;
}

void kernel_request_sleep(void)
{
    g_sleep_requested = true;
}

void kernel_request_graphics_mode(void)
{
    g_graphics_mode_requested = true;
}

bool kernel_shutdown_requested(void)
{
    return g_shutdown_requested;
}

bool kernel_reboot_requested(void)
{
    return g_reboot_requested;
}

bool kernel_sleep_requested(void)
{
    return g_sleep_requested;
}

static void kernel_shutdown_processes_and_drivers(void)
{
    log_write("power: stopping processes");
    exec_shutdown_active();
    graphics_close_all_programs();
    driver_manager_shutdown();
    task_shutdown_all();
}

static void kernel_poweroff(void)
{
    log_write("power: acpi poweroff");

    graphics_shutdown_animation();

    if (acpi_poweroff()) {
        for (uint32_t i = 0; i < 1000000; i++) {
            io_wait();
        }
    }

    log_write("power: fallback poweroff ports");
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    for (;;) { asm volatile ("hlt"); }
}

static void kernel_reboot(void)
{
    log_write("power: acpi reboot");
    graphics_shutdown_animation();
    (void) acpi_reboot();
    for (;;) {
        asm volatile ("hlt");
    }
}

static void kernel_sleep(void)
{
    log_write("power: acpi sleep");
    if (!acpi_sleep()) {
        log_write("power: acpi sleep unavailable");
        return;
    }
    for (uint32_t i = 0; i < 100000; i++) {
        io_wait();
    }
    log_write("power: sleep returned");
}

static void kernel_handle_key_event(const key_event_t *event)
{
    g_keyboard_status = event->status;

    if (event->type == KEY_EVENT_ESC && event->status.ctrl_down && event->status.shift_down) {
        graphics_open_task_manager();
        return;
    }

    if (event->type == KEY_EVENT_POWER) {
        log_write("power: external button pressed");
        kernel_request_shutdown();
        return;
    }

    if (event->type == KEY_EVENT_F4 && event->status.alt_down && graphics_active()) {
        graphics_handle_alt_f4();
        return;
    }

    if (graphics_active()) {
        graphics_handle_key_event(event);
        return;
    }

    switch (event->type) {
    case KEY_EVENT_CHAR:
        shell_handle_key_event(event);
        break;
    case KEY_EVENT_TAB:
        shell_handle_key_event(event);
        break;
    case KEY_EVENT_UP:
        shell_handle_navigation_key(KEY_EVENT_UP);
        break;
    case KEY_EVENT_DOWN:
        shell_handle_navigation_key(KEY_EVENT_DOWN);
        break;
    case KEY_EVENT_LEFT:
        shell_handle_navigation_key(KEY_EVENT_LEFT);
        break;
    case KEY_EVENT_RIGHT:
        shell_handle_navigation_key(KEY_EVENT_RIGHT);
        break;
    case KEY_EVENT_F1: break;
    case KEY_EVENT_F2: break;
    case KEY_EVENT_F3: break;
    case KEY_EVENT_F4:
        if (graphics_active() && event->status.alt_down) {
            graphics_handle_alt_f4();
        }
        break;
    case KEY_EVENT_F5: break;
    case KEY_EVENT_F6: break;
    case KEY_EVENT_F7: break;
    case KEY_EVENT_F8: break;
    case KEY_EVENT_F9: break;
    case KEY_EVENT_F10: break;
    case KEY_EVENT_F11: break;
    case KEY_EVENT_F12: break;
    case KEY_EVENT_CTRL: break;
    case KEY_EVENT_ALT: break;
    case KEY_EVENT_WIN:
        if (graphics_active()) {
            graphics_activate_primary_button();
        }
        break;
    case KEY_EVENT_MENU: break;
    case KEY_EVENT_NUM: break;
    case KEY_EVENT_PAUSE:
        timer_toggle_paused();
        break;
    case KEY_EVENT_CTRL_C:
        shell_handle_key_event(event);
        break;
    case KEY_EVENT_HOME:
        shell_handle_special_key(KEY_EVENT_HOME);
        break;
    case KEY_EVENT_END:
        shell_handle_special_key(KEY_EVENT_END);
        break;
    case KEY_EVENT_DELETE:
        shell_handle_special_key(KEY_EVENT_DELETE);
        break;
    case KEY_EVENT_POWER:
        log_write("power: external button pressed");
        kernel_request_shutdown();
        break;
    default:
        break;
    }
}

static void task_poll_keyboard_events(void *arg)
{
    key_event_t event;
    (void) arg;

    if (exec_active()) {
        return;
    }

    while (keyboard_poll_event(&event)) {
        kernel_handle_key_event(&event);
    }
}

static void task_handle_graphics_request(void *arg)
{
    (void) arg;
    if (!g_graphics_mode_requested) {
        return;
    }

    /* Only perform graphics_enter_mode when the kernel page-tables/MMU are active. */
    if (g_graphics_mode_requested && mmu_is_active()) {
        graphics_enter_mode();
        g_graphics_mode_requested = false;
    }
}

static void kernel_write_file_if_missing(const char *path, const char *text)
{
    if (path == NULL || text == NULL || file_exists(path)) {
        return;
    }
    file_write(path, text, (uint32_t) strlen(text));
}

static void kernel_delete_file_if_present(const char *path)
{
    if (path != NULL && file_exists(path) && !file_is_dir(path)) {
        file_delete(path);
    }
}

static void ensure_desktop_layout(void)
{
    char path[96];

    session_init();
    ui_ensure_system_layout();
    kernel_delete_file_if_present(UI_ROOT_DESKTOP PATH_SEPARATOR_STR "rzdrv.rzs");
    kernel_delete_file_if_present(UI_ROOT_DESKTOP PATH_SEPARATOR_STR "dll.lnk");
    kernel_write_file_if_missing(UI_ROOT_DESKTOP PATH_SEPARATOR_STR "files.lnk", UI_EXPLORER_PATH "\n");
    kernel_write_file_if_missing(UI_ROOT_DESKTOP PATH_SEPARATOR_STR "apps.lnk", UI_APPS_DIR "\n");
    kernel_write_file_if_missing(UI_ROOT_DESKTOP PATH_SEPARATOR_STR "player.lnk", UI_PLAYER_PATH "\n");
    kernel_write_file_if_missing(UI_ROOT_DESKTOP PATH_SEPARATOR_STR "notes.lnk", UI_NOTEPAD_PATH "\n");
    kernel_write_file_if_missing(UI_ROOT_DESKTOP PATH_SEPARATOR_STR "taskmgr.lnk", UI_TASKMGR_PATH "\n");
    kernel_write_file_if_missing(UI_ROOT_DESKTOP PATH_SEPARATOR_STR "square.lnk", UI_APPS_DIR PATH_SEPARATOR_STR "square.exe\n");
    kernel_write_file_if_missing(UI_ROOT_DESKTOP PATH_SEPARATOR_STR "cube3d.lnk", UI_APPS_DIR PATH_SEPARATOR_STR "cube3d.exe\n");
    kernel_write_file_if_missing(UI_ROOT_DESKTOP PATH_SEPARATOR_STR "setup.lnk", UI_SETUP_PATH "\n");
    kernel_write_file_if_missing(UI_ROOT_DESKTOP PATH_SEPARATOR_STR "dev.lnk", UI_APPS_DIR PATH_SEPARATOR_STR "appdev.exe\n");
    kernel_write_file_if_missing(UI_ROOT_DESKTOP PATH_SEPARATOR_STR "dll.lnk", UI_MONIOS_DLL_PATH "\n");
    kernel_write_file_if_missing(UI_ROOT_DESKTOP PATH_SEPARATOR_STR "driver.lnk", UI_RZDRV_PATH "\n");
    kernel_write_file_if_missing(UI_ROOT_DESKTOP PATH_SEPARATOR_STR "blank.txt", "");
    if (file_exists(UI_WALLPAPER_PATH) &&
        !file_exists(UI_ROOT_DESKTOP PATH_SEPARATOR_STR "wallpaper.lnk")) {
        strcpy(path, UI_ROOT_DESKTOP PATH_SEPARATOR_STR "wallpaper.lnk");
        kernel_write_file_if_missing(path, UI_WALLPAPER_PATH "\n");
    }
}

static void task_poll_graphics_input(void *arg)
{
    mouse_snapshot_t snapshot;
    (void) arg;

    if (!graphics_active()) {
        g_last_mouse_buttons = 0;
        return;
    }

    mouse_get_snapshot(&snapshot);
    if (exec_active()) {
        g_last_mouse_buttons = snapshot.buttons;
        return;
    }
    graphics_handle_mouse_move((uint16_t) snapshot.x_pixels, (uint16_t) snapshot.y_pixels, snapshot.buttons);
    if (snapshot.wheel_delta != 0) {
        graphics_handle_mouse_wheel((uint16_t) snapshot.x_pixels,
                                    (uint16_t) snapshot.y_pixels,
                                    mouse_consume_wheel_delta());
    }
    if ((snapshot.buttons & MOUSE_BUTTON_LEFT) != 0 && (g_last_mouse_buttons & MOUSE_BUTTON_LEFT) == 0) {
        graphics_handle_click((uint16_t) snapshot.x_pixels, (uint16_t) snapshot.y_pixels);
    }
    if ((snapshot.buttons & MOUSE_BUTTON_RIGHT) != 0 && (g_last_mouse_buttons & MOUSE_BUTTON_RIGHT) == 0) {
        graphics_handle_right_click((uint16_t) snapshot.x_pixels, (uint16_t) snapshot.y_pixels);
    }
    g_last_mouse_buttons = snapshot.buttons;
}

static void task_update_desktop_shell(void *arg)
{
    (void) arg;
    if (!graphics_active()) {
        return;
    }
    graphics_periodic_update(timer_ticks());
}

static void draw_console_banner(void)
{
    console_clear();
}

static void kernel_boot_animation_update(uint32_t progress, const char *status)
{
    if (graphics_active()) {
        graphics_boot_update(progress, status);
    }
}

void kernel_run_periodic_work(void)
{
    static bool running;

    if (running) {
        return;
    }
    running = true;
    audio_update();
    net_update();
    lwip_update();
    hid_update();
    futex_update();
    task_run_ready();
    running = false;
}

void kernel_run_exec_periodic_work(void)
{
    static bool running;

    if (running) {
        return;
    }
    running = true;
    audio_update();
    net_update();
    lwip_update();
    hid_update();
    futex_update();
    task_poll_keyboard_events(NULL);
    task_poll_graphics_input(NULL);
    task_update_desktop_shell(NULL);
    task_handle_graphics_request(NULL);
    running = false;
}

void kernel_main(uint64_t boot_mode, uint64_t boot_rsdp)
{
    bool boot_is_uefi = (boot_mode & 1ULL) != 0;

    asm volatile ("cli");
    serial_init();
    console_init();
    serial_write("KERNEL BOOT");
    serial_write_char('\n');
    g_log_quiet = false;
    log_write("boot: serial online");
    /* Loaders may provide the actual physical load base in .kconfig after
     * relocating the PE image. BIOS leaves it zero and uses the link base. */
    extern uint64_t _kconfig_start;
    volatile uint64_t *kcfg = &_kconfig_start;
    uint64_t kernel_phys_base = kcfg[0] != 0 ? kcfg[0] : KERNEL_PHYS_BASE;
    uint64_t kernel_virt_base = kcfg[1] != 0 ? kcfg[1] : KERNEL_VIRT_BASE;
    kcfg[0] = kernel_phys_base;
    kcfg[1] = kernel_virt_base;
    kcfg[2] = 0;
    kcfg[3] = 0;
    crash_dump_init();
    log_write("boot: crash dump online");
    gdb_stub_init();
    log_write("boot: gdb stub online (COM1 115200)");
    ftrace_init();
    log_write("boot: ftrace online");

    /* kconfig section – written by kernel, read by crash_dump
     * to locate kernel global state after a crash. */
    /* ftrace symbol table – map PA → name for key entry points.
     * Forward declarations for functions defined in other .c files. */
    extern void cpu_exception_dispatch(void);
    extern void init_gdt(void);
    extern void init_idt(void);
    extern void init_page_tables(void);
    extern void syscall_init(void);
    extern void shell_init(void);
    extern void ftrace_init(void);
    extern void ftrace_dump_serial(void);
    extern void crash_dump_init(void);

    static const uint64_t  ftrace_addrs[] = {
        (uint64_t)(void *)&kernel_main,
        (uint64_t)(void *)&cpu_exception_dispatch,
        (uint64_t)(void *)&init_gdt,
        (uint64_t)(void *)&init_idt,
        (uint64_t)(void *)&init_page_tables,
        (uint64_t)(void *)&syscall_init,
        (uint64_t)(void *)&shell_init,
        (uint64_t)(void *)&ftrace_init,
        (uint64_t)(void *)&ftrace_dump_serial,
        (uint64_t)(void *)&crash_dump_init,
    };
    static const char * const ftrace_names[] = {
        "kernel_main",
        "cpu_exception_dispatch",
        "init_gdt",
        "init_idt",
        "init_page_tables",
        "syscall_init",
        "shell_init",
        "ftrace_init",
        "ftrace_dump_serial",
        "crash_dump_init",
    };
    ftrace_set_symbols(ftrace_addrs, ftrace_names,
        (uint32_t)(sizeof(ftrace_addrs) / sizeof(ftrace_addrs[0])),
        kernel_phys_base);

    log_write("boot: init gdt");
    init_gdt();
    log_write("boot: init idt");
    init_idt();
    log_write("boot: init paging");
    init_page_tables();
    log_write("boot: init graphics");
    graphics_init();
    memset(&g_keyboard_status, 0, sizeof(g_keyboard_status));
    g_graphics_mode_requested = false;
    g_graphics_mode_toggle_requested = false;
    log_write("boot: init heap");
    memory_init();
    bitmap_init();
    pool_system_init();
    heap_init();
    frame_init();
    buddy_init();
    vma_init();
    lazyalloc_init();
    vmext_init();
    bios_init();
    rtc_init();
    gop_init();
    log_write("boot: start startup animation");
    graphics_set_boot_animation_mode(true);
    graphics_enter_mode();
    graphics_boot_animation();
    kernel_boot_animation_update(8, "Detecting hardware");
    log_write("boot: detect cpu");
    cpu_log_info();
    cpu_enable_fpu_sse();
    cmos_log_time();
    log_write("boot: init acpi");
    acpi_init(boot_rsdp);
    log_write("boot: enumerate smp topology");
    smp_init();
    log_write("boot: init dma");
    dma_init();
    dma_log_state();
    log_write("boot: probe pci");
    pci_log_devices();
    kernel_boot_animation_update(28, "Hardware ready");
    scheduler_init();
    schedopt_init();
    pcb_init();
    futex_init();
    ipc_init();
    signal_init();
    prsys_init();
    log_write("boot: init task system");
    task_init();
    log_write("boot: init interrupts");
    init_interrupts(100);
    acpi_enable_power_button();
    power_init();
    opp_init();
    od_init();
    kernel_boot_animation_update(40, "Starting kernel services");
    log_write("boot: probe audio");
    audio_init();
    aac_init();
    audio_log_state();
    log_write("boot: init syscalls");
    syscall_init();
    kernel_boot_animation_update(52, "Starting system services");
    log_write("boot: init filesystem");
    file_init();
    cdrom_init();
    log_write(cdrom_status());
    if (file_auto_mount()) {
        log_write("boot: filesystem mounted");
    } else {
        log_write("boot: filesystem mount failed");
    }
    graphics_boot_load_image();
    graphics_reload_cursor_style();
    kernel_boot_animation_update(58, "Mounting filesystem");
    log_write("boot: load UI font");
    while (!font_ready()) {
        if (font_init_failed()) {
            log_write("boot: UI font load failed; refusing to enter system");
            kernel_boot_animation_update(58, "UI font load failed");
            asm volatile ("cli");
            for (;;) {
                asm volatile ("hlt");
            }
        }
        font_init_step(256u * 1024u);
        kernel_boot_animation_update(58u + (font_init_progress() * 20u) / 100u,
                                     "Loading UI font");
    }
    log_write("boot: UI font ready");
    kernel_boot_animation_update(78, "UI font ready");
    g_installer_boot_media = (file_exists(PATH_ROOT "INSTALL.FLG") &&
                              file_exists(PATH_ROOT "SETUP.EXE")) ||
                             installer_boot_media_present();
    /* Filesystem auto-mounted during boot. */
    log_write("boot: init registry");
    registry_init();
    log_write("boot: init device namespace");
    device_init();
    log_write("boot: init console manager");
    terminal_init();
    log_write("boot: load drivers");
    driver_manager_init();
    storage_ext_init();
    iic_init();
    i2c_init();
    i3c_init();
    spi_init();
    tpm_init();
    mcb_init();
    md_init();
    usb_ext_init();
    bluetooth_init();
    wifi_init();
    ipv6_init();
    net_init();
    lwip_init();
    tls_init();
    http_init();
    browser_init();
    gpu_init();
    gui_init();
    kernel_boot_animation_update(88, "Drivers and services ready");
    log_write("boot: init session");
    ensure_desktop_layout();
    log_write("boot: init input");
    init_input();
    hid_init();
    kernel_boot_animation_update(92, "Initializing input");

    if (g_installer_boot_media) {
        log_write("boot: installer media detected");
    }
    g_graphics_mode_requested = false;
    log_write("boot: create tasks");
    task_create("keypoll", task_poll_keyboard_events, NULL, 1, true);
    task_create("guipoll", task_poll_graphics_input, NULL, 1, true);
    task_create("deskui", task_update_desktop_shell, NULL, 1, true);
    /* Task to handle deferred graphics enter requests (checks request flag). */
    task_create("grreq", task_handle_graphics_request, NULL, 1, true);
    log_write("boot: init shell");
    shell_init();
    shell_env_set("MONIOS_BOOT_MODE", boot_is_uefi ? "uefi" : "mbr");
    kernel_boot_animation_update(98, "Starting shell");
    kernel_boot_animation_update(100, g_installer_boot_media ? "Installer ready" : "Ready");
    graphics_boot_finish();
    graphics_set_boot_animation_mode(false);

    log_write("boot: enable interrupts");
    asm volatile ("sti");
    if (g_installer_boot_media && file_exists(PATH_ROOT "SETUP.EXE")) {
        log_write("boot: launch setup");
        graphics_set_installer_mode(true);
        graphics_leave_mode();
        graphics_enter_mode();
        shell_exec_path_admin(PATH_ROOT "SETUP.EXE");
        shell_set_boot_complete(true);
    } else {
        graphics_draw_shell();
        shell_set_boot_complete(true);
    }
    log_write("boot: main loop");
    while (1) {
        /* 调试：主循环心跳 */
        static uint32_t loop_count = 0;
        if (loop_count++ % 100000 == 0) {
            asm volatile ("movb $0x2E, %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "ax", "dx");
        }
        if (g_shutdown_requested) {
            log_write("power: shutdown requested");
            asm volatile ("cli");
            kernel_shutdown_processes_and_drivers();
            kernel_poweroff();
            for (;;) {
                asm volatile ("cli; hlt");
            }
        }
        if (g_reboot_requested) {
            log_write("power: reboot requested");
            asm volatile ("cli");
            kernel_shutdown_processes_and_drivers();
            kernel_reboot();
            for (;;) {
                asm volatile ("cli; hlt");
            }
        }
        if (g_sleep_requested) {
            g_sleep_requested = false;
            kernel_sleep();
        }
        kernel_run_periodic_work();
    }
}
