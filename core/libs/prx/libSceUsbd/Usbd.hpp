#ifndef CORE_LIBS_PRX_LIBSCEUSBD_USBD_HPP
#define CORE_LIBS_PRX_LIBSCEUSBD_USBD_HPP

#include <cstddef>
#include <cstdint>
#include <libusb.h>
#include "prx/libc/include/general/VabiMacros.hpp"

using UsbdDevice = libusb_device;
using UsbdDeviceHandle = libusb_device_handle;
using UsbdDeviceDescriptor = libusb_device_descriptor;
using UsbdConfigDescriptor = libusb_config_descriptor;

struct UsbdTimeval {
    std::int64_t seconds;
    std::int64_t microseconds;
};

struct UsbdTransfer;
using UsbdTransferCallback = void (APS5_VABI *)(UsbdTransfer*);

struct UsbdTransfer {
    UsbdDeviceHandle* dev_handle;
    std::uint8_t flags;
    std::uint8_t endpoint;
    std::uint8_t type;
    std::uint32_t timeout;
    libusb_transfer_status status;
    std::int32_t length;
    std::int32_t actual_length;
    UsbdTransferCallback callback;
    void* user_data;
    unsigned char* buffer;
    std::int32_t num_iso_packets;
    libusb_iso_packet_descriptor iso_packet_desc[LIBUSB_FLEXIBLE_ARRAY];
};

static_assert(sizeof(UsbdTimeval) == 16);
static_assert(sizeof(UsbdDeviceDescriptor) == 18);
static_assert(sizeof(libusb_endpoint_descriptor) == 32);
static_assert(offsetof(libusb_endpoint_descriptor, extra) == 16);
static_assert(sizeof(libusb_interface_descriptor) == 40);
static_assert(offsetof(libusb_interface_descriptor, endpoint) == 16);
static_assert(sizeof(libusb_interface) == 16);
static_assert(sizeof(UsbdConfigDescriptor) == 40);
static_assert(offsetof(UsbdConfigDescriptor, interface) == 16);
static_assert(offsetof(UsbdConfigDescriptor, extra) == 24);
static_assert(sizeof(UsbdTransfer) == 64);
static_assert(offsetof(UsbdTransfer, dev_handle) == 0);
static_assert(offsetof(UsbdTransfer, flags) == 8);
static_assert(offsetof(UsbdTransfer, endpoint) == 9);
static_assert(offsetof(UsbdTransfer, type) == 10);
static_assert(offsetof(UsbdTransfer, timeout) == 12);
static_assert(offsetof(UsbdTransfer, status) == 16);
static_assert(offsetof(UsbdTransfer, length) == 20);
static_assert(offsetof(UsbdTransfer, actual_length) == 24);
static_assert(offsetof(UsbdTransfer, callback) == 32);
static_assert(offsetof(UsbdTransfer, user_data) == 40);
static_assert(offsetof(UsbdTransfer, buffer) == 48);
static_assert(offsetof(UsbdTransfer, num_iso_packets) == 56);
static_assert(offsetof(UsbdTransfer, iso_packet_desc) == 60);
static_assert(sizeof(libusb_iso_packet_descriptor) == 12);

extern "C" {

std::int32_t APS5_VABI sceUsbdInit();
void APS5_VABI sceUsbdExit();
std::int64_t APS5_VABI sceUsbdGetDeviceList(UsbdDevice*** list);
void APS5_VABI sceUsbdFreeDeviceList(UsbdDevice** list, std::int32_t unrefDevices);
std::int32_t APS5_VABI sceUsbdHandleEventsTimeout(const UsbdTimeval* timeout);
UsbdTransfer* APS5_VABI sceUsbdAllocTransfer(std::int32_t isoPackets);
std::int32_t APS5_VABI sceUsbdAttachKernelDriver(UsbdDeviceHandle* device, std::int32_t interfaceNumber);
std::int32_t APS5_VABI sceUsbdCancelTransfer(UsbdTransfer* transfer);
std::int32_t APS5_VABI sceUsbdCheckConnected(UsbdDeviceHandle* device);
std::int32_t APS5_VABI sceUsbdClaimInterface(UsbdDeviceHandle* device, std::int32_t interfaceNumber);
void APS5_VABI sceUsbdClose(UsbdDeviceHandle* device);
std::int32_t APS5_VABI sceUsbdControlTransfer(UsbdDeviceHandle* device, std::uint8_t requestType, std::uint8_t request, std::uint16_t value, std::uint16_t index, unsigned char* data, std::uint16_t length, std::uint32_t timeout);
std::int32_t APS5_VABI sceUsbdEventHandlingOk();
void APS5_VABI sceUsbdFillInterruptTransfer(UsbdTransfer* transfer, UsbdDeviceHandle* device, std::uint8_t endpoint, unsigned char* buffer, std::int32_t length, UsbdTransferCallback callback, void* userData, std::uint32_t timeout);
void APS5_VABI sceUsbdFreeConfigDescriptor(UsbdConfigDescriptor* config);
void APS5_VABI sceUsbdFreeTransfer(UsbdTransfer* transfer);
std::int32_t APS5_VABI sceUsbdGetActiveConfigDescriptor(UsbdDevice* device, UsbdConfigDescriptor** config);
std::uint8_t APS5_VABI sceUsbdGetBusNumber(UsbdDevice* device);
std::int32_t APS5_VABI sceUsbdGetConfigDescriptor(UsbdDevice* device, std::uint8_t index, UsbdConfigDescriptor** config);
std::uint8_t APS5_VABI sceUsbdGetDeviceAddress(UsbdDevice* device);
std::int32_t APS5_VABI sceUsbdGetDeviceDescriptor(UsbdDevice* device, UsbdDeviceDescriptor* descriptor);
std::int32_t APS5_VABI sceUsbdGetStringDescriptor(UsbdDeviceHandle* device, std::uint8_t index, std::uint16_t language, unsigned char* data, std::int32_t length);
std::int32_t APS5_VABI sceUsbdKernelDriverActive(UsbdDeviceHandle* device, std::int32_t interfaceNumber);
std::int32_t APS5_VABI sceUsbdOpen(UsbdDevice* device, UsbdDeviceHandle** handle);
UsbdDevice* APS5_VABI sceUsbdRefDevice(UsbdDevice* device);
std::int32_t APS5_VABI sceUsbdReleaseInterface(UsbdDeviceHandle* device, std::int32_t interfaceNumber);
std::int32_t APS5_VABI sceUsbdResetDevice(UsbdDeviceHandle* device);
std::int32_t APS5_VABI sceUsbdSetConfiguration(UsbdDeviceHandle* device, std::int32_t configuration);
std::int32_t APS5_VABI sceUsbdSubmitTransfer(UsbdTransfer* transfer);
void APS5_VABI sceUsbdUnrefDevice(UsbdDevice* device);

}

#endif
