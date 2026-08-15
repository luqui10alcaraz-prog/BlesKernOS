[BITS 32]

%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1
    jmp isr_common_stub
%endmacro

%macro IRQ 2
global irq%1
irq%1:
    push dword 0
    push dword %2
    jmp irq_common_stub
%endmacro

; WINE_SAFE_SEGMENT_GUARD
; registers_t: GS=0 FS=4 ES=8 DS=12 ... CS=60
%macro SANITIZE_USER16_SELECTOR 3
    mov eax, [esp + %1]
    and eax, 0xFFFF
    cmp eax, 0x23
    je %%valid
%if %3
    cmp eax, 0x33
    je %%valid
%endif
    ; Selectores dinámicos GDT Ring 3: índices 7..127, RPL=3, TI=0.
    cmp eax, 0x3B
    jb %%fallback
    cmp eax, 0x3FB
    ja %%fallback
    mov edx, eax
    and edx, 7
    cmp edx, 3
    jne %%fallback
%%valid:
    mov [esp + %1], eax
    jmp %%done
%%fallback:
    mov dword [esp + %1], %2
%%done:
%endmacro

%macro SANITIZE_RETURN_SEGMENTS 0
    mov eax, [esp + 60]
    and eax, 3
    cmp eax, 3
    je %%user_context

%%kernel_context:
    mov dword [esp + 0],  0x10
    mov dword [esp + 4],  0x10
    mov dword [esp + 8],  0x10
    mov dword [esp + 12], 0x10
    jmp %%done

%%user_context:
    ; Un CS plano (0x1B) identifica tareas nativas/Win32. En ellas DS, ES y
    ; GS siempre deben volver al segmento plano 0x23; FS puede ser 0x33 para
    ; el TEB. Antes se aceptaba cualquier selector dinamico de Win16 y una
    ; palabra de contexto alterada podia cargar en TinyGL un segmento corto,
    ; haciendo que el siguiente acceso de memoria terminara en #GP(0).
    mov eax, [esp + 60]
    and eax, 0xFFFF
    cmp eax, 0x1B
    jne %%user16_context
    mov dword [esp + 0],  0x23
    SANITIZE_USER16_SELECTOR 4, 0x23, 1
    mov dword [esp + 8],  0x23
    mov dword [esp + 12], 0x23
    jmp %%done

%%user16_context:
    ; Las tareas Win32 siguen usando 0x23/0x33. Win16 necesita conservar los
    ; selectores segmentados que el cargador NE reservó en la GDT.
    SANITIZE_USER16_SELECTOR 0,  0x23, 0
    SANITIZE_USER16_SELECTOR 4,  0x23, 1
    SANITIZE_USER16_SELECTOR 8,  0x23, 0
    SANITIZE_USER16_SELECTOR 12, 0x23, 0

%%done:
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_NOERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

global isr128
isr128:
    ; Keep the original user trap-gate semantics.  The BKL is acquired by the
    ; common stub; forcing IF=0 here changed blocking syscall behavior even
    ; before SMP was started.
    push dword 0
    push dword 128
    jmp syscall_common_stub

extern smp_kernel_enter_interrupt
extern smp_kernel_try_enter_timer
extern smp_kernel_exit_frame

extern isr_handler
isr_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call smp_kernel_enter_interrupt
    push esp
    call isr_handler
    add esp, 4
    mov esp, eax
    push esp
    call smp_kernel_exit_frame
    add esp, 4
    SANITIZE_RETURN_SEGMENTS

    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret

extern irq_handler
irq_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call smp_kernel_enter_interrupt
    push esp
    call irq_handler
    add esp, 4
    mov esp, eax
    push esp
    call smp_kernel_exit_frame
    add esp, 4
    SANITIZE_RETURN_SEGMENTS

    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret

extern syscall_handler
syscall_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Native syscalls are synchronized by the subsystem they touch. The old
    ; unconditional Big Kernel Lock made every API, render and file operation
    ; mutually exclusive even when their data was unrelated.
    push esp
    call syscall_handler
    add esp, 4
    mov esp, eax
    SANITIZE_RETURN_SEGMENTS

    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret


; Local APIC vectors use the same registers_t layout as PIC IRQs but do not
; route through irq_handler/PIC EOI.  The timer handler may return another
; task frame, exactly like IRQ0 on the BSP.
global lapic_timer_stub
lapic_timer_stub:
    push dword 0
    push dword 0xF0
    jmp lapic_timer_common_stub

global lapic_reschedule_stub
lapic_reschedule_stub:
    push dword 0
    push dword 0xF1
    jmp lapic_reschedule_common_stub

global lapic_spurious_stub
lapic_spurious_stub:
    push dword 0
    push dword 0xFF
    jmp lapic_spurious_common_stub

extern smp_lapic_timer_interrupt
extern smp_lapic_eoi
lapic_timer_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; The scheduler has its own short IRQ-safe spinlock. AP timers no longer
    ; touch the legacy kernel lock, so user code on different CPUs can keep
    ; running while another CPU performs a context switch.
    call smp_lapic_eoi
    push esp
    call smp_lapic_timer_interrupt
    add esp, 4
    mov esp, eax
    SANITIZE_RETURN_SEGMENTS
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret

extern smp_reschedule_interrupt
lapic_reschedule_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call smp_lapic_eoi
    push esp
    call smp_reschedule_interrupt
    add esp, 4
    mov esp, eax
    SANITIZE_RETURN_SEGMENTS
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret

extern smp_lapic_spurious_interrupt
lapic_spurious_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call smp_kernel_enter_interrupt
    push esp
    call smp_lapic_spurious_interrupt
    add esp, 4
    mov esp, eax
    push esp
    call smp_kernel_exit_frame
    add esp, 4
    SANITIZE_RETURN_SEGMENTS

    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret
