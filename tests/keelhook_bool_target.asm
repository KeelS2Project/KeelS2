PUBLIC KeelHookBoolFixtureTarget
.code
KeelHookBoolFixtureTarget PROC
    db 0F3h, 00Fh, 01Eh, 0FAh
    REPT 16
    nop
    ENDM
    xor eax, eax
    test cl, cl
    setne al
    ; Only AL holds the Boolean result; higher return bits are unspecified.
    or eax, 05A5A5A00h
    ret
KeelHookBoolFixtureTarget ENDP
END
