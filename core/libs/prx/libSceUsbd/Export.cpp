#include "Usbd.hpp"
#include "prx/libc/include/ApplicationHeap.hpp"

#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace {

std::mutex contextMutex;
std::shared_ptr<libusb_context> context;
std::uint64_t initializationCount = 0;

std::int32_t translateError_nid_no_patch(std::int64_t result) {
    if (result == LIBUSB_ERROR_OTHER) return static_cast<std::int32_t>(0x802400ff);
    if (result < 0) return static_cast<std::int32_t>(0x80240000u - result);
    return static_cast<std::int32_t>(result);
}

std::shared_ptr<libusb_context> getContext_nid_no_patch() {
    std::lock_guard lock(contextMutex);
    if (!context) throw std::runtime_error("libSceUsbd: library is not initialized");
    return context;
}

struct TransferState {
    UsbdTransfer* guest = nullptr;
    libusb_transfer* host = nullptr;
    int capacity = 0;
    bool active = false;
    std::shared_ptr<libusb_context> context;

    ~TransferState() {
        if (host) libusb_free_transfer(host);
        if (guest) {
            if ((guest->flags & LIBUSB_TRANSFER_FREE_BUFFER) != 0) ApplicationHeapFree_nid_no_patch(guest->buffer);
            std::free(guest);
        }
    }
};

std::mutex transferMutex;
std::unordered_map<UsbdTransfer*, std::shared_ptr<TransferState>> transfers;

void LIBUSB_CALL transferCompleted_nid_no_patch(libusb_transfer* host) {
    std::shared_ptr<TransferState> state;
    UsbdTransferCallback callback;
    bool freeTransfer;
    {
        std::lock_guard lock(transferMutex);
        const auto* rawState = static_cast<TransferState*>(host->user_data);
        const auto found = transfers.find(rawState->guest);
        if (found == transfers.end()) throw std::runtime_error("libSceUsbd: completion for an unknown transfer");
        state = found->second;
        state->active = false;
        auto* guest = state->guest;
        guest->status = host->status;
        guest->actual_length = host->actual_length;
        for (int i = 0; i < host->num_iso_packets; ++i) guest->iso_packet_desc[i] = host->iso_packet_desc[i];
        callback = guest->callback;
        freeTransfer = (guest->flags & LIBUSB_TRANSFER_FREE_TRANSFER) != 0;
    }
    if (callback) callback(state->guest);
    if (freeTransfer) {
        std::lock_guard lock(transferMutex);
        transfers.erase(state->guest);
    }
}

}

extern "C" {

std::int32_t APS5_VABI sceUsbdInit() {
    std::lock_guard lock(contextMutex);
    if (context) {
        ++initializationCount;
        return 0;
    }
    libusb_context* native = nullptr;
    const auto result = libusb_init(&native);
    if (result < 0) return translateError_nid_no_patch(result);
    try {
        context = std::shared_ptr<libusb_context>(native, libusb_exit);
    } catch (const std::bad_alloc&) {
        return translateError_nid_no_patch(LIBUSB_ERROR_NO_MEM);
    }
    initializationCount = 1;
    return 0;
}

void APS5_VABI sceUsbdExit() {
    std::shared_ptr<libusb_context> released;
    {
        std::lock_guard lock(contextMutex);
        if (!context) return;
        if (--initializationCount == 0) released = std::move(context);
    }
}

std::int64_t APS5_VABI sceUsbdGetDeviceList(UsbdDevice*** list) {
    if (!list) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    *list = nullptr;
    const auto currentContext = getContext_nid_no_patch();
    const auto result = libusb_get_device_list(currentContext.get(), list);
    return result < 0 ? translateError_nid_no_patch(result) : result;
}

void APS5_VABI sceUsbdFreeDeviceList(UsbdDevice** list, std::int32_t unrefDevices) {
    libusb_free_device_list(list, unrefDevices);
}

std::int32_t APS5_VABI sceUsbdHandleEventsTimeout(const UsbdTimeval* timeout) {
    if (!timeout || timeout->seconds < 0 || timeout->microseconds < 0 || timeout->microseconds >= 1000000 ||
        timeout->seconds > std::numeric_limits<decltype(timeval::tv_sec)>::max()) {
        return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    }
    timeval hostTimeout{};
    hostTimeout.tv_sec = static_cast<decltype(hostTimeout.tv_sec)>(timeout->seconds);
    hostTimeout.tv_usec = static_cast<decltype(hostTimeout.tv_usec)>(timeout->microseconds);
    const auto currentContext = getContext_nid_no_patch();
    return translateError_nid_no_patch(libusb_handle_events_timeout(currentContext.get(), &hostTimeout));
}

UsbdTransfer* APS5_VABI sceUsbdAllocTransfer(std::int32_t isoPackets) {
    if (isoPackets < 0) return nullptr;
    const auto packetCount = static_cast<std::size_t>(isoPackets);
    if (packetCount > (std::numeric_limits<std::size_t>::max() - sizeof(UsbdTransfer)) / sizeof(libusb_iso_packet_descriptor)) return nullptr;
    try {
        auto state = std::make_shared<TransferState>();
        state->guest = static_cast<UsbdTransfer*>(std::calloc(1, sizeof(UsbdTransfer) + packetCount * sizeof(libusb_iso_packet_descriptor)));
        if (!state->guest) return nullptr;
        state->host = libusb_alloc_transfer(isoPackets);
        if (!state->host) return nullptr;
        state->capacity = isoPackets;
        std::lock_guard lock(transferMutex);
        auto* guest = state->guest;
        transfers.emplace(guest, std::move(state));
        return guest;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

std::int32_t APS5_VABI sceUsbdAttachKernelDriver(UsbdDeviceHandle* device, std::int32_t interfaceNumber) {
    if (!device) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    return translateError_nid_no_patch(libusb_attach_kernel_driver(device, interfaceNumber));
}

std::int32_t APS5_VABI sceUsbdCancelTransfer(UsbdTransfer* transfer) {
    std::lock_guard lock(transferMutex);
    const auto found = transfers.find(transfer);
    if (found == transfers.end()) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    return translateError_nid_no_patch(libusb_cancel_transfer(found->second->host));
}

std::int32_t APS5_VABI sceUsbdCheckConnected(UsbdDeviceHandle* device) {
    if (!device) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    const auto* expected = libusb_get_device(device);
    if (!expected) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    const auto currentContext = getContext_nid_no_patch();
    UsbdDevice** list = nullptr;
    const auto count = libusb_get_device_list(currentContext.get(), &list);
    if (count < 0) return translateError_nid_no_patch(count);
    int result = LIBUSB_ERROR_NO_DEVICE;
    for (std::int64_t i = 0; i < count; ++i) {
        if (list[i] == expected) {
            result = LIBUSB_SUCCESS;
            break;
        }
    }
    libusb_free_device_list(list, 1);
    return translateError_nid_no_patch(result);
}

std::int32_t APS5_VABI sceUsbdClaimInterface(UsbdDeviceHandle* device, std::int32_t interfaceNumber) {
    if (!device) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    return translateError_nid_no_patch(libusb_claim_interface(device, interfaceNumber));
}

void APS5_VABI sceUsbdClose(UsbdDeviceHandle* device) {
    libusb_close(device);
}

std::int32_t APS5_VABI sceUsbdControlTransfer(UsbdDeviceHandle* device, std::uint8_t requestType, std::uint8_t request, std::uint16_t value, std::uint16_t index, unsigned char* data, std::uint16_t length, std::uint32_t timeout) {
    if (!device || (length != 0 && !data)) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    return translateError_nid_no_patch(libusb_control_transfer(device, requestType, request, value, index, data, length, timeout));
}

std::int32_t APS5_VABI sceUsbdEventHandlingOk() {
    const auto currentContext = getContext_nid_no_patch();
    return libusb_event_handling_ok(currentContext.get());
}

void APS5_VABI sceUsbdFillInterruptTransfer(UsbdTransfer* transfer, UsbdDeviceHandle* device, std::uint8_t endpoint, unsigned char* buffer, std::int32_t length, UsbdTransferCallback callback, void* userData, std::uint32_t timeout) {
    if (!transfer) throw std::runtime_error("sceUsbdFillInterruptTransfer: null transfer");
    transfer->dev_handle = device;
    transfer->endpoint = endpoint;
    transfer->type = LIBUSB_TRANSFER_TYPE_INTERRUPT;
    transfer->timeout = timeout;
    transfer->buffer = buffer;
    transfer->length = length;
    transfer->user_data = userData;
    transfer->callback = callback;
}

void APS5_VABI sceUsbdFreeConfigDescriptor(UsbdConfigDescriptor* config) {
    libusb_free_config_descriptor(config);
}

void APS5_VABI sceUsbdFreeTransfer(UsbdTransfer* transfer) {
    if (!transfer) return;
    std::shared_ptr<TransferState> released;
    {
        std::lock_guard lock(transferMutex);
        const auto found = transfers.find(transfer);
        if (found == transfers.end()) throw std::runtime_error("sceUsbdFreeTransfer: unknown transfer");
        if (found->second->active) throw std::runtime_error("sceUsbdFreeTransfer: transfer is still pending");
        released = std::move(found->second);
        transfers.erase(found);
    }
}

std::int32_t APS5_VABI sceUsbdGetActiveConfigDescriptor(UsbdDevice* device, UsbdConfigDescriptor** config) {
    if (!device || !config) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    return translateError_nid_no_patch(libusb_get_active_config_descriptor(device, config));
}

std::uint8_t APS5_VABI sceUsbdGetBusNumber(UsbdDevice* device) {
    return libusb_get_bus_number(device);
}

std::int32_t APS5_VABI sceUsbdGetConfigDescriptor(UsbdDevice* device, std::uint8_t index, UsbdConfigDescriptor** config) {
    if (!device || !config) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    return translateError_nid_no_patch(libusb_get_config_descriptor(device, index, config));
}

std::uint8_t APS5_VABI sceUsbdGetDeviceAddress(UsbdDevice* device) {
    return libusb_get_device_address(device);
}

std::int32_t APS5_VABI sceUsbdGetDeviceDescriptor(UsbdDevice* device, UsbdDeviceDescriptor* descriptor) {
    if (!device || !descriptor) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    return translateError_nid_no_patch(libusb_get_device_descriptor(device, descriptor));
}

std::int32_t APS5_VABI sceUsbdGetStringDescriptor(UsbdDeviceHandle* device, std::uint8_t index, std::uint16_t language, unsigned char* data, std::int32_t length) {
    if (!device || !data || length < 0 || length > std::numeric_limits<std::uint16_t>::max()) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    return translateError_nid_no_patch(libusb_get_string_descriptor(device, index, language, data, length));
}

std::int32_t APS5_VABI sceUsbdKernelDriverActive(UsbdDeviceHandle* device, std::int32_t interfaceNumber) {
    if (!device) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    return translateError_nid_no_patch(libusb_kernel_driver_active(device, interfaceNumber));
}

std::int32_t APS5_VABI sceUsbdOpen(UsbdDevice* device, UsbdDeviceHandle** handle) {
    if (!device || !handle) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    return translateError_nid_no_patch(libusb_open(device, handle));
}

UsbdDevice* APS5_VABI sceUsbdRefDevice(UsbdDevice* device) {
    return libusb_ref_device(device);
}

std::int32_t APS5_VABI sceUsbdReleaseInterface(UsbdDeviceHandle* device, std::int32_t interfaceNumber) {
    if (!device) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    return translateError_nid_no_patch(libusb_release_interface(device, interfaceNumber));
}

std::int32_t APS5_VABI sceUsbdResetDevice(UsbdDeviceHandle* device) {
    if (!device) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    return translateError_nid_no_patch(libusb_reset_device(device));
}

std::int32_t APS5_VABI sceUsbdSetConfiguration(UsbdDeviceHandle* device, std::int32_t configuration) {
    if (!device) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    return translateError_nid_no_patch(libusb_set_configuration(device, configuration));
}

std::int32_t APS5_VABI sceUsbdSubmitTransfer(UsbdTransfer* transfer) {
    const auto currentContext = getContext_nid_no_patch();
    std::lock_guard lock(transferMutex);
    const auto found = transfers.find(transfer);
    if (found == transfers.end()) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    auto& state = *found->second;
    if (state.active) return translateError_nid_no_patch(LIBUSB_ERROR_BUSY);
    if (!transfer->dev_handle || transfer->length < 0 || (!transfer->buffer && transfer->length != 0) || transfer->num_iso_packets < 0 || transfer->num_iso_packets > state.capacity) return translateError_nid_no_patch(LIBUSB_ERROR_INVALID_PARAM);
    auto* host = state.host;
    host->dev_handle = transfer->dev_handle;
    host->flags = transfer->flags & ~(LIBUSB_TRANSFER_FREE_BUFFER | LIBUSB_TRANSFER_FREE_TRANSFER);
    host->endpoint = transfer->endpoint;
    host->type = transfer->type;
    host->timeout = transfer->timeout;
    host->length = transfer->length;
    host->callback = transferCompleted_nid_no_patch;
    host->user_data = &state;
    host->buffer = transfer->buffer;
    host->num_iso_packets = transfer->num_iso_packets;
    for (int i = 0; i < transfer->num_iso_packets; ++i) host->iso_packet_desc[i] = transfer->iso_packet_desc[i];
    state.context = currentContext;
    state.active = true;
    const auto result = libusb_submit_transfer(host);
    if (result < 0) state.active = false;
    return translateError_nid_no_patch(result);
}

void APS5_VABI sceUsbdUnrefDevice(UsbdDevice* device) {
    libusb_unref_device(device);
}

}
