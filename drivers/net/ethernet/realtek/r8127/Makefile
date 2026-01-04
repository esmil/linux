# SPDX-License-Identifier: GPL-2.0-only
#
# Kbuild file for Realtek r8127 (vendor driver)

# --- vendor feature toggles (keep same defaults as vendor src/Makefile) ---
CONFIG_SOC_LAN            := y
ENABLE_REALWOW_SUPPORT    := n
ENABLE_DASH_SUPPORT       := n
CONFIG_DOWN_SPEED_100     := n
CONFIG_ASPM               := y
ENABLE_S5WOL              := y
ENABLE_S5_KEEP_CURR_MAC   := n
ENABLE_EEE                := y
ENABLE_S0_MAGIC_PACKET    := n
ENABLE_TX_NO_CLOSE        := y
ENABLE_MULTIPLE_TX_QUEUE  := y
ENABLE_PTP_SUPPORT        := y
ENABLE_RSS_SUPPORT        := y
ENABLE_LIB_SUPPORT        := n
ENABLE_USE_FIRMWARE_FILE  := n
DISABLE_WOL_SUPPORT       := n
DISABLE_MULTI_MSIX_VECTOR := n
ENABLE_DOUBLE_VLAN        := n
ENABLE_PAGE_REUSE         := n
ENABLE_RX_PACKET_FRAGMENT := n
ENABLE_GIGA_LITE          := y

# --- module build ---
obj-$(CONFIG_R8127) += r8127.o

r8127-objs := r8127_n.o rtl_eeprom.o rtltool.o r8127_fiber.o

# --- flags/objects following vendor rules ---
ifeq ($(CONFIG_SOC_LAN),y)
  ccflags-y += -DCONFIG_SOC_LAN
endif

ifeq ($(ENABLE_REALWOW_SUPPORT),y)
  r8127-objs += r8127_realwow.o
  ccflags-y  += -DENABLE_REALWOW_SUPPORT
endif

ifeq ($(ENABLE_DASH_SUPPORT),y)
  r8127-objs += r8127_dash.o
  ccflags-y  += -DENABLE_DASH_SUPPORT
endif

ccflags-y += -DCONFIG_R8127_NAPI
ccflags-y += -DCONFIG_R8127_VLAN

ifeq ($(CONFIG_DOWN_SPEED_100),y)
  ccflags-y += -DCONFIG_DOWN_SPEED_100
endif
ifeq ($(CONFIG_ASPM),y)
  ccflags-y += -DCONFIG_ASPM
endif
ifeq ($(ENABLE_S5WOL),y)
  ccflags-y += -DENABLE_S5WOL
endif
ifeq ($(ENABLE_S5_KEEP_CURR_MAC),y)
  ccflags-y += -DENABLE_S5_KEEP_CURR_MAC
endif
ifeq ($(ENABLE_EEE),y)
  ccflags-y += -DENABLE_EEE
endif
ifeq ($(ENABLE_S0_MAGIC_PACKET),y)
  ccflags-y += -DENABLE_S0_MAGIC_PACKET
endif
ifeq ($(ENABLE_TX_NO_CLOSE),y)
  ccflags-y += -DENABLE_TX_NO_CLOSE
endif
ifeq ($(ENABLE_MULTIPLE_TX_QUEUE),y)
  ccflags-y += -DENABLE_MULTIPLE_TX_QUEUE
endif

ifeq ($(ENABLE_PTP_SUPPORT),y)
  r8127-objs += r8127_ptp.o
  ccflags-y  += -DENABLE_PTP_SUPPORT
endif
ifeq ($(ENABLE_RSS_SUPPORT),y)
  r8127-objs += r8127_rss.o
  ccflags-y  += -DENABLE_RSS_SUPPORT
endif
ifeq ($(ENABLE_LIB_SUPPORT),y)
  r8127-objs += r8127_lib.o
  ccflags-y  += -DENABLE_LIB_SUPPORT
endif
ifeq ($(ENABLE_USE_FIRMWARE_FILE),y)
  r8127-objs += r8127_firmware.o
  ccflags-y  += -DENABLE_USE_FIRMWARE_FILE
endif

ifeq ($(DISABLE_WOL_SUPPORT),y)
  ccflags-y += -DDISABLE_WOL_SUPPORT
endif
ifeq ($(DISABLE_MULTI_MSIX_VECTOR),y)
  ccflags-y += -DDISABLE_MULTI_MSIX_VECTOR
endif
ifeq ($(ENABLE_DOUBLE_VLAN),y)
  ccflags-y += -DENABLE_DOUBLE_VLAN
endif
ifeq ($(ENABLE_PAGE_REUSE),y)
  ccflags-y += -DENABLE_PAGE_REUSE
endif
ifeq ($(ENABLE_RX_PACKET_FRAGMENT),y)
  ccflags-y += -DENABLE_RX_PACKET_FRAGMENT
endif
ifeq ($(ENABLE_RX_PACKET_FRAGMENT),y)
  ccflags-y += -DENABLE_RX_PACKET_FRAGMENT
endif
ifeq ($(ENABLE_GIGA_LITE),y)
  ccflags-y += -DENABLE_GIGA_LITE
endif
