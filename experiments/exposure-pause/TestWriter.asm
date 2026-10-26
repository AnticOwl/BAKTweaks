EXTERN BakExposureWriterHook:PROC
.code
BakTestStore PROC
    movss dword ptr [rbx+164h], xmm1
    ret
BakTestStore ENDP
BakTestWrite PROC
    push rbx
    sub rsp, 20h
    mov rbx, rcx
    mov rax, 11223344h
    mov rdx, 55667788h
    movd r9d, xmm1
    cmp rax, rdx
    pushfq
    pop r10
    call BakTestStore
    pushfq
    pop r11
    cmp r10, r11
    jne failed
    cmp rax, 11223344h
    jne failed
    cmp rdx, 55667788h
    jne failed
    movd ecx, xmm1
    cmp ecx, r9d
    jne failed
    mov eax, 1
    jmp done
failed:
    xor eax, eax
done:
    add rsp, 20h
    pop rbx
    ret
BakTestWrite ENDP
END
