; Intercept the verified eight-byte UE3 exposure blend store.
; Preserve registers, flags and XMM state; override only the active player's caches.
EXTERN g_bakExposureLevelTarget:QWORD
EXTERN g_bakExposureCurrentTarget:QWORD
EXTERN g_bakExposureDesired:DWORD
EXTERN g_bakExposureWriterContinue:QWORD
.code
BakExposureWriterHook PROC
    pushfq
    push rax
    lea rax, [rbx+164h]
    cmp rax, qword ptr [g_bakExposureLevelTarget]
    je override_value
    cmp rax, qword ptr [g_bakExposureCurrentTarget]
    je override_value
    movss dword ptr [rbx+164h], xmm1
    jmp finished
override_value:
    push rdx
    mov edx, dword ptr [g_bakExposureDesired]
    mov dword ptr [rax], edx
    pop rdx
finished:
    pop rax
    popfq
    jmp qword ptr [g_bakExposureWriterContinue]
BakExposureWriterHook ENDP
END
