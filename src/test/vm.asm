public CheckVM
public ExecVMREAD
; vmcheck.asm
; ml64.exe /c vmcheck.asm
;option casemap:none

.code

; ----------------------------------------
; BOOLEAN ExecVMREAD(UINT64 field, UINT64* out)
; rcx = field
; rdx = out
; return:
;   al = 0 success
;   al != 0 fail
; ----------------------------------------

ExecVMREAD PROC

    vmread qword ptr [rdx], rcx

    setz al
    setb cl
    adc  al, cl

    ret

ExecVMREAD ENDP

; ----------------------------------------
; UINT64 CheckCpuidTiming()
; returns accumulated CPUID latency
; ----------------------------------------

CheckCpuidTiming PROC

    push rbx
    push rsi
    push rdi

    sub rsp, 40h

    xor rdi, rdi

    mov r10, 100000h

TimingLoop:

    ; t1
    rdtsc
    shl rdx, 20h
    or  rax, rdx

    mov r8, rax

    ; cpuid storm
    xor ecx, ecx
    mov eax, 1
    cpuid

    ; t2
    rdtsc
    shl rdx, 20h
    or  rax, rdx

    sub rax, r8

    add rdi, rax

    sub r10, 1
    jnz TimingLoop

    mov rax, rdi

    add rsp, 40h

    pop rdi
    pop rsi
    pop rbx

    ret

CheckCpuidTiming ENDP

; ----------------------------------------
; INT CheckVM()
;
; return:
;   eax = 1 => vmx/root/hv behavior
;   eax = 0 => exception/no vmx
; ----------------------------------------

CheckVM PROC

    sub rsp, 28h

    xor eax, eax
    mov [rsp+20h], rax

    ; try vmread
    lea rdx, [rsp+20h]
    xor ecx, ecx

    call ExecVMREAD

    ; success path
    xor eax, eax

    add rsp, 28h
    ret

CheckVM ENDP

END