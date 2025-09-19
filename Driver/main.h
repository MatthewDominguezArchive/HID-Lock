#pragma once
#include <ntddk.h>
#include <hidclass.h>
#include <wdm.h>
#include <Wdmguid.h>
#include <initguid.h>

#ifdef KdMapper
extern "C" {
    NTKERNELAPI NTSTATUS IoCreateDriver(
        PUNICODE_STRING DriverName,
        PDRIVER_INITIALIZE InitializationFunction
    );
}
#endif

// GUIDs
DEFINE_GUID(GUID_DEVINTERFACE_HID, // Guid for when we register our plug and play notifications, we only want to hear about events that are HID related
    0xA5DCBF10, 0x6530, 0x11D2,
    0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED
);
DEFINE_GUID(GUID_DEVICE_INTERFACE_ARRIVAL, // Now this is where we would compare to make sure the event is a HID device being plugged in
    0xCB3A4004, 0x46F0, 0x11D0,
    0xB0, 0x8F, 0x00, 0x60, 0x97, 0x13, 0x05, 0x3F
);

// Data structures
typedef struct _DEVICE_EXTENSION {
    PDEVICE_OBJECT LowerDevice;
} DEVICE_EXTENSION, * PDEVICE_EXTENSION;

// Function prototypes
extern "C" NTSTATUS RealDriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
extern "C" NTSTATUS HIDDeviceCallback(PVOID NotificationStructure, PVOID Context);
NTSTATUS FilterDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp);
