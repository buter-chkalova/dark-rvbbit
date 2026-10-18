// Device control requests and guarded VMFUNC calls.
// SEH handles the exception path when the instruction is unavailable.

#include "hv_ctl.h"

ULONG64 HvCtlG_State = 0;

extern NTSTATUS HvCtlVmfunc(ULONG op, ULONG64 arg1, ULONG64 arg2, PULONG64 out);

static BOOLEAN HvCtlProbe(void) {
    ULONG64 v = 0;
    __try {
        HvCtlVmfunc(0x9A05, 0, 0, &v);      // HV_OP_QUERY_STATE
        return (v & 1) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
}

static NTSTATUS HvCtlDispatch(PDEVICE_OBJECT dev, PIRP irp) {
    UNREFERENCED_PARAMETER(dev);
    PIO_STACK_LOCATION sp = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG outLen = 0;
    PVOID inBuf  = irp->AssociatedIrp.SystemBuffer;
    ULONG inLen  = sp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outCap = sp->Parameters.DeviceIoControl.OutputBufferLength;

    switch (sp->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_HV_PROBE:
        if (outCap >= sizeof(ULONG)) {
            *(PULONG)inBuf = HvCtlProbe() ? 1 : 0;
            outLen = sizeof(ULONG);
            status = STATUS_SUCCESS;
        }
        break;

    case IOCTL_HV_HIDE_RANGE:
        if (inLen >= sizeof(HVCTL_RANGE)) {
            PHVCTL_RANGE r = (PHVCTL_RANGE)inBuf;
            __try { HvCtlVmfunc(0x9A03, r->PhysStart, r->Size, NULL); status = STATUS_SUCCESS; }
            __except (EXCEPTION_EXECUTE_HANDLER) { status = STATUS_UNSUCCESSFUL; }
        }
        break;

    case IOCTL_HV_UNHIDE_RANGE:
        if (inLen >= sizeof(HVCTL_RANGE)) {
            PHVCTL_RANGE r = (PHVCTL_RANGE)inBuf;
            __try { HvCtlVmfunc(0x9A02, r->PhysStart, r->Size, NULL); status = STATUS_SUCCESS; }
            __except (EXCEPTION_EXECUTE_HANDLER) { status = STATUS_UNSUCCESSFUL; }
        }
        break;

    case IOCTL_HV_PEEK_HIDDEN:
        if (inLen >= sizeof(HVCTL_PEEK) && outCap >= sizeof(HVCTL_PEEK)) {
            PHVCTL_PEEK p = (PHVCTL_PEEK)inBuf;
            ULONG64 val = 0;
            __try {
                HvCtlVmfunc(0x9A01, p->PhysAddr, 0, &val);
                p->Value = val;
                outLen = sizeof(HVCTL_PEEK);
                status = STATUS_SUCCESS;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                status = STATUS_UNSUCCESSFUL;
            }
        }
        break;

    case IOCTL_HV_SET_TSC_FOG:
        if (inLen >= sizeof(ULONG)) {
            ULONG s = *(PULONG)inBuf;
            __try { HvCtlVmfunc(0x9A04, s, 0, NULL); status = STATUS_SUCCESS; }
            __except (EXCEPTION_EXECUTE_HANDLER) { status = STATUS_UNSUCCESSFUL; }
        }
        break;

    case IOCTL_HV_QUERY_STATE:
        if (outCap >= sizeof(HVCTL_STATE)) {
            ULONG64 flags = 0;
            __try { HvCtlVmfunc(0x9A05, 0, 0, &flags); }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                status = STATUS_UNSUCCESSFUL;
                break;
            }
            PHVCTL_STATE st = (PHVCTL_STATE)inBuf;
            st->HiddenCount   = (ULONG)(flags >> 32);
            st->Launched      = (BOOLEAN)(flags & 1);
            st->CpuCount      = KeQueryActiveProcessorCount(NULL);
            st->TscFog        = 0;
            st->TscBaseOffset = 0;
            outLen = sizeof(HVCTL_STATE);
            status = STATUS_SUCCESS;
        }
        break;

    case IOCTL_HV_HIDE_DRIVER:
        if (inLen >= sizeof(HVCTL_HIDE_DRV)) {
            PHVCTL_HIDE_DRV h = (PHVCTL_HIDE_DRV)inBuf;
            ULONG64 phys = MmGetPhysicalAddress((PVOID)h->Base).QuadPart;
            __try { HvCtlVmfunc(0x9A03, phys, h->Size, NULL); status = STATUS_SUCCESS; }
            __except (EXCEPTION_EXECUTE_HANDLER) { status = STATUS_UNSUCCESSFUL; }
        }
        break;
    }

    irp->IoStatus.Status = status;
    irp->IoStatus.Information = outLen;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS HvCtlCreateClose(PDEVICE_OBJECT dev, PIRP irp) {
    UNREFERENCED_PARAMETER(dev);
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static VOID HvCtlUnload(PDRIVER_OBJECT drv) {
    UNICODE_STRING sym;
    RtlInitUnicodeString(&sym, HVCTL_SYMLINK_NAME);
    IoDeleteSymbolicLink(&sym);
    if (drv->DeviceObject) IoDeleteDevice(drv->DeviceObject);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT drv, PUNICODE_STRING reg) {
    UNREFERENCED_PARAMETER(reg);

    UNICODE_STRING devName, symName;
    RtlInitUnicodeString(&devName, HVCTL_DEVICE_NAME);
    RtlInitUnicodeString(&symName, HVCTL_SYMLINK_NAME);

    PDEVICE_OBJECT dev = NULL;
    NTSTATUS s = IoCreateDevice(drv, 0, &devName, HVCTL_DEVICE_TYPE, 0, FALSE, &dev);
    if (!NT_SUCCESS(s)) return s;

    s = IoCreateSymbolicLink(&symName, &devName);
    if (!NT_SUCCESS(s)) { IoDeleteDevice(dev); return s; }

    drv->MajorFunction[IRP_MJ_CREATE] = HvCtlCreateClose;
    drv->MajorFunction[IRP_MJ_CLOSE] = HvCtlCreateClose;
    drv->MajorFunction[IRP_MJ_DEVICE_CONTROL] = HvCtlDispatch;
    drv->DriverUnload = HvCtlUnload;

    dev->Flags |= DO_BUFFERED_IO;
    dev->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}