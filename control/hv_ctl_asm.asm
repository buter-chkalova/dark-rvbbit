.code
EXTERN HvCtlG_State:QWORD

HvCtlVmfunc PROC
    push rbx
    push rsi
    push rdi

    mov rbx, rcx
    mov rsi, rdx
    mov rdi, r8
    mov [HvCtlG_State], r9

    mov eax, ebx
    mov rcx, rsi
    mov rdx, rdi
    vmfunc

    mov rcx, [HvCtlG_State]
    test rcx, rcx
    jz short done
    mov [rcx], rax
done:
    pop rdi
    pop rsi
    pop rbx
    xor eax, eax
    ret
HvCtlVmfunc ENDP
END