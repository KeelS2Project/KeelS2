PUBLIC KeelHookBranchTarget

.code

PUBLIC KeelHookLandingPadTarget

KeelHookLandingPadTarget PROC
    db 0F3h, 00Fh, 01Eh, 0FAh
    lea eax, [rcx+rcx*2+7]
    ret
KeelHookLandingPadTarget ENDP

KeelHookBranchTarget PROC
    db 0EBh, 02h, 0EBh, 00h
    mov eax, ecx
    ret
KeelHookBranchTarget ENDP

PUBLIC KeelHookUnsupportedBranchTarget

KeelHookUnsupportedBranchTarget PROC
    db 0E3h, 01h, 090h
    mov eax, ecx
    ret
KeelHookUnsupportedBranchTarget ENDP

PUBLIC KeelHookHazardLoop
PUBLIC KeelHookHazardLoopEnd

KeelHookHazardLoop PROC
    mov byte ptr [rcx], 1
KeelHookHazardLoopSpin:
    cmp byte ptr [rdx], 0
    je KeelHookHazardLoopSpin
    ret
KeelHookHazardLoop ENDP

KeelHookHazardLoopEnd LABEL BYTE

END
