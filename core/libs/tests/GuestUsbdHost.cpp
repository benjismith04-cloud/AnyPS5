#include "prx/libSceUsbd/Usbd.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "USBD host: %s\n", message);
        std::abort();
    }
}

}

int main() {
    Require(sceUsbdInit() == 0, "initialization failed");
    UsbdDevice** devices = nullptr;
    const auto count = sceUsbdGetDeviceList(&devices);
    Require(count >= 0 && devices != nullptr, "enumeration failed");
    Require(devices[count] == nullptr, "device list is not terminated");
    for (std::int64_t index = 0; index < count; ++index) {
        UsbdDeviceDescriptor descriptor{};
        Require(sceUsbdGetDeviceDescriptor(devices[index], &descriptor) == 0, "cached descriptor lookup failed");
        Require(descriptor.bLength == 18 && descriptor.bDescriptorType == LIBUSB_DT_DEVICE,
            "device descriptor has an invalid format");
        auto* retained = sceUsbdRefDevice(devices[index]);
        Require(retained == devices[index], "reference changed device identity");
        sceUsbdUnrefDevice(retained);
    }
    sceUsbdFreeDeviceList(devices, 1);
    const UsbdTimeval immediate{0, 0};
    Require(sceUsbdHandleEventsTimeout(&immediate) == 0, "event handling failed");
    sceUsbdExit();
    Require(sceUsbdInit() == 0, "reinitialization failed");
    sceUsbdExit();
    std::puts("USBD host tests passed");
}
