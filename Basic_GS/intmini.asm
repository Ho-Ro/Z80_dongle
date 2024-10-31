;==================================================================================
; Contents of this file are copyright Grant Searle
;
; You have permission to use this for NON COMMERCIAL USE ONLY
; If you wish to use it elsewhere, please include an acknowledgement to myself.
;
; http://searle.hostei.com/grant/index.html
;
; eMail: home.micros01@btinternet.com
;
; If the above don't work, please perform an Internet search to see if I have
; updated the web page hosting service.
;
;==================================================================================

BASICCOLD       .EQU    0100H
BASICWARM       .EQU    BASICCOLD + 3

RAMBEGIN        .EQU    2000H
RAMSIZE         .EQU    1A00H
basicStarted    .EQU    RAMBEGIN
TEMPSTACK       .EQU    RAMBEGIN + RAMSIZE ; Top of RAM

; Minimum 6850 ACIA polled serial I/O

DATA6850        .EQU    01H             ; ACIA 6850 Data Register
STAT6850        .EQU    02H             ; ACIA 6850 Status Register

; Control character

CR              .EQU    0DH
LF              .EQU    0AH
CS              .EQU    0CH             ; Clear screen

;------------------------------------------------------------------------------
; Reset - Here we go

                .ORG    0000

RST00:          DI                      ;Disable interrupts
                JP      INIT            ;Initialize Hardware and go

                .DC     0008H-$,0       ; Padding so RST8 starts at org 0x0008H

;------------------------------------------------------------------------------
; TX a character over RS232

                .ORG    0008H
TX:             OUT     (DATA6850),A    ; 6850 Transmit Data Register
                RET

                .DC     0010H-$,0       ; Padding so RST10 starts at org 0x0010H

;------------------------------------------------------------------------------
; RX a character over RS232 Channel A [Console], hold here until char ready.

                .ORG    0010H
RX:             RST     $18
                JR      Z,RX
                IN      A,(DATA6850)     ; 6850 Receive Data Register
                RET

                .DC     0018H-$,0

;------------------------------------------------------------------------------
; Check serial status

                .ORG    0018H
RST18:          IN      A,(STAT6850)    ; 6850 Status Register
                AND     1               ; 6850 Receive Data Register Full
                RET

                .DC     0020H-$,0

;------------------------------------------------------------------------------
; Output string
                .ORG 0020H
PRINT:          LD      A,(HL)          ; Get character
                OR      A               ; Is it $00 ?
                RET     Z               ; Then RETurn on terminator
                RST     TX              ; Print it
                INC     HL              ; Next Character
                JR      PRINT           ; Continue until $00
                RET

;------------------------------------------------------------------------------

INIT:
                LD      HL,TEMPSTACK    ; Temp stack
                LD      SP,HL           ; Set up a temporary stack

                ;LD      HL,SIGNON1      ; Sign-on message
                ;CALL    PRINT           ; Output string

                LD      A,(basicStarted); Check the BASIC STARTED flag
                CP      JP              ; to see if this is power-up
                JR      NZ,COLDSTART    ; If not BASIC started then always do cold start
                LD      HL,SIGNON2      ; Cold/warm message
                CALL    PRINT           ; Output string
CORW:
                RST     RX
                AND     11011111B       ; lower to uppercase
                CP      'C'
                JR      NZ, CHECKWARM
                RST     TX
                LD      A,0DH
                RST     TX
                LD      A,0AH
                RST     TX
COLDSTART:      LD      A,JP            ; Set the BASIC STARTED flag
                LD      (basicStarted),A
                JP      BASICCOLD       ; Start BASIC COLD
CHECKWARM:
                CP      'W'
                JR      NZ, CORW
                RST     TX
                LD      A,CR
                RST     TX
                LD      A,LF
                RST     TX
                JP      BASICWARM       ; Start BASIC WARM

;SIGNON1:        .DB     CS
;                .DB     "Z80 SBC By Grant Searle",CR,LF,0
SIGNON2:        .DB     CR,LF
                .DB     "(C)old or (W)arm start? ",0

                .DC     (BASICCOLD - $), 0

.END
