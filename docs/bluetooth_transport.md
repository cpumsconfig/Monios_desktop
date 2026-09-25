# 蓝牙 USB HCI 传输层说明

## 已经完成的部分

`drivers/bluetooth/bluetooth.c` 里的传输层现在是完整实现，不再是空壳：

1. **控制器发现**：遍历 xHCI 已枚举设备表（`xhci_device_count()` /
   `xhci_get_device()`），逐个读 18 字节设备描述符，匹配
   `bDeviceClass=0xE0` / `bDeviceSubClass=0x01` / `bDeviceProtocol=0x01`
   （Wireless / RF controller / Bluetooth）。
2. **端点解析**：读完整配置描述符链，定位蓝牙接口
   （`bInterfaceClass=0xE0`），记录它的 bulk-in / bulk-out / interrupt-in
   端点地址。拿不到 bulk-in 与 bulk-out 就认为传输层不可用。
3. **发送**：HCI 命令与 ACL 数据统一走 bulk-out。ACL 帧在
   `bt_acl_send()` 里拼成连续缓冲区
   （`handle|PB|BC` + 总长 + L2CAP 长 + CID + 载荷）后发出。
4. **接收**：`bt_hci_poll()` 事件走中断端点、ACL 走批量端点，
   分别交给 `bt_hci_process_event()` 与 `bt_l2cap_process_acl()`。
5. **协议收敛**：
   - CONN_COMPLETE 之后才发缓存的 L2CAP 信令包（`bt_l2cap_flush_pending`）
   - CONN_RSP 协商出远端 DCID，出向帧按 DCID 寻址（`remote_cid`）
   - RFCOMM SABME 与 UIH 帧经 `bt_l2cap_send()` 发出，带正确 FCS
   - 入向 RFCOMM 帧校验 FCS 后按 DLCI 入队

## xHCI 核心侧的阻塞项（已补齐）

原先这三项不补齐，批量端点在真实控制器上不会工作。现已全部按规范实现，
细节见 `docs/known_limitations.md` 的「USB / xHCI」小节：

### 1. `xhci_address_device()` 构建独立的 Input Context（已完成）

`g_input_context[设备][264]`＝Input Control Context + Slot Context +
31 个 EP Context。Add Flags 置 `SLOT｜EP0`；Slot Context 填 Speed /
Root Hub Port Number / Context Entries / Device Address=0；EP0 Context 填
EP State=Running、CErr=3、EP Type=Control Bidirectional、Max Packet Size
（SuperSpeed 512、High 64、Full/Low 8）、TR Dequeue Pointer 与 DCS。
TRB 的 `param_lo/hi` 指向它。

### 2. 非 0 号端点经 Configure Endpoint 使能（已完成）

`g_xfer_ring[]` 改为按 **DCI** 索引（`g_xfer_ring[设备][DCI][槽]`），
每个端点一条独立环、独立 enqueue 指针与 cycle 位。新增
`xhci_configure_endpoint()` 填写 EP Context（EP Type、Max Packet Size、
Max Burst Size、CErr、Interval、Average TRB Length、DCS），
并把 Slot Context 的 Context Entries 更新为最高 DCI。

### 3. 枚举入口（已完成）

`usb_probe()` 此前从未被调用，整条 USB 设备栈是死代码；现由
`usb_ext_init()` 调用。它读配置描述符、发 SET_CONFIGURATION、再调用
`xhci_configure_endpoint()`，并把真实的 `bEndpointAddress` 写回设备表。

### 顺带修掉的其它真实缺陷（已完成）

- `xhci_control_transfer()` 的 Status 阶段方向位写死为 IN，导致
  control-IN（例如 GET_DESCRIPTOR）状态阶段方向错误。现已按数据阶段
  反方向设置。
- `xhci_bulk_transfer()` 的门铃号写死为 `endpoint*2`，IN 传输应使用
  `endpoint*2+1`。已修；端点参数现在同时接受"端点号"与"地址字节"两种写法。
- 每 slot 的传输环 enqueue 指针与 cycle 位此前直接复用命令环的
  `g_cmd_ptr` / `g_cmd_cycle`，多设备会互相踩踏；已改为按端点独立，
  并补上传输环必需的 Link TRB。
- PORTSC 基址 `0x40` → `0x400`；中断器 0 的 `ERSTSZ`/`ERSTBA` 偏移错位
  （ERSTBA 此前根本没被编程）。

## 尚未取得的证据

软件侧已完整，但在本项目的 QEMU 环境里 xHCI 的运算寄存器读回全 0，
事件环无法工作，因此本链路**尚未取得实地通过的证据**。
`bt_hci_transport_ready()` 只有在真正拿到端点后才会为真。详见
`docs/known_limitations.md` 的 P0 第 1 条。

## 验证方式

补齐上面 1、2 之后，插上 USB 蓝牙适配器：

1. 启动日志应出现 `bluetooth: usb controller ready`
   （在此之前是 `bluetooth: not found` 或
   `bluetooth: controller found, no HCI endpoints`）。
2. 调用 `bt_hci_poll()` 后 `bluetooth_info()->local_addr` 应被
   HCI_OP_READ_BD_ADDR 的 Command Complete 填上。
3. `bt_scan_start()` 之后持续 `bt_hci_poll()`，`bt_device_count()` 应增长。
