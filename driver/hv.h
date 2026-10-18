#pragma once
#include <ntddk.h>
#include <intrin.h>

// -- tunables -------------------------------------------------------

#define HV_MAX_CPUS            256
#define HV_VMXON_SIZE          0x1000
#define HV_VMCS_SIZE           0x1000
#define HV_MSR_BITMAP_SIZE     0x1000
#define HV_HOST_STACK_SIZE     0x4000
#define HV_EPT_COVER_GB        64
#define HV_MAX_HIDDEN_RANGES   64
#define HV_MAX_VPID            0xFFFE
#define HV_MAX_SHADOWS         128

// -- vmcs field encodings -------------------------------------------

// 32-bit control
#define VMCS_CTRL_VPID                 0x0000
#define VMCS_CTRL_PIN_BASED            0x4000
#define VMCS_CTRL_CPU_BASED            0x4002
#define VMCS_CTRL_EXCEPTION_BITMAP     0x4004
#define VMCS_CTRL_MSR_BITMAP           0x4006
#define VMCS_CTRL_VM_EXIT_CONTROLS     0x400C
#define VMCS_CTRL_VM_ENTRY_CONTROLS    0x4012
#define VMCS_CTRL_VM_ENTRY_INTR_INFO   0x4016
#define VMCS_CTRL_VM_ENTRY_EXC_ERRCODE 0x4018
#define VMCS_CTRL_TPR_THRESHOLD        0x401C
#define VMCS_CTRL_CPU_BASED2           0x401E
#define VMCS_CTRL_PLE_GAP              0x4020
#define VMCS_CTRL_PLE_WINDOW           0x4022

// 64-bit control
#define VMCS_CTRL_TSC_OFFSET           0x2010
#define VMCS_CTRL_VIRTUAL_APIC_ADDR    0x2012
#define VMCS_CTRL_APIC_ACCESS_ADDR     0x2014
#define VMCS_CTRL_POSTED_INTR_DESC_ADDR 0x2016
#define VMCS_CTRL_EPT_POINTER          0x201A
#define VMCS_CTRL_EOI_EXIT_BITMAP_0    0x201C
#define VMCS_CTRL_EOI_EXIT_BITMAP_1    0x201E
#define VMCS_CTRL_EOI_EXIT_BITMAP_2    0x2020
#define VMCS_CTRL_EOI_EXIT_BITMAP_3    0x2022
#define VMCS_CTRL_TSC_MULTIPLIER       0x2032
#define VMCS_CTRL_PREEMPTION_TIMER     0x482E

// guest state
#define VMCS_GUEST_ES_SELECTOR       0x0800
#define VMCS_GUEST_CS_SELECTOR       0x0802
#define VMCS_GUEST_SS_SELECTOR       0x0804
#define VMCS_GUEST_DS_SELECTOR       0x0806
#define VMCS_GUEST_FS_SELECTOR       0x0808
#define VMCS_GUEST_GS_SELECTOR       0x080A
#define VMCS_GUEST_LDTR_SELECTOR     0x080C
#define VMCS_GUEST_TR_SELECTOR       0x080E
#define VMCS_GUEST_ES_LIMIT          0x4800
#define VMCS_GUEST_CS_LIMIT          0x4802
#define VMCS_GUEST_SS_LIMIT          0x4804
#define VMCS_GUEST_DS_LIMIT          0x4806
#define VMCS_GUEST_FS_LIMIT          0x4808
#define VMCS_GUEST_GS_LIMIT          0x480A
#define VMCS_GUEST_LDTR_LIMIT        0x480C
#define VMCS_GUEST_TR_LIMIT          0x480E
#define VMCS_GUEST_GDTR_LIMIT        0x4810
#define VMCS_GUEST_IDTR_LIMIT        0x4812
#define VMCS_GUEST_ES_AR             0x4814
#define VMCS_GUEST_CS_AR             0x4816
#define VMCS_GUEST_SS_AR             0x4818
#define VMCS_GUEST_DS_AR             0x481A
#define VMCS_GUEST_FS_AR             0x481C
#define VMCS_GUEST_GS_AR             0x481E
#define VMCS_GUEST_LDTR_AR           0x4820
#define VMCS_GUEST_TR_AR             0x4822
#define VMCS_GUEST_INTERRUPTIBILITY  0x4824
#define VMCS_GUEST_ACTIVITY_STATE    0x4826
#define VMCS_GUEST_SYSENTER_CS       0x482A
#define VMCS_GUEST_VMCS_LINK_PTR     0x2800
#define VMCS_GUEST_CR0               0x6800
#define VMCS_GUEST_CR3               0x6802
#define VMCS_GUEST_CR4               0x6804
#define VMCS_GUEST_ES_BASE           0x6806
#define VMCS_GUEST_CS_BASE           0x6808
#define VMCS_GUEST_SS_BASE           0x680A
#define VMCS_GUEST_DS_BASE           0x680C
#define VMCS_GUEST_FS_BASE           0x680E
#define VMCS_GUEST_GS_BASE           0x6810
#define VMCS_GUEST_LDTR_BASE         0x6812
#define VMCS_GUEST_TR_BASE           0x6814
#define VMCS_GUEST_GDTR_BASE         0x6816
#define VMCS_GUEST_IDTR_BASE         0x6818
#define VMCS_GUEST_DR7               0x681A
#define VMCS_GUEST_RSP               0x681C
#define VMCS_GUEST_RIP               0x681E
#define VMCS_GUEST_RFLAGS            0x6820
#define VMCS_GUEST_PENDING_DBG_EXC   0x6822
#define VMCS_GUEST_SYSENTER_ESP      0x6824
#define VMCS_GUEST_SYSENTER_EIP      0x6826

// host state
#define VMCS_HOST_ES_SELECTOR        0x0C00
#define VMCS_HOST_CS_SELECTOR        0x0C02
#define VMCS_HOST_SS_SELECTOR        0x0C04
#define VMCS_HOST_DS_SELECTOR        0x0C06
#define VMCS_HOST_FS_SELECTOR        0x0C08
#define VMCS_HOST_GS_SELECTOR        0x0C0A
#define VMCS_HOST_TR_SELECTOR        0x0C0C
#define VMCS_HOST_CR0                0x6C00
#define VMCS_HOST_CR3                0x6C02
#define VMCS_HOST_CR4                0x6C04
#define VMCS_HOST_FS_BASE            0x6C06
#define VMCS_HOST_GS_BASE            0x6C08
#define VMCS_HOST_TR_BASE            0x6C0A
#define VMCS_HOST_GDTR_BASE          0x6C0C
#define VMCS_HOST_IDTR_BASE          0x6C0E
#define VMCS_HOST_SYSENTER_ESP       0x6C10
#define VMCS_HOST_SYSENTER_EIP       0x6C12
#define VMCS_HOST_RSP                0x6C14
#define VMCS_HOST_RIP                0x6C16

// read-only
#define VMCS_RO_VM_EXIT_REASON       0x4402
#define VMCS_RO_VM_EXIT_INTR_INFO    0x4404
#define VMCS_RO_VM_EXIT_INSTR_LEN    0x440C
#define VMCS_RO_EXIT_QUALIFICATION   0x6400
#define VMCS_RO_GUEST_PHYS_ADDR      0x2400
#define VMCS_RO_GUEST_LINEAR_ADDR    0x240A

// -- msrs ----------------------------------------------------------
#define IA32_FEATURE_CONTROL         0x0000003A
#define IA32_VMX_BASIC               0x00000480
#define IA32_VMX_PINBASED_CTLS       0x00000481
#define IA32_VMX_PROCBASED_CTLS      0x00000482
#define IA32_VMX_EXIT_CTLS           0x00000483
#define IA32_VMX_ENTRY_CTLS          0x00000484
#define IA32_VMX_CR0_FIXED0          0x00000486
#define IA32_VMX_CR0_FIXED1          0x00000487
#define IA32_VMX_CR4_FIXED0          0x00000488
#define IA32_VMX_CR4_FIXED1          0x00000489
#define IA32_VMX_PROCBASED_CTLS2     0x0000048B

// x2apic — used by fallback path when apicv not available
#define IA32_X2APIC_LVT_TIMER        0x00000832
#define IA32_X2APIC_TIMER_INITIAL    0x00000838
#define IA32_X2APIC_TIMER_CURRENT    0x00000839
#define IA32_X2APIC_TIMER_DIVIDE     0x0000083E

// -- cpuid ---------------------------------------------------------
#define CPUID_VMX_BIT                (1u << 5)
#define CPUID_HYPERVISOR_BIT         (1u << 31)

// -- control flags -------------------------------------------------

// pin-based (table 25-6)
#define PIN_BASED_EXT_INTR_EXITING       (1u << 0)
#define PIN_BASED_VIRTUAL_NMI            (1u << 5)
#define PIN_BASED_ACTIVATE_PREEMPT_TMR   (1u << 6)
#define PIN_BASED_PROCESS_POSTED_INTR    (1u << 7)

// primary proc-based (table 25-7)
#define CPU_BASED_USE_TSC_OFFSETTING  (1u << 3)
#define CPU_BASED_HLT_EXITING         (1u << 7)
#define CPU_BASED_INVLPG_EXITING      (1u << 9)
#define CPU_BASED_MWAIT_EXITING       (1u << 10)
#define CPU_BASED_RDPMC_EXITING       (1u << 11)
#define CPU_BASED_RDTSC_EXITING       (1u << 12)
#define CPU_BASED_CR3_LOAD_EXITING    (1u << 15)
#define CPU_BASED_CR3_STORE_EXITING   (1u << 16)
#define CPU_BASED_CR8_LOAD_EXITING    (1u << 19)
#define CPU_BASED_CR8_STORE_EXITING   (1u << 20)
#define CPU_BASED_TPR_SHADOW          (1u << 21)
#define CPU_BASED_NMI_WINDOW_EXITING  (1u << 22)
#define CPU_BASED_MOV_DR_EXITING      (1u << 23)
#define CPU_BASED_UNCOND_IO_EXITING   (1u << 24)
#define CPU_BASED_USE_IO_BITMAPS      (1u << 25)
#define CPU_BASED_MONITOR_TRAP_FLAG   (1u << 27)
#define CPU_BASED_USE_MSR_BITMAPS     (1u << 28)
#define CPU_BASED_MONITOR_EXITING     (1u << 29)
#define CPU_BASED_PAUSE_EXITING       (1u << 30)
#define CPU_BASED_ACTIVATE_SECONDARY  (1u << 31)

// secondary proc-based (table 25-7)
#define CPU_BASED2_VIRTUALIZE_APIC         (1u << 0)
#define CPU_BASED2_ENABLE_EPT              (1u << 1)
#define CPU_BASED2_DESC_TABLE_EXITING      (1u << 2)
#define CPU_BASED2_ENABLE_RDTSCP           (1u << 3)
#define CPU_BASED2_VIRTUALIZE_X2APIC       (1u << 4)
#define CPU_BASED2_ENABLE_VPID             (1u << 5)
#define CPU_BASED2_WBINVD_EXITING          (1u << 6)
#define CPU_BASED2_UNRESTRICTED_GUEST      (1u << 7)
#define CPU_BASED2_APIC_REG_VIRT           (1u << 8)
#define CPU_BASED2_VIRTUAL_INT_DELIVERY    (1u << 9)
#define CPU_BASED2_PAUSE_LOOP_EXITING      (1u << 10)
#define CPU_BASED2_RDRAND_EXITING          (1u << 11)
#define CPU_BASED2_ENABLE_INVPCID          (1u << 12)
#define CPU_BASED2_ENABLE_VMFUNC           (1u << 13)
#define CPU_BASED2_VMCS_SHADOWING          (1u << 14)
#define CPU_BASED2_ENABLE_XSAVES           (1u << 20)
#define CPU_BASED2_USE_TSC_SCALING         (1u << 22)

// exit/entry
#define VM_EXIT_HOST_ADDR_SPACE_SIZE  (1u << 9)
#define VM_ENTRY_IA32E_MODE           (1u << 9)

// -- exit reasons --------------------------------------------------
#define EXIT_REASON_EXCEPTION_NMI     0
#define EXIT_REASON_CPUID             10
#define EXIT_REASON_MOV_DR            26
#define EXIT_REASON_RDMSR             31
#define EXIT_REASON_WRMSR             32
#define EXIT_REASON_PAUSE             33
#define EXIT_REASON_APIC_ACCESS       44
#define EXIT_REASON_EOI               45
#define EXIT_REASON_EPT_VIOLATION     48
#define EXIT_REASON_PREEMPTION_TIMER  52
#define EXIT_REASON_SELF_IPI          55
#define EXIT_REASON_APIC_WRITE        56

#define EXCEPTION_UD                  6
#define EXCEPTION_GP                  13

// ept violation qual bits
#define EPT_QUAL_READ                 (1u << 0)
#define EPT_QUAL_WRITE                (1u << 1)
#define EPT_QUAL_FETCH                (1u << 2)

// vmfunc #ud qual bit
#define UD_QUAL_VMFUNC                (1u << 0)

// -- apicv vapic page layout (sdm vol 3 appendix c) -----------------
#define VAPIC_OFF_TPR                 0x080
#define VAPIC_OFF_PPR                 0x0A0
#define VAPIC_OFF_EOI                 0x0B0
#define VAPIC_OFF_SVR                 0x0F0
#define VAPIC_OFF_ISR_BASE            0x100
#define VAPIC_OFF_TMR_BASE            0x180
#define VAPIC_OFF_IRR_BASE            0x200
#define VAPIC_OFF_ESR                 0x280
#define VAPIC_OFF_LVT_TIMER           0x320
#define VAPIC_OFF_LVT_THERMAL         0x330
#define VAPIC_OFF_LVT_PERF            0x340
#define VAPIC_OFF_LVT_LINT0           0x350
#define VAPIC_OFF_LVT_LINT1           0x360
#define VAPIC_OFF_LVT_ERROR           0x370
#define VAPIC_OFF_TIMER_INITIAL       0x380
#define VAPIC_OFF_TIMER_CURRENT       0x390
#define VAPIC_OFF_TIMER_DIVIDE        0x3E0

// -- covert opcodes ------------------------------------------------
#define HV_OP_PEEK_HIDDEN             0x9A01
#define HV_OP_UNHIDE_RANGE            0x9A02
#define HV_OP_HIDE_RANGE              0x9A03
#define HV_OP_SET_TSC_FOG             0x9A04
#define HV_OP_QUERY_STATE             0x9A05

// -- ept structures ------------------------------------------------
typedef union _EPT_PML4E {
    ULONG64 Value;
    struct { ULONG64 Present:1, Rw:1, Us:1, Pwt:1, Pcd:1, A:1, Ign:1, Ps:1, Ign2:4, PhysAddr:40, Ign3:12; };
} EPT_PML4E, *PEPT_PML4E;

typedef union _EPT_PDPTE {
    ULONG64 Value;
    struct { ULONG64 Present:1, Rw:1, Us:1, Pwt:1, Pcd:1, A:1, Ign:1, Ps:1, Ign2:4, PhysAddr:40, Ign3:12; };
} EPT_PDPTE, *PEPT_PDPTE;

typedef union _EPT_PDE {
    ULONG64 Value;
    struct { ULONG64 Present:1, Rw:1, Us:1, Pwt:1, Pcd:1, A:1, Ign:1, Ps:1, Ign2:4, PhysAddr:40, Ign3:12; };
} EPT_PDE, *PEPT_PDE;

typedef union _EPT_PTE {
    ULONG64 Value;
    struct { ULONG64 Present:1, Rw:1, Us:1, Pwt:1, Pcd:1, A:1, Ign:1, Pat:1, Ign2:4, PhysAddr:40, Ign3:12; };
} EPT_PTE, *PEPT_PTE;

// -- guest regs ----------------------------------------------------
typedef struct _HV_GUEST_REGS {
    ULONG64 R15, R14, R13, R12, R11, R10, R9, R8;
    ULONG64 Rbp, Rdi, Rsi, Rdx, Rcx, Rbx, Rax;
} HV_GUEST_REGS, *PHV_GUEST_REGS;

// -- state ---------------------------------------------------------
typedef struct _HV_HIDDEN_RANGE {
    ULONG64 PhysStart;
    ULONG64 Size;
} HV_HIDDEN_RANGE;

typedef struct _HV_SHADOW {
    ULONG64 OrigPhys;
    ULONG64 ShadowPhys;
    PVOID   ShadowVirt;
} HV_SHADOW;

// posted-interrupt descriptor (64-byte aligned, sdm vol 3 29.6)
typedef union _HV_PI_DESC {
    struct {
        ULONG64 Pir;
        ULONG64 PirUpper;
        ULONG64 PirUpper2;
        ULONG64 PirUpper3;
        ULONG   Outstanding;
        ULONG   Vector;
        ULONG   Reserved;
        ULONG   Reserved2;
    };
    UCHAR Pad[64];
} HV_PI_DESC, *PHV_PI_DESC;

// apic timer state (unified between apicv and fallback paths)
typedef struct _HV_APIC_TIMER {
    ULONG64 StartGuestTsc;
    ULONG64 DeadlineTsc;
    ULONG   Count;
    ULONG   Divide;
    ULONG   Vector;
    BOOLEAN Enabled;
    BOOLEAN Periodic;
} HV_APIC_TIMER;

typedef struct _HV_CAPS {
    BOOLEAN HasPle;
    BOOLEAN HasTscScaling;
    BOOLEAN HasMovDr;
    BOOLEAN HasVirtualNmi;
    BOOLEAN HasVpid;
    BOOLEAN HasApicv;
    BOOLEAN HasApicRegVirt;
    BOOLEAN HasVirtIntDelivery;
    BOOLEAN HasX2ApicVirt;
    BOOLEAN HasPostedIntr;
} HV_CAPS;

typedef struct _HV_CPU_CONTEXT_EX {
    PVOID   Self;
    ULONG   CpuIndex;
    USHORT  Vpid;
    BOOLEAN Launched;

    PVOID   VmxonRegion;   ULONG64 VmxonPhys;
    PVOID   VmcsRegion;    ULONG64 VmcsPhys;
    PVOID   MsrBitmap;     ULONG64 MsrBitmapPhys;
    PVOID   EptPml4;       ULONG64 EptPml4Phys;
    PVOID   HostStack;     ULONG64 HostStackPhys;

    // apicv
    PVOID   VapicPage;         ULONG64 VapicPhys;
    PVOID   ApicAccessPage;    ULONG64 ApicAccessPhys;
    PVOID   PostedIntrDesc;    ULONG64 PostedIntrDescPhys;

    HV_HIDDEN_RANGE Hidden[HV_MAX_HIDDEN_RANGES];
    ULONG HiddenCount;

    HV_SHADOW Shadows[HV_MAX_SHADOWS];
    ULONG ShadowCount;

    ULONG   TscFogStrength;
    ULONG64 TscBaseOffset;

    HV_APIC_TIMER ApicTimer;
    HV_CAPS Caps;
} HV_CPU_CONTEXT_EX, *PHV_CPU_CONTEXT_EX;

typedef HV_CPU_CONTEXT_EX hvctx;

extern HV_CPU_CONTEXT_EX g_HvEx[HV_MAX_CPUS];
extern ULONG g_HvCpuCount;

#ifdef __cplusplus
extern "C" {
#endif

    UCHAR    HvAsmReadTr(VOID);
    UCHAR    HvAsmReadLdtr(VOID);
    USHORT   HvAsmReadCs(VOID);
    USHORT   HvAsmReadDs(VOID);
    USHORT   HvAsmReadSs(VOID);
    USHORT   HvAsmReadEs(VOID);
    USHORT   HvAsmReadFs(VOID);
    USHORT   HvAsmReadGs(VOID);
    VOID     HvAsmReadGdtr(PVOID buffer);
    NTSTATUS HvAsmLaunch(PHV_CPU_CONTEXT_EX ctx);
    VOID     HvAsmOff(VOID);
    VOID     HvVmExitEntry(VOID);
    VOID     HvAsmInveptSingle(ULONG64 eptp);
    VOID     HvAsmInveptVpid(USHORT vpid);

#ifdef __cplusplus
}
#endif

VOID     HvVmExitDispatch(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx);
NTSTATUS HvInitializeCpu(ULONG cpuIndex);
VOID     HvTeardownCpu(ULONG cpuIndex);

NTSTATUS HvSetupVmcs(PHV_CPU_CONTEXT_EX ctx);
NTSTATUS HvEptInit(PHV_CPU_CONTEXT_EX ctx);
NTSTATUS HvEptHideRange(PHV_CPU_CONTEXT_EX ctx, ULONG64 physStart, ULONG64 size);
NTSTATUS HvEptUnhideRange(PHV_CPU_CONTEXT_EX ctx, ULONG64 physStart, ULONG64 size);
BOOLEAN  HvEptIsHidden(PHV_CPU_CONTEXT_EX ctx, ULONG64 physAddr);
PEPT_PTE HvEptWalk(PHV_CPU_CONTEXT_EX ctx, ULONG64 phys, BOOLEAN alloc);

NTSTATUS HvMsrSetup(PHV_CPU_CONTEXT_EX ctx);
BOOLEAN  HvMsrFakeRead(ULONG msr, PULONG64 value);

VOID HvTscTick(PHV_CPU_CONTEXT_EX ctx);

// apic (fallback path — used when apicv unavailable)
VOID    HvApicTimerInit(PHV_CPU_CONTEXT_EX ctx);
VOID    HvApicTimerReadMsr(PHV_CPU_CONTEXT_EX ctx, ULONG msr, PULONG64 out);
BOOLEAN HvApicTimerWriteMsr(PHV_CPU_CONTEXT_EX ctx, ULONG msr, ULONG64 value);
VOID    HvApicTimerTick(PHV_CPU_CONTEXT_EX ctx);

// apicv (full path — used when hardware supports it)
NTSTATUS HvApicvInit(PHV_CPU_CONTEXT_EX ctx);
VOID     HvApicvTeardown(PHV_CPU_CONTEXT_EX ctx);
VOID     HvHandleApicAccess(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx, ULONG64 qual);
VOID     HvHandleApicWrite(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx, ULONG64 qual);
VOID     HvHandleEoi(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx, ULONG64 qual);
VOID     HvHandleSelfIpi(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx, ULONG64 qual);
VOID     HvApicvTimerTick(PHV_CPU_CONTEXT_EX ctx);
VOID     HvApicvInjectVector(PHV_CPU_CONTEXT_EX ctx, ULONG vector);
BOOLEAN  HvApicvPending(PHV_CPU_CONTEXT_EX ctx);

VOID HvHandleCpuid(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx);
VOID HvHandleMsrRead(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx);
VOID HvHandleMsrWrite(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx);
VOID HvHandleVmfuncUd(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx, ULONG64 qual);
VOID HvHandleEptViolation(PHV_GUEST_REGS regs, PHV_CPU_CONTEXT_EX ctx, ULONG64 qual);
VOID HvHandlePreemptionTimer(PHV_CPU_CONTEXT_EX ctx);

NTSTATUS HvDkomHideMulti(PHV_CPU_CONTEXT_EX ctx, PVOID listHead, PVOID entryToRemove);
NTSTATUS HvDkomRestoreAll(PHV_CPU_CONTEXT_EX ctx);