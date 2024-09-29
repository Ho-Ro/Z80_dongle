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

; Minimum 6850 ACIA interrupt driven serial I/O to run modified NASCOM Basic 4.7
; Full input buffering with incoming data hardware handshaking
; Handshake shows full before the buffer is totally filled to allow run-on from the sender

SER_BUFSIZE     .EQU    3FH
SER_FULLSIZE    .EQU    30H
SER_EMPTYSIZE   .EQU    5

serBuf          .EQU    02000H
serInPtr        .EQU    serBuf+SER_BUFSIZE
serRdPtr        .EQU    serInPtr+2
serBufUsed      .EQU    serRdPtr+2
basicStarted    .EQU    serBufUsed+1
TEMPSTACK       .EQU    020EDH          ; Top of BASIC line input buffer so is "free ram" when BASIC resets

CR              .EQU    0DH
LF              .EQU    0AH
CS              .EQU    0CH             ; Clear screen

                .ORG    0000
;------------------------------------------------------------------------------
; Reset

RST00:          DI                      ;Disable interrupts
                JP      INIT            ;Initialize Hardware and go

                .FILL   0008H-$,0       ; Padding so RST8 starts at org 0x0008H

;------------------------------------------------------------------------------
; TX a character over RS232

                .ORG    0008H
RST08:          OUT     (1),A
                RET

                .FILL   0010H-$,0       ; Padding so RST10 starts at org 0x0010H

;------------------------------------------------------------------------------
; RX a character over RS232 Channel A [Console], hold here until char ready.

                .ORG    0010H
RST10:          RST     $18
                JR      Z,RST10
                IN      A,(1)
                RET

                .FILL   0018H-$,0

;------------------------------------------------------------------------------
; Check serial status

                .ORG    0018H
RST18:          IN      A,(2)
                OR      A
                RET

;------------------------------------------------------------------------------
PRINT:          LD      A,(HL)          ; Get character
                OR      A               ; Is it $00 ?
                RET     Z               ; Then RETurn on terminator
                RST     08H             ; Print it
                INC     HL              ; Next Character
                JR      PRINT           ; Continue until $00
                RET
;------------------------------------------------------------------------------
INIT:
                LD      HL,TEMPSTACK    ; Temp stack
                LD      SP,HL           ; Set up a temporary stack
                LD      HL,serBuf
                LD      (serInPtr),HL
                LD      (serRdPtr),HL
                XOR     A               ;0 to accumulator
                LD      (serBufUsed),A

                IM      1               ; Interrupt-mode 1
                DI                      ; Disable interrupts
                LD      HL,SIGNON1      ; Sign-on message
                CALL    PRINT           ; Output string
                LD      A,(basicStarted); Check the BASIC STARTED flag
                CP      'Y'             ; to see if this is power-up
                JR      NZ,COLDSTART    ; If not BASIC started then always do cold start
                LD      HL,SIGNON2      ; Cold/warm message
                CALL    PRINT           ; Output string
CORW:
                RST     10H
                AND     11011111B       ; lower to uppercase
                CP      'C'
                JR      NZ, CHECKWARM
                RST     08H
                LD      A,0DH
                RST     08H
                LD      A,0AH
                RST     08H
COLDSTART:      LD      A,'Y'           ; Set the BASIC STARTED flag
                LD      (basicStarted),A
                JP      0150H           ; Start BASIC COLD
CHECKWARM:
                CP      'W'
                JR      NZ, CORW
                RST     08H
                LD      A,0DH
                RST     08H
                LD      A,0AH
                RST     08H
                JP      0153H           ; Start BASIC WARM

SIGNON1:        .DB     CS
                .DB     "Z80 SBC By Grant Searle",CR,LF,0
SIGNON2:        .DB     CR,LF
                .DB     "(C)old or (W)arm start?",0

                .FILL   (00150H - $), 0

.END
