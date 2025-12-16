[BITS 16]
[ORG 0x100]  ; Dirección típica donde BleskernOS carga programas

start:
    cli                     ; Deshabilitar interrupciones
    
    ; Cargar la GDT
    lgdt [gdt_descriptor]
    
    ; Habilitar modo protegido
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    
    ; Saltar a código de 32 bits
    jmp CODE_SEG:init_pm

[BITS 32]
init_pm:
    ; Configurar segmentos de datos
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Configurar la pila
    mov ebp, 0x90000
    mov esp, ebp
    
    ; Mensaje de éxito en modo protegido
    mov ebx, msg_pm
    call print_string_pm
    
    ; Aquí puedes agregar tu código de 32 bits
    
    jmp $  ; Bucle infinito

; Rutina para imprimir en modo protegido
print_string_pm:
    pusha
    mov edx, 0xB8000  ; Memoria de video en modo protegido
    
.loop:
    mov al, [ebx]
    mov ah, 0x0F      ; Atributo: blanco sobre negro
    
    cmp al, 0
    je .done
    
    mov [edx], ax
    add ebx, 1
    add edx, 2
    
    jmp .loop
    
.done:
    popa
    ret

; Datos
msg_pm db "Modo protegido de 32 bits activado!", 0

; Definición de la GDT
gdt_start:
    ; Descriptor nulo (obligatorio)
    gdt_null:
        dd 0
        dd 0
    
    ; Segmento de código
    gdt_code:
        dw 0xFFFF       ; Límite (bits 0-15)
        dw 0x0000       ; Base (bits 0-15)
        db 0x00         ; Base (bits 16-23)
        db 10011010b    ; Flags (Presente, Privilegio 0, Segmento de código, Lectura/ejecución)
        db 11001111b    ; Flags + Límite (bits 16-19)
        db 0x00         ; Base (bits 24-31)
    
    ; Segmento de datos
    gdt_data:
        dw 0xFFFF       ; Límite (bits 0-15)
        dw 0x0000       ; Base (bits 0-15)
        db 0x00         ; Base (bits 16-23)
        db 10010010b    ; Flags (Presente, Privilegio 0, Segmento de datos, Lectura/escritura)
        db 11001111b    ; Flags + Límite (bits 16-19)
        db 0x00         ; Base (bits 24-31)
    
gdt_end:

; Descriptor de la GDT
gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; Tamaño de la GDT
    dd gdt_start                ; Dirección de la GDT

; Definiciones de segmentos
CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start