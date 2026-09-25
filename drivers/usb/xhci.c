#include "common.h"
#include "kernel.h"
#include "mmu.h"
#include "pci.h"
#include "xhci.h"

#define PCI_CLASS_SERIAL_BUS    0x0C
#define PCI_SUBCLASS_USB        0x03
#define PCI_PROGIF_XHCI         0x30
#define PCI_COMMAND_OFFSET      0x04
#define PCI_COMMAND_MEMORY      0x0002
#define PCI_COMMAND_BUS_MASTER  0x0004
#define PCI_COMMAND_MMIO_ENABLE 0x0006

/* ── xHCI register offsets (capability) ──────────────────────────── */
#define XHCI_CAPLENGTH          0x00
#define XHCI_HCIVERSION         0x02
#define XHCI_HCSPARAMS1         0x04
#define XHCI_HCSPARAMS2         0x08
#define XHCI_HCSPARAMS3         0x0C
#define XHCI_HCCPARAMS1         0x10
#define XHCI_DBOFF              0x14
#define XHCI_RTSOFF             0x18

/* Operational register offsets (relative to op_base = mmio + cap_length) */
#define XHCI_USBCMD             0x00
#define XHCI_USBSTS             0x04
#define XHCI_PAGESIZE           0x08
#define XHCI_DNCTRL             0x14
#define XHCI_CRCR               0x18   /* 64-bit */
#define XHCI_DCBAAP             0x30   /* 64-bit */
#define XHCI_CONFIG             0x38
/* Port register set: PORTSC(n) sits at 0x400 + (n-1)*0x10. The value used to
 * be 0x40 here, which is reserved space inside the operational register block;
 * every PORTSC access therefore read 0 and wrote nowhere, so the root hub
 * always looked empty and port reset never touched the hardware. Checked
 * against xHCI 1.1 table 5-26 and Linux's struct xhci_op_regs, where the port
 * registers follow a 0xF0-dword reserved hole at 0x3C. */
#define XHCI_PORTSC_BASE        0x400  /* + (port-1)*0x10 */

/* USBCMD bits */
#define XHCI_CMD_RS             (1u << 0)
#define XHCI_CMD_HCRESET        (1u << 1)
#define XHCI_CMD_INTE           (1u << 2)
#define XHCI_CMD_XHCIEN         XHCI_CMD_RS

/* USBSTS bits */
#define XHCI_STS_HCH            (1u << 0)
#define XHCI_STS_HSE            (1u << 2)
#define XHCI_STS_EINT           (1u << 3)
#define XHCI_STS_PCD            (1u << 4)

/* CRCR bits */
#define XHCI_CRCR_RCS           (1u << 0)
#define XHCI_CRCR_CS_MASK       (1u << 1)
#define XHCI_CRCR_CRP_MASK      (~0x3Fu)

/* Runtime registers (relative to rts_base = mmio + rts_off) */
#define XHCI_MFINDEX            0x00
/* Interrupter register sets. xHCI 1.1 table 5-28: offset 0x00 holds MFINDEX,
 * 0x04..0x1F is reserved, and interrupter 0's register set begins at 0x20 with
 * interrupter n at 0x20 + n*0x20. The *inner* layout (table 5-30) is
 *   +0x00 IMAN   +0x04 IMOD   +0x08 ERSTSZ   +0x0C reserved
 *   +0x10 ERSTBA (64-bit)     +0x18 ERDP (64-bit)
 * so ERSTBA is 0x30 and ERDP is 0x38 once the 0x20 interrupter base is added.
 * These two were swapped here: ERSTBA was written to ERDP's register and vice
 * versa, so the controller got an Event Ring *dequeue pointer* where the
 * Event Ring Segment Table base address belonged and no valid segment table at
 * all. With no segment table the xHC cannot queue a single Event TRB, so every
 * Command Completion and Transfer Event wait timed out and the whole USB stack
 * looked dead while the registers benignly accepted the writes. */
#define XHCI_INTR_STRIDE        0x20
#define XHCI_IMAN0              0x20
#define XHCI_IMOD0              0x24
#define XHCI_ERSTSZ0            0x28
#define XHCI_ERSTBA0            0x30   /* 64-bit */
#define XHCI_ERDP0              0x38   /* 64-bit */

/* ERDP bits */
#define XHCI_ERDP_DESI          (1u << 3)
#define XHCI_ERDP_EHB           (1u << 3)

/* PORTSC bits */
#define XHCI_PS_CCS             (1u << 0)
#define XHCI_PS_PED             (1u << 1)
#define XHCI_PS_OCA             (1u << 3)
#define XHCI_PS_PR              (1u << 4)
#define XHCI_PS_PLS_SHIFT       5
#define XHCI_PS_PLS_MASK        (0xFu << XHCI_PS_PLS_SHIFT)
#define XHCI_PS_SPEED_SHIFT     10
#define XHCI_PS_SPEED_MASK      (0xFu << XHCI_PS_SPEED_SHIFT)
#define XHCI_PS_CSC             (1u << 17)
#define XHCI_PS_PRC             (1u << 18)
#define XHCI_PS_PLC             (1u << 19)
#define XHCI_PS_CEC             (1u << 23)

/* ── TRB types ────────────────────────────────────────────────────
 * Command TRB type numbers come from xHCI 1.1 table 6-91. The command block
 * starts at 9 - "No Op" is 8 and belongs to the transfer/event TRB space, not
 * to the command space. An earlier table here had TRB_TYPE_NOOP = 9, which
 * pushed Enable Slot to 10 and made every Enable Slot Command go out as a
 * Disable Slot Command. The controller answered each one with a Command
 * Completion Event carrying "TRB Error" (code 5) because the slot id field of
 * a Disable Slot TRB was zero, and no device was ever enumerated. The
 * registers, the rings and the doorbells were all correct; only this table
 * was wrong. */
#define TRB_TYPE_NORMAL         1u
#define TRB_TYPE_SETUP          2u
#define TRB_TYPE_DATA           3u
#define TRB_TYPE_STATUS         4u
#define TRB_TYPE_LINK           6u
#define TRB_TYPE_NOOP           8u   /* transfer/event No Op */
#define TRB_TYPE_ENABLE_SLOT    9u
#define TRB_TYPE_DISABLE_SLOT   10u
#define TRB_TYPE_ADDRESS_DEVICE 11u
#define TRB_TYPE_CONFIG_ENDPOINT 12u
#define TRB_TYPE_EVALUATE_ENDPOINT 13u
#define TRB_TYPE_RESET_ENDPOINT 14u
#define TRB_TYPE_CMD_NOOP       23u
#define TRB_TYPE_TRANSFER_EVENT 32u
#define TRB_TYPE_CMD_COMPLETION 33u
#define TRB_TYPE_PORT_STATUS_CHANGE 34u

/* Transfer-TRB control-dword bits. These are shared by Setup / Data / Status /
 * Normal TRBs (xHCI 1.1 table 6-30..6-33, and Linux's drivers/usb/host/xhci.h
 * which names them TRB_IOC / TRB_IDT / TRB_DIR_IN). */
#define TRB_TR_IOC               (1u << 5)   /* Interrupt On Completion */
#define TRB_TR_IDT               (1u << 6)   /* buffer pointer holds immediate data */
#define TRB_TR_BEI               (1u << 9)   /* Block Event Interrupt (opposite) */
#define TRB_TR_DIR               (1u << 16)  /* Data/Status direction, 1 = IN */
/* Setup-stage-only: TRT shares bit 16 with DIR and tells the controller whether
 * a data stage follows and in which direction (Linux: TRB_TX_TYPE(p) = p << 16,
 * TRB_DATA_OUT = 2, TRB_DATA_IN = 3). Getting TRT wrong is not recoverable -
 * the controller simply never runs the data stage. */
#define TRB_SETUP_TRT_SHIFT      16u
#define TRB_SETUP_TRT_NONE       0u
#define TRB_SETUP_TRT_OUT        2u
#define TRB_SETUP_TRT_IN         3u

/* ── Slot / Endpoint Context 字段布局 ──────────────────────────────
 * 依据 xHCI 1.1 规范表 6-6（Slot Context）与表 6-7（Endpoint Context）。
 * 这两张表是 Address Device / Configure Endpoint 能工作的前提，位域写错
 * 只会表现成"命令超时"，所以在这里集中定义、不要在调用点散写魔数。 */

/* Slot Context */
#define XHCI_SLOT_SPEED_SHIFT        20u   /* dword 0 bits 23:20 */
#define XHCI_SLOT_ENTRIES_SHIFT      27u   /* dword 0 bits 31:27，最后一项上下文的索引 */
#define XHCI_SLOT_ROOT_PORT_SHIFT    16u   /* dword 1 bits 23:16 */
#define XHCI_SLOT_DEV_ADDR_SHIFT     0u    /* dword 3 bits 7:0 */

/* Endpoint Context */
#define XHCI_EP_STATE_SHIFT          0u    /* dword 0 bits 2:0 */
#define XHCI_EP_INTERVAL_SHIFT       8u    /* dword 0 bits 15:8，仅周期端点非 0 */
#define XHCI_EP_CERR_SHIFT           1u    /* dword 1 bits 2:1，错误计数 */
#define XHCI_EP_TYPE_SHIFT           3u    /* dword 1 bits 5:3 */
#define XHCI_EP_MAX_BURST_SHIFT      8u    /* dword 1 bits 15:8 */
#define XHCI_EP_MAX_PACKET_SHIFT     16u   /* dword 1 bits 31:16 */

#define XHCI_EP_DCS                  1u    /* dequeue cycle state，与环 cycle 位一致 */

#define XHCI_EP_STATE_DISABLED       0u
#define XHCI_EP_STATE_RUNNING        1u
#define XHCI_EP_STATE_HALTED         2u

/* Endpoint Type（xHCI 表 6-7）。注意 IN/OUT 是不同的编码，不是方向位。 */
#define XHCI_EP_TYPE_ISOCH_OUT       1u
#define XHCI_EP_TYPE_BULK_OUT        2u
#define XHCI_EP_TYPE_INTR_OUT        3u
#define XHCI_EP_TYPE_CONTROL         4u    /* Control Bidirectional，仅 EP0 */
#define XHCI_EP_TYPE_ISOCH_IN        5u
#define XHCI_EP_TYPE_BULK_IN         6u
#define XHCI_EP_TYPE_INTR_IN         7u

/* USB 速度编码，Slot Context / PORTSC 共用 */
#define XHCI_SPEED_FULL              1u
#define XHCI_SPEED_LOW               2u
#define XHCI_SPEED_HIGH              3u
#define XHCI_SPEED_SUPER             4u

/* Input Context 布局（33 个 32 字节上下文 = 264 dword）：
 *   dword   0..7   Input Control Context（0 = Add Flags，1 = Drop Flags）
 *   dword   8..15  Slot Context
 *   dword  16..23  EP0 Context
 *   dword  24..    EP1..EP30 Context
 * Input Control Context 的 Add/Drop Flags 位 n 对应上下文 n：
 * bit0 = Slot，bit1 = EP0，bit2 = EP1 …… bit31 = EP30。 */
#define XHCI_ICTX_DWORDS             264u  /* 33 contexts x 32 bytes / 4 */
/* Stride between per-slot Input Contexts. The real content is 264 dwords
 * (1056 bytes), which is NOT a multiple of 64 - and the Address Device /
 * Configure Endpoint TRB parameter has bits 5:0 RsvdZ, so every slot's buffer
 * must begin on a 64-byte boundary. With the unpadded stride, slot 1 started
 * at +1056 (32-byte aligned only) and the xHC would have rejected or
 * mis-read the command. 272 dwords = 1088 bytes keeps every slot aligned. */
#define XHCI_ICTX_STRIDE_DWORDS      272u
/* Input Control Context, dwords 0..1 (xHCI 1.1 section 6.2.5.1). DWORD 0 is the
 * Drop Context Flags and DWORD 1 is the Add Context Flags - in that order. The
 * two used to be defined the other way round, so the Add Flags (Slot + EP0)
 * were written into the Drop Flags slot and the Add Flags slot was left at
 * zero. A controller that validates the Input Control Context - every real one
 * does, and so does QEMU - then rejects Address Device outright with a TRB
 * Error, which is precisely the "addressed nothing, explained nothing" failure
 * this was. Bit n of either dword refers to Device Context Index n: bit 0 is
 * the Slot Context, bit 1 is EP0. */
#define XHCI_ICTX_DROP_FLAGS         0u
#define XHCI_ICTX_ADD_FLAGS          1u
#define XHCI_ICTX_SLOT_CTX           8u
#define XHCI_ICTX_EP0_CTX            16u

#define XHCI_ICTX_ADD_SLOT           (1u << 0)
#define XHCI_ICTX_ADD_EP0            (1u << 1)

/* ── TRB structure (16 bytes) ────────────────────────────────────── */
typedef struct {
    uint32_t param_lo;
    uint32_t param_hi;
    uint32_t status;
    uint32_t control;
} xhci_trb_t;

/* Ring sizing */
#define XHCI_CMD_RING_SIZE      64u
#define XHCI_EVT_RING_SIZE      64u
#define XHCI_XFER_RING_SIZE     32u
#define XHCI_MAX_SLOTS          32u
#define XHCI_MAX_DEVICES        16u

/* Device Context Index (DCI) space. DCI 0 addresses the Slot Context; the
 * endpoint contexts occupy DCI 1..31, where EPn OUT is 2n and EPn IN is 2n+1.
 * Everything that is addressed "per endpoint" - transfer rings, dequeue
 * pointers, Input Context Add-Flags - is keyed by DCI, not by endpoint number. */
#define XHCI_MAX_DCI            32u
#define XHCI_DCI_SLOT           0u
#define XHCI_DCI_EP0            1u

/* Highest DCI we are willing to describe in a single Configure Endpoint
 * command. Bounding it keeps the Input Context inside g_input_context[] and
 * bounds the per-endpoint ring memory. DCI 15 covers EP1..EP7 in both
 * directions, which is more than any device this kernel drives needs. */
#define XHCI_MAX_CONFIGURED_EPS 14u

/* ── Static DMA-coherent (identity-mapped) rings ──────────────────── */
static xhci_trb_t g_cmd_ring[XHCI_CMD_RING_SIZE] __attribute__((aligned(64)));
static xhci_trb_t g_evt_ring[XHCI_EVT_RING_SIZE] __attribute__((aligned(64)));
/* Event Ring Segment Table: one 64-bit base + 32-bit size per segment */
typedef struct {
    uint64_t seg_base;
    uint32_t seg_size;
    uint32_t rsvd;
} __attribute__((packed)) xhci_erst_entry_t;
static xhci_erst_entry_t g_erst[2] __attribute__((aligned(64)));
/* Device Context Base Address Array: one 64-bit entry per slot */
static uint64_t g_dcbaa[XHCI_MAX_SLOTS] __attribute__((aligned(64)));
/* Per-device, per-endpoint transfer rings, indexed by Device Context Index.
 * Each endpoint owns an independent ring and dequeue pointer: xHCI tracks ring
 * state per endpoint, so sharing one ring across endpoints desynchronises the
 * controller's dequeue pointer from ours and the transfer silently stalls. */
static xhci_trb_t g_xfer_ring[XHCI_MAX_DEVICES][XHCI_MAX_DCI][XHCI_XFER_RING_SIZE]
    __attribute__((aligned(64)));
/* Device context array (slot context + 31 endpoint contexts, 32 bytes each = 1024 bytes) */
static uint64_t g_dev_context[XHCI_MAX_DEVICES][128] __attribute__((aligned(64)));
/* Input Context 数组：Address Device / Configure Endpoint 命令的输入。
 * 必须是**独立**的缓冲 —— 规范要求 TRB 参数指向 Input Context，xHC 再从中
 * 把 Slot Context / EP Context 复制进 Device Context。把 Device Context 本身
 * 当输入上下文用，真机上 Address Device 会失败。 */
static uint32_t g_input_context[XHCI_MAX_DEVICES][XHCI_ICTX_STRIDE_DWORDS]
    __attribute__((aligned(64)));

/* Per-slot geometry captured at Address Device time. A later Configure
 * Endpoint command has to re-state the Slot Context, and the xHC rejects an
 * Input Context whose speed / root-port / route fields contradict what it was
 * told once - so remember rather than re-derive. */
static uint8_t g_slot_port[XHCI_MAX_DEVICES];
static uint8_t g_slot_speed[XHCI_MAX_DEVICES];
/* Highest DCI currently valid for the slot; drives Slot Context "Context
 * Entries" and the check on the next Configure Endpoint command. */
static uint8_t g_slot_last_dci[XHCI_MAX_DEVICES];

/* Root-hub port -> "a slot was assigned to it" flag. Indexed by the 1-based
 * PORTSC port number, NOT by slot: the same slot numbering space is reused
 * after a disconnect, and a slot id has no fixed relationship to a port.
 * Sized for the full 8-bit port range so no controller can index past it. */
#define XHCI_MAX_PORTS          256u
static uint8_t g_port_enumerated[XHCI_MAX_PORTS];

static xhci_info_t g_xhci_info;
static xhci_device_t g_devices[XHCI_MAX_DEVICES];

static uint32_t g_cmd_ptr;          /* enqueue index into command ring */
static uint32_t g_cmd_cycle;        /* command ring cycle bit */
static uint32_t g_evt_ptr;          /* dequeue index into event ring */
static uint32_t g_evt_cycle;        /* event ring expected cycle bit */
/* Per-device, per-DCI transfer ring producer state. These MUST NOT be shared
 * with the command ring, nor between endpoints: each ring has its own enqueue
 * pointer and producer cycle bit, and the host controller tracks them
 * independently. */
static uint32_t g_xfer_ptr[XHCI_MAX_DEVICES][XHCI_MAX_DCI];
static uint32_t g_xfer_cycle[XHCI_MAX_DEVICES][XHCI_MAX_DCI];
static uint64_t g_op_base;
static uint64_t g_rts_base;
static uint64_t g_db_base;

/* ── MMIO helpers ──────────────────────────────────────────────────
 * Register offsets are small, but the BAR itself is not necessarily below
 * 4 GiB: xHCI controllers commonly expose a 64-bit BAR, and UEFI firmware in
 * particular likes to place such BARs above the 4 GiB line. So the base is
 * kept 64-bit and the high dword of the BAR must not be discarded. */
static uint64_t xhci_bar_base(uint32_t bar_lo, uint32_t bar_hi)
{
    uint64_t base;

    if ((bar_lo & 1u) != 0) {
        return 0;   /* an I/O-space BAR is not a register window */
    }
    base = (uint64_t) (bar_lo & 0xFFFFFFF0u);

    /* Bits 2:1 of the low dword give the BAR type: 0b10 means 64-bit, in which
     * case the next BAR slot holds the upper 32 address bits. */
    if (((bar_lo >> 1) & 0x3u) == 0x2u) {
        base |= ((uint64_t) bar_hi) << 32;
    }
    return base;
}

static uint32_t xhci_read32(uint64_t offset)
{
    volatile uint32_t *ptr = (volatile uint32_t *) (uintptr_t) (g_xhci_info.mmio_base + offset);
    return *ptr;
}

static void xhci_write32(uint64_t offset, uint32_t value)
{
    volatile uint32_t *ptr = (volatile uint32_t *) (uintptr_t) (g_xhci_info.mmio_base + offset);
    *ptr = value;
}

static uint8_t xhci_read8(uint64_t offset)
{
    volatile uint8_t *ptr = (volatile uint8_t *) (uintptr_t) (g_xhci_info.mmio_base + offset);
    return *ptr;
}

uint32_t xhci_mmio_read32(uint32_t offset)
{
    return g_xhci_info.mmio_ready ? xhci_read32(offset) : 0u;
}

void xhci_mmio_write32(uint32_t offset, uint32_t value)
{
    if (g_xhci_info.mmio_ready) {
        xhci_write32(offset, value);
    }
}

/* Operational / runtime register accessors.
 *
 * These take the three bases as ABSOLUTE addresses (mmio_base + offset), so
 * they must go through the absolute-address helpers, NOT through
 * xhci_read32()/xhci_write32(), which add g_xhci_info.mmio_base themselves.
 * Routing an absolute base through a base-relative helper adds the MMIO base
 * twice: with a BAR of 0xFEBF0000 every operational and runtime access landed
 * near 0x1FD7E000 (~7.96 GiB) instead of the device - inside the kernel's
 * identity-mapped RAM window, so reads returned zero and writes either
 * vanished past the end of RAM or scribbled on it. The controller never saw a
 * single register access from this driver. */
static uint32_t mmio_read32_at(uint64_t addr)
{
    return *(volatile uint32_t *) (uintptr_t) addr;
}

static void mmio_write32_at(uint64_t addr, uint32_t value)
{
    *(volatile uint32_t *) (uintptr_t) addr = value;
}

static uint64_t mmio_read64_at(uint64_t addr)
{
    return ((uint64_t) mmio_read32_at(addr + 4u) << 32) |
           (uint64_t) mmio_read32_at(addr);
}

static void mmio_write64_at(uint64_t addr, uint64_t value)
{
    /* LOW dword first, then HIGH - the order is load-bearing.
     *
     * Several 64-bit xHC registers latch their value (and begin acting on it)
     * when the HIGH dword is written, because that is the last write that
     * makes the whole 64-bit address meaningful: CRCR latches and publishes
     * the Command Ring base address, ERSTBA latches and re-initialises the
     * Event Ring, DCBAAP likewise. Writing HIGH first therefore hands the
     * controller a base address whose low half is still the previous value -
     * normally zero - so the ring ends up based at 0x0 and the controller
     * DMAs its TRBs into low memory. That is invisible from the read-back
     * side (both dwords do get stored), which is why this survived: the
     * registers read back exactly what was written while the controller
     * quietly used an address of 0. */
    mmio_write32_at(addr, (uint32_t) value);
    mmio_write32_at(addr + 4u, (uint32_t) (value >> 32));
}

static uint32_t op_read32(uint64_t off)  { return mmio_read32_at(g_op_base + off); }
static void     op_write32(uint64_t off, uint32_t v) { mmio_write32_at(g_op_base + off, v); }
static uint64_t op_read64(uint64_t off)  { return mmio_read64_at(g_op_base + off); }
static void     op_write64(uint64_t off, uint64_t v) { mmio_write64_at(g_op_base + off, v); }
static uint32_t rt_read32(uint64_t off)  { return mmio_read32_at(g_rts_base + off); }
static void     rt_write32(uint64_t off, uint32_t v) { mmio_write32_at(g_rts_base + off, v); }
static uint64_t rt_read64(uint64_t off)   { return mmio_read64_at(g_rts_base + off); }
static void     rt_write64(uint64_t off, uint64_t v) { mmio_write64_at(g_rts_base + off, v); }
static void     db_write32(uint32_t doorbell, uint32_t value)
{
    mmio_write32_at(g_db_base + (uint64_t) doorbell * 4u, value);
}

/* ── Register dump for the read-back-mismatch path ─────────────────
 * Only reached when the xHC did not retain what we programmed. Dumps the
 * bases, the operational registers, the runtime interrupter set and the first
 * two port register sets, so the mismatch can be diagnosed from the boot log
 * without another instrumented rebuild. */
static uint64_t portsc_reg(uint8_t port);
static void xhci_dump_regs(const char *tag)
{
    log_write(tag);
    kernel_log_hex_u32("xhci:  mmio lo ", (uint32_t) g_xhci_info.mmio_base);
    kernel_log_hex_u32("xhci:  mmio hi ", (uint32_t) (g_xhci_info.mmio_base >> 32));
    kernel_log_hex_u32("xhci:  cap_length ", g_xhci_info.cap_length);
    kernel_log_hex_u32("xhci:  rts_off ", g_xhci_info.rts_off);
    kernel_log_hex_u32("xhci:  dboff ", g_xhci_info.dboff);
    kernel_log_hex_u32("xhci:  usbcmd ", op_read32(XHCI_USBCMD));
    kernel_log_hex_u32("xhci:  usbsts ", op_read32(XHCI_USBSTS));
    kernel_log_hex_u32("xhci:  pagesize ", op_read32(XHCI_PAGESIZE));
    kernel_log_hex_u32("xhci:  dcbaap lo ", op_read32(XHCI_DCBAAP));
    kernel_log_hex_u32("xhci:  config ", op_read32(XHCI_CONFIG));
    kernel_log_hex_u32("xhci:  crcr lo ", op_read32(XHCI_CRCR));
    kernel_log_hex_u32("xhci:  erstsz ", rt_read32(XHCI_ERSTSZ0));
    kernel_log_hex_u32("xhci:  erstba lo ", rt_read32(XHCI_ERSTBA0));
    kernel_log_hex_u32("xhci:  erdp lo ", rt_read32(XHCI_ERDP0));
    if (g_xhci_info.max_ports >= 1u) {
        kernel_log_hex_u32("xhci:  portsc1 ", mmio_read32_at(portsc_reg(1u)));
    }
    if (g_xhci_info.max_ports >= 2u) {
        kernel_log_hex_u32("xhci:  portsc2 ", mmio_read32_at(portsc_reg(2u)));
    }
}

/* ── PCI probe callback ──────────────────────────────────────────── */
static bool xhci_find_callback(const pci_device_info_t *info, void *ctx)
{
    pci_device_info_t *out = (pci_device_info_t *) ctx;

    if (info->class_code == PCI_CLASS_SERIAL_BUS &&
        info->subclass == PCI_SUBCLASS_USB &&
        info->prog_if == PCI_PROGIF_XHCI) {
        *out = *info;
        return false;
    }
    return true;
}

/* ── Host controller reset and setup ───────────────────────────────
 * Bring the controller to the Halted state and reset it.
 *
 * Two independent defects used to live here:
 *
 *  1. The function waited for USBSTS.HCH to *clear* after the reset. xHCI 1.1
 *     §4.2 says the opposite - a completed host controller reset leaves the
 *     controller in the Halted state, i.e. HCH reads 1. That wait could never
 *     succeed, so hc_running stayed false, no root port was ever polled, no
 *     slot was enabled, and neither Address Device nor Configure Endpoint was
 *     ever issued. The whole USB stack was inert.
 *
 *  2. It treated "USBCMD.HCRESET still reads back set" as a hard failure. The
 *     self-clear is the documented completion signal, but it is not universally
 *     reported: QEMU's xHCI performs the reset on the write (visible as
 *     usb_xhci_reset in a QEMU trace) yet can leave the bit readable. Bailing
 *     out of bring-up over a read-back that the controller has already acted on
 *     discards a perfectly usable controller.
 *
 * So: attempt the reset, accept either completion signal, and only give up if
 * both are absent. Programming DCBAA/CRCR immediately afterwards is correct in
 * every case where a reset was actually performed, and if it was not, the
 * controller will report the failure on the first command instead - which is a
 * far more actionable symptom than an aborted bring-up. */
static bool xhci_hc_reset(void)
{
    uint32_t timeout;

    /* Best-effort stop. The reset is valid from any state, so a controller that
     * will not report Halted is not a reason to abort - it only means firmware
     * (or an earlier pass) left it running. */
    op_write32(XHCI_USBCMD, 0u);
    timeout = 100000u;
    while ((op_read32(XHCI_USBSTS) & XHCI_STS_HCH) == 0u) {
        if (--timeout == 0u) {
            log_write("xhci: HC not reporting Halted before reset");
            break;
        }
        io_wait();
    }

    /* Write HCRESET. Reading USBCMD first and OR-ing the bit in matches what
     * every mainstream driver does and avoids clobbering USBCMD.RS. */
    op_write32(XHCI_USBCMD, op_read32(XHCI_USBCMD) | XHCI_CMD_HCRESET);

    /* Completion signal 1: the controller clears HCRESET itself. */
    timeout = 100000u;
    while ((op_read32(XHCI_USBCMD) & XHCI_CMD_HCRESET) != 0u) {
        if (--timeout == 0u) {
            break;
        }
        io_wait();
    }
    if ((op_read32(XHCI_USBCMD) & XHCI_CMD_HCRESET) == 0u) {
        return true;
    }

    /* Completion signal 2: the reset happened anyway and the controller is in
     * the halted state the rest of bring-up depends on. */
    if ((op_read32(XHCI_USBSTS) & XHCI_STS_HCH) != 0u) {
        log_write("xhci: HCRESET read-back stuck; controller is halted, continuing");
        return true;
    }

    log_write("xhci: reset failed - no completion signal");
    kernel_log_hex_u32("xhci: usbcmd ", op_read32(XHCI_USBCMD));
    kernel_log_hex_u32("xhci: usbsts ", op_read32(XHCI_USBSTS));
    return false;
}

static bool xhci_setup_rings(void)
{
    uint32_t i;
    uint32_t dci;

    /* The xHC hard-wires the low 6 bits of ERSTBA, DCBAAP and CRCR to zero, and
     * bits 5:0 of every Device / Input Context pointer too. If any of these
     * buffers is not 64-byte aligned the controller silently rounds the address
     * down, reads someone else's memory, and DMA-writes an event TRB into it -
     * a wild write whose only symptom is a hang somewhere unrelated and much
     * later. Checking here turns that into one unmistakable log line.
     * (The usual cause is a linker script that forces input sections to a
     * smaller alignment than the source asked for.) */
    {
        uintptr_t bad = 0u;
        const char *which = "";

        if (((uintptr_t) g_erst & 0x3Fu) != 0u) { bad = (uintptr_t) g_erst; which = "g_erst"; }
        else if (((uintptr_t) g_dcbaa & 0x3Fu) != 0u) { bad = (uintptr_t) g_dcbaa; which = "g_dcbaa"; }
        else if (((uintptr_t) g_cmd_ring & 0x3Fu) != 0u) { bad = (uintptr_t) g_cmd_ring; which = "g_cmd_ring"; }
        else if (((uintptr_t) g_evt_ring & 0x3Fu) != 0u) { bad = (uintptr_t) g_evt_ring; which = "g_evt_ring"; }
        else if (((uintptr_t) &g_dev_context[0][0] & 0x3Fu) != 0u) { bad = (uintptr_t) g_dev_context; which = "g_dev_context"; }
        else if (((uintptr_t) &g_input_context[0][0] & 0x3Fu) != 0u) { bad = (uintptr_t) g_input_context; which = "g_input_context"; }
        else if (((uintptr_t) &g_xfer_ring[0][0][0] & 0x3Fu) != 0u) { bad = (uintptr_t) g_xfer_ring; which = "g_xfer_ring"; }

        if (which[0] != '\0') {
            log_write_event("xhci: DMA buffer not 64-byte aligned: ", which);
            kernel_log_hex_u32("xhci: misaligned addr ", (uint32_t) bad);
            log_write("xhci: refusing to start - controller would round the address down");
            return false;
        }
    }

    /* Identity-map all ring memory (already in kernel image, but be safe) */
    mmu_map_identity((uint64_t) (uintptr_t) g_cmd_ring, sizeof(g_cmd_ring));
    mmu_map_identity((uint64_t) (uintptr_t) g_evt_ring, sizeof(g_evt_ring));
    mmu_map_identity((uint64_t) (uintptr_t) g_erst, sizeof(g_erst));
    mmu_map_identity((uint64_t) (uintptr_t) g_dcbaa, sizeof(g_dcbaa));
    /* The xHC reads the Input Context over DMA while it services a command, so
     * it has to be mapped and zeroed before the first Address Device. */
    mmu_map_identity((uint64_t) (uintptr_t) g_input_context, sizeof(g_input_context));
    mmu_map_identity((uint64_t) (uintptr_t) g_xfer_ring, sizeof(g_xfer_ring));

    memset(g_cmd_ring, 0, sizeof(g_cmd_ring));
    memset(g_evt_ring, 0, sizeof(g_evt_ring));
    memset(g_erst, 0, sizeof(g_erst));
    memset(g_dcbaa, 0, sizeof(g_dcbaa));
    memset(g_input_context, 0, sizeof(g_input_context));
    memset(g_slot_port, 0, sizeof(g_slot_port));
    memset(g_slot_speed, 0, sizeof(g_slot_speed));
    memset(g_slot_last_dci, 0, sizeof(g_slot_last_dci));
    memset(g_port_enumerated, 0, sizeof(g_port_enumerated));

    /* Command ring: Link TRB at last entry points back to entry 0, toggle cycle */
    g_cmd_ring[XHCI_CMD_RING_SIZE - 1].param_lo =
        (uint32_t) ((uint64_t) (uintptr_t) &g_cmd_ring[0]);
    g_cmd_ring[XHCI_CMD_RING_SIZE - 1].param_hi =
        (uint32_t) (((uint64_t) (uintptr_t) g_cmd_ring) >> 32);
    g_cmd_ring[XHCI_CMD_RING_SIZE - 1].status = 0u;
    g_cmd_ring[XHCI_CMD_RING_SIZE - 1].control =
        ((uint32_t) TRB_TYPE_LINK << 10) | (1u << 1);   /* TC=1, cycle set later */
    g_cmd_ptr = 0u;
    g_cmd_cycle = 1u;

    /* Event ring segment table: segment 0 = g_evt_ring */
    g_erst[0].seg_base = (uint64_t) (uintptr_t) &g_evt_ring[0];
    g_erst[0].seg_size = XHCI_EVT_RING_SIZE;
    g_erst[1].seg_base = 0u;
    g_erst[1].seg_size = 0u;
    g_evt_ptr = 0u;
    g_evt_cycle = 1u;

    /* Program DCBAAP */
    op_write64(XHCI_DCBAAP, (uint64_t) (uintptr_t) &g_dcbaa[0]);

    /* Program Event Ring Register Set for interrupter 0 */
    rt_write32(XHCI_ERSTSZ0, 1u);
    rt_write64(XHCI_ERSTBA0, (uint64_t) (uintptr_t) &g_erst[0]);
    rt_write64(XHCI_ERDP0, (uint64_t) (uintptr_t) &g_evt_ring[0]);

    /* Start command ring: CRCR = ring base | RCS */
    op_write64(XHCI_CRCR,
               ((uint64_t) (uintptr_t) &g_cmd_ring[0] & XHCI_CRCR_CRP_MASK) |
               (uint64_t) XHCI_CRCR_RCS);

    /* Page size register: program 1 (means 4KB) */
    op_write32(XHCI_PAGESIZE, 1u);

    for (i = 0; i < XHCI_MAX_DEVICES; i++) {
        memset(g_dev_context[i], 0, sizeof(g_dev_context[i]));
        g_dcbaa[i + 1] = (uint64_t) (uintptr_t) &g_dev_context[i][0];

        for (dci = 0u; dci < XHCI_MAX_DCI; dci++) {
            xhci_trb_t *link;
            xhci_trb_t *ring = &g_xfer_ring[i][dci][0];

            memset(ring, 0, sizeof(g_xfer_ring[i][dci]));

            /* Transfer ring: like the command ring, the last entry must be a
             * Link TRB with TC=1 pointing back at entry 0. Without it the
             * controller would walk past the end of the ring after the final
             * usable entry. Entry XHCI_XFER_RING_SIZE-1 is therefore never used
             * for transfers. */
            link = &ring[XHCI_XFER_RING_SIZE - 1u];
            link->param_lo = (uint32_t) ((uint64_t) (uintptr_t) &ring[0]);
            link->param_hi = (uint32_t) (((uint64_t) (uintptr_t) ring) >> 32);
            link->status = 0u;
            link->control = ((uint32_t) TRB_TYPE_LINK << 10) | (1u << 1); /* TC=1 */
            g_xfer_ptr[i][dci] = 0u;
            g_xfer_cycle[i][dci] = 1u;
        }
    }
    g_dcbaa[0] = 0u;
    g_xhci_info.device_count = 0u;

    /* Read back the 64-bit / 32-bit register programming. A mismatch means the
     * MMIO window is not what we think it is (or the controller silently
     * dropped the write); surfacing the values here turns a later "mysterious
     * transfer timeout" into an actionable boot-log line. */
    {
        uint64_t want_dcbaap = (uint64_t) (uintptr_t) &g_dcbaa[0];
        uint64_t got_dcbaap = op_read64(XHCI_DCBAAP);
        uint64_t want_erstba = (uint64_t) (uintptr_t) &g_erst[0];
        uint64_t got_erstba = rt_read64(XHCI_ERSTBA0);
        uint32_t got_erstsz = rt_read32(XHCI_ERSTSZ0);

        if (got_dcbaap != want_dcbaap || got_erstba != want_erstba ||
            got_erstsz != 1u) {
            log_write("xhci: register read-back mismatch (dcbaap/erstba/erstsz)");
            xhci_dump_regs("xhci: register dump after programming");
            kernel_log_hex_u32("xhci: dcbaap want lo ", (uint32_t) want_dcbaap);
            kernel_log_hex_u32("xhci: dcbaap got  lo ", (uint32_t) got_dcbaap);
            kernel_log_hex_u32("xhci: erstba want lo ", (uint32_t) want_erstba);
            kernel_log_hex_u32("xhci: erstba got  lo ", (uint32_t) got_erstba);
        }
    }
    return true;
}

/* ── Command ring submission ─────────────────────────────────────── */
static void xhci_enqueue_command(const xhci_trb_t *cmd)
{
    xhci_trb_t *slot = &g_cmd_ring[g_cmd_ptr];

    slot->param_lo = cmd->param_lo;
    slot->param_hi = cmd->param_hi;
    slot->status = cmd->status;
    slot->control = cmd->control | (g_cmd_cycle & 1u);

    g_cmd_ptr++;
    if (g_cmd_ptr >= XHCI_CMD_RING_SIZE) {
        g_cmd_ptr = 0u;
        g_cmd_cycle ^= 1u;
    }
    /* Ring the Host Controller Command Doorbell (doorbell 0).
     *
     * The payload MUST be zero: doorbell 0's DB Target / DB Stream ID fields
     * (bits 31:0) are RsvdZ - "reserved, must write zero". Only device slot
     * doorbells carry a value (the endpoint DCI). This used to write the
     * enqueue pointer here, which is a spec violation; controllers are
     * entitled to treat a non-zero value as a request for a device slot
     * doorbell and drop it. The result was that the command ring was never
     * consumed, no Command Completion Event was ever posted, and every
     * Enable Slot / Address Device / Configure Endpoint command timed out
     * while the rest of the controller (port reset, for instance) worked
     * perfectly - which made it look like a hardware fault rather than a
     * one-word bug. */
    db_write32(0u, 0u);
}

/* ── Event ring wait ─────────────────────────────────────────────── */
static int xhci_wait_event(uint32_t expected_trb_type, uint32_t timeout_ms,
                           uint32_t *out_status, uint32_t *out_slot)
{
    uint64_t start = timer_ticks();

    for (;;) {
        xhci_trb_t *ev = &g_evt_ring[g_evt_ptr];
        uint32_t cyc = (ev->control & 1u);

        if (cyc == g_evt_cycle) {
            uint32_t type = (ev->control >> 10) & 0x3Fu;

            /* Advance event dequeue pointer */
            g_evt_ptr++;
            if (g_evt_ptr >= XHCI_EVT_RING_SIZE) {
                g_evt_ptr = 0u;
                g_evt_cycle ^= 1u;
            }
            /* Update ERDP with EHB when we wrapped */
            rt_write64(XHCI_ERDP0,
                        (uint64_t) (uintptr_t) &g_evt_ring[g_evt_ptr] |
                        (uint64_t) XHCI_ERDP_EHB);

            if (expected_trb_type == 0u || type == expected_trb_type) {
                if (out_status) *out_status = ev->status;
                if (out_slot) *out_slot = (ev->control >> 24) & 0xFFu;
                return 0;
            }
            /* Other event (e.g. port status change): keep waiting for expected */
        }

        if (timer_ticks() - start > timeout_ms) {
            return -1;
        }
        io_wait();
    }
}

/* ── Completion code handling ─────────────────────────────────────
 * A Command Completion Event always arrives, even when the command failed: the
 * failure lives in status bits 31:24 as a Completion Code. The old code only
 * checked "did an event arrive", so a rejected Address Device looked exactly
 * like a successful one. Every command path now decodes the code and reports
 * it, which is what makes a bad Input Context diagnosable from the boot log.
 *
 * Numbers are xHCI 1.1 table 6-90. Note that SUCCESS is 1, not 0: code 0 is
 * "Invalid". Getting this wrong inverts the meaning of every completion event,
 * so a controller that had just enabled a device slot successfully was
 * reported as having failed. */
#define XHCI_CC_INVALID              0u
#define XHCI_CC_SUCCESS              1u
#define XHCI_CC_DATA_BUFFER_ERROR    2u
#define XHCI_CC_BABBLE               3u
#define XHCI_CC_USB_TRANSACTION      4u
#define XHCI_CC_TRB_ERROR            5u
#define XHCI_CC_STALL                6u
#define XHCI_CC_RESOURCE_ERROR       7u
#define XHCI_CC_BANDWIDTH_ERROR      8u
#define XHCI_CC_NO_SLOTS             9u
#define XHCI_CC_SLOT_NOT_ENABLED     11u
#define XHCI_CC_EP_NOT_ENABLED       12u
#define XHCI_CC_SHORT_PACKET         13u
#define XHCI_CC_RING_UNDERRUN        14u
#define XHCI_CC_RING_OVERRUN         15u
#define XHCI_CC_PARAMETER_ERROR      17u
#define XHCI_CC_BW_OVERRUN           18u
#define XHCI_CC_CONTEXT_STATE_ERROR  19u
#define XHCI_CC_NO_PING_RESPONSE     20u
#define XHCI_CC_EVENT_RING_FULL      21u
#define XHCI_CC_INCOMPATIBLE_DEVICE  22u
#define XHCI_CC_CMD_RING_STOPPED     24u
#define XHCI_CC_CMD_ABORTED          25u
#define XHCI_CC_STOPPED              26u

static uint32_t xhci_completion_code(uint32_t status)
{
    return (status >> 24) & 0xFFu;
}

static const char *xhci_completion_name(uint32_t code)
{
    switch (code) {
    case XHCI_CC_SUCCESS:             return "success";
    case XHCI_CC_INVALID:             return "invalid";
    case XHCI_CC_DATA_BUFFER_ERROR:   return "data buffer error";
    case XHCI_CC_BABBLE:              return "babble detected";
    case XHCI_CC_USB_TRANSACTION:     return "usb transaction error";
    case XHCI_CC_TRB_ERROR:           return "trb error";
    case XHCI_CC_STALL:               return "stall";
    case XHCI_CC_RESOURCE_ERROR:      return "resource error";
    case XHCI_CC_BANDWIDTH_ERROR:     return "bandwidth error";
    case XHCI_CC_NO_SLOTS:            return "no slots available";
    case XHCI_CC_SLOT_NOT_ENABLED:    return "slot not enabled";
    case XHCI_CC_EP_NOT_ENABLED:      return "endpoint not enabled";
    case XHCI_CC_SHORT_PACKET:        return "short packet";
    case XHCI_CC_RING_UNDERRUN:       return "ring underrun";
    case XHCI_CC_RING_OVERRUN:        return "ring overrun";
    case XHCI_CC_PARAMETER_ERROR:     return "parameter error";
    case XHCI_CC_BW_OVERRUN:          return "bandwidth overrun";
    case XHCI_CC_CONTEXT_STATE_ERROR: return "context state error";
    case XHCI_CC_NO_PING_RESPONSE:    return "no ping response";
    case XHCI_CC_EVENT_RING_FULL:     return "event ring full";
    case XHCI_CC_INCOMPATIBLE_DEVICE: return "incompatible device";
    case XHCI_CC_CMD_RING_STOPPED:    return "command ring stopped";
    case XHCI_CC_CMD_ABORTED:         return "command aborted";
    case XHCI_CC_STOPPED:             return "stopped";
    default:                          return "unknown completion code";
    }
}

/* Log a non-success completion. Success stays silent: the boot log already
 * carries a line per attached device and a "everything is fine" line per
 * command would bury the real signal. */
static void xhci_report_completion(const char *what, uint32_t status)
{
    uint32_t code = xhci_completion_code(status);

    if (code == XHCI_CC_SUCCESS) {
        return;
    }
    log_write_event(what, xhci_completion_name(code));
    kernel_log_hex_u32("xhci: raw completion status ", status);
}

/* ── Input Context construction ───────────────────────────────────
 * Both Address Device and Configure Endpoint take an Input Context, which is a
 * *separate* 33-context array: the xHC reads the Slot/Endpoint Contexts out of
 * it and copies them into the Device Context. Handing the xHC the Device
 * Context itself (the previous behaviour) fails on real controllers. */

/* Default EP0 max packet size for a link speed. This is the value the initial
 * Address Device command must carry. The device's actual bMaxPacketSize0 is
 * learned afterwards and applied with an Evaluate Context command. */
static uint16_t xhci_ep0_default_mps(uint8_t speed)
{
    switch (speed) {
    case XHCI_SPEED_SUPER: return 512u;
    case XHCI_SPEED_HIGH:  return 64u;
    default:               return 8u;     /* Full and Low speed */
    }
}

/* Point an Input Context endpoint entry at its transfer ring. */
static void xhci_input_ctx_set_dequeue(uint32_t *ep_ctx, uint32_t dev, uint32_t dci)
{
    uint64_t deq = (uint64_t) (uintptr_t) &g_xfer_ring[dev][dci][0];

    /* TR Dequeue Pointer occupies dwords 2:3. Bits 3:0 are not address bits:
     * bit0 is the Dequeue Cycle State, which has to agree with the ring's live
     * producer cycle or the controller halts the endpoint on its first TRB. */
    ep_ctx[2] = ((uint32_t) deq & 0xFFFFFFF0u) |
                (g_xfer_cycle[dev][dci] & XHCI_EP_DCS);
    ep_ctx[3] = (uint32_t) (deq >> 32);
}

static void xhci_input_ctx_fill_slot(uint32_t *slot_ctx, uint8_t port,
                                     uint8_t speed, uint8_t context_entries)
{
    /* dword0: Route String(19:0)=0 for a root-hub device, Speed(23:20) and
     * Context Entries(31:27)= index of the last valid endpoint context. */
    slot_ctx[0] = ((uint32_t) speed << XHCI_SLOT_SPEED_SHIFT) |
                  ((uint32_t) context_entries << XHCI_SLOT_ENTRIES_SHIFT);
    /* dword1: Root Hub Port Number(23:16). Required for a root-hub device -
     * the xHC uses it to pick the right port state machine. */
    slot_ctx[1] = ((uint32_t) port << XHCI_SLOT_ROOT_PORT_SHIFT);
    /* dword3: USB Device Address(7:0) stays 0: the xHC assigns it while
     * servicing Address Device. */
    slot_ctx[3] = 0u;
}

static void xhci_input_ctx_fill_ep(uint32_t *ep_ctx, uint32_t dev, uint32_t dci,
                                   uint8_t type, uint16_t max_packet,
                                   uint8_t interval, uint8_t max_burst)
{
    /* dword0: EP State(2:0), Interval(15:8). Interval must be 0 for
     * control/bulk; only interrupt and isochronous endpoints use it. */
    ep_ctx[0] = (XHCI_EP_STATE_RUNNING << XHCI_EP_STATE_SHIFT) |
                ((uint32_t) interval << XHCI_EP_INTERVAL_SHIFT);
    /* dword1: CErr(2:1)=3 (retry three times, then halt the endpoint),
     * EP Type(5:3), Max Burst(15:8), Max Packet Size(31:16). */
    ep_ctx[1] = (3u << XHCI_EP_CERR_SHIFT) |
                ((uint32_t) type << XHCI_EP_TYPE_SHIFT) |
                ((uint32_t) max_burst << XHCI_EP_MAX_BURST_SHIFT) |
                ((uint32_t) max_packet << XHCI_EP_MAX_PACKET_SHIFT);
    xhci_input_ctx_set_dequeue(ep_ctx, dev, dci);
    /* dword4 bits 15:0: Average TRB Length, advisory - the xHC only uses it to
     * estimate bandwidth. */
    ep_ctx[4] = (uint32_t) (max_packet > 8u ? 1024u : 8u);
}

/* Begin a fresh Input Context for `slot`, leaving the Slot Context and EP0
 * handled but every endpoint blank. Returns the Add-Flags dword. */
static uint32_t xhci_input_context_begin(uint8_t slot, uint8_t port,
                                         uint8_t speed, uint8_t context_entries)
{
    uint32_t *ictx = &g_input_context[slot - 1u][0];

    memset(ictx, 0, XHCI_ICTX_STRIDE_DWORDS * sizeof(uint32_t));

    /* Input Control Context: bit0 -> Slot Context, bit1 -> EP0 Context, and
     * bit n -> DCI n. The Drop Flags dword stays zero: Address Device and
     * Configure Endpoint never remove contexts here. */
    ictx[XHCI_ICTX_ADD_FLAGS] =
        XHCI_ICTX_ADD_SLOT | XHCI_ICTX_ADD_EP0;
    ictx[XHCI_ICTX_DROP_FLAGS] = 0u;

    xhci_input_ctx_fill_slot(ictx + XHCI_ICTX_SLOT_CTX, port, speed,
                             context_entries);
    return ictx[XHCI_ICTX_ADD_FLAGS];
}

/* ── Standard commands ───────────────────────────────────────────── */
int xhci_enable_slot(uint8_t *out_slot)
{
    xhci_trb_t cmd;
    uint32_t status = 0u;
    uint32_t slot = 0u;

    if (!g_xhci_info.hc_running) {
        return -1;
    }
    memset(&cmd, 0, sizeof(cmd));
    cmd.control = ((uint32_t) TRB_TYPE_ENABLE_SLOT << 10);
    xhci_enqueue_command(&cmd);

    if (xhci_wait_event(TRB_TYPE_CMD_COMPLETION, 1000u, &status, &slot) != 0) {
        return -2;
    }
    xhci_report_completion("xhci: enable slot failed", status);
    if (xhci_completion_code(status) != XHCI_CC_SUCCESS) {
        return -3;
    }
    if (out_slot) {
        *out_slot = (uint8_t) slot;
    }
    g_xhci_info.num_active_slots++;
    return 0;
}

/* Address Device: hand the xHC a *built* Input Context so it can populate the
 * Device Context and move the slot to Addressed state. Without this the slot
 * exists but no endpoint is usable and every later transfer stalls.
 *
 * `port` is the 1-based root-hub port and `speed` is the PORTSC speed code;
 * both end up in the Slot Context. */
int xhci_address_device(uint8_t slot, uint8_t port, uint8_t speed)
{
    xhci_trb_t cmd;
    uint32_t status = 0u;
    uint32_t evslot = 0u;
    uint32_t *ictx;
    uint32_t *ep0_ctx;
    uint16_t mps;

    if (!g_xhci_info.hc_running) {
        return -1;
    }
    /* g_dev_context / g_xfer_ring / g_input_context are all sized for
     * XHCI_MAX_DEVICES slots and indexed by slot-1, so a slot id beyond that
     * would index past the end of every per-slot array. */
    if (slot == 0u || slot > XHCI_MAX_DEVICES) {
        return -1;
    }

    /* Slot Context says "the last valid context is DCI 1", i.e. only EP0 is
     * present at this point. Configure Endpoint raises it later. */
    (void) xhci_input_context_begin(slot, port, speed, (uint8_t) XHCI_DCI_EP0);

    mps = xhci_ep0_default_mps(speed);
    ictx = &g_input_context[slot - 1u][0];
    ep0_ctx = ictx + XHCI_ICTX_EP0_CTX;
    xhci_input_ctx_fill_ep(ep0_ctx, (uint32_t) (slot - 1u), XHCI_DCI_EP0,
                           XHCI_EP_TYPE_CONTROL, mps, 0u, 0u);

    memset(&cmd, 0, sizeof(cmd));
    cmd.param_lo = (uint32_t) ((uint64_t) (uintptr_t) ictx);
    cmd.param_hi = (uint32_t) (((uint64_t) (uintptr_t) ictx) >> 32);
    cmd.control = ((uint32_t) TRB_TYPE_ADDRESS_DEVICE << 10) |
                  ((uint32_t) slot << 24);
    xhci_enqueue_command(&cmd);

    if (xhci_wait_event(TRB_TYPE_CMD_COMPLETION, 1000u, &status, &evslot) != 0) {
        log_write("xhci: address device timed out");
        return -2;
    }
    xhci_report_completion("xhci: address device failed", status);
    if (xhci_completion_code(status) != XHCI_CC_SUCCESS) {
        return -3;
    }

    /* Remember the geometry so a later Configure Endpoint can restate the Slot
     * Context consistently. */
    g_slot_port[slot - 1u] = port;
    g_slot_speed[slot - 1u] = speed;
    g_slot_last_dci[slot - 1u] = (uint8_t) XHCI_DCI_EP0;
    return 0;
}

/* Configure Endpoint: enable the non-zero endpoints described by `eps`.
 *
 * This is the second half of the pair - Address Device brings the device to
 * Addressed state with EP0 only, and nothing but EP0 can carry a transfer
 * until this command has run. Every enabled endpoint gets its own transfer
 * ring, because the xHC tracks one dequeue pointer per endpoint.
 *
 * Returns 0 on success, negative on failure. */
int xhci_configure_endpoint(uint8_t slot, const xhci_ep_config_t *eps,
                            uint8_t ep_count)
{
    xhci_trb_t cmd;
    uint32_t status = 0u;
    uint32_t evslot = 0u;
    uint32_t *ictx;
    uint32_t add_flags;
    uint8_t i;
    uint8_t highest_dci = (uint8_t) XHCI_DCI_EP0;
    uint8_t port;
    uint8_t speed;

    if (!g_xhci_info.hc_running) {
        return -1;
    }
    if (slot == 0u || slot > XHCI_MAX_DEVICES) {
        return -1;
    }
    if (eps == NULL || ep_count == 0u || ep_count > XHCI_MAX_CONFIGURED_EPS) {
        return -1;
    }

    port = g_slot_port[slot - 1u];
    speed = g_slot_speed[slot - 1u];

    /* Validate first: a Context Entries value that does not match the highest
     * Add Flag bit makes the xHC reject the whole command with a Parameter
     * Error, so catch it here rather than in the controller. */
    for (i = 0u; i < ep_count; i++) {
        uint8_t dci = (uint8_t) ((uint32_t) eps[i].ep_number * 2u +
                                 (eps[i].direction_in ? 1u : 0u));
        if (eps[i].ep_number == 0u || eps[i].ep_number > 15u) {
            return -1;
        }
        if (dci > highest_dci) {
            highest_dci = dci;
        }
    }
    if (highest_dci > XHCI_MAX_CONFIGURED_EPS + (uint8_t) XHCI_DCI_EP0) {
        return -1;
    }

    (void) xhci_input_context_begin(slot, port, speed, highest_dci);
    add_flags = g_input_context[slot - 1u][XHCI_ICTX_ADD_FLAGS];
    ictx = &g_input_context[slot - 1u][0];

    /* Configure Endpoint re-states the Slot Context (Add Flags bit 0) but must
     * NOT re-add EP0 (bit 1). A Configure Endpoint command whose Add Flags
     * carry EP0 is rejected outright with a TRB Error by any controller that
     * validates the Input Control Context, and QEMU is explicit about it:
     * it requires (add_flags & 0x3) == 0x1. EP0 is established once, by
     * Address Device, and never again. */
    add_flags &= ~XHCI_ICTX_ADD_EP0;

    for (i = 0u; i < ep_count; i++) {
        uint8_t dci = (uint8_t) ((uint32_t) eps[i].ep_number * 2u +
                                 (eps[i].direction_in ? 1u : 0u));
        uint8_t type;

        /* Map the USB transfer type plus the direction onto the xHCI Endpoint
         * Type encoding. Keeping the mapping here means callers can hand over
         * a parsed endpoint descriptor verbatim. */
        switch (eps[i].xfer_type) {
        case XHCI_XFER_BULK:
            type = (uint8_t) (eps[i].direction_in ? XHCI_EP_TYPE_BULK_IN
                                                  : XHCI_EP_TYPE_BULK_OUT);
            break;
        case XHCI_XFER_INTERRUPT:
            type = (uint8_t) (eps[i].direction_in ? XHCI_EP_TYPE_INTR_IN
                                                  : XHCI_EP_TYPE_INTR_OUT);
            break;
        case XHCI_XFER_ISOCH:
            type = (uint8_t) (eps[i].direction_in ? XHCI_EP_TYPE_ISOCH_IN
                                                  : XHCI_EP_TYPE_ISOCH_OUT);
            break;
        default:
            /* Control is EP0 only and is already established by Address
             * Device; a caller listing it here is a bug. */
            return -1;
        }

        xhci_input_ctx_fill_ep(ictx + XHCI_ICTX_EP0_CTX + (uint32_t) (dci - 1u) * 8u,
                               (uint32_t) (slot - 1u), dci, type,
                               eps[i].max_packet, eps[i].interval,
                               eps[i].max_burst);
        add_flags |= (1u << dci);
    }

    ictx[XHCI_ICTX_ADD_FLAGS] = add_flags;

    memset(&cmd, 0, sizeof(cmd));
    cmd.param_lo = (uint32_t) ((uint64_t) (uintptr_t) ictx);
    cmd.param_hi = (uint32_t) (((uint64_t) (uintptr_t) ictx) >> 32);
    cmd.control = ((uint32_t) TRB_TYPE_CONFIG_ENDPOINT << 10) |
                  ((uint32_t) slot << 24);
    xhci_enqueue_command(&cmd);

    if (xhci_wait_event(TRB_TYPE_CMD_COMPLETION, 1000u, &status, &evslot) != 0) {
        log_write("xhci: configure endpoint timed out");
        return -2;
    }
    xhci_report_completion("xhci: configure endpoint failed", status);
    if (xhci_completion_code(status) != XHCI_CC_SUCCESS) {
        return -3;
    }

    g_slot_last_dci[slot - 1u] = highest_dci;
    return 0;
}

/* ── Port management ─────────────────────────────────────────────── */
/* The port register set lives at op_base + 0x400 (xHCI 1.1 table 5-26: the
 * operational block is 0x400 bytes of registers followed by a reserved hole,
 * and PORTSC(n) is at operational base + 0x400 + (n-1)*0x10). The result is an
 * ABSOLUTE address, so it must only ever be dereferenced through the
 * mmio_*_at() helpers. Feeding it to xhci_read32()/xhci_write32() adds the
 * MMIO base a second time and the ports appear permanently empty. */
static uint64_t portsc_reg(uint8_t port)
{
    return g_op_base + XHCI_PORTSC_BASE + (uint64_t) (port - 1u) * 0x10u;
}

int xhci_port_reset(uint8_t port)
{
    uint64_t reg;
    uint32_t timeout;

    if (port == 0u || port > g_xhci_info.max_ports) {
        return -1;
    }
    reg = portsc_reg(port);

    /* Trigger port reset (PR bit). PR is RW1S, and every other bit written as
     * 0 leaves that register alone, so a bare PR write is the correct way to
     * start a reset. */
    mmio_write32_at(reg, XHCI_PS_PR);
    timeout = 100000u;
    while ((mmio_read32_at(reg) & XHCI_PS_PR) != 0u) {
        if (--timeout == 0u) {
            return -2;
        }
        io_wait();
    }

    /* Enable the port. The xHC sets PED itself on a successful reset; setting
     * it here is harmless when it already did and required when it did not. */
    {
        uint32_t portsc = mmio_read32_at(reg);
        portsc |= XHCI_PS_PED;
        mmio_write32_at(reg, portsc);
    }

    /* Write-1-to-clear the change bits so the next poll sees a fresh state
     * rather than the stale "something changed" flags from this reset. */
    mmio_write32_at(reg, XHCI_PS_CSC | XHCI_PS_PRC | XHCI_PS_CEC | XHCI_PS_PLC);
    return 0;
}

int xhci_roothub_poll(void)
{
    uint8_t found = 0;

    if (!g_xhci_info.present) {
        return 0;
    }
    if (!g_xhci_info.hc_running) {
        return 0;
    }

    kernel_log_hex_u32("xhci: roothub max_ports ", g_xhci_info.max_ports);

    for (uint8_t port = 1u; port <= g_xhci_info.max_ports; port++) {
        uint32_t portsc = mmio_read32_at(portsc_reg(port));
        uint32_t speed;

        if ((portsc & XHCI_PS_CCS) == 0u) {
            continue;   /* no device connected */
        }
        found++;
        if (g_port_enumerated[port] != 0u) {
            continue;   /* already enumerated on a previous poll */
        }
        /* Enumerate even if the firmware already enabled the port (PED set).
         * SeaBIOS/OVMF routinely reset and enable attached USB ports before
         * hand-off, but they leave the device at an address the firmware owns
         * and which this driver cannot use. Treating PED as "already done" made
         * the whole root hub look enumerated while no slot existed, so every
         * device was silently dropped. The Speed field in PORTSC is only
         * meaningful after a reset, so it is re-read here rather than reused
         * from the pre-reset read. */
        if (xhci_port_reset(port) == 0) {
            uint8_t slot = 0u;

            portsc = mmio_read32_at(portsc_reg(port));
            speed = (portsc & XHCI_PS_SPEED_MASK) >> XHCI_PS_SPEED_SHIFT;
            kernel_log_hex_u32("xhci: portsc after reset ", portsc);

            if (xhci_enable_slot(&slot) == 0 && slot != 0u) {
                int rc = xhci_address_device(slot, port, (uint8_t) speed);

                log_write_event("xhci: address device ",
                                rc == 0 ? "ok" : "failed");
                if (rc == 0) {
                    g_port_enumerated[port] = 1u;
                    for (uint32_t i = 0; i < XHCI_MAX_DEVICES; i++) {
                        if (!g_devices[i].present) {
                            g_devices[i].present = true;
                            g_devices[i].slot_id = slot;
                            g_devices[i].port = port;
                            g_devices[i].speed = (uint8_t) speed;
                            g_xhci_info.device_count++;
                            break;
                        }
                    }
                }
            } else {
                log_write("xhci: enable slot failed");
            }
        } else {
            log_write("xhci: port reset failed");
        }
    }
    return (int) found;
}

/* ── Transfer ring helpers ───────────────────────────────────────── */
static void xhci_queue_trb(xhci_trb_t *ring, uint32_t *ptr, uint32_t *cycle,
                           const xhci_trb_t *trb)
{
    xhci_trb_t *dst = &ring[*ptr];

    dst->param_lo = trb->param_lo;
    dst->param_hi = trb->param_hi;
    dst->status = trb->status;
    dst->control = trb->control | (*cycle & 1u);
    *ptr += 1u;
    if (*ptr >= XHCI_XFER_RING_SIZE - 1u) {
        /* Wrap before the trailing Link TRB. Its cycle bit has to be flipped so
         * the controller accepts it as the terminator of this pass. */
        ring[XHCI_XFER_RING_SIZE - 1u].control ^= 1u;
        *ptr = 0u;
        *cycle ^= 1u;
    }
}

/* Enqueue one TRB onto the transfer ring of `slot` / `dci`. Each endpoint has
 * its own ring, so the DCI must be carried all the way down from the caller. */
static void xhci_queue_trb_dci(uint8_t slot, uint32_t dci, const xhci_trb_t *trb)
{
    xhci_queue_trb(g_xfer_ring[slot - 1u][dci], &g_xfer_ptr[slot - 1u][dci],
                   &g_xfer_cycle[slot - 1u][dci], trb);
}

int32_t xhci_control_transfer(uint8_t slot, const xhci_setup_packet_t *setup,
                              void *data, uint32_t data_len)
{
    xhci_trb_t trb;
    uint32_t status = 0u;
    uint32_t dummy = 0u;
    uint32_t trt = TRB_SETUP_TRT_NONE;

    if (!g_xhci_info.hc_running || setup == NULL || slot == 0u) {
        return -1;
    }
    if (slot > XHCI_MAX_DEVICES) {
        return -2;
    }
    if (data != NULL && data_len > 0u) {
        trt = (setup->bmRequestType & 0x80u) ? TRB_SETUP_TRT_IN
                                             : TRB_SETUP_TRT_OUT;
    }

    /* Setup stage.
     *
     * With IDT (bit 6) set, DWORD 0 and DWORD 1 of this TRB hold the eight
     * bytes of the setup packet *inline* - not a pointer to them (xHCI 1.1
     * table 6-30: "Immediate Data"). The controller hands those bytes straight
     * to the device; there is no DMA read of a setup buffer at all. Writing a
     * pointer here makes the controller decode the address itself as
     * bmRequestType/bRequest/wValue/wIndex/wLength, which produces a nonsense
     * request that every device answers with STALL.
     *
     * IDT is bit 6 (Linux: TRB_IDT = BIT(6)) and TRT sits in bits 17:16:
     * 0 = no data stage, 2 = OUT data stage, 3 = IN data stage. TRT is not
     * recoverable if wrong - the controller simply never runs the data stage. */
    memset(&trb, 0, sizeof(trb));
    {
        const uint8_t *raw = (const uint8_t *) setup;

        trb.param_lo = (uint32_t) raw[0] | ((uint32_t) raw[1] << 8) |
                       ((uint32_t) raw[2] << 16) | ((uint32_t) raw[3] << 24);
        trb.param_hi = (uint32_t) raw[4] | ((uint32_t) raw[5] << 8) |
                       ((uint32_t) raw[6] << 16) | ((uint32_t) raw[7] << 24);
    }
    trb.status = 8u;
    trb.control = ((uint32_t) TRB_TYPE_SETUP << 10) | TRB_TR_IDT |
                  ((uint32_t) trt << TRB_SETUP_TRT_SHIFT);
    xhci_queue_trb_dci(slot, XHCI_DCI_EP0, &trb);

    if (data != NULL && data_len > 0u) {
        bool is_in = (setup->bmRequestType & 0x80u) != 0u;
        memset(&trb, 0, sizeof(trb));
        trb.param_lo = (uint32_t) ((uint64_t) (uintptr_t) data);
        trb.param_hi = (uint32_t) (((uint64_t) (uintptr_t) data) >> 32);
        trb.status = data_len;
        trb.control = ((uint32_t) TRB_TYPE_DATA << 10) |
                      (is_in ? TRB_TR_DIR : 0u);
        xhci_queue_trb_dci(slot, XHCI_DCI_EP0, &trb);
    }

    /* Status stage. DIR=1 means the status token is IN, DIR=0 means OUT. The
     * status stage always runs opposite to the data stage, so for a control-IN
     * transfer (device -> host, e.g. GET_DESCRIPTOR) it must be OUT; only
     * OUT-data / no-data transfers use an IN status stage.
     *
     * IOC (bit 5) is mandatory on this TRB: the xHC posts a Transfer Event only
     * for TRBs with IOC set, so leaving it clear means a transfer that actually
     * completed is reported as a timeout. The status stage is the last TRB of
     * the transfer, which is exactly where the single completion event belongs. */
    memset(&trb, 0, sizeof(trb));
    trb.control = ((uint32_t) TRB_TYPE_STATUS << 10) | TRB_TR_IOC |
                  ((setup->bmRequestType & 0x80u) ? 0u : TRB_TR_DIR);
    xhci_queue_trb_dci(slot, XHCI_DCI_EP0, &trb);

    /* Ring the Device Slot doorbell for EP0.
     *
     * A device slot doorbell carries the Device Context Index of the endpoint
     * to service in its DB Target field (bits 7:0) - EP0 is DCI 1. Writing 0
     * is "no endpoint", which controllers discard; the effect is that the
     * control transfer TRBs are never fetched, no Transfer Event is ever
     * posted, and every descriptor request times out even though the rings,
     * the EP0 context and the doorbell address are all correct. */
    db_write32(slot, XHCI_DCI_EP0);

    if (xhci_wait_event(TRB_TYPE_TRANSFER_EVENT, 2000u, &status, &dummy) != 0) {
        return -3;
    }
    /* A Transfer Event reports the Completion Code in status bits 31:24 just
     * like a Command Completion Event does. Only Success (and the Short Packet
     * / Stopped variants, which carry partial data) mean the TRBs went through;
     * e.g. a Stall on a GET_DESCRIPTOR means the endpoint rejected the request
     * and the buffer must not be trusted. */
    {
        uint32_t code = xhci_completion_code(status);

        if (code != XHCI_CC_SUCCESS && code != XHCI_CC_SHORT_PACKET) {
            xhci_report_completion("xhci: control transfer failed", status);
            return -4;
        }
    }
    /* status bits 0-23 = Transfer Length Remaining */
    return (int32_t) data_len;
}

/* Bulk / interrupt transfer on a non-zero endpoint.
 *
 * `endpoint` is accepted either as a bare endpoint number (0..15) or as a full
 * bEndpointAddress byte (0x00..0x8F): only bits 3:0 are used as the number, and
 * IN-ness is the OR of the caller's `is_in` flag and bit7 of `endpoint`. Both
 * conventions are in use in-tree (the Bluetooth stack passes numbers, the HID
 * stack passes address bytes) and silently reinterpreting one as the other used
 * to produce a garbage doorbell write and a hang, so this reconciles them
 * instead of picking a winner. */
int32_t xhci_bulk_transfer(uint8_t slot, uint8_t endpoint,
                           void *data, uint32_t len, bool is_in)
{
    xhci_trb_t trb;
    uint32_t status = 0u;
    uint32_t evslot = 0u;
    uint32_t dci;
    uint8_t ep_num;
    bool in;

    if (!g_xhci_info.hc_running || slot == 0u || data == NULL || len == 0u) {
        return -1;
    }
    if (slot > XHCI_MAX_DEVICES) {
        return -2;
    }

    ep_num = (uint8_t) (endpoint & 0x0Fu);
    in = is_in || ((endpoint & 0x80u) != 0u);
    if (ep_num == 0u) {
        /* Endpoint 0 is control-only and has its own entry point. */
        return -2;
    }

    /* Endpoint n OUT is DCI 2n, endpoint n IN is DCI 2n+1. The DCI selects both
     * the transfer ring and the doorbell target; the two must agree or the TRB
     * is written to one ring and rung on another, which hangs the slot. */
    dci = (uint32_t) ep_num * 2u + (in ? 1u : 0u);

    memset(&trb, 0, sizeof(trb));
    trb.param_lo = (uint32_t) ((uint64_t) (uintptr_t) data);
    trb.param_hi = (uint32_t) (((uint64_t) (uintptr_t) data) >> 32);
    trb.status = len;
    /* IOC must be bit 5. Bit 9 - which this used to set, with an "IOC" comment
     * next to it - is BEI (Block Event Interrupt), the opposite in effect: it
     * suppresses the completion notification. The xHC therefore never posted a
     * Transfer Event for a bulk or interrupt transfer, and every one of them
     * came back as a timeout even when the data had been moved correctly. */
    trb.control = ((uint32_t) TRB_TYPE_NORMAL << 10) |
                  (in ? TRB_TR_DIR : 0u) | TRB_TR_IOC;
    xhci_queue_trb_dci(slot, dci, &trb);

    db_write32(slot, dci);

    if (xhci_wait_event(TRB_TYPE_TRANSFER_EVENT, 5000u, &status, &evslot) != 0) {
        return -3;
    }
    {
        uint32_t code = xhci_completion_code(status);

        if (code != XHCI_CC_SUCCESS && code != XHCI_CC_SHORT_PACKET) {
            xhci_report_completion("xhci: bulk transfer failed", status);
            return -4;
        }
    }
    /* status bits 0-23 = Transfer Length Remaining */
    uint32_t remaining = status & 0xFFFFFFu;
    return (int32_t) (len - remaining);
}

int32_t xhci_interrupt_transfer(uint8_t slot, uint8_t endpoint,
                                void *data, uint32_t len)
{
    /* Interrupt transfers are polled: reuse the normal-TRB path. */
    return xhci_bulk_transfer(slot, endpoint, data, len, true /* is_in */);
}

int32_t xhci_read(uint8_t slot, uint8_t endpoint, void *data, uint32_t len)
{
    /* Force the IN bit so a bare endpoint number and an address byte behave
     * identically. */
    return xhci_bulk_transfer(slot, (uint8_t) (endpoint | 0x80u), data, len, true);
}

int32_t xhci_write(uint8_t slot, uint8_t endpoint, const void *data, uint32_t len)
{
    return xhci_bulk_transfer(slot, (uint8_t) (endpoint & 0x7Fu),
                              (void *) (uintptr_t) data, len, false);
}

/* ── Descriptor / device table access ────────────────────────────── */
int32_t xhci_get_descriptor(uint8_t slot, uint8_t type, uint8_t index,
                            void *buf, uint16_t len)
{
    xhci_setup_packet_t setup;

    if (buf == NULL || len == 0u) {
        return -1;
    }
    setup.bmRequestType = 0x80u;   /* device-to-host, standard, recipient device */
    setup.bRequest = 0x06u;        /* GET_DESCRIPTOR */
    setup.wValue = (uint16_t) (((uint16_t) type << 8) | (uint16_t) index);
    setup.wIndex = 0u;
    setup.wLength = len;

    return xhci_control_transfer(slot, &setup, buf, len);
}

uint32_t xhci_device_count(void)
{
    return g_xhci_info.device_count;
}

const xhci_device_t *xhci_get_device(uint32_t index)
{
    uint32_t seen = 0u;

    for (uint32_t i = 0u; i < XHCI_MAX_DEVICES; i++) {
        if (!g_devices[i].present) {
            continue;
        }
        if (seen == index) {
            return &g_devices[i];
        }
        seen++;
    }
    return NULL;
}

/* ── Controller bring-up ─────────────────────────────────────────── */
bool xhci_driver_init(void)
{
    pci_device_info_t info;
    uint16_t command;

    memset(&g_xhci_info, 0, sizeof(g_xhci_info));
    memset(&info, 0, sizeof(info));
    memset(g_devices, 0, sizeof(g_devices));
    strcpy(g_xhci_info.status, "xhci: not found");

    pci_enumerate(xhci_find_callback, &info);
    if (info.vendor_id == 0) {
        log_write(g_xhci_info.status);
        return false;
    }

    g_xhci_info.present = true;
    g_xhci_info.vendor_id = info.vendor_id;
    g_xhci_info.device_id = info.device_id;
    g_xhci_info.bus = info.bus;
    g_xhci_info.slot = info.slot;
    g_xhci_info.func = info.func;
    g_xhci_info.irq = info.interrupt_line;
    g_xhci_info.mmio_base = xhci_bar_base(info.bar0, info.bar1);
    if (g_xhci_info.mmio_base == 0) {
        strcpy(g_xhci_info.status, "xhci: mmio bar missing");
        log_write(g_xhci_info.status);
        return true;
    }

    /* MMIO must be mapped uncacheable. With a plain mmu_map_identity() this
     * range is write-back cached, so writes to USBCMD/DCBAAP/CRCR can stop in
     * the cache and never reach the controller, and reads of USBSTS return
     * stale data. Every other working driver in the tree - AHCI, virtio, the
     * framebuffer, the LAPIC - already uses the device mapping for its BAR.
     *
     * The mapping can also fail outright: xHCI uses a 64-bit BAR and firmware
     * places it wherever it likes, including well above the range this kernel's
     * page tables reach. Refusing there is the only safe option - the previous
     * attempt to map such an address wrote past the end of the page-directory
     * table and corrupted memory. */
    if (!mmu_map_device_identity(g_xhci_info.mmio_base, 0x10000)) {
        strcpy(g_xhci_info.status, "xhci: mmio bar outside mapped window");
        log_write(g_xhci_info.status);
        kernel_log_hex_u32("xhci: bar lo ", info.bar0);
        kernel_log_hex_u32("xhci: bar hi ", info.bar1);
        return true;
    }
    command = pci_config_read16(info.bus, info.slot, info.func, PCI_COMMAND_OFFSET);
    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(info.bus, info.slot, info.func, PCI_COMMAND_OFFSET, command);

    g_xhci_info.cap_length = xhci_read8(XHCI_CAPLENGTH);
    g_xhci_info.hci_version = (uint16_t) (xhci_read32(0x00) >> 16);
    g_xhci_info.hcsparams1 = xhci_read32(XHCI_HCSPARAMS1);
    g_xhci_info.hcsparams2 = xhci_read32(XHCI_HCSPARAMS2);
    g_xhci_info.hcsparams3 = xhci_read32(XHCI_HCSPARAMS3);
    g_xhci_info.hccparams1 = xhci_read32(XHCI_HCCPARAMS1);
    g_xhci_info.dboff = xhci_read32(XHCI_DBOFF) & 0xFFFFFFFEu;
    g_xhci_info.rts_off = xhci_read32(XHCI_RTSOFF) & 0xFFFFFFFEu;
    g_xhci_info.max_slots = (uint8_t) (g_xhci_info.hcsparams1 & 0xFFu);
    g_xhci_info.max_ports = (uint8_t) ((g_xhci_info.hcsparams1 >> 24) & 0xFFu);
    g_xhci_info.mmio_ready = true;

    g_op_base = g_xhci_info.mmio_base + g_xhci_info.cap_length;
    g_rts_base = g_xhci_info.mmio_base + g_xhci_info.rts_off;
    g_db_base = g_xhci_info.mmio_base + g_xhci_info.dboff;

    /* A port count of zero means the capability registers did not read back
     * sensibly: the BAR is wrong, or the MMIO window is not decodable. Fail
     * loudly here rather than walking an empty port list later. */
    if (g_xhci_info.max_ports == 0u) {
        strcpy(g_xhci_info.status, "xhci: no ports reported by controller");
        log_write(g_xhci_info.status);
        return true;
    }

    /* Reset and program the host controller */
    if (xhci_hc_reset()) {
        if (!xhci_setup_rings()) {
            strcpy(g_xhci_info.status, "xhci: DMA buffers misaligned (see above)");
            log_write(g_xhci_info.status);
            return true;
        }
        /* Set Run/Stop to leave the Halted state. Per spec the controller
         * clears USBSTS.HCH once it is actually running. */
        op_write32(XHCI_USBCMD, op_read32(XHCI_USBCMD) | XHCI_CMD_RS);
        {
            uint32_t timeout = 100000u;
            while ((op_read32(XHCI_USBSTS) & XHCI_STS_HCH) != 0u &&
                   timeout-- > 0u) {
                io_wait();
            }
        }

        /* Decide whether the controller is actually usable. USBSTS.HCH alone is
         * not enough: if the whole operational window reads back as zero then
         * HCH reads 0 too and the controller would look "running" while in fact
         * nothing we program reaches it - and every later command and transfer
         * would time out with no clue as to why. DCBAAP is written by
         * xhci_setup_rings() immediately above, so a zero read-back is a
         * reliable tell that the operational registers are not responding. */
        if (op_read64(XHCI_DCBAAP) == 0u) {
            strcpy(g_xhci_info.status,
                   "xhci: operational registers not responding (reads zero)");
            log_write(g_xhci_info.status);
            return true;
        }

        if ((op_read32(XHCI_USBSTS) & XHCI_STS_HCH) != 0u) {
            strcpy(g_xhci_info.status, "xhci: HC did not leave Halted state");
            log_write(g_xhci_info.status);
            return true;
        }
        g_xhci_info.hc_running = true;
        strcpy(g_xhci_info.status, "xhci: controller running");
    } else {
        strcpy(g_xhci_info.status, "xhci: controller reset failed");
    }
    log_write(g_xhci_info.status);
    return true;
}

bool xhci_probe(void)
{
    return xhci_driver_init();
}

void xhci_init(void)
{
    (void) xhci_driver_init();
}

void xhci_info_dump(void)
{
    char line[96];
    if (!g_xhci_info.present) {
        log_write("xhci: not found");
        return;
    }
    strcpy(line, "xhci: slots=");
    line[12] = '0' + (char) (g_xhci_info.max_slots / 10);
    line[13] = '0' + (char) (g_xhci_info.max_slots % 10);
    strcpy(line + 14, " ports=");
    line[21] = '0' + (char) (g_xhci_info.max_ports / 10);
    line[22] = '0' + (char) (g_xhci_info.max_ports % 10);
    log_write(line);
}

void xhci_shutdown(void)
{
    if (g_xhci_info.present) {
        if (g_xhci_info.hc_running) {
            op_write32(XHCI_USBCMD, 0u);
            g_xhci_info.hc_running = false;
        }
        strcpy(g_xhci_info.status, "xhci: shutdown");
        log_write(g_xhci_info.status);
    }
}

const xhci_info_t *xhci_info(void)
{
    return &g_xhci_info;
}

const char *xhci_status(void)
{
    return g_xhci_info.status;
}
