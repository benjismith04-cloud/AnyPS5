#include "prx/libSceUsbd/Usbd.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

struct libusb_context {};
struct libusb_device { int references = 1; };
struct libusb_device_handle { libusb_device* device; };

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "USBD: %s\n", message);
        std::abort();
    }
}

libusb_context context;
libusb_device device;
libusb_device otherDevice;
libusb_device_handle handle{&device};
int initResult = 0;
int operationResult = 0;
int initCalls = 0;
int exitCalls = 0;
int listFrees = 0;
int descriptorFrees = 0;
int closes = 0;
int nativeAllocations = 0;
int nativeFrees = 0;
int lastArgument = -1;
std::array<libusb_device*, 2> listedDevices{};
int listedCount = 0;
int lastUnrefDevices = -1;
void* expectedGuestBuffer = nullptr;
int guestBufferFrees = 0;
bool cancelled = false;
libusb_transfer* pending = nullptr;
libusb_transfer_status completionStatus = LIBUSB_TRANSFER_COMPLETED;
int completionLength = 0;
timeval lastTimeout{};
std::array<unsigned char, 8> controlData{};
std::uint8_t controlRequestType = 0;
std::uint8_t controlRequest = 0;
std::uint16_t controlValue = 0;
std::uint16_t controlIndex = 0;
std::uint16_t controlLength = 0;
unsigned int controlTimeout = 0;

constexpr std::int32_t invalidArgument = static_cast<std::int32_t>(0x80240002);
constexpr std::int32_t noDevice = static_cast<std::int32_t>(0x80240004);
constexpr std::int32_t busy = static_cast<std::int32_t>(0x80240006);
constexpr std::int32_t otherError = static_cast<std::int32_t>(0x802400ff);

int HandleOperation(libusb_device_handle* supplied, int argument) {
    Require(supplied == &handle, "backend received a different handle");
    lastArgument = argument;
    return operationResult;
}

}

extern "C" {

int LIBUSB_CALL libusb_init(libusb_context** output) {
    ++initCalls;
    *output = nullptr;
    if (initResult == 0) *output = &context;
    return initResult;
}

void LIBUSB_CALL libusb_exit(libusb_context* supplied) {
    Require(supplied == &context, "exit received a different context");
    ++exitCalls;
}

ssize_t LIBUSB_CALL libusb_get_device_list(libusb_context* supplied, libusb_device*** output) {
    Require(supplied == &context, "enumeration received a different context");
    if (operationResult < 0) return operationResult;
    *output = new libusb_device*[listedCount + 1]{};
    for (int index = 0; index < listedCount; ++index) {
        (*output)[index] = listedDevices[index];
        ++listedDevices[index]->references;
    }
    return listedCount;
}

void LIBUSB_CALL libusb_free_device_list(libusb_device** list, int unrefDevices) {
    if (list == nullptr) return;
    lastUnrefDevices = unrefDevices;
    if (unrefDevices != 0) {
        for (int index = 0; list[index] != nullptr; ++index) --list[index]->references;
    }
    ++listFrees;
    delete[] list;
}

libusb_device* LIBUSB_CALL libusb_ref_device(libusb_device* supplied) {
    Require(supplied == &device || supplied == &otherDevice, "reference received a different device");
    ++supplied->references;
    return supplied;
}

void LIBUSB_CALL libusb_unref_device(libusb_device* supplied) {
    Require(supplied == &device || supplied == &otherDevice, "unreference received a different device");
    --supplied->references;
}

int LIBUSB_CALL libusb_get_device_descriptor(libusb_device* supplied, libusb_device_descriptor* descriptor) {
    Require(supplied == &device, "descriptor received a different device");
    if (operationResult < 0) return operationResult;
    *descriptor = {};
    descriptor->bLength = 18;
    descriptor->bDescriptorType = LIBUSB_DT_DEVICE;
    descriptor->idVendor = 0x1234;
    descriptor->idProduct = 0xabcd;
    descriptor->bNumConfigurations = 1;
    return 0;
}

int LIBUSB_CALL libusb_get_config_descriptor(libusb_device* supplied, std::uint8_t index, libusb_config_descriptor** output) {
    Require(supplied == &device, "configuration received a different device");
    lastArgument = index;
    if (operationResult < 0) return operationResult;
    *output = new libusb_config_descriptor{};
    (*output)->bLength = 9;
    (*output)->bDescriptorType = LIBUSB_DT_CONFIG;
    (*output)->bConfigurationValue = 3;
    (*output)->MaxPower = 25;
    return 0;
}

int LIBUSB_CALL libusb_get_active_config_descriptor(libusb_device* supplied, libusb_config_descriptor** output) {
    return libusb_get_config_descriptor(supplied, 0, output);
}

void LIBUSB_CALL libusb_free_config_descriptor(libusb_config_descriptor* descriptor) {
    if (descriptor != nullptr) ++descriptorFrees;
    delete descriptor;
}

std::uint8_t LIBUSB_CALL libusb_get_bus_number(libusb_device* supplied) {
    Require(supplied == &device || supplied == &otherDevice, "bus lookup received a different device");
    return 17;
}

std::uint8_t LIBUSB_CALL libusb_get_device_address(libusb_device* supplied) {
    Require(supplied == &device || supplied == &otherDevice, "address lookup received a different device");
    return 23;
}

int LIBUSB_CALL libusb_open(libusb_device* supplied, libusb_device_handle** output) {
    Require(supplied == &device, "open received a different device");
    if (operationResult < 0) return operationResult;
    *output = &handle;
    return 0;
}

void LIBUSB_CALL libusb_close(libusb_device_handle* supplied) {
    Require(supplied == &handle, "close received a different handle");
    ++closes;
}

libusb_device* LIBUSB_CALL libusb_get_device(libusb_device_handle* supplied) {
    Require(supplied == &handle, "device lookup received a different handle");
    return supplied->device;
}

int LIBUSB_CALL libusb_get_configuration(libusb_device_handle* supplied, int* output) {
    Require(supplied == &handle, "configuration lookup received a different handle");
    if (operationResult < 0) return operationResult;
    *output = 3;
    return 0;
}

int LIBUSB_CALL libusb_set_configuration(libusb_device_handle* supplied, int value) { return HandleOperation(supplied, value); }
int LIBUSB_CALL libusb_claim_interface(libusb_device_handle* supplied, int value) { return HandleOperation(supplied, value); }
int LIBUSB_CALL libusb_release_interface(libusb_device_handle* supplied, int value) { return HandleOperation(supplied, value); }
int LIBUSB_CALL libusb_attach_kernel_driver(libusb_device_handle* supplied, int value) { return HandleOperation(supplied, value); }
int LIBUSB_CALL libusb_kernel_driver_active(libusb_device_handle* supplied, int value) { return HandleOperation(supplied, value); }
int LIBUSB_CALL libusb_reset_device(libusb_device_handle* supplied) { return HandleOperation(supplied, 0); }

int LIBUSB_CALL libusb_control_transfer(libusb_device_handle* supplied, std::uint8_t requestType,
    std::uint8_t request, std::uint16_t value, std::uint16_t index, unsigned char* data,
    std::uint16_t length, unsigned int timeout) {
    Require(supplied == &handle, "control transfer received a different handle");
    controlRequestType = requestType;
    controlRequest = request;
    controlValue = value;
    controlIndex = index;
    controlLength = length;
    controlTimeout = timeout;
    if (operationResult < 0) return operationResult;
    Require(length <= controlData.size(), "unexpected control transfer size");
    if ((requestType & LIBUSB_ENDPOINT_IN) != 0) std::memcpy(data, controlData.data(), length);
    else std::memcpy(controlData.data(), data, length);
    return length;
}

libusb_transfer* LIBUSB_CALL libusb_alloc_transfer(int isoPackets) {
    Require(isoPackets >= 0, "negative native isochronous allocation");
    ++nativeAllocations;
    return static_cast<libusb_transfer*>(std::calloc(1, sizeof(libusb_transfer) + isoPackets * sizeof(libusb_iso_packet_descriptor)));
}

void LIBUSB_CALL libusb_free_transfer(libusb_transfer* transfer) {
    if (transfer == nullptr) return;
    Require(transfer != pending, "native transfer freed while pending");
    if ((transfer->flags & LIBUSB_TRANSFER_FREE_BUFFER) != 0) std::free(transfer->buffer);
    ++nativeFrees;
    std::free(transfer);
}

int LIBUSB_CALL libusb_submit_transfer(libusb_transfer* transfer) {
    if (operationResult < 0) return operationResult;
    Require(pending == nullptr, "unexpected concurrent native transfer");
    Require(transfer->dev_handle == &handle, "submitted native transfer lost its handle");
    pending = transfer;
    cancelled = false;
    return 0;
}

int LIBUSB_CALL libusb_cancel_transfer(libusb_transfer* transfer) {
    if (operationResult < 0) return operationResult;
    if (pending != transfer) return LIBUSB_ERROR_NOT_FOUND;
    cancelled = true;
    return 0;
}

int LIBUSB_CALL libusb_event_handling_ok(libusb_context* supplied) {
    Require(supplied == &context, "event state received a different context");
    return operationResult;
}

int LIBUSB_CALL libusb_handle_events_timeout_completed(libusb_context* supplied, timeval* timeout, int*) {
    Require(supplied == &context, "event handling received a different context");
    lastTimeout = *timeout;
    if (operationResult < 0) return operationResult;
    if (pending != nullptr) {
        auto* transfer = pending;
        pending = nullptr;
        transfer->status = cancelled ? LIBUSB_TRANSFER_CANCELLED : completionStatus;
        transfer->actual_length = completionLength;
        if (completionLength > 0) {
            Require(completionLength <= transfer->length, "completion exceeds transfer buffer");
            std::fill_n(transfer->buffer, completionLength, 0xa5);
        }
        for (int index = 0; index < transfer->num_iso_packets; ++index) {
            transfer->iso_packet_desc[index].actual_length = transfer->iso_packet_desc[index].length / 2;
            transfer->iso_packet_desc[index].status = index == 0 ? LIBUSB_TRANSFER_COMPLETED : LIBUSB_TRANSFER_ERROR;
        }
        const bool autoFree = (transfer->flags & LIBUSB_TRANSFER_FREE_TRANSFER) != 0;
        transfer->callback(transfer);
        if (autoFree) libusb_free_transfer(transfer);
    }
    return 0;
}

int LIBUSB_CALL libusb_handle_events_timeout(libusb_context* supplied, timeval* timeout) {
    return libusb_handle_events_timeout_completed(supplied, timeout, nullptr);
}

void ApplicationHeapFree_nid_no_patch(void* buffer) {
    Require(buffer == expectedGuestBuffer, "free-buffer ownership used an unexpected guest pointer");
    ++guestBufferFrees;
    std::free(buffer);
    expectedGuestBuffer = nullptr;
}

}

namespace {

void TestDevices() {
    UsbdDevice** list = nullptr;
    Require(sceUsbdGetDeviceList(&list) == 0, "empty enumeration failed");
    Require(list != nullptr && list[0] == nullptr, "empty enumeration lacks a terminator");
    sceUsbdFreeDeviceList(list, 1);
    Require(listFrees == 1, "empty list was not freed");

    listedDevices = {&otherDevice, &device};
    listedCount = 2;
    Require(sceUsbdGetDeviceList(&list) == 2, "enumeration count changed");
    Require(list[0] == &otherDevice && list[1] == &device && list[2] == nullptr, "enumerated devices changed");
    Require(sceUsbdRefDevice(list[1]) == &device, "device reference changed identity");
    sceUsbdFreeDeviceList(list, 1);
    Require(device.references == 2 && otherDevice.references == 1, "list ownership was not released");
    sceUsbdUnrefDevice(&device);
    Require(device.references == 1, "device reference leaked");
    Require(sceUsbdGetDeviceList(&list) == 2, "second enumeration failed");
    sceUsbdFreeDeviceList(list, 0);
    Require(device.references == 2 && otherDevice.references == 2, "free-list ignored unref flag");
    sceUsbdUnrefDevice(&otherDevice);
    sceUsbdUnrefDevice(&device);

    UsbdDeviceDescriptor descriptor{};
    Require(sceUsbdGetDeviceDescriptor(&device, &descriptor) == 0, "device descriptor failed");
    Require(descriptor.bLength == 18 && descriptor.idVendor == 0x1234 && descriptor.idProduct == 0xabcd,
        "device descriptor contents changed");
    Require(sceUsbdGetBusNumber(&device) == 17 && sceUsbdGetDeviceAddress(&device) == 23,
        "device topology changed");
    Require(sceUsbdGetBusNumber(&otherDevice) == 17 && sceUsbdGetDeviceAddress(&otherDevice) == 23,
        "replacement-device fixture does not reuse the same address");
    UsbdConfigDescriptor* config = nullptr;
    Require(sceUsbdGetConfigDescriptor(&device, 7, &config) == 0 && lastArgument == 7,
        "configuration index changed");
    Require(config != nullptr && config->bConfigurationValue == 3 && config->MaxPower == 25,
        "configuration descriptor contents changed");
    sceUsbdFreeConfigDescriptor(config);
    Require(sceUsbdGetActiveConfigDescriptor(&device, &config) == 0 && lastArgument == 0,
        "active configuration failed");
    sceUsbdFreeConfigDescriptor(config);
    Require(descriptorFrees == 2, "configuration descriptor ownership leaked");

    UsbdDeviceHandle* opened = nullptr;
    operationResult = LIBUSB_ERROR_ACCESS;
    Require(sceUsbdOpen(&device, &opened) == static_cast<std::int32_t>(0x80240003), "open access error changed");
    Require(opened == nullptr, "failed open produced a handle");
    operationResult = 0;
    Require(sceUsbdOpen(&device, &opened) == 0 && opened == &handle, "device open failed");
    int frees = listFrees;
    Require(sceUsbdCheckConnected(opened) == 0, "connected second device was missed");
    Require(listFrees == frees + 1 && lastUnrefDevices == 1, "connection check leaked its device list");
    listedCount = 1;
    Require(sceUsbdCheckConnected(opened) == noDevice, "a different device was treated as the open device");
    listedCount = 0;
    Require(sceUsbdCheckConnected(opened) == noDevice, "empty enumeration reported connected");
    operationResult = LIBUSB_ERROR_OTHER;
    Require(sceUsbdCheckConnected(opened) == otherError, "connection enumeration failure changed");
    operationResult = 0;
    sceUsbdClose(opened);
    Require(closes == 1 && device.references == 1 && otherDevice.references == 1, "device cleanup leaked ownership");
}

void TestOperations() {
    Require(sceUsbdSetConfiguration(&handle, 3) == 0 && lastArgument == 3, "set-configuration argument changed");
    Require(sceUsbdClaimInterface(&handle, 4) == 0 && lastArgument == 4, "claim-interface argument changed");
    Require(sceUsbdReleaseInterface(&handle, 5) == 0 && lastArgument == 5, "release-interface argument changed");
    Require(sceUsbdAttachKernelDriver(&handle, 6) == 0 && lastArgument == 6, "attach-driver argument changed");
    operationResult = 1;
    Require(sceUsbdKernelDriverActive(&handle, 7) == 1 && lastArgument == 7, "active-driver result changed");
    Require(sceUsbdEventHandlingOk() == 1, "event-handling boolean changed");
    operationResult = 0;
    Require(sceUsbdEventHandlingOk() == 0, "event-handling false result changed");
    Require(sceUsbdResetDevice(&handle) == 0, "reset failed");

    std::array<unsigned char, 4> data{1, 2, 3, 4};
    Require(sceUsbdControlTransfer(&handle, 0x21, 9, 0x1234, 0x4567, data.data(), data.size(), 321) == 4,
        "control transfer byte count changed");
    Require(controlRequestType == 0x21 && controlRequest == 9 && controlValue == 0x1234
        && controlIndex == 0x4567 && controlLength == 4 && controlTimeout == 321,
        "control setup fields changed");
    Require(std::equal(data.begin(), data.end(), controlData.begin()), "control output buffer changed");
    data.fill(0);
    Require(sceUsbdGetStringDescriptor(&handle, 2, 0x409, data.data(), data.size()) == 4,
        "string descriptor byte count changed");
    Require(controlRequestType == 0x80 && controlRequest == LIBUSB_REQUEST_GET_DESCRIPTOR
        && controlValue == 0x302 && controlIndex == 0x409 && controlLength == 4,
        "string descriptor request changed");
    Require(data == std::array<unsigned char, 4>{1, 2, 3, 4}, "string descriptor input buffer changed");

    for (int error = -1; error >= LIBUSB_ERROR_NOT_SUPPORTED; --error) {
        operationResult = error;
        auto expected = static_cast<std::int32_t>(0x80240000u - error);
        Require(sceUsbdResetDevice(&handle) == expected, "backend error mapping changed");
        UsbdDevice** list = nullptr;
        Require(sceUsbdGetDeviceList(&list) == expected, "64-bit enumeration error lost its sign");
    }
    operationResult = LIBUSB_ERROR_OTHER;
    Require(sceUsbdControlTransfer(&handle, 0, 0, 0, 0, nullptr, 0, 0) == otherError,
        "generic backend error mapping changed");
    operationResult = 0;
    const UsbdTimeval timeout{2, 123456};
    Require(sceUsbdHandleEventsTimeout(&timeout) == 0 && lastTimeout.tv_sec == 2 && lastTimeout.tv_usec == 123456,
        "event timeout conversion changed");
    const UsbdTimeval invalid{0, 1000000};
    Require(sceUsbdHandleEventsTimeout(&invalid) == invalidArgument, "out-of-range timeout was accepted");
    Require(sceUsbdHandleEventsTimeout(nullptr) == invalidArgument, "null timeout was accepted");
    Require(sceUsbdGetDeviceList(nullptr) == invalidArgument, "null enumeration output was accepted");
    Require(sceUsbdCheckConnected(nullptr) == invalidArgument, "null connectivity handle was accepted");
}

struct Completion {
    UsbdTransfer* expected = nullptr;
    int calls = 0;
    libusb_transfer_status status = LIBUSB_TRANSFER_ERROR;
    int actualLength = -1;
    bool resubmit = false;
    bool freeTransfer = false;
    bool clearFlags = false;
};

void APS5_VABI OnCompletion(UsbdTransfer* transfer) {
    auto* completion = static_cast<Completion*>(transfer->user_data);
    Require(transfer == completion->expected, "callback received a different guest transfer");
    Require(transfer->callback == OnCompletion, "native callback replaced guest callback");
    ++completion->calls;
    completion->status = transfer->status;
    completion->actualLength = transfer->actual_length;
    if (completion->clearFlags) transfer->flags = 0;
    if (completion->resubmit && completion->calls == 1) {
        Require(sceUsbdSubmitTransfer(transfer) == 0, "callback could not resubmit transfer");
    }
    if (completion->freeTransfer) sceUsbdFreeTransfer(transfer);
}

void CompletePending(int length = 4) {
    completionLength = length;
    const UsbdTimeval immediate{0, 0};
    Require(sceUsbdHandleEventsTimeout(&immediate) == 0, "completion event handling failed");
}

UsbdTransfer* InterruptTransfer(Completion& completion, unsigned char* buffer, int length) {
    auto* transfer = sceUsbdAllocTransfer(0);
    Require(transfer != nullptr, "transfer allocation failed");
    completion.expected = transfer;
    sceUsbdFillInterruptTransfer(transfer, &handle, 0x81, buffer, length, OnCompletion, &completion, 123);
    Require(transfer->dev_handle == &handle && transfer->endpoint == 0x81
        && transfer->type == LIBUSB_TRANSFER_TYPE_INTERRUPT && transfer->timeout == 123
        && transfer->buffer == buffer && transfer->length == length && transfer->callback == OnCompletion
        && transfer->user_data == &completion, "interrupt transfer fields changed");
    return transfer;
}

void TestTransfers() {
    std::array<unsigned char, 8> buffer{};
    auto* filled = sceUsbdAllocTransfer(2);
    Require(filled != nullptr, "fill-preservation allocation failed");
    filled->flags = LIBUSB_TRANSFER_SHORT_NOT_OK;
    filled->status = LIBUSB_TRANSFER_TIMED_OUT;
    filled->actual_length = 7;
    filled->num_iso_packets = 1;
    sceUsbdFillInterruptTransfer(filled, &handle, 0x83, buffer.data(), buffer.size(), nullptr, nullptr, 456);
    Require(filled->flags == LIBUSB_TRANSFER_SHORT_NOT_OK && filled->status == LIBUSB_TRANSFER_TIMED_OUT
        && filled->actual_length == 7 && filled->num_iso_packets == 1,
        "fill-interrupt overwrote unrelated transfer fields");
    sceUsbdFreeTransfer(filled);
    Completion completion;
    auto* transfer = InterruptTransfer(completion, buffer.data(), buffer.size());
    transfer->flags = LIBUSB_TRANSFER_SHORT_NOT_OK;
    Require(sceUsbdSubmitTransfer(transfer) == 0 && pending != nullptr, "transfer submission failed");
    Require(pending->buffer == buffer.data() && pending->length == 8 && pending->timeout == 123
        && pending->endpoint == 0x81 && pending->type == LIBUSB_TRANSFER_TYPE_INTERRUPT
        && pending->flags == LIBUSB_TRANSFER_SHORT_NOT_OK, "native transfer fields changed");
    Require(transfer->callback == OnCompletion && transfer->user_data == &completion,
        "submission overwrote guest callback state");
    Require(sceUsbdSubmitTransfer(transfer) == busy, "pending transfer could be submitted twice");
    CompletePending(8);
    Require(completion.calls == 1 && completion.status == LIBUSB_TRANSFER_COMPLETED && completion.actualLength == 8,
        "completion result was not delivered");
    Require(buffer[0] == 0xa5 && buffer[7] == 0xa5, "completion data was not preserved");
    transfer->flags = 0;

    Require(sceUsbdSubmitTransfer(transfer) == 0, "transfer could not be reused");
    Require(sceUsbdCancelTransfer(transfer) == 0 && completion.calls == 1, "cancellation completed synchronously");
    CompletePending(2);
    Require(completion.calls == 2 && completion.status == LIBUSB_TRANSFER_CANCELLED && completion.actualLength == 2,
        "cancelled completion result was not delivered");

    completionStatus = LIBUSB_TRANSFER_TIMED_OUT;
    Require(sceUsbdSubmitTransfer(transfer) == 0, "timeout transfer could not be submitted");
    CompletePending(0);
    Require(completion.status == LIBUSB_TRANSFER_TIMED_OUT && completion.actualLength == 0,
        "timeout status was not preserved");
    completionStatus = LIBUSB_TRANSFER_COMPLETED;
    operationResult = LIBUSB_ERROR_NO_DEVICE;
    Require(sceUsbdSubmitTransfer(transfer) == noDevice && pending == nullptr, "failed submission changed ownership");
    operationResult = 0;
    completion.calls = 0;
    completion.resubmit = true;
    Require(sceUsbdSubmitTransfer(transfer) == 0, "retry after failed submission failed");
    CompletePending();
    Require(completion.calls == 1 && pending != nullptr, "callback resubmission was lost");
    CompletePending();
    Require(completion.calls == 2 && pending == nullptr, "resubmitted completion failed");
    sceUsbdFreeTransfer(transfer);

    Completion freeing;
    transfer = InterruptTransfer(freeing, buffer.data(), buffer.size());
    freeing.freeTransfer = true;
    Require(sceUsbdSubmitTransfer(transfer) == 0, "freeing callback submission failed");
    CompletePending();
    Require(freeing.calls == 1, "freeing callback was not called");

    Completion owned;
    expectedGuestBuffer = std::malloc(8);
    transfer = InterruptTransfer(owned, static_cast<unsigned char*>(expectedGuestBuffer), 8);
    transfer->flags = LIBUSB_TRANSFER_FREE_BUFFER;
    sceUsbdFreeTransfer(transfer);
    Require(guestBufferFrees == 1, "manual free did not release guest buffer exactly once");

    expectedGuestBuffer = std::malloc(8);
    transfer = InterruptTransfer(owned, static_cast<unsigned char*>(expectedGuestBuffer), 8);
    transfer->flags = LIBUSB_TRANSFER_FREE_BUFFER | LIBUSB_TRANSFER_FREE_TRANSFER;
    Require(sceUsbdSubmitTransfer(transfer) == 0, "owned transfer submission failed");
    Require((pending->flags & (LIBUSB_TRANSFER_FREE_BUFFER | LIBUSB_TRANSFER_FREE_TRANSFER)) == 0,
        "native transfer retained guest ownership flags");
    CompletePending();
    Require(owned.calls == 1 && guestBufferFrees == 2, "automatic free did not release guest buffer exactly once");

    Completion changedFlags;
    expectedGuestBuffer = std::malloc(8);
    transfer = InterruptTransfer(changedFlags, static_cast<unsigned char*>(expectedGuestBuffer), 8);
    transfer->flags = LIBUSB_TRANSFER_FREE_BUFFER | LIBUSB_TRANSFER_FREE_TRANSFER;
    changedFlags.clearFlags = true;
    Require(sceUsbdSubmitTransfer(transfer) == 0, "flag-changing callback submission failed");
    CompletePending();
    Require(changedFlags.calls == 1 && guestBufferFrees == 2, "automatic free ignored updated buffer ownership");
    std::free(expectedGuestBuffer);
    expectedGuestBuffer = nullptr;

    Completion iso;
    transfer = sceUsbdAllocTransfer(2);
    Require(transfer != nullptr && transfer->num_iso_packets == 0, "isochronous descriptor allocation was not zeroed");
    iso.expected = transfer;
    transfer->dev_handle = &handle;
    transfer->endpoint = 0x82;
    transfer->type = LIBUSB_TRANSFER_TYPE_ISOCHRONOUS;
    transfer->buffer = buffer.data();
    transfer->length = 8;
    transfer->callback = OnCompletion;
    transfer->user_data = &iso;
    transfer->num_iso_packets = 2;
    transfer->iso_packet_desc[0].length = 2;
    transfer->iso_packet_desc[1].length = 6;
    Require(sceUsbdSubmitTransfer(transfer) == 0 && pending->num_iso_packets == 2,
        "isochronous packet count changed");
    Require(pending->iso_packet_desc[0].length == 2 && pending->iso_packet_desc[1].length == 6,
        "isochronous packet lengths changed");
    CompletePending();
    Require(iso.calls == 1 && transfer->iso_packet_desc[0].actual_length == 1
        && transfer->iso_packet_desc[1].actual_length == 3
        && transfer->iso_packet_desc[0].status == LIBUSB_TRANSFER_COMPLETED
        && transfer->iso_packet_desc[1].status == LIBUSB_TRANSFER_ERROR,
        "isochronous completion descriptors changed");
    sceUsbdFreeTransfer(transfer);
    Require(nativeAllocations == nativeFrees, "native transfer ownership leaked");
}

}

int main() {
    initResult = LIBUSB_ERROR_OTHER;
    Require(sceUsbdInit() == otherError, "initialization error was not mapped");
    Require(initCalls == 1 && exitCalls == 0, "failed initialization changed context ownership");
    initResult = 0;
    Require(sceUsbdInit() == 0, "initialization failed");
    TestDevices();
    TestOperations();
    TestTransfers();
    sceUsbdExit();
    Require(exitCalls == 1, "context was not released");
    Require(sceUsbdInit() == 0, "reinitialization failed");
    sceUsbdExit();
    Require(exitCalls == 2, "reinitialized context was not released");
    std::puts("USBD deterministic tests passed");
}
