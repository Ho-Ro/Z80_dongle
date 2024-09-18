; test for IM2
stack	equ $100
	ld hl, counter
	ld sp, stack
	ld a,$55
	ld (hl),a
	ld c,$55
	out (c),0
	ld a,im2vector >> 8
	ld i,a ; high part of int vector
	im 2
	ei
	jr $ ; wait for interrupt

counter
	defs 1

	org $20 ; int routine
im2routine
	inc (hl)
	ei
	reti


	org $30 ; int vector
im2vector
	defw im2routine

	org $38 ; RST38 / IM 1 routine
	inc (hl)
	ei
	reti

	org $66 ; NMI routine
	dec (hl)
	ei
	retn
