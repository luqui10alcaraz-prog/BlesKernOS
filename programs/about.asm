; ------------------------------------------------------------------
; About MichalOS
; ------------------------------------------------------------------

	%INCLUDE "michalos.inc"

start:
	mov bx, .footer_msg
	call draw_background

	call os_draw_logo
	
	mov16 dx, 2, 10
	call os_move_cursor
	mov si, osname
	call os_print_string
	
	mov16 dx, 0, 12
	call os_move_cursor
	mov si, .introtext0
	call os_print_string
	
	call os_hide_cursor
	
	call os_wait_for_key
	cmp al, ' '
	je .hall_of_fame
	cmp al, 'l'
	je .license
	
	ret

.hall_of_fame:
	mov bx, .footer_msg_hall
	call draw_background

	call os_draw_logo
	
	mov16 dx, 0, 10
	call os_move_cursor
	mov si, .hoftext0
	call os_print_string
	
	call os_hide_cursor
	
	call os_wait_for_key
	cmp al, ' '
	je start
	cmp al, 'l'
	je .license
	
	ret
	
.license:
	mov cl, 0

	call .draw_license

.licenseloop:
	call os_wait_for_key
	
	cmp al, ' '
	je .hall_of_fame
	cmp al, 'l'
	je start
	cmp ah, KEY_UP
	je .license_cur_up
	cmp ah, KEY_DOWN
	je .license_cur_down
	
	ret
	
.license_cur_down:
	cmp cl, 6
	je .licenseloop
	
	inc cl
	call .draw_license
	jmp .licenseloop
		
.license_cur_up:
	cmp cl, 0
	je .licenseloop
	
	dec cl
	call .draw_license
	jmp .licenseloop
		
.draw_license:
	mov bx, .footer_msg_lic
	push cx
	call draw_background
	pop cx
	
	mov si, .licensetext
	call print_text_wall
	ret
		
	.introtext0			db '  Bleskernos: Copyright (C) Bleskernos Team, 1980-1982', 13, 10
	.introtext1			db '  Fuente y logo: Copyright (C) Lucas, 1980-1982', 13, 10, 10
	.introtext2			db '  Si encuentras un error o tienes sugerencias, por favor abre un ticket', 13, 10
	.introtext3			db '  en la seccion correspondiente en SourceForge. Agradecemos tus comentarios.', 0

	.hoftext0			db '  Agradecimientos especiales a: (orden alfabetico)', 13, 10
	.hoftext1			db '    fanzyflani por portear Reality AdLib Tracker a NASM', 13, 10
	.hoftext2			db '    Ivan Ivanov por encontrar y ayudar a corregir errores', 13, 10
	.hoftext3			db '    Jasper Ziller por crear el juego Fisher', 13, 10
	.hoftext4			db '    Leonardo Ono por crear el juego Snake', 13, 10
	.hoftext5			db '    Mike Saunders por crear el sistema base - MikeOS :)', 13, 10
	.hoftext6			db '    Mis maravillosos compañeros por sus comentarios (y caza de bugs)', 13, 10
	.hoftext7			db '    REALITY por publicar el codigo de Reality AdLib Tracker en 1995', 13, 10
	.hoftext8			db '    Sebastian Mihai por crear y publicar el codigo de aSMtris', 13, 10
	.hoftext9			db '    ZeroKelvinKeyboard por crear TachyonOS y apps para MikeOS', 13, 10, 0

	.footer_msg			db '[Espacio] Ver creditos [L] Ver licencia', 0
	.footer_msg_hall		db '[Espacio] Volver [L] Ver licencia', 0
	.footer_msg_lic		db '[Espacio] Ver creditos [L] Volver [Arriba/Abajo] Scroll', 0

	.licensetext:		incbin "../misc/LICENSE"
							db 0
							
	%INCLUDE "../source/features/name.asm"

print_text_wall:
	pusha
;	mov al, cl
;	call os_print_2hex
	
	cmp cl, 0
	je .print_loop
	
.skip_loop:
	lodsb
	
	cmp al, 0
	je .exit
	
	cmp al, 10
	jne .skip_loop
	
	loop .skip_loop
	
.print_loop:
	lodsb
	cmp al, 0
	je .exit
	
	call os_putchar
	
	call os_get_cursor_pos
	cmp dh, 24
	jne .print_loop
	
.exit:
	popa
	ret
	
draw_background:
	mov ax, .title_msg
	mov cx, 7
	call os_draw_background
	ret
	
	.title_msg			db 'BlesKernOS Info', 0

; ------------------------------------------------------------------
