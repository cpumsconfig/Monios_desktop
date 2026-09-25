#ifndef _RTL8139_H_
#define _RTL8139_H_

#include "net.h"
#include "pci.h"

bool rtl8139_supported(const pci_device_info_t *info);
bool rtl8139_detect(const pci_device_info_t *info, net_info_t *net, uint8_t mac[6]);

#endif
