; VMX entry/exit assembly, invalidation wrappers and segment helpers.

.code

EXTERN HvVmExitDispatch:PROC

HvAsmReadTr PROC
    xor eax, eax
    str ax
    movzx eax, ax
    ret
HvAsmReadTr ENDP

HvAsmReadLdtr PROC
    xor eax, eax
    sldt ax
    movzx eax, ax
    ret
HvAsmReadLdtr ENDP

HvAsmReadCs PROC
    xor eax, eax
    mov ax, cs
    ret
HvAsmReadCs ENDP

HvAsmReadDs PROC
    xor eax, eax
    mov ax, ds
    ret
HvAsmReadDs ENDP

HvAsmReadSs PROC
    xor eax, eax
    mov ax, ss
    ret
HvAsmReadSs ENDP

HvAsmReadEs PROC
    xor eax, eax
    mov ax, es
    ret
HvAsmReadEs ENDP

HvAsmReadFs PROC
    xor eax, eax
    mov ax, fs
    ret
HvAsmReadFs ENDP

HvAsmReadGs PROC
    xor eax, eax
    mov ax, gs
    ret
HvAsmReadGs ENDP

HvAsmReadGdtr PROC
    sgdt [rcx]
    ret
HvAsmReadGdtr ENDP

HvAsmOff PROC
    vmxoff
    ret
HvAsmOff ENDP

; invept type 1 (single-context). descriptor must be 16-byte aligned.
HvAsmInveptSingle PROC
    push rbp
    mov rbp, rsp
    and rsp, -16
    sub rsp, 16
    mov [rsp], rcx
    mov qword ptr [rsp+8], 0
    mov rax, 1
    mov rdx, rsp
    invept rax, oword ptr [rdx]
    mov rsp, rbp
    pop rbp
    ret
HvAsmInveptSingle ENDP

; invept type 2 (single-vpid)
HvAsmInveptVpid PROC
    push rbp
    mov rbp, rsp
    and rsp, -16
    sub rsp, 16
    xor eax, eax
    mov ax, cx
    mov [rsp], rax
    mov qword ptr [rsp+8], 0
    mov rax, 2
    mov rdx, rsp
    invept rax, oword ptr [rdx]
    mov rsp, rbp
    pop rbp
    ret
HvAsmInveptVpid ENDP

; launch. sets guest rip to resume point, guest rsp to caller's return slot.
; on success, caller "returns" from HvAsmLaunch via resume path.
; on failure, returns error status via normal path.
HvAsmLaunch PROC
    push rbx
    push rsi
    push rdi

    mov rbx, rcx
    mov rsi, rsp

    lea rdx, [rsp + 8*3 + 8]
    mov rcx, 681Ch          ; GUEST_RSP
    vmwrite rcx, rdx

    lea rdx, [HvLaunchResume]
    mov rcx, 681Eh          ; GUEST_RIP
    vmwrite rcx, rdx

    vmlaunch

    ; launch failed
    mov eax, 0C0000001h
    mov rsp, rsi
    pop rdi
    pop rsi
    pop rbx
    ret

HvLaunchResume:
    xor eax, eax
    ret
HvAsmLaunch ENDP

; vmexit entry. 15 pushes + 0x28 shadow = 8+40 mod 16 = 0, so
; stack is 16-byte aligned when we call C. don't touch the sub value.
HvVmExitEntry PROC
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; ctx = gs:[0], set per-cpu via VMCS_HOST_GS_BASE
    mov rdx, qword ptr gs:[0]
    mov rcx, rsp
    sub rsp, 28h
    call HvVmExitDispatch
    add rsp, 28h

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    vmresume
    ud2
HvVmExitEntry ENDP

END