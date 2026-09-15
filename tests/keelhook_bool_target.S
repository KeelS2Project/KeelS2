.text
.globl KeelHookBoolFixtureTarget
.type KeelHookBoolFixtureTarget, @function
KeelHookBoolFixtureTarget:
    .byte 0xf3, 0x0f, 0x1e, 0xfa
    .rept 16
    nop
    .endr
    xorl %eax, %eax
    testb %dil, %dil
    setne %al
    # Only AL holds the Boolean result; higher return bits are unspecified.
    orl $0x5a5a5a00, %eax
    ret
.size KeelHookBoolFixtureTarget, .-KeelHookBoolFixtureTarget
.section .note.GNU-stack,"",@progbits
