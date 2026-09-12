; Preserve the verified native replay command register without relying on a C++ prologue.
; Windows x64 ABI: original six DrawIndexedInstanced arguments, return address, caller R15.
EXTERN VortexDrawIndexedInstancedDispatch:PROC
PUBLIC VortexDrawIndexedInstancedShim
.code
VortexDrawIndexedInstancedShim PROC FRAME
    sub rsp, 58h
    .allocstack 58h
    .endprolog
    mov eax, DWORD PTR [rsp+80h]
    mov DWORD PTR [rsp+20h], eax
    mov eax, DWORD PTR [rsp+88h]
    mov DWORD PTR [rsp+28h], eax
    mov rax, QWORD PTR [rsp+58h]
    mov QWORD PTR [rsp+30h], rax
    mov QWORD PTR [rsp+38h], r15
    call VortexDrawIndexedInstancedDispatch
    add rsp, 58h
    ret
VortexDrawIndexedInstancedShim ENDP
END
