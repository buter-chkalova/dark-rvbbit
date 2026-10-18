# Development build notes for the Windows research prototype.
# Intended for VS 2022 and WDK 10.0.22621+. Reproducibility is unverified.

WDK    = C:/Program Files (x86)/Windows Kits/10
KM     = $(WDK)/Include/km
WDM    = $(WDK)/Include/wdm
UCRT   = $(WDK)/Include/10.0.22621.0/ucrt

CC     = cl.exe
AS     = ml64.exe
LNK    = link.exe

# /GS- no stack cookies in kernel. /Gs9999999 kills chkstk which we don't link.
# /wd4201 nameless structs (ept unions), /wd4200 zero-sized arrays.
CFLAGS_K = /nologo /c /O2 /GS- /Gs9999999 /Zp8 /W3 /wd4201 /wd4200 \
           /D_AMD64_ /DAMD64 /DWIN32_LEAN_AND_MEAN /DNTSYSTEM /D_WIN64=1 \
           /kernel /I$(WDM) /I$(KM) /I.

CFLAGS_U = /nologo /c /O2 /W3 /D_AMD64_ /DAMD64 /DWIN32_LEAN_AND_MEAN \
           /D_UNICODE /DUNICODE /I$(UCRT)

DRV_OBJ  = hv_main.obj hv_vmx.obj hv_exit.obj hv_ept.obj hv_msr.obj hv_tsc.obj hv_apic.obj hv_apicv.obj hv_dkom.obj hv_asm.obj
CTL_OBJ  = hv_ctl.obj hv_ctl_asm.obj

all: dark_rvbbit.sys dark_rvbbit_ctl.sys rvbbitctl.exe

dark_rvbbit.sys: $(DRV_OBJ)
	$(LNK) /nologo /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry /OUT:$@ \
	       /MACHINE:X64 /NODEFAULTLIB $(DRV_OBJ) ntoskrnl.lib hal.lib

dark_rvbbit_ctl.sys: $(CTL_OBJ)
	$(LNK) /nologo /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry /OUT:$@ \
	       /MACHINE:X64 /NODEFAULTLIB $(CTL_OBJ) ntoskrnl.lib

rvbbitctl.exe: tool/rvbbitctl.c
	$(CC) $(CFLAGS_U) /Fo$@ tool/rvbbitctl.c
	$(LNK) /nologo /SUBSYSTEM:CONSOLE /OUT:$@ /MACHINE:X64 $@ kernel32.lib

{driver}.c{}.obj:
	$(CC) $(CFLAGS_K) $<

{control}.c{}.obj:
	$(CC) $(CFLAGS_K) $<

{driver}.asm{}.obj:
	$(AS) /c /Fo$@ $<

{control}.asm{}.obj:
	$(AS) /c /Fo$@ $<

clean:
	del *.obj *.sys *.exe 2>nul

.PHONY: all clean