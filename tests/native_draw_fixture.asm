; Offline fixture emulates only the validated replay register/argument contract.
EXTERN VortexDrawIndexedInstancedShim:PROC
PUBLIC VortexTestReplay
PUBLIC VortexTestReplayReturn
.code
VortexTestReplay PROC FRAME
    push r15
    .pushreg r15
    sub rsp, 30h
    .allocstack 30h
    .endprolog
    mov r15, QWORD PTR [rsp+70h]
    mov eax, DWORD PTR [rsp+60h]
    mov DWORD PTR [rsp+20h], eax
    mov eax, DWORD PTR [rsp+68h]
    mov DWORD PTR [rsp+28h], eax
    call VortexDrawIndexedInstancedShim
VortexTestReplayReturn LABEL BYTE
    add rsp, 30h
    pop r15
    ret
VortexTestReplay ENDP
END
