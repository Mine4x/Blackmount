global context_switch

; void context_switch(uint32_t** old_esp, uint32_t* new_esp)
; Arguments:
;   [esp+4] = pointer to old task's ESP storage location
;   [esp+8] = new task's ESP value to load
context_switch:
    ; Save current task's context
    pushad                  ; Push all general purpose registers (EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI)
    pushfd                  ; Push EFLAGS register
    
    ; Get arguments
    mov eax, [esp + 36]     ; old_esp (36 = 32 bytes from pushad + 4 bytes from pushfd)
    mov ecx, [esp + 40]     ; new_esp
    
    ; Save old ESP
    mov [eax], esp          ; Store current ESP to *old_esp
    
    ; Switch to new task's stack
    mov esp, ecx            ; Load new ESP
    
    ; Restore new task's context
    popfd                   ; Restore EFLAGS
    popad                   ; Restore all general purpose registers
    
    ret                     ; Return to new task


; uint32_t* setup_task_stack(void* stack_top, void (*entry_point)(void))
global setup_task_stack
setup_task_stack:
    mov eax, [esp + 4]      ; stack_top
    mov ecx, [esp + 8]      ; entry_point
    
    ; Setup stack as if task was interrupted
    sub eax, 4
    mov dword [eax], 0x202  ; EFLAGS (IF=1, reserved bit=1)
    
    sub eax, 32             ; Space for pushad (8 registers * 4 bytes)
    mov dword [eax + 28], ecx  ; Set saved EIP to entry_point
    
    ; Zero out other registers
    mov dword [eax], 0      ; EDI
    mov dword [eax + 4], 0  ; ESI
    mov dword [eax + 8], 0  ; EBP
    ; [eax + 12] is ESP (ignored by popad)
    mov dword [eax + 16], 0 ; EBX
    mov dword [eax + 20], 0 ; EDX
    mov dword [eax + 24], 0 ; ECX
    ; [eax + 28] is EAX (already set to entry_point)
    
    ret                     ; Return stack pointer in EAX