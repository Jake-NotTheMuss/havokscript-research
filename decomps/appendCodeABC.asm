
; prefix code
push rbp
mov rbp, rsp

; allocate stack space
sub rsp, $48

; RDI - this
; ESI - op
; EDX - a
; ECX - b
; R8D - c

; save registers for new function call
mov [rbp - 16], rdi
mov [rbp - 20], esi
mov [rbp - 24], edx
mov [rbp - 28], ecx
mov [rbp - 32], r8d

; ???
mov rdi, [rbp - 16]
mov ecx, [rbp - 20]
mov esi, [rbp - 24]
mov edx, [rbp - 28]
mov r8d, [rbp - 32]

; save RDI for new function call
mov [rbp - 40], rdi
mov edi, ecx ; 
mov ecx, r8d
call BUILD_INSN_ABC

mov r9, [rbp - 40]
mov r10, [r9 + 24] ; this->m_stateInterface
mov r11, [r10] ; this->m_stateInterface->v_ptr$CompilerStateInterface
mov rdi, r10

mov [rbp - 44], eax ; inst = BUILD_INSN_ABC(op, a, b, c)

call [r11 + 48] ; getLastLine

mov rdi, [rbp - 40] ; restore this
mov esi, [rbp - 44] ; inst
mov edx, eax ; return value of getLastLine()
call appendCode

add rsp, $48
pop rbp
ret
