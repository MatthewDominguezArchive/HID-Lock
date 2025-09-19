#include <main.h>

// KdMapper passes no arguments by default to the driver entry.
// Which is not good because we later on need a PDRIVER_OBJECT.
// Instead I make a landing pad little entry point which doesn't expect arguments from KdMapper.
// And then creates a driver object that's entry point is our real entry point.
#ifdef KdMapper
extern "C" NTSTATUS DriverEntry()
{
    UNICODE_STRING driver_name;
    RtlInitUnicodeString(&driver_name, L"\\Driver\\HIDLock"); // Path to our driver object.
    return IoCreateDriver(&driver_name, &RealDriverEntry); // Create driver object and calls RealDriverEntry.
}
// Header for real entry point.
extern "C" NTSTATUS RealDriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
#else

// Normal driver entry header. If you arent using KdMapper to load your driver, you are good to go with a normal driver entry point.
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
#endif

{
    // We dont actually need our registry path, but at the same time we can't just forget about it. Most compilers will treat this Unreferenced parameter warning as an error.
    UNREFERENCED_PARAMETER(RegistryPath);

    // Forget about this, I only am making this to have a valid pointer to IoRegisterPlugPlayNotification argument 7.
    // Because if I directly put a nullptr into the function call and the kernel tries to fill in an invalid pointer you can blue screen. Fun Fact :)
    PVOID notificationEntry = nullptr;

    // This will make it so that HIDDeviceCallback is called every time an HID event happens, specifically device interface changes, which is why we specify that filter in argument 1.
    NTSTATUS status = IoRegisterPlugPlayNotification(
        EventCategoryDeviceInterfaceChange,
        0,
        (PVOID)&GUID_DEVINTERFACE_HID,
        DriverObject, // Why we needed the KdMapper trampoline.
        HIDDeviceCallback, // This is our function that will run when this notification is hit.
        DriverObject, // Passing driver object thru context bc we need it to make the filter device.
        &notificationEntry
    );

    return status;
}

// This is what gets ran when a device interface change is made.
NTSTATUS HIDDeviceCallback(PVOID NotificationStructure, PVOID Context)
{
    auto notif = (PDEVICE_INTERFACE_CHANGE_NOTIFICATION)NotificationStructure;
    
    // Only handle arrivals, the events are classified by a GUID which has a unique structure: https://learn.microsoft.com/en-us/windows/win32/api/guiddef/ns-guiddef-guid#syntax
    if (!IsEqualGUID(notif->Event, GUID_DEVICE_INTERFACE_ARRIVAL))
        return STATUS_SUCCESS;

    // This would be our potential threat device, our HID that we are throwing a filter onto.
    PDEVICE_OBJECT targetDevice = nullptr;
    // This is the file object the IRPs are sent through. This is not like a filesystem file. We only need this for getting the device pointer, we never actually use this.
    PFILE_OBJECT fileObject = nullptr;

    // Getting a pointer to the HID object in order to later attach the filter to it.
    NTSTATUS status = IoGetDeviceObjectPointer(
        notif->SymbolicLinkName,
        FILE_READ_DATA,
        &fileObject,
        &targetDevice
    );

    // Once again, we never end up using this so safer to just dereference it now.
    ObDereferenceObject(fileObject);

    // Make sure we actually succeeded to get the object pointer.
    // Fear the bluescreen, play it safe yall.
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Create a filter device attached above the new HID device.
    PDEVICE_OBJECT filterDevice = nullptr;
    status = IoCreateDevice(
        (PDRIVER_OBJECT)Context,
        sizeof(DEVICE_EXTENSION),
        nullptr, // no symbolic link since its not like we are communicating with a usermode application.
        FILE_DEVICE_UNKNOWN,
        0,
        FALSE,
        &filterDevice
    );

    // Make sure we made our filter with no problems.
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // We are copying our flags over from the target device to our filter device. The reason we are doing this is because having a mismatched flag can cause BSOD.
    filterDevice->Flags |= targetDevice->Flags & (DO_POWER_PAGABLE | DO_BUFFERED_IO | DO_DIRECT_IO);
    filterDevice->DeviceType = targetDevice->DeviceType;
    filterDevice->Characteristics = targetDevice->Characteristics;

    // Finally, we are attaching our filter device to our target device and storing a pointer to the target device in our DeviceExtension->LowerDevice.
    PDEVICE_EXTENSION ext = (PDEVICE_EXTENSION)filterDevice->DeviceExtension;
    ext->LowerDevice = IoAttachDeviceToDeviceStack(filterDevice, targetDevice);

    // Set all dispatch routines for this filter device only. Dispatch routines are functions that handle irps.
    for (int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
        filterDevice->DriverObject->MajorFunction[i] = FilterDispatch;

    // Set filter device flag to initialized
    filterDevice->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

// This is our filter function that will have irp be passed through before making it to the HID. Which btw, since we never call IoCallDriver, it never makes it to the HID.
// A HID like a keyboard or mouse doesn't "send" data to the computer, rather the computer sends a irp to the HID and waits for the response.
NTSTATUS FilterDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    // Once again, can't just leave parameters unreferenced
    UNREFERENCED_PARAMETER(DeviceObject);

    // As long as Irp->AssociatedIrp isn't already zero, we are just going to turn it to zero.
    // Can't just force overwrite bc it can be a nullptr and like i said before we can't write to a nullptr in kernel or we risk a bluescreen
    if (Irp->AssociatedIrp.SystemBuffer)
        RtlZeroMemory(Irp->AssociatedIrp.SystemBuffer, Irp->IoStatus.Information);

    // Complete the IRP without incrementing because incrementation of this irp would be unnecessary
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0; // Setting the information to zero because this is what tells the IO manager how many bytes were read or written, and we are just telling the IO manager that we trashed the irp and sent it back
    IoCompleteRequest(Irp, IO_NO_INCREMENT); // No increment literally just means that we would be boosting the priority of the irp, but in reality who cares about this irp.

    return STATUS_SUCCESS;
}
