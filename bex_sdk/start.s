section .text
global _start
extern main

_start:
    ; Call the C main function
    call main

    ; If main returns, invoke sys_exit to return control to Kernel
    mov eax, 2  ; sys_exit syscall number
    int 0x80

    ; Fallback halt if exit fails
.hang:
    hlt
    jmp .hang

global sys_print
sys_print:
    push ebp
    mov ebp, esp
    push ebx

    mov eax, 1         ; sys_print syscall number
    mov ebx, [ebp+8]   ; First argument (char* string)
    int 0x80

    pop ebx
    pop ebp
    ret

global sys_popup
sys_popup:
    push ebp
    mov ebp, esp
    push ebx

    mov eax, 3         ; sys_popup syscall number
    mov ebx, [ebp+8]   ; First argument (char* string)
    int 0x80

    pop ebx
    pop ebp
    ret

global sys_window_create
sys_window_create:
    push ebp
    mov ebp, esp
    push ebx
    push esi

    mov eax, 4         ; sys_window_create
    mov ebx, [ebp+8]   ; w
    mov ecx, [ebp+12]  ; h
    mov edx, [ebp+16]  ; title
    mov esi, [ebp+20]  ; buffer
    int 0x80

    pop esi
    pop ebx
    pop ebp
    ret

global sys_window_update
sys_window_update:
    mov eax, 5         ; sys_window_update
    int 0x80
    ret

global sys_get_event
sys_get_event:
    push ebp
    mov ebp, esp
    push ebx

    mov eax, 6         ; sys_get_event
    mov ebx, [ebp+8]   ; &x
    mov ecx, [ebp+12]  ; &y
    mov edx, [ebp+16]  ; &clicked
    int 0x80

    pop ebx
    pop ebp
    ret

global sys_exit
sys_exit:
    mov eax, 2         ; sys_exit syscall number
    int 0x80
    ret