// Experimental APIC virtualization using the virtual APIC page layout.
// EOI, timer and self-IPI paths are handled here. Posted interrupt
// support has not been validated.

#include "hv.h"
#include "hv_apic_util.h"

static inline ULONG HvVapicRead32(PHV_CPU_CONTEXT_EX ctx, ULONG off) {
    return *(volatile ULONG*)((PUCHAR)ctx->VapicPage + off);
}

static inline VOID HvVapicWrite32(PHV_CPU_CONTEXT_EX ctx, ULONG off, ULONG v) {
    *(volatile ULONG*)((PUCHAR)ctx->VapicPage + off) = v;
}

static VOID HvIsrSet(PHV_CPU_CONTEXT_EX ctx, ULONG vector) {
    ULONG off = VAPIC_OFF_ISR_BASE + (vector >> 3) * 16 + (vector >> 5) * 4;
    HvVapicWrite32(ctx, off, HvVapicRead32(ctx, off) | (1u << (vector & 31)));
}

static VOID HvIsrClear(PHV_CPU_CONTEXT_EX ctx, ULONG vector) {
    ULONG off = VAPIC_OFF_ISR_BASE + (vector >> 3) * 16 + (vector >> 5) * 4;
    HvVapicWrite32(ctx, off, HvVapicRead32(ctx, off) & ~(1u << (vector & 31)));
}

static ULONG HvFindPendingIrr(PHV_CPU_CONTEXT_EX ctx) {
    ULONG ppr = HvVapicRead32(ctx, VAPIC_OFF_PPR) >> 24;
    for (int i = 7; i >= 0; i--) {
        ULONG off = VAPIC_OFF_IRR_BASE + i * 16;
        ULONG v = HvVapicRead32(ctx, off);
        if (!v) continue;
        for (int b = 31; b >= 0; b--) {
            if (!(v & (1u << b))) continue;
            ULONG vec = i * 32 + b;
            if ((vec >> 4) > ppr) return vec;
        }
    }
    return 0;
}

NTSTATUS HvApicvInit(PHV_CPU_CONTEXT_EX ctx) {
    if (!ctx->Caps.HasApicv) return STATUS_NOT_SUPPORTED;

    LARGE_INTEGER mp = { .QuadPart = ~0ULL };

    ctx->VapicPage = MmAllocateContiguousMemory(PAGE_SIZE, mp);
    if (!ctx->VapicPage) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(ctx->VapicPage, PAGE_SIZE);
    ctx->VapicPhys = MmGetPhysicalAddress(ctx->VapicPage).QuadPart;

    // apic access page must be non-zero when VIRTUALIZE_APIC set,
    // even for x2apic guests that never touch it
    ctx->ApicAccessPage = MmAllocateContiguousMemory(PAGE_SIZE, mp);
    if (!ctx->ApicAccessPage) {
        MmFreeContiguousMemory(ctx->VapicPage);
        ctx->VapicPage = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(ctx->ApicAccessPage, PAGE_SIZE);
    ctx->ApicAccessPhys = MmGetPhysicalAddress(ctx->ApicAccessPage).QuadPart;

    if (ctx->Caps.HasPostedIntr) {
        ctx->PostedIntrDesc = MmAllocateContiguousMemory(PAGE_SIZE, mp);
        if (ctx->PostedIntrDesc) {
            RtlZeroMemory(ctx->PostedIntrDesc, PAGE_SIZE);
            ctx->PostedIntrDescPhys = MmGetPhysicalAddress(ctx->PostedIntrDesc).QuadPart;
            PHV_PI_DESC pid = (PHV_PI_DESC)ctx->PostedIntrDesc;
            pid->Vector = 0xF0;
        }
    }

    __vmx_vmwrite(VMCS_CTRL_VIRTUAL_APIC_ADDR, ctx->VapicPhys);
    __vmx_vmwrite(VMCS_CTRL_APIC_ACCESS_ADDR,  ctx->ApicAccessPhys);

    // trap EOI on all 256 vectors so we can clear isr bit
    __vmx_vmwrite(VMCS_CTRL_EOI_EXIT_BITMAP_0, 0xFFFFFFFFFFFFFFFFULL);
    __vmx_vmwrite(VMCS_CTRL_EOI_EXIT_BITMAP_1, 0xFFFFFFFFFFFFFFFFULL);
    __vmx_vmwrite(VMCS_CTRL_EOI_EXIT_BITMAP_2, 0xFFFFFFFFFFFFFFFFULL);
    __vmx_vmwrite(VMCS_CTRL_EOI_EXIT_BITMAP_3, 0xFFFFFFFFFFFFFFFFULL);

    // tpr threshold 0 = every tpr write exits. full control.
    __vmx_vmwrite(VMCS_CTRL_TPR_THRESHOLD, 0);

    if (ctx->Caps.HasPostedIntr && ctx->PostedIntrDesc) {
        __vmx_vmwrite(VMCS_CTRL_POSTED_INTR_DESC_ADDR, ctx->PostedIntrDescPhys);
    }

    return STATUS_SUCCESS;
}

VOID HvApicvTeardown(PHV_CPU_CONTEXT_EX ctx) {
    if (ctx->VapicPage)         MmFreeContiguousMemory(ctx->VapicPage);
    if (ctx->ApicAccessPage)    MmFreeContiguousMemory(ctx->ApicAccessPage);
    if (ctx->PostedIntrDesc)    MmFreeContiguousMemory(ctx->PostedIntrDesc);
    ctx->VapicPage = NULL;
    ctx->ApicAccessPage = NULL;
    ctx->PostedIntrDesc = NULL;
}

VOID HvApicvInjectVector(PHV_CPU_CONTEXT_EX ctx, ULONG vector) {
    ULONG off = VAPIC_OFF_IRR_BASE + (vector >> 3) * 16 + (vector >> 5) * 4;
    HvVapicWrite32(ctx, off, HvVapicRead32(ctx, off) | (1u << (vector & 31)));

    ULONG64 info = 0x80000000ULL | (0ULL << 8) | (vector & 0xFF);
    __vmx_vmwrite(VMCS_CTRL_VM_ENTRY_INTR_INFO, info);
}

BOOLEAN HvApicvPending(PHV_CPU_CONTEXT_EX ctx) {
    return HvFindPendingIrr(ctx) != 0;
}

// timer tick: initial and divide come from vapic page directly.
// decrement by delta on each preemption tick.
VOID HvApicvTimerTick(PHV_CPU_CONTEXT_EX ctx) {
    ULONG lvt = HvVapicRead32(ctx, VAPIC_OFF_LVT_TIMER);
    if (lvt & (1u << 16)) return;   // masked

    ULONG initial = HvVapicRead32(ctx, VAPIC_OFF_TIMER_INITIAL);
    if (initial == 0) return;

    ULONG current = HvVapicRead32(ctx, VAPIC_OFF_TIMER_CURRENT);

    ULONG divide = HvVapicRead32(ctx, VAPIC_OFF_TIMER_DIVIDE) & 0xF;
    ULONG div = HvDecodeApicDivide(divide);
    ULONG delta = 0x100000 / div;

    if (current <= delta) {
        ULONG vec = lvt & 0xFF;
        HvApicvInjectVector(ctx, vec);

        BOOLEAN periodic = (lvt >> 17) & 1;
        HvVapicWrite32(ctx, VAPIC_OFF_TIMER_CURRENT, periodic ? initial : 0);
    } else {
        HvVapicWrite32(ctx, VAPIC_OFF_TIMER_CURRENT, current - delta);
    }
}

// APIC MMIO handling is currently a placeholder, not full emulation.
VOID HvHandleApicAccess(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx, ULONG64 qual) {
    UNREFERENCED_PARAMETER(regs);
    UNREFERENCED_PARAMETER(ctx);
    UNREFERENCED_PARAMETER(qual);

    ULONG64 la = 0;
    __vmx_vmread(VMCS_RO_GUEST_LINEAR_ADDR, &la);
    ULONG off = (ULONG)(la & 0xFFF);

    ULONG64 gpa = 0;
    __vmx_vmread(VMCS_RO_GUEST_PHYS_ADDR, &gpa);
    ULONG apicOff = (ULONG)(gpa & 0xFFF);

    UNREFERENCED_PARAMETER(off);
    UNREFERENCED_PARAMETER(apicOff);

    ULONG64 rip = 0;
    __vmx_vmread(VMCS_GUEST_RIP, &rip);
    ULONG64 len = 0;
    __vmx_vmread(VMCS_RO_VM_EXIT_INSTR_LEN, &len);
    if (len == 0) len = 1;
    rip += len;
    __vmx_vmwrite(VMCS_GUEST_RIP, rip);
}

// apic-write exit. qual has register offset in low 12 bits.
// hw already wrote to vapic page, we just sync our state.
VOID HvHandleApicWrite(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx, ULONG64 qual) {
    UNREFERENCED_PARAMETER(regs);

    ULONG off = (ULONG)(qual & 0xFFF);

    if (off == VAPIC_OFF_LVT_TIMER) {
        ULONG lvt = HvVapicRead32(ctx, VAPIC_OFF_LVT_TIMER);
        ctx->ApicTimer.Vector   = lvt & 0xFF;
        ctx->ApicTimer.Periodic = (lvt >> 17) & 1;
        if (lvt & (1u << 16)) ctx->ApicTimer.Enabled = FALSE;
    } else if (off == VAPIC_OFF_TIMER_INITIAL) {
        ULONG initial = HvVapicRead32(ctx, VAPIC_OFF_TIMER_INITIAL);
        ctx->ApicTimer.Count = initial;
        ctx->ApicTimer.Enabled = (initial != 0);
        HvVapicWrite32(ctx, VAPIC_OFF_TIMER_CURRENT, initial);
    } else if (off == VAPIC_OFF_TIMER_DIVIDE) {
        ULONG d = HvVapicRead32(ctx, VAPIC_OFF_TIMER_DIVIDE) & 0xF;
        ULONG div = HvDecodeApicDivide(d);
        ctx->ApicTimer.Divide = div;
    }
}

// eoi: clear isr bit, deliver next pending irr if above priority.
VOID HvHandleEoi(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx, ULONG64 qual) {
    UNREFERENCED_PARAMETER(regs);

    ULONG vector = (ULONG)(qual & 0xFF);
    HvIsrClear(ctx, vector);

    ULONG pending = HvFindPendingIrr(ctx);
    if (pending) {
        HvIsrSet(ctx, pending);
        ULONG64 info = 0x80000000ULL | (0ULL << 8) | (pending & 0xFF);
        __vmx_vmwrite(VMCS_CTRL_VM_ENTRY_INTR_INFO, info);
    }
}

// self-ipi exit. common case handled by apic-write exit path.
VOID HvHandleSelfIpi(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx, ULONG64 qual) {
    UNREFERENCED_PARAMETER(regs);
    UNREFERENCED_PARAMETER(ctx);
    UNREFERENCED_PARAMETER(qual);

    ULONG64 rip = 0;
    __vmx_vmread(VMCS_GUEST_RIP, &rip);
    ULONG64 len = 0;
    __vmx_vmread(VMCS_RO_VM_EXIT_INSTR_LEN, &len);
    if (len == 0) len = 2;
    rip += len;
    __vmx_vmwrite(VMCS_GUEST_RIP, rip);
}