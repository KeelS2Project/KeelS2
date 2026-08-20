.text
.globl KeelHookLandingPadTarget
.type KeelHookLandingPadTarget, @function
KeelHookLandingPadTarget:
    .byte 0xF3, 0x0F, 0x1E, 0xFA
    leal 7(%rdi,%rdi,2), %eax
    ret
.size KeelHookLandingPadTarget, .-KeelHookLandingPadTarget

.globl KeelHookBranchTarget
.type KeelHookBranchTarget, @function
KeelHookBranchTarget:
    .byte 0xEB, 0x02, 0xEB, 0x00
    movl %edi, %eax
    ret
.size KeelHookBranchTarget, .-KeelHookBranchTarget

.globl KeelHookUnsupportedBranchTarget
.type KeelHookUnsupportedBranchTarget, @function
KeelHookUnsupportedBranchTarget:
    .byte 0xE3, 0x01, 0x90
    movl %edi, %eax
    ret
.size KeelHookUnsupportedBranchTarget, .-KeelHookUnsupportedBranchTarget

.globl KeelHookHazardLoop
.type KeelHookHazardLoop, @function
KeelHookHazardLoop:
    movb $1, (%rdi)
1:
    cmpb $0, (%rsi)
    je 1b
    ret
.globl KeelHookHazardLoopEnd
KeelHookHazardLoopEnd:
.size KeelHookHazardLoop, .-KeelHookHazardLoop
.section .note.GNU-stack,"",@progbits
