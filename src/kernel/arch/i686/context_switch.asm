; context_switch.asm - Context switching for i686
[bits 32]

global context_switch

; void context_switch(Context* old_ctx, Context* new_ctx)
; Context layout (offsets):
;   0: eax, 4: ebx, 8: ecx, 12: edx
;   16: esi, 20: edi, 24: esp, 28: ebp
;   32: eip, 36: eflags

context_switch:
    push ebp
    mov ebp, esp
    pushfd              ; Save flags first
    
    ; Save all registers to old context
    mov eax, [ebp+8]    ; Get old_ctx pointer
    
    mov [eax+4], ebx    ; Save EBX
    mov [eax+8], ecx    ; Save ECX
    mov [eax+12], edx   ; Save EDX
    mov [eax+16], esi   ; Save ESI
    mov [eax+20], edi   ; Save EDI
    
    ; Save ESP (before we pushed ebp and flags)
    mov ecx, ebp
    add ecx, 12         ; Skip return address, old ebp, and our args
    mov [eax+24], ecx   ; Save ESP
    
    mov ecx, [ebp]      ; Get caller's EBP
    mov [eax+28], ecx   ; Save EBP
    
    ; Save return address as EIP
    mov ecx, [ebp+4]    ; Get return address
    mov [eax+32], ecx   ; Save EIP
    
    ; Save EFLAGS
    pop ecx             ; Get flags we pushed earlier
    mov [eax+36], ecx   ; Save EFLAGS
    
    ; Now restore new context
    mov eax, [ebp+12]   ; Get new_ctx pointer
    
    ; Restore registers
    mov ebx, [eax+4]    ; Restore EBX
    mov ecx, [eax+8]    ; Restore ECX
    mov edx, [eax+12]   ; Restore EDX
    mov esi, [eax+16]   ; Restore ESI
    mov edi, [eax+20]   ; Restore EDI
    
    ; Restore ESP and EBP
    mov esp, [eax+24]   ; Restore ESP
    mov ebp, [eax+28]   ; Restore EBP
    
    ; Restore EFLAGS
    push dword [eax+36]
    popfd
    
    ; Jump to new EIP
    push dword [eax+32] ; Push new EIP as return address
    mov eax, [eax+0]    ; Restore EAX last
    
    ret                 ; Jump to new EIP