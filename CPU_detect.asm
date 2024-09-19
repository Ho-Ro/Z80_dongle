; use undocumented out command
    ld c,0
    out (c),0   ; NMOS writes 00, CMOS writes FF

; detect CPU
; https://github.com/EtchedPixels/FUZIX/blob/master/Applications/util/cpuinfo-z80.S

    ld sp,$80
    ld bc,0x00ff
    push bc
    pop af      ; Flags is now 0xFF A is 0. Now play with XF and YF
    scf         ; Will give us 0 for NEC clones, 28 for Zilog
    nop
    push af
    pop bc
    ld a,c
    and $28
    out (1),a

    nop
    nop

; detect U880

    ld  hl,$ffff
    ld  bc,$0102
    scf
    outi
    jp  c,U880
; Not a U880 CPU
Z80:
    ld a,$80
    out (c),a
    halt

; A U880 CPU
U880:
    ld a,$88
    out (c),a
    halt
