enable_language(C)

set(usbdBackendRoot "${PROJECT_SOURCE_DIR}/3rdparty/libusb/libusb")
set(usbdBackendConfig "${CMAKE_CURRENT_BINARY_DIR}/libusb-config")
if(NOT EXISTS "${usbdBackendRoot}/core.c")
    message(FATAL_ERROR "libusb submodule is missing; run git submodule update --init 3rdparty/libusb")
endif()

include(CheckFunctionExists)
include(CheckIncludeFile)
include(CheckSymbolExists)
include(CheckTypeSize)

set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_include_file(sys/time.h HAVE_SYS_TIME_H)
check_type_size("struct timespec" USBD_TIMESPEC LANGUAGE C)
set(HAVE_STRUCT_TIMESPEC "${HAVE_USBD_TIMESPEC}")
if(WIN32)
    set(PLATFORM_WINDOWS 1)
else()
    set(PLATFORM_POSIX 1)
    check_symbol_exists(nfds_t poll.h HAVE_NFDS_T)
    check_function_exists(clock_gettime HAVE_CLOCK_GETTIME)
    check_function_exists(pthread_condattr_setclock HAVE_PTHREAD_CONDATTR_SETCLOCK)
    check_function_exists(pipe2 HAVE_PIPE2)
    if(APPLE)
        check_function_exists(pthread_threadid_np HAVE_PTHREAD_THREADID_NP)
        check_include_file(IOKit/usb/IOUSBHostFamilyDefinitions.h HAVE_IOKIT_USB_IOUSBHOSTFAMILYDEFINITIONS_H)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        check_function_exists(pthread_setname_np HAVE_PTHREAD_SETNAME_NP)
        check_include_file(asm/types.h HAVE_ASM_TYPES_H)
        check_symbol_exists(eventfd sys/eventfd.h HAVE_EVENTFD)
        check_symbol_exists(timerfd_create sys/timerfd.h HAVE_TIMERFD)
    else()
        message(FATAL_ERROR "libSceUsbd backend is not configured for ${CMAKE_SYSTEM_NAME}")
    endif()
endif()
unset(CMAKE_REQUIRED_DEFINITIONS)

configure_file("${CMAKE_CURRENT_LIST_DIR}/LibusbConfig.h.in" "${usbdBackendConfig}/config.h")
add_library(usbd_backend STATIC
    "${usbdBackendRoot}/core.c"
    "${usbdBackendRoot}/descriptor.c"
    "${usbdBackendRoot}/hotplug.c"
    "${usbdBackendRoot}/io.c"
    "${usbdBackendRoot}/strerror.c"
    "${usbdBackendRoot}/sync.c"
)
set_target_properties(usbd_backend PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
target_include_directories(usbd_backend PRIVATE "${usbdBackendConfig}" "${usbdBackendRoot}/os")
target_include_directories(usbd_backend SYSTEM PUBLIC "${usbdBackendRoot}")

if(WIN32)
    target_sources(usbd_backend PRIVATE
        "${usbdBackendRoot}/os/events_windows.c"
        "${usbdBackendRoot}/os/threads_windows.c"
        "${usbdBackendRoot}/os/windows_common.c"
        "${usbdBackendRoot}/os/windows_usbdk.c"
        "${usbdBackendRoot}/os/windows_winusb.c"
    )
    target_compile_definitions(usbd_backend PRIVATE _WIN32_WINNT=0x0600)
else()
    target_sources(usbd_backend PRIVATE
        "${usbdBackendRoot}/os/events_posix.c"
        "${usbdBackendRoot}/os/threads_posix.c"
    )
    find_package(Threads REQUIRED)
    target_link_libraries(usbd_backend PRIVATE Threads::Threads)
    if(APPLE)
        target_sources(usbd_backend PRIVATE "${usbdBackendRoot}/os/darwin_usb.c")
        target_link_libraries(usbd_backend PRIVATE "-framework IOKit" "-framework CoreFoundation" "-framework Security" objc)
    else()
        target_sources(usbd_backend PRIVATE "${usbdBackendRoot}/os/linux_usbfs.c" "${usbdBackendRoot}/os/linux_netlink.c")
    endif()
endif()
