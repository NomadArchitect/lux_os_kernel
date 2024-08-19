
; lux - a lightweight unix-like operating system
; Omar Elghoul, 2024

[bits 64]

; Wrappers for instructions that are not directly usable in higher-level code

section .text

global readCR0
align 16
readCR0:
    mov rax, cr0
    ret

global writeCR0
align 16
writeCR0:
    mov cr0, rdi
    ret

global readCR2
align 16
readCR2:
    mov rax, cr2
    ret

global readCR3
align 16
readCR3:
    mov rax, cr3
    ret

global writeCR3
align 16
writeCR3:
    mov cr3, rdi
    ret

global readCR4
align 16
readCR4:
    mov rax, cr4
    ret

global writeCR4
align 16
writeCR4:
    mov cr4, rdi
    ret

global loadGDT
align 16
loadGDT:
    lgdt [rdi]
    ret

global loadIDT
align 16
loadIDT:
    lidt [rdi]
    ret

global outb
align 16
outb:
    mov rdx, rdi
    mov rax, rsi
    out dx, al
    ret

global outw
align 16
outw:
    mov rdx, rdi
    mov rax, rsi
    out dx, ax
    ret

global outd
align 16
outd:
    mov rdx, rdi
    mov rax, rsi
    out dx, eax
    ret

global inb
align 16
inb:
    mov rdx, rdi
    in al, dx
    ret

global inw
align 16
inw:
    mov rdx, rdi
    in ax, dx
    ret

global ind
align 16
ind:
    mov rdx, rdi
    in eax, dx
    ret
