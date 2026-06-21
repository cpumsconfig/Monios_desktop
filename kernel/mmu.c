#include "common.h"
#include "mmu.h"
#include "kernel.h"

#define IDT_TYPE_INT_GATE 0x8E

#define PAGE_PRESENT 0x001ULL
#define PAGE_WRITABLE 0x002ULL
#define PAGE_USER 0x004ULL
#define PAGE_LARGE_2M 0x080ULL

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    gdt_entry_t low;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed)) tss_desc_t;

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss_t;

typedef struct {
    gdt_entry_t null_desc;
    gdt_entry_t code_desc;
    gdt_entry_t data_desc;
    gdt_entry_t user_data_desc;
    gdt_entry_t user_code_desc;
    tss_desc_t tss_desc;
} __attribute__((packed)) gdt_table_t;

static gdt_table_t gdt;
static tss_t tss;
static desc_ptr_t gdt_ptr;
static idt_entry_t idt[256];
static desc_ptr_t idt_ptr;
static uint64_t pml4[512] __attribute__((section(".pml4"), aligned(4096)));
static uint64_t pdpt[512] __attribute__((section(".pml4"), aligned(4096)));
static uint64_t pd0[512] __attribute__((section(".pml4"), aligned(4096)));
static uint64_t pd1[512] __attribute__((section(".pml4"), aligned(4096)));
static uint64_t pd3[512] __attribute__((section(".pml4"), aligned(4096)));
static uint64_t pd3b[512] __attribute__((section(".pml4"), aligned(4096)));
static uint64_t pd7[512] __attribute__((section(".pml4"), aligned(4096)));
static uint64_t pd7b[512] __attribute__((section(".pml4"), aligned(4096)));

/* Published kernel PML4 physical address for runtime checks. */
static uint64_t mmu_pml4_phys = 0;

static void lidt_ptr(const desc_ptr_t *ptr)
{
    asm volatile ("lidt %0" : : "m" (*ptr));
}

static void lgdt_ptr(const desc_ptr_t *ptr)
{
    asm volatile ("lgdt %0" : : "m" (*ptr));
}

static void load_tr(uint16_t selector)
{
    asm volatile ("ltr %w0" : : "r" (selector));
}

static void write_cr3(uint64_t value)
{
    /* Keep early boot interrupts disabled while switching page tables.
     * kernel_main() enables them only after the PIC/IDT handlers are ready.
     */
    asm volatile (
        "mov %0, %%cr3\n\t"
        "lfence\n\t"
        :
        : "r" (value)
        : "memory"
    );
}

void reload_segments(void)
{
    asm volatile (
        "pushq %0\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "movw %1, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        :
        : "i" ((uint64_t) GDT_KERNEL_CODE_SELECTOR), "i" (GDT_KERNEL_DATA_SELECTOR)
        : "rax", "memory"
    );
}

static void gdt_set_code_data(gdt_entry_t *entry, uint8_t access, uint8_t flags)
{
    entry->limit_low = 0;
    entry->base_low = 0;
    entry->base_mid = 0;
    entry->access = access;
    entry->granularity = flags;
    entry->base_high = 0;
}

static void gdt_set_tss(tss_desc_t *desc, uint64_t base, uint32_t limit)
{
    desc->low.limit_low = limit & 0xFFFF;
    desc->low.base_low = base & 0xFFFF;
    desc->low.base_mid = (base >> 16) & 0xFF;
    desc->low.access = 0x89;
    desc->low.granularity = (limit >> 16) & 0x0F;
    desc->low.base_high = (base >> 24) & 0xFF;
    desc->base_upper = (uint32_t) (base >> 32);
    desc->reserved = 0;
}

static void idt_set_gate(uint8_t vector, uint64_t handler, uint16_t selector, uint8_t type_attr)
{
    idt[vector].offset_low = handler & 0xFFFF;
    idt[vector].selector = selector;
    idt[vector].ist = 0;
    idt[vector].type_attr = type_attr;
    idt[vector].offset_mid = (handler >> 16) & 0xFFFF;
    idt[vector].offset_high = (uint32_t) (handler >> 32);
    idt[vector].zero = 0;
}

extern void default_interrupt_handler(void);
extern void exception0_handler(void);
extern void exception1_handler(void);
extern void exception2_handler(void);
extern void exception3_handler(void);
extern void exception4_handler(void);
extern void exception5_handler(void);
extern void exception6_handler(void);
extern void exception7_handler(void);
extern void exception8_handler(void);
extern void exception9_handler(void);
extern void exception10_handler(void);
extern void exception11_handler(void);
extern void exception12_handler(void);
extern void exception13_handler(void);
extern void exception14_handler(void);
extern void exception15_handler(void);
extern void exception16_handler(void);
extern void exception17_handler(void);
extern void exception18_handler(void);
extern void exception19_handler(void);
extern void exception20_handler(void);
extern void exception21_handler(void);
extern void exception22_handler(void);
extern void exception23_handler(void);
extern void exception24_handler(void);
extern void exception25_handler(void);
extern void exception26_handler(void);
extern void exception27_handler(void);
extern void exception28_handler(void);
extern void exception29_handler(void);
extern void exception30_handler(void);
extern void exception31_handler(void);

static void init_cpu_exception_handlers(void)
{
    idt_set_gate(0, (uint64_t) exception0_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(1, (uint64_t) exception1_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(2, (uint64_t) exception2_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(3, (uint64_t) exception3_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(4, (uint64_t) exception4_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(5, (uint64_t) exception5_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(6, (uint64_t) exception6_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(7, (uint64_t) exception7_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(8, (uint64_t) exception8_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(9, (uint64_t) exception9_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(10, (uint64_t) exception10_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(11, (uint64_t) exception11_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(12, (uint64_t) exception12_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(13, (uint64_t) exception13_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(14, (uint64_t) exception14_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(15, (uint64_t) exception15_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(16, (uint64_t) exception16_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(17, (uint64_t) exception17_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(18, (uint64_t) exception18_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(19, (uint64_t) exception19_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(20, (uint64_t) exception20_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(21, (uint64_t) exception21_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(22, (uint64_t) exception22_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(23, (uint64_t) exception23_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(24, (uint64_t) exception24_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(25, (uint64_t) exception25_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(26, (uint64_t) exception26_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(27, (uint64_t) exception27_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(28, (uint64_t) exception28_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(29, (uint64_t) exception29_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(30, (uint64_t) exception30_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    idt_set_gate(31, (uint64_t) exception31_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
}

void idt_set_handler(uint8_t vector, uint64_t handler, uint8_t type_attr)
{
    idt_set_gate(vector, handler, GDT_KERNEL_CODE_SELECTOR, type_attr);
}

void tss_set_rsp0(uint64_t rsp0)
{
    tss.rsp0 = rsp0;
}

void init_gdt(void)
{
    memset(&gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));

    gdt_set_code_data(&gdt.code_desc, 0x9A, 0x20);
    gdt_set_code_data(&gdt.data_desc, 0x92, 0x00);
    gdt_set_code_data(&gdt.user_data_desc, 0xF2, 0x00);
    gdt_set_code_data(&gdt.user_code_desc, 0xFA, 0x20);

    tss.iomap_base = sizeof(tss);
    gdt_set_tss(&gdt.tss_desc, (uint64_t) &tss, sizeof(tss) - 1);

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint64_t) &gdt;

    lgdt_ptr(&gdt_ptr);
    reload_segments();
    load_tr(GDT_TSS_SELECTOR);
}

void init_idt(void)
{
    memset(idt, 0, sizeof(idt));
    for (uint16_t i = 0; i < 256; i++) {
        idt_set_gate((uint8_t) i, (uint64_t) default_interrupt_handler, GDT_KERNEL_CODE_SELECTOR, IDT_TYPE_INT_GATE);
    }
    init_cpu_exception_handlers();

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t) &idt;
    lidt_ptr(&idt_ptr);
}

void init_page_tables(void)
{
    memset(pml4, 0, sizeof(pml4));
    memset(pdpt, 0, sizeof(pdpt));
    memset(pd0, 0, sizeof(pd0));
    memset(pd1, 0, sizeof(pd1));
    memset(pd3, 0, sizeof(pd3));
    memset(pd3b, 0, sizeof(pd3b));
    memset(pd7, 0, sizeof(pd7));
    memset(pd7b, 0, sizeof(pd7b));

    pml4[0] = (uint64_t) pdpt | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    pdpt[0] = (uint64_t) pd0 | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    pdpt[1] = (uint64_t) pd1 | PAGE_PRESENT | PAGE_WRITABLE;
    pdpt[3] = (uint64_t) pd3 | PAGE_PRESENT | PAGE_WRITABLE;
    pdpt[4] = (uint64_t) pd3b | PAGE_PRESENT | PAGE_WRITABLE;
    pdpt[7] = (uint64_t) pd7 | PAGE_PRESENT | PAGE_WRITABLE;
    pdpt[8] = (uint64_t) pd7b | PAGE_PRESENT | PAGE_WRITABLE;

    for (uint64_t i = 0; i < 512; i++) {
        pd0[i] = (i * 0x200000ULL) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_LARGE_2M;
        pd1[i] = ((i + 512) * 0x200000ULL) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_LARGE_2M;
    }
    pd0[32] |= PAGE_USER;
    pd0[33] |= PAGE_USER;
    for (uint64_t i = 0; i < 512; i++) {
        pd3[i] = (0xC0000000ULL + i * 0x200000ULL) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_LARGE_2M;
        pd3b[i] = (0x100000000ULL + i * 0x200000ULL) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_LARGE_2M;
        pd7[i] = (0x1C0000000ULL + i * 0x200000ULL) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_LARGE_2M;
        pd7b[i] = (0x200000000ULL + i * 0x200000ULL) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_LARGE_2M;
    }

    /* pml4 is a virtual address in the kernel's higher-half VA region.
     * CR3 requires the physical address of the PML4. Convert using the
     * kernel link base addresses stored in the kconfig section (written
     * by kernel_main). Layout at _kconfig_start: [0]=kernel_base_pa, [1]=kernel_base_va
     */
    extern uint64_t _kconfig_start;
    volatile uint64_t *kcfg = (volatile uint64_t *)&_kconfig_start;
    uint64_t kernel_base_pa = kcfg[0];
    uint64_t kernel_base_va = kcfg[1];

    /* Print key kernel symbol VAs and expected PAs for verification. */
    extern uint64_t _text_start;
    extern uint64_t _data_start;
    extern uint64_t __bss_start;
    extern uint8_t StackTop;

    uint64_t text_va = (uint64_t) &_text_start;
    uint64_t data_va = (uint64_t) &_data_start;
    uint64_t bss_va = (uint64_t) &__bss_start;
    uint64_t stacktop_va = (uint64_t) &StackTop;

    uint64_t text_pa = (kernel_base_va != 0) ? (text_va - kernel_base_va + kernel_base_pa) : text_va;
    uint64_t data_pa = (kernel_base_va != 0) ? (data_va - kernel_base_va + kernel_base_pa) : data_va;
    uint64_t bss_pa = (kernel_base_va != 0) ? (bss_va - kernel_base_va + kernel_base_pa) : bss_va;
    uint64_t stacktop_pa = (kernel_base_va != 0) ? (stacktop_va - kernel_base_va + kernel_base_pa) : stacktop_va;

    kernel_log_hex_u32("mmu: _text_start_va=", (uint32_t) (text_va & 0xFFFFFFFF));
    kernel_log_hex_u32("mmu: _text_start_pa=", (uint32_t) (text_pa & 0xFFFFFFFF));
    kernel_log_hex_u32("mmu: _data_start_va=", (uint32_t) (data_va & 0xFFFFFFFF));
    kernel_log_hex_u32("mmu: _data_start_pa=", (uint32_t) (data_pa & 0xFFFFFFFF));
    kernel_log_hex_u32("mmu: __bss_start_va=", (uint32_t) (bss_va & 0xFFFFFFFF));
    kernel_log_hex_u32("mmu: __bss_start_pa=", (uint32_t) (bss_pa & 0xFFFFFFFF));
    kernel_log_hex_u32("mmu: StackTop_va=", (uint32_t) (stacktop_va & 0xFFFFFFFF));
    kernel_log_hex_u32("mmu: StackTop_pa=", (uint32_t) (stacktop_pa & 0xFFFFFFFF));

    uint64_t pml4_virt = (uint64_t) pml4;
    uint64_t pml4_phys;
    /* If pml4 virtual address is in the kernel high-half, convert to PA.
     * Otherwise we are running identity-mapped and the pointer is already PA.
     */
    if (kernel_base_va != 0 && pml4_virt >= kernel_base_va) {
        pml4_phys = pml4_virt - kernel_base_va + kernel_base_pa;
    } else {
        pml4_phys = pml4_virt;
    }

    /* Now convert the PD/PT virtual pointers stored above to physical
     * addresses so the entries in the PML4/PDPT are valid physicals. */
    {
        uint64_t pdpt_phys = (kernel_base_va != 0 && (uint64_t)pdpt >= kernel_base_va) ? ((uint64_t)pdpt - kernel_base_va + kernel_base_pa) : (uint64_t)pdpt;
        uint64_t pd0_phys = (kernel_base_va != 0 && (uint64_t)pd0 >= kernel_base_va) ? ((uint64_t)pd0 - kernel_base_va + kernel_base_pa) : (uint64_t)pd0;
        uint64_t pd1_phys = (kernel_base_va != 0 && (uint64_t)pd1 >= kernel_base_va) ? ((uint64_t)pd1 - kernel_base_va + kernel_base_pa) : (uint64_t)pd1;
        uint64_t pd3_phys = (kernel_base_va != 0 && (uint64_t)pd3 >= kernel_base_va) ? ((uint64_t)pd3 - kernel_base_va + kernel_base_pa) : (uint64_t)pd3;
        uint64_t pd3b_phys = (kernel_base_va != 0 && (uint64_t)pd3b >= kernel_base_va) ? ((uint64_t)pd3b - kernel_base_va + kernel_base_pa) : (uint64_t)pd3b;
        uint64_t pd7_phys = (kernel_base_va != 0 && (uint64_t)pd7 >= kernel_base_va) ? ((uint64_t)pd7 - kernel_base_va + kernel_base_pa) : (uint64_t)pd7;
        uint64_t pd7b_phys = (kernel_base_va != 0 && (uint64_t)pd7b >= kernel_base_va) ? ((uint64_t)pd7b - kernel_base_va + kernel_base_pa) : (uint64_t)pd7b;

        pml4[0] = pdpt_phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        pdpt[0] = pd0_phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        pdpt[1] = pd1_phys | PAGE_PRESENT | PAGE_WRITABLE;
        pdpt[3] = pd3_phys | PAGE_PRESENT | PAGE_WRITABLE;
        pdpt[4] = pd3b_phys | PAGE_PRESENT | PAGE_WRITABLE;
        pdpt[7] = pd7_phys | PAGE_PRESENT | PAGE_WRITABLE;
        pdpt[8] = pd7b_phys | PAGE_PRESENT | PAGE_WRITABLE;
    }

    /* Ensure common MMIO/high framebuffer ranges are identity-mapped
     * in the kernel page tables before switching CR3. Map 0xF0000000..0xFFFFFFFF (256MB)
     * which covers QEMU/Bochs framebuffers and common MMIO regions. */
    mmu_map_identity(0xF0000000ULL, 0x10000000ULL);
    /* Also map VMware SVGA MMIO region which can appear at 0xE8000000. */
    mmu_map_identity(0xE8000000ULL, 0x01000000ULL);

    /* Dump page table entries that should cover the VMware SVGA MMIO region
     * (physical 0xE8000000). This helps verify the pd3 entry written by
     * mmu_map_identity(). */
    {
        uint64_t target = 0xE8000000ULL;
        const uint64_t page_size = 0x200000ULL;
        if (target >= 0xC0000000ULL && target < 0x100000000ULL) {
            uint64_t pd_index = (target - 0xC0000000ULL) / page_size;
            kernel_log_hex_u32("mmu: svga_pd_index=", (uint32_t) pd_index);
            kernel_log_hex_u32("mmu: pd3[svga]=", (uint32_t) (pd3[pd_index] & 0xFFFFFFFF));
        } else {
            kernel_log_hex_u32("mmu: svga_region_out_of_pd3", 0);
        }
    }

    kernel_log_hex_u32("mmu: pml4_virt=", (uint32_t) (pml4_virt & 0xFFFFFFFF));
    kernel_log_hex_u32("mmu: kernel_base_pa=", (uint32_t) (kernel_base_pa & 0xFFFFFFFF));
    kernel_log_hex_u32("mmu: kernel_base_va=", (uint32_t) (kernel_base_va & 0xFFFFFFFF));
    kernel_log_hex_u32("mmu: pml4_phys=", (uint32_t) (pml4_phys & 0xFFFFFFFF));

    /* Print a few page table entries for verification. */
    kernel_log_hex_u32("mmu: pml4[0]=" , (uint32_t) (pml4[0] & 0xFFFFFFFF));
    kernel_log_hex_u32("mmu: pdpt[0]=" , (uint32_t) (pdpt[0] & 0xFFFFFFFF));
    kernel_log_hex_u32("mmu: pd0[0]="   , (uint32_t) (pd0[0] & 0xFFFFFFFF));
    kernel_log_hex_u32("mmu: pd3[0]="   , (uint32_t) (pd3[0] & 0xFFFFFFFF));

    /* Ensure CR3 is page-aligned (PML4 aligned to 4096). */
    pml4_phys &= ~((uint64_t)0xFFF);
    /* Install kernel page tables now. */
    kernel_log_hex_u32("mmu: install cr3=", (uint32_t) (pml4_phys & 0xFFFFFFFF));
    /* publish pml4 phys so other subsystems can check whether the kernel
     * page-tables are active. */
    mmu_pml4_phys = pml4_phys;
    {
        uint64_t cur_cr3 = 0;
        asm volatile ("mov %%cr3, %0" : "=r" (cur_cr3));
        kernel_log_hex_u32("mmu: cr3_before=", (uint32_t) (cur_cr3 & 0xFFFFFFFF));
    }
    write_cr3(pml4_phys);
    {
        uint64_t cur_cr3 = 0;
        asm volatile ("mov %%cr3, %0" : "=r" (cur_cr3));
        kernel_log_hex_u32("mmu: cr3_after=", (uint32_t) (cur_cr3 & 0xFFFFFFFF));
    }
    log_write("mmu: post-cr3 continue");
}

/* Map a physical range into the kernel page-tables using 2MiB large pages.
 * This helper is intended for early use (before writing CR3) so the kernel
 * can ensure device MMIO / framebuffer physical regions are present when
 * switching to its own page tables.
 */
void mmu_map_identity(uint64_t phys_base, uint64_t length)
{
    const uint64_t page_size = 0x200000ULL; /* 2MiB */
    uint64_t start = phys_base & ~(page_size - 1);
    uint64_t end = ((phys_base + length + page_size - 1) & ~(page_size - 1));

    for (uint64_t addr = start; addr < end; addr += page_size) {
        /* map into appropriate PD array depending on physical address */
        if (addr < (uint64_t)512 * page_size) {
            /* 0..1GiB -> pd0 */
            uint64_t index = addr / page_size;
            pd0[index] = addr | PAGE_PRESENT | PAGE_WRITABLE | PAGE_LARGE_2M;
        } else if (addr < (uint64_t)1024 * page_size) {
            /* 1GiB..2GiB -> pd1 */
            uint64_t index = (addr / page_size) - 512;
            pd1[index] = addr | PAGE_PRESENT | PAGE_WRITABLE | PAGE_LARGE_2M;
        } else if (addr >= 0xC0000000ULL && addr < 0x100000000ULL) {
            /* 3GiB..4GiB -> pd3 */
            uint64_t index = (addr - 0xC0000000ULL) / page_size;
            pd3[index] = addr | PAGE_PRESENT | PAGE_WRITABLE | PAGE_LARGE_2M;
        } else if (addr >= 0x100000000ULL && addr < 0x1C0000000ULL) {
            /* 4GiB..7GiB -> pd3b (1:1 layout used earlier) */
            uint64_t index = (addr - 0x100000000ULL) / page_size;
            pd3b[index] = addr | PAGE_PRESENT | PAGE_WRITABLE | PAGE_LARGE_2M;
        } else if (addr >= 0x1C0000000ULL && addr < 0x200000000ULL) {
            /* 7GiB..8GiB -> pd7 */
            uint64_t index = (addr - 0x1C0000000ULL) / page_size;
            pd7[index] = addr | PAGE_PRESENT | PAGE_WRITABLE | PAGE_LARGE_2M;
        } else if (addr >= 0x200000000ULL) {
            /* 8GiB+ -> pd7b */
            uint64_t index = (addr - 0x200000000ULL) / page_size;
            pd7b[index] = addr | PAGE_PRESENT | PAGE_WRITABLE | PAGE_LARGE_2M;
        }
    }
}

/* Expose current kernel PML4 physical and activation check. */
uint64_t mmu_get_pml4_phys(void)
{
    return mmu_pml4_phys;
}

int mmu_is_active(void)
{
    uint64_t cur;
    asm volatile ("mov %%cr3, %0" : "=r" (cur));
    return cur == mmu_pml4_phys;
}
