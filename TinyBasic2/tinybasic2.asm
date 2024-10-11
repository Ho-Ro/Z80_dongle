;        .Z80
;Modified Nov 1 2016 by Donn Stewart for use in CPUville Z80 computer
;Changed UART (ACIA) port numbers to 3 for status, 2 for data in INIT, CHKIO, OUTC
;Status bit for read in CHKIO changed to 0x02
;Status bit for write in OUTC (actually OC3) changed to 0x01
;Changed UART initialization parameters in INIT
;Changed ORG statements at end of file to match system with 2K RAM
;
;2024-10-11 Ho-Ro:
;Automatically converted from 8080 syntax to Z80 syntax:
;https://hc-ddr.hucki.net/wiki/doku.php/cpm/8080_z80
;Modified for use with Z80 dongle simulator
;UART data port 1
;UART status port 2
;New:
;Case insensitive input
;PEEK and DEEK
;PRINT modifier for hex out: PRINT %16,..
;Hex numbers: $xxxx

;*************************************************************
;
;                 TINY BASIC FOR INTEL 8080
;                       VERSION 2.0
;                     BY LI-CHEN WANG
;                  MODIFIED AND TRANSLATED
;                    TO INTEL MNEMONICS
;                     BY ROGER RAUSKOLB
;                      10 OCTOBER,1976
;                        @COPYLEFT
;                   ALL WRONGS RESERVED
;
;*************************************************************
;
; *** ZERO PAGE SUBROUTINES ***
;
; THE 8080 INSTRUCTION SET LETS YOU HAVE 8 ROUTINES IN LOW
; MEMORY THAT MAY BE CALLED BY RST N, N BEING 0 THROUGH 7.
; THIS IS A ONE BYTE INSTRUCTION AND HAS THE SAME POWER AS
; THE THREE BYTE INSTRUCTION CALL LLHH.  TINY BASIC WILL
; USE RST 0 AS START AND RST 1 THROUGH RST 7 FOR
; THE SEVEN MOST FREQUENTLY USED SUBROUTINES.
; TWO OTHER SUBROUTINES (CRLF AND TSTNUM) ARE ALSO IN THIS
; SECTION.  THEY CAN BE REACHED ONLY BY 3-BYTE CALLS.
;

; Memory map
ROMBGN          .EQU            $0000           ; Execution must start here
RAMBGN          .EQU            $0800           ; 2K ROM
RAMSZE          .EQU            $1800           ; 6K RAM

; IO map
IODATA          .EQU            1
IOSTAT          .EQU            2
IO_RX_BIT       .EQU            $01

; Control character
BS              .EQU            08H             ; ^H, BACKSPACE
CR              .EQU            0DH             ; ^M, CARRIAGE RETURN
LF              .EQU            0AH             ; ^J, LINE FEED
CAN             .EQU            18H             ; ^X, CANCEL
DEL             .EQU            7FH             ; DELETE

; Store a label address as BIG endian with bit A15 set
#DEFINE DWA(LABEL) .DB (LABEL >> 8) | $80 \ .DB (LABEL & $FF)

; if CHAR = A THEN JUMP RELATIVE TO LABEL
#DEFINE TSTC(CHAR,LABEL) RST RTSTC \ .DB CHAR \ .DB LABEL-$-1


.ORG            ROMBGN

START:          LD      SP,STACK        ;*** COLD START ***
                LD      A,0FFH
                JP      INIT

RTSTC           .EQU    $       ;*** RST 1 @ $0008 ***
                EX      (SP),HL
                RST     RIGNBLK ;IGNORE BLANKS AND
                CP      (HL)    ;TEST CHARACTER
                JP      TC1     ;REST OF THIS IS AT TC1

CRLF:           LD      A,CR    ;*** CRLF ***
;
ROUTC           .EQU    $               ;*** RST 2 @ $0010 ***
OUTC:           OUT     (IODATA),A      ;Out to data port
                CP      CR      ;WAS IT CR?
                RET     NZ      ;NO, FINISHED
                JP      OC1     ;REST OF THIS IS AT OC1

REXPR           .EQU    $       ;*** RST 3 @ $0018 ***
EXPR:           CALL    EXPR2
                PUSH    HL      ;EVALUATE AN EXPRESSION
                JP      EXPR1   ;REST OF IT AT EXPR1
.DB             "W"

RCOMP           .EQU    $       ;*** RST 4 @ $0020 ***
COMP:           LD      A,H
                CP      D       ;COMPARE HL WITH DE
                RET     NZ      ;RETURN CORRECT C AND
                LD      A,L     ;Z FLAGS
                CP      E       ;BUT OLD A IS LOST
                RET
.DB             "AN"

RIGNBLK         .EQU    $       ;*** RST 5 @ $0028 ***
IGNBLK:         LD      A,(DE)
                CP      20H     ;IGNORE BLANKS
                RET     NZ      ;IN TEXT (WHERE DE->)
                INC     DE      ;AND RETURN THE FIRST
                JP      IGNBLK  ;NON-BLANK CHAR. IN A

RFINISH         .EQU    $       ;*** RST 6 @ $0030 ***
FINISH:         POP     AF
                CALL    FIN     ;CHECK END OF COMMAND
                JP      QWHAT   ;PRINT "WHAT?" IF WRONG
.DB             "G"

RTSTV           .EQU    $       ;*** RST 7 @ $0038 ***
TSTV:           RST     RIGNBLK ;IGNBLK
                SUB     40H     ;TEST VARIABLES
                RET     C       ;C:NOT A VARIABLE
                JP      NZ,TV1  ;NOT "@" ARRAY
                INC     DE      ;IT IS THE "@" ARRAY
                CALL    PARN    ;@ SHOULD BE FOLLOWED
                ADD     HL,HL   ;BY (EXPR) AS ITS INDEX
                JP      C,QHOW  ;IS INDEX TOO BIG (>0x7FFF)?
                PUSH    DE      ;WILL IT OVERWRITE
                EX      DE,HL   ;TEXT?
                CALL    SIZE    ;FIND SIZE OF FREE
                RST     RCOMP   ;AND CHECK THAT
                JP      C,ASORRY        ;IF SO, SAY "SORRY"
                LD      HL,VARBGN       ;IF NOT GET ADDRESS
                CALL    SUBDE   ;OF @(EXPR) AND PUT IT
                POP     DE      ;IN HL (top-down from TXTEND)
                RET     ;C FLAG IS CLEARED
;
TV1:            CP      21H     ;>='a'?
                JP      C,TV2   ;NO
                SUB     20H     ;MAKE UPPER CASE
TV2:            CP      1BH     ;NOT @, IS IT A TO Z?
                CCF     ;IF NOT RETURN C FLAG
                RET     C
                INC     DE      ;IF A THROUGH Z
                LD      HL,VARBGN       ;COMPUTE ADDRESS OF
                RLCA    ;THAT VARIABLE
                ADD     A,L     ;AND RETURN IT IN HL
                LD      L,A     ;WITH C FLAG CLEARED
                LD      A,0
                ADC     A,H
                LD      H,A
                RET

;TSTC:          EX      (SP),HL ;*** TSTC OR RST 1 ***
;               RST  RIGNBLK    ;THIS IS AT LOC. 8
;               CMP     (HL)    ;AND THEN JUMP HERE
TC1:            INC     HL      ;COMPARE THE BYTE THAT
                JP      Z,TC2   ;FOLLOWS THE RST INST.
                PUSH    BC      ;WITH THE TEXT (DE->)
                LD      C,(HL)  ;IF NOT =, ADD THE 2ND
                LD      B,0     ;BYTE THAT FOLLOWS THE
                ADD     HL,BC   ;RST TO THE OLD PC
                POP     BC      ;I.E., DO A RELATIVE
                DEC     DE      ;JUMP IF NOT =
TC2:            INC     DE      ;IF =, SKIP THOSE BYTES
                INC     HL      ;AND CONTINUE
                EX      (SP),HL
                RET

TSTNUM:         LD      HL,0    ;*** TSTNUM ***
                LD      B,H     ;TEST IF THE TEXT IS
                RST     RIGNBLK ;A NUMBER
                CP      '$'     ;HEX NUMBER?
                JP      Z,TX1   ;YES
TN1:            CP      '0'     ;IF NOT, RETURN 0 IN
                RET     C       ;B AND HL
                CP      '9'+1   ;IF NUMBERS, CONVERT
                RET     NC      ;TO BINARY IN HL AND
                LD      A,0F0H  ;SET B TO # OF DIGITS
                AND     H       ;IF H>15, THERE IS NO
                JP      NZ,QHOW ;ROOM FOR NEXT DIGIT
                INC     B       ;B COUNTS # OF DIGITS
                PUSH    BC
                LD      B,H     ;HL=10*HL+(NEW DIGIT)
                LD      C,L
                ADD     HL,HL   ; 2*HL
                ADD     HL,HL   ; 4*HL
                ADD     HL,BC   ; 5*HL
                ADD     HL,HL   ;10*HL
                LD      A,(DE)  ;AND (DIGIT) IS FROM
                INC     DE      ;STRIPPING THE ASCII
                AND     0FH     ;CODE
                ADD     A,L
                LD      L,A
                LD      A,0
                ADC     A,H
                LD      H,A
                POP     BC
                LD      A,(DE)  ;DO THIS DIGIT AFTER
                JP      P,TN1   ;DIGIT. S SAYS OVERFLOW

TX1:            INC     DE      ;SKIP TO NEXT HEX
                LD      A,(DE)  ;GET HEX DIGIT
                CP      '0'     ;< '0'
                RET     C       ;ERROR
                CP      '9'+1   ;<= '9'
                JP      C,TX2   ;OK '0'..'9'
                CP      'A'     ;< 'A'
                RET     C       ;ERROR, >'9' && < 'A'
                AND     5FH     ;CONVERT ALPHA TO UPPER
                CP      'F'+1   ;> 'F'
                RET     NC      ;ERROR
                SUB     'A'-'0'-10      ;SKIP GAP '9' -> 'A'
TX2:            AND     0FH     ;GET HEX CODE 0..F
                PUSH    BC
                LD      B,A     ;SAVE HEX CODE
                LD      A,0F0H  ;IF H>15
                AND     H       ;THERE IS NO ROOM
                LD      A,B
                POP     BC
                JP      NZ,QHOW ;FOR NEXT DIGIT

                INC     B       ;B COUNTS # OF DIGITS
                ADD     HL,HL   ;2*HL
                ADD     HL,HL   ;4*HL
                ADD     HL,HL   ;8*HL
                ADD     HL,HL   ;16*HL
                OR      L       ;PUT HEX CODE INTO
                LD      L,A     ;THE 4 LSB OF HL
;MOV  A,H
;ORA  A
                JP      TX1     ;DIGIT AFTER DIGIT

QHOW:           PUSH    DE      ;*** ERROR "HOW?" ***
AHOW:           LD      DE,HOW
                JP      ERROR

HOW:            .DB             "HOW?"
.DB             CR

OK:             .DB             "OK"
.DB             CR

WHAT:           .DB             "WHAT?"
.DB             CR

SORRY:          .DB             "SORRY"
.DB             CR

;*************************************************************
;
; *** MAIN ***
;
; THIS IS THE MAIN LOOP THAT COLLECTS THE TINY BASIC PROGRAM
; AND STORES IT IN THE MEMORY.
;
; AT START, IT PRINTS OUT "(CR)OK(CR)", AND INITIALIZES THE
; STACK AND SOME OTHER INTERNAL VARIABLES.  THEN IT PROMPTS
; ">" AND READS A LINE.  IF THE LINE STARTS WITH A NON-ZERO
; NUMBER, THIS NUMBER IS THE LINE NUMBER.  THE LINE NUMBER
; (IN 16 BIT BINARY) AND THE REST OF THE LINE (INCLUDING CR)
; IS STORED IN THE MEMORY.  IF A LINE WITH THE SAME LINE
; NUMBER IS ALREADY THERE, IT IS REPLACED BY THE NEW ONE.  IF
; THE REST OF THE LINE CONSISTS OF A CR ONLY, IT IS NOT STORED
; AND ANY EXISTING LINE WITH THE SAME LINE NUMBER IS DELETED.
;
; AFTER A LINE IS INSERTED, REPLACED, OR DELETED, THE PROGRAM
; LOOPS BACK AND ASKS FOR ANOTHER LINE.  THIS LOOP WILL BE
; TERMINATED WHEN IT READS A LINE WITH ZERO OR NO LINE
; NUMBER; AND CONTROL IS TRANSFERED TO "DIRECT".
;
; TINY BASIC PROGRAM SAVE AREA STARTS AT THE MEMORY LOCATION
; LABELED "TXTBGN" AND ENDS AT "TXTEND".  WE ALWAYS FILL THIS
; AREA STARTING AT "TXTBGN", THE UNFILLED PORTION IS POINTED
; BY THE CONTENT OF A MEMORY LOCATION LABELED "TXTUNF".
;
; THE MEMORY LOCATION "CURRNT" POINTS TO THE LINE NUMBER
; THAT IS CURRENTLY BEING INTERPRETED.  WHILE WE ARE IN
; THIS LOOP OR WHILE WE ARE INTERPRETING A DIRECT COMMAND
; (SEE NEXT SECTION). "CURRNT" SHOULD POINT TO A 0.
;
RSTART:         LD      SP,STACK
ST1:            CALL    CRLF    ;AND JUMP TO HERE
                LD      DE,OK   ;DE->STRING
                SUB     A       ;A=0
                CALL    PRTSTG  ;PRINT STRING UNTIL CR
                LD      HL,ST2+1        ;LITERAL 0
                LD      (CURRNT),HL     ;CURRENT->LINE # = 0
ST2:            LD      HL,0
                LD      (LOPVAR),HL
                LD      (STKGOS),HL
ST3:            LD      A,'>'   ;PROMPT '>' AND
                CALL    GETLN   ;READ A LINE
                PUSH    DE      ;DE->END OF LINE
                LD      DE,BUFFER       ;DE->BEGINNING OF LINE
                CALL    TSTNUM  ;TEST IF IT IS A NUMBER
                RST     RIGNBLK
                LD      A,H     ;HL=VALUE OF THE # OR
                OR      L       ;0 IF NO # WAS FOUND
                POP     BC      ;BC->END OF LINE
                JP      Z,DIRECT
                DEC     DE      ;BACKUP DE AND SAVE
                LD      A,H     ;VALUE OF LINE # THERE
                LD      (DE),A
                DEC     DE
                LD      A,L
                LD      (DE),A
                PUSH    BC      ;BC,DE->BEGIN, END
                PUSH    DE
                LD      A,C
                SUB     E
                PUSH    AF      ;A=# OF BYTES IN LINE
                CALL    FNDLN   ;FIND THIS LINE IN SAVE
                PUSH    DE      ;AREA, DE->SAVE AREA
                JP      NZ,ST4  ;NZ:NOT FOUND, INSERT
                PUSH    DE      ;Z:FOUND, DELETE IT
                CALL    FNDNXT  ;FIND NEXT LINE
;DE->NEXT LINE
                POP     BC      ;BC->LINE TO BE DELETED
                LD      HL,(TXTUNF)     ;HL->UNFILLED SAVE AREA
                CALL    MVUP    ;MOVE UP TO DELETE
                LD      H,B     ;TXTUNF->UNFILLED AREA
                LD      L,C
                LD      (TXTUNF),HL     ;UPDATE
ST4:            POP     BC      ;GET READY TO INSERT
                LD      HL,(TXTUNF)     ;BUT FIRST CHECK IF
                POP     AF      ;THE LENGTH OF NEW LINE
                PUSH    HL      ;IS 3 (LINE # AND CR)
                CP      3       ;THEN DO NOT INSERT
                JP      Z,RSTART        ;MUST CLEAR THE STACK
                ADD     A,L     ;COMPUTE NEW TXTUNF
                LD      L,A
                LD      A,0
                ADC     A,H
                LD      H,A     ;HL->NEW UNFILLED AREA
                LD      DE,TXTEND       ;CHECK TO SEE IF THERE
                RST     RCOMP   ;COMP HL,DE - IS ENOUGH SPACE
                JP      NC,QSORRY       ;SORRY, NO ROOM FOR IT
                LD      (TXTUNF),HL     ;OK, UPDATE TXTUNF
                POP     DE      ;DE->OLD UNFILLED AREA
                CALL    MVDOWN
                POP     DE      ;DE->BEGIN, HL->END
                POP     HL
                CALL    MVUP    ;MOVE NEW LINE TO SAVE
                JP      ST3     ;AREA

;*************************************************************
;
; WHAT FOLLOWS IS THE CODE TO EXECUTE DIRECT AND STATEMENT
; COMMANDS.  CONTROL IS TRANSFERED TO THESE POINTS VIA THE
; COMMAND TABLE LOOKUP CODE OF 'DIRECT' AND 'EXEC' IN LAST
; SECTION.  AFTER THE COMMAND IS EXECUTED, CONTROL IS
; TRANSFERED TO OTHERS SECTIONS AS FOLLOWS:
;
; FOR 'LIST', 'NEW', AND 'STOP': GO BACK TO 'RSTART'
; FOR 'RUN': GO EXECUTE THE FIRST STORED LINE IF ANY, ELSE
; GO BACK TO 'RSTART'.
; FOR 'GOTO' AND 'GOSUB': GO EXECUTE THE TARGET LINE.
; FOR 'RETURN' AND 'NEXT': GO BACK TO SAVED RETURN LINE.
; FOR ALL OTHERS: IF 'CURRENT' -> 0, GO TO 'RSTART', ELSE
; GO EXECUTE NEXT COMMAND.  (THIS IS DONE IN 'FINISH'.)
;*************************************************************
;
; *** NEW *** STOP *** RUN (& FRIENDS) *** & GOTO ***
;
; 'NEW(CR)' SETS 'TXTUNF' TO POINT TO 'TXTBGN'
;
; 'STOP(CR)' GOES BACK TO 'RSTART'
;
; 'RUN(CR)' FINDS THE FIRST STORED LINE, STORE ITS ADDRESS (IN
; 'CURRENT'), AND START EXECUTE IT.  NOTE THAT ONLY THOSE
; COMMANDS IN TAB2 ARE LEGAL FOR STORED PROGRAM.
;
; THERE ARE 3 MORE ENTRIES IN 'RUN':
; 'RUNNXL' FINDS NEXT LINE, STORES ITS ADDR. AND EXECUTES IT.
; 'RUNTSL' STORES THE ADDRESS OF THIS LINE AND EXECUTES IT.
; 'RUNSML' CONTINUES THE EXECUTION ON SAME LINE.
;
; 'GOTO EXPR(CR)' EVALUATES THE EXPRESSION, FIND THE TARGET
; LINE, AND JUMP TO 'RUNTSL' TO DO IT.
;
NEW:            CALL    ENDCHK  ;*** NEW(CR) ***
                LD      HL,TXTBGN
                LD      (TXTUNF),HL
;
STOP:           CALL    ENDCHK  ;*** STOP(CR) ***
                JP      RSTART

RUN:            CALL    ENDCHK  ;*** RUN(CR) ***
                LD      DE,TXTBGN       ;FIRST SAVED LINE
;
RUNNXL:         LD      HL,0    ;*** RUNNXL ***
                CALL    FNDLP   ;FIND WHATEVER LINE #
                JP      C,RSTART        ;C:PASSED TXTUNF, QUIT
;
RUNTSL:         EX      DE,HL   ;*** RUNTSL ***
                LD      (CURRNT),HL     ;SET 'CURRENT'->LINE #
                EX      DE,HL
                INC     DE      ;BUMP PASS LINE #
                INC     DE
;
RUNSML:         CALL    CHKIO   ;*** RUNSML ***
                LD      HL,TAB2-1       ;FIND COMMAND IN TAB2
                JP      EXEC    ;AND EXECUTE IT

GOTO:           RST     REXPR   ;*** GOTO EXPR ***
                PUSH    DE      ;SAVE FOR ERROR ROUTINE
                CALL    ENDCHK  ;MUST FIND A CR
                CALL    FNDLN   ;FIND THE TARGET LINE
                JP      NZ,AHOW ;NO SUCH LINE #
                POP     AF      ;CLEAR THE PUSH DE
                JP      RUNTSL  ;GO DO IT
;
;*************************************************************
;
; *** LIST *** & PRINT ***
;
; LIST HAS TWO FORMS:
; 'LIST(CR)' LISTS ALL SAVED LINES
; 'LIST #(CR)' START LIST AT THIS LINE #
; YOU CAN STOP THE LISTING BY CONTROL C KEY
;
; PRINT COMMAND IS 'PRINT ....;' OR 'PRINT ....(CR)'
; WHERE '....' IS A LIST OF EXPRESIONS, FORMATS, BACK-
; ARROWS, AND STRINGS.  THESE ITEMS ARE SEPERATED BY COMMAS.
;
; A FORMAT IS A POUND SIGN FOLLOWED BY A NUMBER.  IT CONTROLS
; THE NUMBER OF SPACES THE VALUE OF A EXPRESION IS GOING TO
; BE PRINTED.  IT STAYS EFFECTIVE FOR THE REST OF THE PRINT
; COMMAND UNLESS CHANGED BY ANOTHER FORMAT.  IF NO FORMAT IS
; SPECIFIED, 6 POSITIONS WILL BE USED.
;
; A STRING IS QUOTED IN A PAIR OF SINGLE QUOTES OR A PAIR OF
; DOUBLE QUOTES.
;
; A BACK-ARROW MEANS GENERATE A (CR) WITHOUT (LF)
;
; A (CRLF) IS GENERATED AFTER THE ENTIRE LIST HAS BEEN
; PRINTED OR IF THE LIST IS A NULL LIST.  HOWEVER IF THE LIST
; ENDED WITH A COMMA, NO (CRLF) IS GENERATED.
;
LIST:           CALL    TSTNUM  ;TEST IF THERE IS A #
                CALL    ENDCHK  ;IF NO # WE GET A 0
                CALL    FNDLN   ;FIND THIS OR NEXT LINE
LS1:            JP      C,RSTART        ;C:PASSED TXTUNF
                CALL    PRTLN   ;PRINT THE LINE
                CALL    CHKIO   ;STOP IF HIT CONTROL-C
                CALL    FNDLP   ;FIND NEXT LINE
                JP      LS1     ;AND LOOP BACK

PRINT:          LD      C,8     ;C = # OF SPACES
                LD      A,10    ;DEFAULT BASE FOR PRTNUM
                LD      (PNBASE),A
                TSTC(';',PR2)   ;IF NULL LIST & ";"
                CALL    CRLF    ;GIVE CR-LF AND
                JP      RUNSML  ;CONTINUE SAME LINE
PR2:            TSTC(CR,PR0)    ;IF NULL LIST (CR)
                CALL    CRLF    ;ALSO GIVE CR-LF AND
                JP      RUNNXL  ;GO TO NEXT LINE
PR0:            TSTC('#',PR5)   ;ELSE IS IT FORMAT?
                RST     REXPR   ;YES, EVALUATE EXPR.
                LD      C,L     ;AND SAVE IT IN C
                JP      PR3     ;LOOK FOR MORE TO PRINT
PR5:            TSTC('%',PR1)   ;ELSE IS IT PRTNUM BASE?
                RST     REXPR   ;YES, EVALUATE EXPR.
                LD      A,L     ;AND STORE THE LOW PART
                LD      (PNBASE),A      ;IN PNBASE
                JP      PR3     ;LOOK FOR MORE TO PRINT
PR1:            CALL    QTSTG   ;OR IS IT A STRING?
                JP      PR8     ;IF NOT, MUST BE EXPR.
PR3:            TSTC($2C,PR6)   ;IF ",", GO FIND NEXT
                CALL    FIN     ;IN THE LIST.
                JP      PR0     ;LIST CONTINUES
PR6:            LD      A,10    ;END OF LIST REACHED
                LD      (PNBASE),A      ;RESET DEFAULT BASE
                CALL    CRLF    ;LIST ENDS WITH CRLF
                RST     RFINISH ;FINISH
PR8:            RST     REXPR   ;EVALUATE THE EXPR
                PUSH    BC
                CALL    PRTNUM  ;PRINT THE VALUE
                POP     BC
                JP      PR3     ;MORE TO PRINT?
;
;*************************************************************
;
; *** GOSUB *** & RETURN ***
;
; 'GOSUB EXPR;' OR 'GOSUB EXPR (CR)' IS LIKE THE 'GOTO'
; COMMAND, EXCEPT THAT THE CURRENT TEXT POINTER, STACK POINTER
; ETC. ARE SAVE SO THAT EXECUTION CAN BE CONTINUED AFTER THE
; SUBROUTINE 'RETURN'.  IN ORDER THAT 'GOSUB' CAN BE NESTED
; (AND EVEN RECURSIVE), THE SAVE AREA MUST BE STACKED.
; THE STACK POINTER IS SAVED IN 'STKGOS', THE OLD 'STKGOS' IS
; SAVED IN THE STACK.  IF WE ARE IN THE MAIN ROUTINE, 'STKGOS'
; IS ZERO (THIS WAS DONE BY THE "MAIN" SECTION OF THE CODE),
; BUT WE STILL SAVE IT AS A FLAG FOR NO FURTHER 'RETURN'S.
;
; 'RETURN(CR)' UNDOS EVERYTHING THAT 'GOSUB' DID, AND THUS
; RETURN THE EXECUTION TO THE COMMAND AFTER THE MOST RECENT
; 'GOSUB'.  IF 'STKGOS' IS ZERO, IT INDICATES THAT WE
; NEVER HAD A 'GOSUB' AND IS THUS AN ERROR.
;
GOSUB:          CALL    PUSHA   ;SAVE THE CURRENT "FOR"
                RST     REXPR   ;PARAMETERS
                PUSH    DE      ;AND TEXT POINTER
                CALL    FNDLN   ;FIND THE TARGET LINE
                JP      NZ,AHOW ;NOT THERE. SAY "HOW?"
                LD      HL,(CURRNT)     ;FOUND IT, SAVE OLD
                PUSH    HL      ;'CURRNT' OLD 'STKGOS'
                LD      HL,(STKGOS)
                PUSH    HL
                LD      HL,0    ;AND LOAD NEW ONES
                LD      (LOPVAR),HL
                ADD     HL,SP
                LD      (STKGOS),HL
                JP      RUNTSL  ;THEN RUN THAT LINE
RETURN:         CALL    ENDCHK  ;THERE MUST BE A CR
                LD      HL,(STKGOS)     ;OLD STACK POINTER
                LD      A,H     ;0 MEANS NOT EXIST
                OR      L
                JP      Z,QWHAT ;SO, WE SAY: "WHAT?"
                LD      SP,HL   ;ELSE, RESTORE IT
                POP     HL
                LD      (STKGOS),HL     ;AND THE OLD 'STKGOS'
                POP     HL
                LD      (CURRNT),HL     ;AND THE OLD 'CURRNT'
                POP     DE      ;OLD TEXT POINTER
                CALL    POPA    ;OLD "FOR" PARAMETERS
                RST     RFINISH ;AND WE ARE BACK HOME
;
;*************************************************************
;
; *** FOR *** & NEXT ***
;
; 'FOR' HAS TWO FORMS:
; 'FOR VAR=EXP1 TO EXP2 STEP EXP3' AND 'FOR VAR=EXP1 TO EXP2'
; THE SECOND FORM MEANS THE SAME THING AS THE FIRST FORM WITH
; EXP3=1.  (I.E., WITH A STEP OF +1.)
; TBI WILL FIND THE VARIABLE VAR, AND SET ITS VALUE TO THE
; CURRENT VALUE OF EXP1.  IT ALSO EVALUATES EXP2 AND EXP3
; AND SAVE ALL THESE TOGETHER WITH THE TEXT POINTER ETC. IN
; THE 'FOR' SAVE AREA, WHICH CONSISTS OF 'LOPVAR', 'LOPINC',
; 'LOPLMT', 'LOPLN', AND 'LOPPT'.  IF THERE IS ALREADY SOME-
; THING IN THE SAVE AREA (THIS IS INDICATED BY A NON-ZERO
; 'LOPVAR'), THEN THE OLD SAVE AREA IS SAVED IN THE STACK
; BEFORE THE NEW ONE OVERWRITES IT.
; TBI WILL THEN DIG IN THE STACK AND FIND OUT IF THIS SAME
; VARIABLE WAS USED IN ANOTHER CURRENTLY ACTIVE 'FOR' LOOP.
; IF THAT IS THE CASE, THEN THE OLD 'FOR' LOOP IS DEACTIVATED.
; (PURGED FROM THE STACK..)
;
; 'NEXT VAR' SERVES AS THE LOGICAL (NOT NECESSARILLY PHYSICAL)
; END OF THE 'FOR' LOOP.  THE CONTROL VARIABLE VAR. IS CHECKED
; WITH THE 'LOPVAR'.  IF THEY ARE NOT THE SAME, TBI DIGS IN
; THE STACK TO FIND THE RIGHT ONE AND PURGES ALL THOSE THAT
; DID NOT MATCH.  EITHER WAY, TBI THEN ADDS THE 'STEP' TO
; THAT VARIABLE AND CHECK THE RESULT WITH THE LIMIT.  IF IT
; IS WITHIN THE LIMIT, CONTROL LOOPS BACK TO THE COMMAND
; FOLLOWING THE 'FOR'.  IF OUTSIDE THE LIMIT, THE SAVE AREA
; IS PURGED AND EXECUTION CONTINUES.
;
FOR:            CALL    PUSHA   ;SAVE THE OLD SAVE AREA
                CALL    SETVAL  ;SET THE CONTROL VAR.
                DEC     HL      ;HL IS ITS ADDRESS
                LD      (LOPVAR),HL     ;SAVE THAT
                LD      HL,TAB5-1       ;USE 'EXEC' TO LOOK
                JP      EXEC    ;FOR THE WORD 'TO'
FR1:            RST     REXPR   ;EVALUATE THE LIMIT
                LD      (LOPLMT),HL     ;SAVE THAT
                LD      HL,TAB6-1       ;USE 'EXEC' TO LOOK
                JP      EXEC    ;FOR THE WORD 'STEP'
FR2:            RST     REXPR   ;FOUND IT, GET STEP
                JP      FR4
FR3:            LD      HL,1H   ;NOT FOUND, SET TO 1
FR4:            LD      (LOPINC),HL     ;SAVE THAT TOO
FR5:            LD      HL,(CURRNT)     ;SAVE CURRENT LINE #
                LD      (LOPLN),HL
                EX      DE,HL   ;AND TEXT POINTER
                LD      (LOPPT),HL
                LD      BC,0AH  ;DIG INTO STACK TO
                LD      HL,(LOPVAR)     ;FIND 'LOPVAR'
                EX      DE,HL
                LD      H,B
                LD      L,B     ;HL=0 NOW
                ADD     HL,SP   ;HERE IS THE STACK
.DB             3EH
FR7:            ADD     HL,BC   ;EACH LEVEL IS 10 DEEP
                LD      A,(HL)  ;GET THAT OLD 'LOPVAR'
                INC     HL
                OR      (HL)
                JP      Z,FR8   ;0 SAYS NO MORE IN IT
                LD      A,(HL)
                DEC     HL
                CP      D       ;SAME AS THIS ONE?
                JP      NZ,FR7
                LD      A,(HL)  ;THE OTHER HALF?
                CP      E
                JP      NZ,FR7
                EX      DE,HL   ;YES, FOUND ONE
                LD      HL,0H
                ADD     HL,SP   ;TRY TO MOVE SP
                LD      B,H
                LD      C,L
                LD      HL,0AH
                ADD     HL,DE
                CALL    MVDOWN  ;AND PURGE 10 WORDS
                LD      SP,HL   ;IN THE STACK
FR8:            LD      HL,(LOPPT)      ;JOB DONE, RESTORE DE
                EX      DE,HL
                RST     RFINISH ;AND CONTINUE
;
NEXT:           RST     RTSTV   ;GET ADDRESS OF VAR.
                JP      C,QWHAT ;NO VARIABLE, "WHAT?"
                LD      (VARNXT),HL     ;YES, SAVE IT
NX0:            PUSH    DE      ;SAVE TEXT POINTER
                EX      DE,HL
                LD      HL,(LOPVAR)     ;GET VAR. IN 'FOR'
                LD      A,H
                OR      L       ;0 SAYS NEVER HAD ONE
                JP      Z,AWHAT ;SO WE ASK: "WHAT?"
                RST     RCOMP   ;ELSE WE CHECK THEM
                JP      Z,NX3   ;OK, THEY AGREE
                POP     DE      ;NO, LET'S SEE
                CALL    POPA    ;PURGE CURRENT LOOP
                LD      HL,(VARNXT)     ;AND POP ONE LEVEL
                JP      NX0     ;GO CHECK AGAIN
NX3:            LD      E,(HL)  ;COME HERE WHEN AGREED
                INC     HL
                LD      D,(HL)  ;DE=VALUE OF VAR.
                LD      HL,(LOPINC)
                PUSH    HL
                LD      A,H
                XOR     D
                LD      A,D
                ADD     HL,DE   ;ADD ONE STEP
                JP      M,NX4
                XOR     H
                JP      M,NX5
NX4:            EX      DE,HL
                LD      HL,(LOPVAR)     ;PUT IT BACK
                LD      (HL),E
                INC     HL
                LD      (HL),D
                LD      HL,(LOPLMT)     ;HL->LIMIT
                POP     AF      ;OLD HL
                OR      A
                JP      P,NX1   ;STEP > 0
                EX      DE,HL   ;STEP < 0
NX1:            CALL    CKHLDE  ;COMPARE WITH LIMIT
                POP     DE      ;RESTORE TEXT POINTER
                JP      C,NX2   ;OUTSIDE LIMIT
                LD      HL,(LOPLN)      ;WITHIN LIMIT, GO
                LD      (CURRNT),HL     ;BACK TO THE SAVED
                LD      HL,(LOPPT)      ;'CURRNT' AND TEXT
                EX      DE,HL   ;POINTER
                RST     RFINISH
NX5:            POP     HL
                POP     DE
NX2:            CALL    POPA    ;PURGE THIS LOOP
                RST     RFINISH
;
;*************************************************************
;
; *** REM *** IF *** INPUT *** & LET (& DEFLT) ***
;
; 'REM' CAN BE FOLLOWED BY ANYTHING AND IS IGNORED BY TBI.
; TBI TREATS IT LIKE AN 'IF' WITH A FALSE CONDITION.
;
; 'IF' IS FOLLOWED BY AN EXPR. AS A CONDITION AND ONE OR MORE
; COMMANDS (INCLUDING OTHER 'IF'S) SEPERATED BY SEMI-COLONS.
; NOTE THAT THE WORD 'THEN' IS NOT USED.  TBI EVALUATES THE
; EXPR. IF IT IS NON-ZERO, EXECUTION CONTINUES.  IF THE
; EXPR. IS ZERO, THE COMMANDS THAT FOLLOWS ARE IGNORED AND
; EXECUTION CONTINUES AT THE NEXT LINE.
;
; 'INPUT' COMMAND IS LIKE THE 'PRINT' COMMAND, AND IS FOLLOWED
; BY A LIST OF ITEMS.  IF THE ITEM IS A STRING IN SINGLE OR
; DOUBLE QUOTES, OR IS A BACK-ARROW, IT HAS THE SAME EFFECT AS
; IN 'PRINT'.  IF AN ITEM IS A VARIABLE, THIS VARIABLE NAME IS
; PRINTED OUT FOLLOWED BY A COLON.  THEN TBI WAITS FOR AN
; EXPR. TO BE TYPED IN.  THE VARIABLE IS THEN SET TO THE
; VALUE OF THIS EXPR.  IF THE VARIABLE IS PROCEDED BY A STRING
; (AGAIN IN SINGLE OR DOUBLE QUOTES), THE STRING WILL BE
; PRINTED FOLLOWED BY A COLON.  TBI THEN WAITS FOR INPUT EXPR.
; AND SET THE VARIABLE TO THE VALUE OF THE EXPR.
;
; IF THE INPUT EXPR. IS INVALID, TBI WILL PRINT "WHAT?",
; "HOW?" OR "SORRY" AND REPRINT THE PROMPT AND REDO THE INPUT.
; THE EXECUTION WILL NOT TERMINATE UNLESS YOU TYPE CONTROL-C.
; THIS IS HANDLED IN 'INPERR'.
;
; 'LET' IS FOLLOWED BY A LIST OF ITEMS SEPERATED BY COMMAS.
; EACH ITEM CONSISTS OF A VARIABLE, AN EQUAL SIGN, AND AN EXPR.
; TBI EVALUATES THE EXPR. AND SET THE VARIABLE TO THAT VALUE.
; TBI WILL ALSO HANDLE 'LET' COMMAND WITHOUT THE WORD 'LET'.
; THIS IS DONE BY 'DEFLT'.
;
REM:            LD      HL,0H   ;*** REM ***
.DB             3EH             ;SKIP RST, THIS IS LIKE 'IF 0'
;
IFF:            RST     REXPR   ;*** IF ***
                LD      A,H     ;IS THE EXPR.=0?
                OR      L
                JP      NZ,RUNSML       ;NO, CONTINUE
                CALL    FNDSKP  ;YES, SKIP REST OF LINE
                JP      NC,RUNTSL       ;AND RUN THE NEXT LINE
                JP      RSTART  ;IF NO NEXT, RE-START
;
INPERR:         LD      HL,(STKINP)     ;*** INPERR ***
                LD      SP,HL   ;RESTORE OLD SP
                POP     HL      ;AND OLD 'CURRNT'
                LD      (CURRNT),HL
                POP     DE      ;AND OLD TEXT POINTER
                POP     DE      ;REDO INPUT
;
INPUT:          ;*** INPUT ***
IP1:            PUSH    DE      ;SAVE IN CASE OF ERROR
                CALL    QTSTG   ;IS NEXT ITEM A STRING?
                JP      IP2     ;NO
                RST     RTSTV   ;YES, BUT FOLLOWED BY A
                JP      C,IP4   ;VARIABLE?   NO.
                JP      IP3     ;YES.  INPUT VARIABLE
IP2:            PUSH    DE      ;SAVE FOR 'PRTSTG'
                RST     RTSTV   ;MUST BE VARIABLE NOW
                JP      C,QWHAT ;"WHAT?" IT IS NOT?
                LD      A,(DE)  ;GET READY FOR 'PRTSTR'
                LD      C,A
                SUB     A
                LD      (DE),A
                POP     DE
                CALL    PRTSTG  ;PRINT STRING AS PROMPT
                LD      A,C     ;RESTORE TEXT
                DEC     DE
                LD      (DE),A
IP3:            PUSH    DE      ;SAVE TEXT POINTER
                EX      DE,HL
                LD      HL,(CURRNT)     ;ALSO SAVE 'CURRNT'
                PUSH    HL
                LD      HL,IP1  ;A NEGATIVE NUMBER
                LD      (CURRNT),HL     ;AS A FLAG
                LD      HL,0H   ;SAVE SP TOO
                ADD     HL,SP
                LD      (STKINP),HL
                PUSH    DE      ;OLD HL
                LD      A,':'   ;PRINT THIS TOO
                CALL    GETLN   ;AND GET A LINE
                LD      DE,BUFFER       ;POINTS TO BUFFER
                RST     REXPR   ;EVALUATE INPUT
;NOP                             ;CAN BE 'CALL ENDCHK'
;NOP
;NOP
                POP     DE      ;OK, GET OLD HL
                EX      DE,HL
                LD      (HL),E  ;SAVE VALUE IN VAR.
                INC     HL
                LD      (HL),D
                POP     HL      ;GET OLD 'CURRNT'
                LD      (CURRNT),HL
                POP     DE      ;AND OLD TEXT POINTER
IP4:            POP     AF      ;PURGE JUNK IN STACK
                TSTC($2C,IP5)   ;IS NEXT CH. ','?
                JP      IP1     ;YES, MORE ITEMS.
IP5:            RST     RFINISH
;
DEFLT:          LD      A,(DE)  ;***  DEFLT ***
                CP      CR      ;EMPTY LINE IS OK
                JP      Z,LT1   ;ELSE IT IS 'LET'
;
LET:            CALL    SETVAL  ;*** LET ***
                TSTC($2C,LT1)   ;SET VALUE TO VAR.
                JP      LET     ;ITEM BY ITEM
LT1:            RST     RFINISH ;UNTIL FINISH
;
;*************************************************************
;
; *** EXPR ***
;
; 'EXPR' EVALUATES ARITHMETICAL OR LOGICAL EXPRESSIONS.
; <EXPR>::<EXPR2>
;         <EXPR2><REL.OP.><EXPR2>
; WHERE <REL.OP.> IS ONE OF THE OPERATORS IN TAB8 AND THE
; RESULT OF THESE OPERATIONS IS 1 IF TRUE AND 0 IF FALSE.
; <EXPR2>::=(+ OR -)<EXPR3>(+ OR -<EXPR3>)(....)
; WHERE () ARE OPTIONAL AND (....) ARE OPTIONAL REPEATS.
; <EXPR3>::=<EXPR4>(* OR /><EXPR4>)(....)
; <EXPR4>::=<VARIABLE>
;           <FUNCTION>
;           (<EXPR>)
; <EXPR> IS RECURSIVE SO THAT VARIABLE '@' CAN HAVE AN <EXPR>
; AS INDEX, FUNCTIONS CAN HAVE AN <EXPR> AS ARGUMENTS, AND
; <EXPR4> CAN BE AN <EXPR> IN PARANTHESE.
;
;EXPR:          CALL    EXPR2   ;THIS IS AT LOC. 18
;               PUSH    HL      ;SAVE <EXPR2> VALUE
;               JP      EXPR1   ;REST OF IT AT EXPR1
EXPR1:          LD      HL,TAB8-1       ;LOOKUP REL.OP.
                JP      EXEC    ;GO DO IT
;
XP11:           CALL    XP18    ;REL.OP.">="
                RET     C       ;NO, RETURN HL=0
                LD      L,A     ;YES, RETURN HL=1
                RET
;
XP12:           CALL    XP18    ;REL.OP."#"
                RET     Z       ;FALSE, RETURN HL=0
                LD      L,A     ;TRUE, RETURN HL=1
                RET
;
XP13:           CALL    XP18    ;REL.OP.">"
                RET     Z       ;FALSE
                RET     C       ;ALSO FALSE, HL=0
                LD      L,A     ;TRUE, HL=1
                RET
;
XP14:           CALL    XP18    ;REL.OP."<="
                LD      L,A     ;SET HL=1
                RET     Z       ;REL. TRUE, RETURN
                RET     C
                LD      L,H     ;ELSE SET HL=0
                RET
;
XP15:           CALL    XP18    ;REL.OP."="
                RET     NZ      ;FALSE, RETURN HL=0
                LD      L,A     ;ELSE SET HL=1
                RET
;
XP16:           CALL    XP18    ;REL.OP."<"
                RET     NC      ;FALSE, RETURN HL=0
                LD      L,A     ;ELSE SET HL=1
                RET
;
XP17:           POP     HL      ;NOT .REL.OP
                RET             ;RETURN HL=<EXPR2>
;
XP18:           LD      A,C     ;SUBROUTINE FOR ALL
                POP     HL      ;REL.OP.'S
                POP     BC
                PUSH    HL      ;REVERSE TOP OF STACK
                PUSH    BC
                LD      C,A
                CALL    EXPR2   ;GET 2ND <EXPR2>
                EX      DE,HL   ;VALUE IN DE NOW
                EX      (SP),HL ;1ST <EXPR2> IN HL
                CALL    CKHLDE  ;COMPARE 1ST WITH 2ND
                POP     DE      ;RESTORE TEXT POINTER
                LD      HL,0H   ;SET HL=0, A=1
                LD      A,1
                RET
;
EXPR2:          TSTC('-',XP21)  ;NEGATIVE SIGN?
                LD      HL,0H   ;YES, FAKE '0-'
                JP      XP26    ;TREAT LIKE SUBTRACT
;
XP21:           TSTC('+',XP22)  ;POSITIVE SIGN? IGNORE
XP22:           CALL    EXPR3   ;1ST <EXPR3>
XP23:           TSTC('+',XP25)  ;ADD?
                PUSH    HL      ;YES, SAVE VALUE
                CALL    EXPR3   ;GET 2ND <EXPR3>
XP24:           EX      DE,HL   ;2ND IN DE
                EX      (SP),HL ;1ST IN HL
                LD      A,H     ;COMPARE SIGN
                XOR     D
                LD      A,D
                ADD     HL,DE
                POP     DE      ;RESTORE TEXT POINTER
                JP      M,XP23  ;1ST AND 2ND SIGN DIFFER
                XOR     H       ;1ST AND 2ND SIGN EQUAL
                JP      P,XP23  ;SO IS RESULT
;
                JP      QHOW    ;ELSE WE HAVE OVERFLOW
;
XP25:           TSTC('-',XP42)  ;SUBTRACT?
XP26:           PUSH    HL      ;YES, SAVE 1ST <EXPR3>
                CALL    EXPR3   ;GET 2ND <EXPR3>
                CALL    CHGSGN  ;NEGATE
                JP      XP24    ;AND ADD THEM
;
EXPR3:          CALL    EXPR4   ;GET 1ST <EXPR4>
XP31:           TSTC('*',XP34)  ;MULTIPLY?
                PUSH    HL      ;YES, SAVE 1ST
                CALL    EXPR4   ;AND GET 2ND <EXPR4>
                LD      B,0H    ;CLEAR B FOR SIGN
                CALL    CHKSGN  ;CHECK SIGN
                EX      (SP),HL ;1ST IN HL
                CALL    CHKSGN  ;CHECK SIGN OF 1ST
                EX      DE,HL
                EX      (SP),HL
                LD      A,H     ;IS HL > 255 ?
                OR      A
                JP      Z,XP32  ;NO
                LD      A,D     ;YES, HOW ABOUT DE
                OR      D
                EX      DE,HL   ;PUT SMALLER IN HL
                JP      NZ,AHOW ;ALSO >, WILL OVERFLOW
XP32:           LD      A,L     ;THIS IS DUMB
                LD      HL,0H   ;CLEAR RESULT
                OR      A       ;ADD AND COUNT
                JP      Z,XP35
XP33:           ADD     HL,DE
                JP      C,AHOW  ;OVERFLOW
                DEC     A
                JP      NZ,XP33
                JP      XP35    ;FINISHED
;
XP34:           TSTC('/',XP42)  ;DIVIDE?
                PUSH    HL      ;YES, SAVE 1ST <EXPR4>
                CALL    EXPR4   ;AND GET THE SECOND ONE
                LD      B,0H    ;CLEAR B FOR SIGN
                CALL    CHKSGN  ;CHECK SIGN OF 2ND
                EX      (SP),HL ;GET 1ST IN HL
                CALL    CHKSGN  ;CHECK SIGN OF 1ST
                EX      DE,HL
                EX      (SP),HL
                EX      DE,HL
                LD      A,D     ;DIVIDE BY 0?
                OR      E
                JP      Z,AHOW  ;SAY "HOW?"
                PUSH    BC      ;ELSE SAVE SIGN
                CALL    DIVIDE  ;USE SUBROUTINE
                LD      H,B     ;RESULT IN HL NOW
                LD      L,C
                POP     BC      ;GET SIGN BACK
XP35:           POP     DE      ;AND TEXT POINTER
                LD      A,H     ;HL MUST BE +
                OR      A
                JP      M,QHOW  ;ELSE IT IS OVERFLOW
                LD      A,B
                OR      A
                CALL    M,CHGSGN        ;CHANGE SIGN IF NEEDED
                JP      XP31    ;LOOK FOR MORE TERMS
;
EXPR4:          LD      HL,TAB4-1       ;FIND FUNCTION IN TAB4
                JP      EXEC    ;AND GO DO IT
;
XP40:           RST     RTSTV   ;NO, NOT A FUNCTION
                JP      C,XP41  ;NOR A VARIABLE
                LD      A,(HL)  ;VARIABLE
                INC     HL
                LD      H,(HL)  ;VALUE IN HL
                LD      L,A
                RET
;
XP41:           CALL    TSTNUM  ;OR IS IT A NUMBER
                LD      A,B     ;# OF DIGIT
                OR      A
                RET     NZ      ;OK
PARN:           TSTC($28,XP43)  ; '('
                RST     REXPR   ;"(EXPR)"
                TSTC($29,XP43)  ; ')'
XP42:           RET
XP43:           JP      QWHAT   ;ELSE SAY: "WHAT?"

RND:            CALL    PARN    ;*** RND(EXPR) ***
                LD      A,H     ;EXPR MUST BE +
                OR      A
                JP      M,QHOW
                OR      L       ;AND NON-ZERO
                JP      Z,QHOW
                PUSH    DE      ;SAVE BOTH
                PUSH    HL
                LD      HL,(RANPNT)     ;GET MEMORY AS RANDOM
                LD      DE,LSTROM       ;NUMBER
                RST     RCOMP
                JP      C,RA1   ;WRAP AROUND IF LAST
                LD      HL,START
RA1:            LD      E,(HL)
                INC     HL
                LD      D,(HL)
                LD      (RANPNT),HL
                POP     HL
                EX      DE,HL
                PUSH    BC
                CALL    DIVIDE  ;RND(N)=MOD(M,N)+1
                POP     BC
                POP     DE
                INC     HL
                RET

ABS:            CALL    PARN    ;*** ABS(EXPR) ***
                DEC     DE
                CALL    CHKSGN  ;CHECK SIGN
                INC     DE
                RET

SIZE:           LD      HL,(TXTUNF)     ;*** RETURN SIZE IN HL ***
                PUSH    DE      ;GET THE NUMBER OF FREE
                EX      DE,HL   ;BYTES BETWEEN 'TXTUNF'
                LD      HL,VARBGN       ;AND 'VARBGN'
                CALL    SUBDE
                POP     DE
                RET

PEEK:           CALL    PARN    ;*** PEEK(EXPR) ***
                LD      A,(HL)  ;GET CONTENT OF (HL)
                LD      H,0     ;RETURN RESULT IN HL
                LD      L,A
                RET

DEEK:           CALL    PARN    ;*** PEEK(EXPR) ***
                PUSH    DE
                LD      E,(HL)  ;GET LOW CONTENT OF (HL)
                INC     HL
                LD      D,(HL)  ;GET HIGH CONTENT OF (HL)
                EX      DE,HL   ;RETURN RESULT IN HL
                POP     DE
                RET

;
;*************************************************************
;
; *** DIVIDE *** SUBDE *** CHKSGN *** CHGSGN *** & CKHLDE ***
;
; 'DIVIDE' DIVIDES HL BY DE, RESULT IN BC, REMAINDER IN HL
;
; 'SUBDE' SUBSTRACTS DE FROM HL
;
; 'CHKSGN' CHECKS SIGN OF HL.  IF +, NO CHANGE.  IF -, CHANGE
; SIGN AND FLIP SIGN OF B.
;
; 'CHGSGN' CHECKS SIGN N OF HL AND B UNCONDITIONALLY.
;
; 'CKHLDE' CHECKS SIGN OF HL AND DE.  IF DIFFERENT, HL AND DE
; ARE INTERCHANGED.  IF SAME SIGN, NOT INTERCHANGED.  EITHER
; CASE, HL DE ARE THEN COMPARED TO SET THE FLAGS.
;
DIVIDE:         PUSH    HL      ;*** DIVIDE ***
                LD      L,H     ;DIVIDE H BY DE
                LD      H,0
                CALL    DV1
                LD      B,C     ;SAVE RESULT IN B
                LD      A,L     ;(REMINDER+L)/DE
                POP     HL
                LD      H,A
DV1:            LD      C,0FFH  ;RESULT IN C
DV2:            INC     C       ;DUMB ROUTINE
                CALL    SUBDE   ;DIVIDE BY SUBTRACT
                JP      NC,DV2  ;AND COUNT
                ADD     HL,DE
                RET
;
SUBDE:          LD      A,L     ;*** SUBDE ***
                SUB     E       ;SUBSTRACT DE FROM
                LD      L,A     ;HL
                LD      A,H
                SBC     A,D
                LD      H,A
                RET
;
CHKSGN:         LD      A,H     ;*** CHKSGN ***
                OR      A       ;CHECK SIGN OF HL
                RET     P       ;IF -, CHANGE SIGN
;
CHGSGN:         LD      A,H     ;*** CHGSGN ***
                PUSH    AF
                CPL             ;CHANGE SIGN OF HL
                LD      H,A
                LD      A,L
                CPL
                LD      L,A
                INC     HL
                POP     AF
                XOR     H
                JP      P,QHOW
                LD      A,B     ;AND ALSO FLIP B
                XOR     80H
                LD      B,A
                RET

CKHLDE:         LD      A,H
                XOR     D       ;SAME SIGN?
                JP      P,CK1   ;YES, COMPARE
                EX      DE,HL   ;NO, XCH AND COMP
CK1:            RST     RCOMP
                RET
;
;*************************************************************
;
; *** SETVAL *** FIN *** ENDCHK *** & ERROR (& FRIENDS) ***
;
; "SETVAL" EXPECTS A VARIABLE, FOLLOWED BY AN EQUAL SIGN AND
; THEN AN EXPR.  IT EVALUATES THE EXPR. AND SET THE VARIABLE
; TO THAT VALUE.
;
; "FIN" CHECKS THE END OF A COMMAND.  IF IT ENDED WITH ";",
; EXECUTION CONTINUES.  IF IT ENDED WITH A CR, IT FINDS THE
; NEXT LINE AND CONTINUE FROM THERE.
;
; "ENDCHK" CHECKS IF A COMMAND IS ENDED WITH CR.  THIS IS
; REQUIRED IN CERTAIN COMMANDS.  (GOTO, RETURN, AND STOP ETC.)
;
; "ERROR" PRINTS THE STRING POINTED BY DE (AND ENDS WITH CR).
; IT THEN PRINTS THE LINE POINTED BY 'CURRNT' WITH A "?"
; INSERTED AT WHERE THE OLD TEXT POINTER (SHOULD BE ON TOP
; OF THE STACK) POINTS TO.  EXECUTION OF TB IS STOPPED
; AND TBI IS RESTARTED.  HOWEVER, IF 'CURRNT' -> ZERO
; (INDICATING A DIRECT COMMAND), THE DIRECT COMMAND IS NOT
; PRINTED.  AND IF 'CURRNT' -> NEGATIVE # (INDICATING 'INPUT'
; COMMAND), THE INPUT LINE IS NOT PRINTED AND EXECUTION IS
; NOT TERMINATED BUT CONTINUED AT 'INPERR'.
;
; RELATED TO 'ERROR' ARE THE FOLLOWING:
; 'QWHAT' SAVES TEXT POINTER IN STACK AND GET MESSAGE "WHAT?"
; 'AWHAT' JUST GET MESSAGE "WHAT?" AND JUMP TO 'ERROR'.
; 'QSORRY' AND 'ASORRY' DO SAME KIND OF THING.
; 'AHOW' AND 'AHOW' IN THE ZERO PAGE SECTION ALSO DO THIS.
;
SETVAL:         RST     RTSTV   ;*** SETVAL ***
                JP      C,QWHAT ;"WHAT?" NO VARIABLE
                PUSH    HL      ;SAVE ADDRESS OF VAR.
                TSTC('=',SV1)   ;PASS "=" SIGN
                RST     REXPR   ;EVALUATE EXPR.
                LD      B,H     ;VALUE IS IN BC NOW
                LD      C,L
                POP     HL      ;GET ADDRESS
                LD      (HL),C  ;SAVE VALUE
                INC     HL
                LD      (HL),B
                RET
SV1:            JP      QWHAT   ;NO "=" SIGN

FIN:            TSTC(';',FI1)   ;*** FIN ***
                POP     AF      ;";", PURGE RET. ADDR.
                JP      RUNSML  ;CONTINUE SAME LINE
FI1:            TSTC(CR,FI2)    ;NOT ";", IS IT CR?
                POP     AF      ;YES, PURGE RET. ADDR.
                JP      RUNNXL  ;RUN NEXT LINE
FI2:            RET             ;ELSE RETURN TO CALLER

ENDCHK:         RST     RIGNBLK ;IGNBLK
                CP      CR      ;END WITH CR?
                RET     Z       ;OK, ELSE SAY: "WHAT?"
;
QWHAT:          PUSH    DE      ;*** QWHAT ***
AWHAT:          LD      DE,WHAT ;*** AWHAT ***
ERROR:          SUB     A       ;*** ERROR ***
                CALL    PRTSTG  ;PRINT 'WHAT?', 'HOW?'
                POP     DE      ;OR 'SORRY'
                LD      A,(DE)  ;SAVE THE CHARACTER
                PUSH    AF      ;AT WHERE OLD DE ->
                SUB     A       ;AND PUT A 0 THERE
                LD      (DE),A
                LD      HL,(CURRNT)     ;GET CURRENT LINE #
                PUSH    HL
                LD      A,(HL)  ;CHECK THE VALUE
                INC     HL
                OR      (HL)
                POP     DE
                JP      Z,RSTART        ;IF ZERO, JUST RESTART
                LD      A,(HL)  ;IF NEGATIVE,
                OR      A
                JP      M,INPERR        ;REDO INPUT
                CALL    PRTLN   ;ELSE PRINT THE LINE
                DEC     DE      ;UPTO WHERE THE 0 IS
                POP     AF      ;RESTORE THE CHARACTER
                LD      (DE),A
                LD      A,'?'   ;PRINT A "?"
                RST     ROUTC
                SUB     A       ;AND THE REST OF THE
                CALL    PRTSTG  ;LINE
                JP      RSTART  ;THEN RESTART
;
QSORRY:         PUSH    DE      ;*** QSORRY ***
ASORRY:         LD      DE,SORRY        ;*** ASORRY ***
                JP      ERROR
;
;*************************************************************
;
; *** GETLN *** FNDLN (& FRIENDS) ***
;
; 'GETLN' READS A INPUT LINE INTO 'BUFFER'.  IT FIRST PROMPT
; THE CHARACTER IN A (GIVEN BY THE CALLER), THEN IT FILLS
; THE BUFFER AND ECHOS.  IT IGNORES LF'S AND NULLS, BUT STILL
; ECHOS THEM BACK.  RUB-OUT IS USED TO CAUSE IT TO DELETE
; THE LAST CHARACTER (IF THERE IS ONE), AND ALT-MOD IS USED TO
; CAUSE IT TO DELETE THE WHOLE LINE AND START IT ALL OVER.
; CR SIGNALS THE END OF A LINE, AND CAUSE 'GETLN' TO RETURN.
;
; 'FNDLN' FINDS A LINE WITH A GIVEN LINE # (IN HL) IN THE
; TEXT SAVE AREA.  DE IS USED AS THE TEXT POINTER.  IF THE
; LINE IS FOUND, DE WILL POINT TO THE BEGINNING OF THAT LINE
; (I.E., THE LOW BYTE OF THE LINE #), AND FLAGS ARE NC & Z.
; IF THAT LINE IS NOT THERE AND A LINE WITH A HIGHER LINE #
; IS FOUND, DE POINTS TO THERE AND FLAGS ARE NC & NZ.  IF
; WE REACHED THE END OF TEXT SAVE AREA AND CANNOT FIND THE
; LINE, FLAGS ARE C & NZ.
; 'FNDLN' WILL INITIALIZE DE TO THE BEGINNING OF THE TEXT SAVE
; AREA TO START THE SEARCH.  SOME OTHER ENTRIES OF THIS
; ROUTINE WILL NOT INITIALIZE DE AND DO THE SEARCH.
; 'FNDLNP' WILL START WITH DE AND SEARCH FOR THE LINE #.
; 'FNDNXT' WILL BUMP DE BY 2, FIND A CR AND THEN START SEARCH.
; 'FNDSKP' USE DE TO FIND A CR, AND THEN START SEARCH.
;
GETLN:          RST     ROUTC   ;*** GETLN ***
                LD      DE,BUFFER       ;PROMPT AND INIT.
GL1:            CALL    CHKIO   ;CHECK KEYBOARD
                JP      Z,GL1   ;NO INPUT, WAIT
                CP      BS      ;BS, DELETE LAST CHARACTER?
                JP      Z,GL3   ;YES
                CP      DEL     ;DEL, DELETE LAST CHARACTER?
                JP      Z,GL3   ;YES
                RST     ROUTC   ;INPUT, ECHO BACK
                CP      LF      ;IGNORE LF
                JP      Z,GL1
                OR      A       ;IGNORE NULL
                JP      Z,GL1
                CP      CAN     ;^X, DELETE THE WHOLE LINE?
                JP      Z,GL4   ;YES
                LD      (DE),A  ;ELSE SAVE INPUT
                INC     DE      ;AND BUMP POINTER
                CP      CR      ;WAS IT CR?
                RET     Z       ;YES, END OF LINE
                LD      A,E     ;ELSE MORE FREE ROOM?
                CP      BUFEND & 0FFH
                JP      NZ,GL1  ;YES, GET NEXT INPUT
GL3:            LD      A,E     ;DELETE LAST CHARACTER
                CP      BUFFER & 0FFH   ;BUT DO WE HAVE ANY?
                JP      Z,GL4   ;NO, REDO WHOLE LINE
                DEC     DE      ;YES, BACKUP POINTER
                LD      A,BS    ;AND ECHO A BACKSPACE
                RST     ROUTC
                LD      A,' '   ;AND ECHO A BLANK
                RST     ROUTC
                LD      A,BS    ;AND ECHO A BACKSPACE
                RST     ROUTC
                JP      GL1     ;GO GET NEXT INPUT
GL4:            CALL    CRLF    ;REDO ENTIRE LINE
                LD      A,'^'   ;CR, LF AND UP-ARROW
                JP      GETLN
;
FNDLN:          LD      A,H     ;*** FNDLN ***
                OR      A       ;CHECK SIGN OF HL
                JP      M,QHOW  ;IT CANNOT BE -
                LD      DE,TXTBGN       ;INIT TEXT POINTER
;
FNDLP:          ;*** FDLNP ***
FL1:            PUSH    HL      ;SAVE LINE #
                LD      HL,(TXTUNF)     ;CHECK IF WE PASSED END
                DEC     HL
                RST     RCOMP
                POP     HL      ;GET LINE # BACK
                RET     C       ;C,NZ PASSED END
                LD      A,(DE)  ;WE DID NOT, GET BYTE 1
                SUB     L       ;IS THIS THE LINE?
                LD      B,A     ;COMPARE LOW ORDER
                INC     DE
                LD      A,(DE)  ;GET BYTE 2
                SBC     A,H     ;COMPARE HIGH ORDER
                JP      C,FL2   ;NO, NOT THERE YET
                DEC     DE      ;ELSE WE EITHER FOUND
                OR      B       ;IT, OR IT IS NOT THERE
                RET     ;NC,Z:FOUND, NC,NZ:NO
;
FNDNXT:         ;*** FNDNXT ***
                INC     DE      ;FIND NEXT LINE
FL2:            INC     DE      ;JUST PASSED BYTE 1 & 2
;
FNDSKP:         LD      A,(DE)  ;*** FNDSKP ***
                CP      CR      ;TRY TO FIND CR
                JP      NZ,FL2  ;KEEP LOOKING
                INC     DE      ;FOUND CR, SKIP OVER
                JP      FL1     ;CHECK IF END OF TEXT
;
;*************************************************************
;
; *** PRTSTG *** QTSTG *** PRTNUM *** & PRTLN ***
;
; 'PRTSTG' PRINTS A STRING POINTED BY DE.  IT STOPS PRINTING
; AND RETURNS TO CALLER WHEN EITHER A CR IS PRINTED OR WHEN
; THE NEXT BYTE IS THE SAME AS WHAT WAS IN A (GIVEN BY THE
; CALLER).  OLD A IS STORED IN B, OLD B IS LOST.
;
; 'QTSTG' LOOKS FOR A BACK-ARROW, SINGLE QUOTE, OR DOUBLE
; QUOTE.  IF NONE OF THESE, RETURN TO CALLER.  IF BACK-ARROW,
; OUTPUT A CR WITHOUT A LF.  IF SINGLE OR DOUBLE QUOTE, PRINT
; THE STRING IN THE QUOTE AND DEMANDS A MATCHING UNQUOTE.
; AFTER THE PRINTING THE NEXT 3 BYTES OF THE CALLER IS SKIPPED
; OVER (USUALLY A JUMP INSTRUCTION.
;
; 'PRTNUM' PRINTS THE NUMBER IN HL.  LEADING BLANKS ARE ADDED
; IF NEEDED TO PAD THE NUMBER OF SPACES TO THE NUMBER IN C.
; HOWEVER, IF THE NUMBER OF DIGITS IS LARGER THAN THE # IN
; C, ALL DIGITS ARE PRINTED ANYWAY.  NEGATIVE SIGN IS ALSO
; PRINTED AND COUNTED IN, POSITIVE SIGN IS NOT.
;
; 'PRTLN' PRINTS A SAVED TEXT LINE WITH LINE # AND ALL.
;
PRTSTG:         LD      B,A     ;*** PRTSTG ***
PS1:            LD      A,(DE)  ;GET A CHARACTER
                INC     DE      ;BUMP POINTER
                CP      B       ;SAME AS OLD A?
                RET     Z       ;YES, RETURN
                RST     ROUTC   ;ELSE PRINT IT
                CP      CR      ;WAS IT A CR?
                JP      NZ,PS1  ;NO, NEXT
                RET     ;YES, RETURN
;
QTSTG:          TSTC($22,QT3)   ;*** QTSTG ***
                LD      A,22H   ;IT IS A "
QT1:            CALL    PRTSTG  ;PRINT UNTIL ANOTHER
                CP      CR      ;WAS LAST ONE A CR?
                POP     HL      ;RETURN ADDRESS
                JP      Z,RUNNXL        ;WAS CR, RUN NEXT LINE
QT2:            INC     HL      ;SKIP 3 BYTES ON RETURN
                INC     HL
                INC     HL
                JP      (HL)    ;RETURN
QT3:            TSTC($27,QT4)   ;IS IT A '?
                LD      A,27H   ;YES, DO THE SAME
                JP      QT1     ;AS IN "
QT4:            TSTC($5F,QT5)   ;IS IT BACK-ARROW?
                LD      A,08DH  ;YES, CR WITHOUT LF
                RST     ROUTC
                POP     HL      ;RETURN ADDRESS
                JP      QT2
QT5:            RET     ;NONE OF ABOVE

PRTNUM:         PUSH    DE      ;*** PRINT NUMBER IN HL ***
                LD      A,(PNBASE)      ;GET NUMBER BASE
                LD      D,0     ;16 BIT FOR DIVIDE
                LD      E,A
                LD      B,D     ;NO PREFIX YET
                CP      16      ;HEX NUMBER?
                JP      NZ,PN0  ;NO, CHECK SIGN
                LD      B,'$'   ;PRINT LEADING '$'
                DEC     C       ;'$' TAKES SPACE
                JP      PN1     ;HEX IS UNSIGNED
PN0:            CALL    CHKSGN  ;CHECK SIGN
                JP      P,PN1   ;NO SIGN
                LD      B,'-'   ;B=SIGN
                DEC     C       ;'-' TAKES SPACE
PN1:            XOR     A
                LD      D,A
                PUSH    DE      ;SAVE AS A FLAG
                DEC     C       ;C=SPACES
                PUSH    BC      ;SAVE SIGN & SPACE
PN2:            CALL    DIVIDE  ;DIVIDE HL BY 10
                LD      A,B     ;RESULT 0?
                OR      C
                JP      Z,PN3   ;YES, WE GOT ALL
                EX      (SP),HL ;NO, SAVE REMAINDER
                DEC     L       ;AND COUNT SPACE
                PUSH    HL      ;HL IS OLD BC
                LD      H,B     ;MOVE RESULT TO BC
                LD      L,C
                JP      PN2     ;AND DIVIDE BY 10
PN3:            POP     BC      ;WE GOT ALL DIGITS IN
PN4:            DEC     C       ;THE STACK
                LD      A,C     ;LOOK AT SPACE COUNT
                OR      A
                JP      M,PN5   ;NO LEADING BLANKS
                LD      A,' '   ;LEADING BLANKS
                RST     ROUTC
                JP      PN4     ;MORE?
PN5:            LD      A,B     ;PRINT SIGN
                OR      A
                CALL    NZ,OUTC
                LD      E,L     ;LAST REMAINDER IN E
PN6:            LD      A,(PNBASE)      ;GET NUMBER BASE
                CP      E       ;IT IS FLAG FOR NO MORE
                LD      A,E     ;CHECK DIGIT IN E
                POP     DE
                RET     Z       ;IF SO, RETURN
                CP      10      ;0-9? < A hex?
                JP      C,PN7   ;Skip Add 7
                ADD     A,'A'-'0'-10    ;Bring it up to ASCII A-F
PN7:            ADD     A,'0'   ;ELSE CONVERT TO ASCII
                RST     ROUTC   ;AND PRINT THE DIGIT
                JP      PN6     ;GO BACK FOR MORE

PRTLN:          LD      A,10    ;SET 10 AS DEFAULT BASE
                LD      (PNBASE),A      ;FOR PRTNUM
                LD      A,(DE)
                LD      L,A     ;LOW ORDER LINE #
                INC     DE
                LD      A,(DE)  ;HIGH ORDER
                LD      H,A
                INC     DE
                LD      C,4     ;PRINT 4 DIGIT LINE #
                CALL    PRTNUM
                LD      A,' '   ;FOLLOWED BY A BLANK
                RST     ROUTC
                SUB     A       ;AND THEN THE NEXT
                CALL    PRTSTG
                RET
;
;*************************************************************
;
; *** MVUP *** MVDOWN *** POPA *** & PUSHA ***
;
; 'MVUP' MOVES A BLOCK UP FROM WHERE DE-> TO WHERE BC-> UNTIL
; DE = HL
;
; 'MVDOWN' MOVES A BLOCK DOWN FROM WHERE DE-> TO WHERE HL->
; UNTIL DE = BC
;
; 'POPA' RESTORES THE 'FOR' LOOP VARIABLE SAVE AREA FROM THE
; STACK
;
; 'PUSHA' STACKS THE 'FOR' LOOP VARIABLE SAVE AREA INTO THE
; STACK
;
MVUP:           RST     RCOMP   ;*** MVUP ***
                RET     Z       ;DE = HL, RETURN
                LD      A,(DE)  ;GET ONE BYTE
                LD      (BC),A  ;MOVE IT
                INC     DE      ;INCREASE BOTH POINTERS
                INC     BC
                JP      MVUP    ;UNTIL DONE
;
MVDOWN:         LD      A,B     ;*** MVDOWN ***
                SUB     D       ;TEST IF DE = BC
                JP      NZ,MD1  ;NO, GO MOVE
                LD      A,C     ;MAYBE, OTHER BYTE?
                SUB     E
                RET     Z       ;YES, RETURN
MD1:            DEC     DE      ;ELSE MOVE A BYTE
                DEC     HL      ;BUT FIRST DECREASE
                LD      A,(DE)  ;BOTH POINTERS AND
                LD      (HL),A  ;THEN DO IT
                JP      MVDOWN  ;LOOP BACK
;
POPA:           POP     BC      ;BC = RETURN ADDR.
                POP     HL      ;RESTORE LOPVAR, BUT
                LD      (LOPVAR),HL     ;=0 MEANS NO MORE
                LD      A,H
                OR      L
                JP      Z,PP1   ;YEP, GO RETURN
                POP     HL      ;NOP, RESTORE OTHERS
                LD      (LOPINC),HL
                POP     HL
                LD      (LOPLMT),HL
                POP     HL
                LD      (LOPLN),HL
                POP     HL
                LD      (LOPPT),HL
PP1:            PUSH    BC      ;BC = RETURN ADDR.
                RET
;
PUSHA:          LD      HL,STKLMT       ;*** PUSHA ***
                CALL    CHGSGN
                POP     BC      ;BC=RETURN ADDRESS
                ADD     HL,SP   ;IS STACK NEAR THE TOP?
                JP      NC,QSORRY       ;YES, SORRY FOR THAT
                LD      HL,(LOPVAR)     ;ELSE SAVE LOOP VAR'S
                LD      A,H     ;BUT IF LOPVAR IS 0
                OR      L       ;THAT WILL BE ALL
                JP      Z,PU1
                LD      HL,(LOPPT)      ;ELSE, MORE TO SAVE
                PUSH    HL
                LD      HL,(LOPLN)
                PUSH    HL
                LD      HL,(LOPLMT)
                PUSH    HL
                LD      HL,(LOPINC)
                PUSH    HL
                LD      HL,(LOPVAR)
PU1:            PUSH    HL
                PUSH    BC      ;BC = RETURN ADDR.
                RET

;*************************************************************
;
; *** OUTC *** CHKIO ***
;
; THESE ARE THE ONLY I/O ROUTINES IN TBI.
; OUTC WILL OUTPUT THE BYTE IN A.
; IF THAT IS A CR, A LF IS ALSO SEND OUT.
; ONLY THE FLAGS MAY BE CHANGED AT RETURN.
; ALL REGISTERS ARE RESTORED.
;
; 'CHKIO' CHECKS THE INPUT.  IF NO INPUT, IT WILL RETURN TO
; THE CALLER WITH THE Z FLAG SET.  IF THERE IS INPUT, Z FLAG
; IS CLEARED AND THE INPUT BYTE IS IN A.  HOWEVER, IF THE
; INPUT IS A CONTROL-O, THE 'OCSW' SWITCH IS COMPLIMENTED, AND
; Z FLAG IS RETURNED.  IF A CONTROL-C IS READ, 'CHKIO' WILL
; RESTART TBI AND DO NOT RETURN TO THE CALLER.
;
INIT:           LD      D,30    ;30 new lines to clear screen
PATLOP:         CALL    CRLF
                DEC     D
                JP      NZ,PATLOP
                SUB     A
                LD      DE,MSG1
                CALL    PRTSTG
                LD      HL,START
                LD      (RANPNT),HL
                LD      HL,TXTBGN
                LD      (TXTUNF),HL
                JP      RSTART

;THIS IS AT LOC. 10
;OUTC:          OUT     (IODATA),A      ;Out to data port
;               CP      CR      ;WAS IT CR?
;               RET     NZ      ;NO, FINISHED
;               JP      OC1     ;REST OF THIS IS AT OC1
OC1:            LD      A,LF    ;YES, WE SEND LF TOO
                RST     ROUTC   ;THIS IS RECURSIVE
                LD      A,CR    ;GET CR BACK IN A
                RET

CHKIO:          IN      A,(IOSTAT)      ;*** CHKIO ***
                AND     IO_RX_BIT       ;MASK STATUS BIT
                RET     Z       ;NOT READY, RETURN "Z"
                IN      A,(IODATA)      ;READY, READ DATA
                AND     7FH     ;MASK BIT 7 OFF
CI0:            CP      03H     ;IS IT CONTROL-C?
                RET     NZ      ;NO, RETURN "NZ"
                JP      RSTART  ;YES, RESTART TBI
;
MSG1:           .DB     "TinyBASIC"
                .DB     CR

;*************************************************************
;
; *** TABLES *** DIRECT *** & EXEC ***
;
; THIS SECTION OF THE CODE TESTS A STRING AGAINST A TABLE.
; WHEN A MATCH IS FOUND, CONTROL IS TRANSFERED TO THE SECTION
; OF CODE ACCORDING TO THE TABLE.
;
; AT 'EXEC', DE SHOULD POINT TO THE STRING AND HL SHOULD POINT
; TO THE TABLE-1.  AT 'DIRECT', DE SHOULD POINT TO THE STRING.
; HL WILL BE SET UP TO POINT TO TAB1-1, WHICH IS THE TABLE OF
; ALL DIRECT AND STATEMENT COMMANDS.
;
; A '.' IN THE STRING WILL TERMINATE THE TEST AND THE PARTIAL
; MATCH WILL BE CONSIDERED AS A MATCH.  E.G., 'P.', 'PR.',
; 'PRI.', 'PRIN.', OR 'PRINT' WILL ALL MATCH 'PRINT'.
;
; THE TABLE CONSISTS OF ANY NUMBER OF ITEMS.  EACH ITEM
; IS A STRING OF CHARACTERS WITH BIT 7 SET TO 0 AND
; A JUMP ADDRESS STORED HI-LOW WITH BIT 7 OF THE HIGH
; BYTE SET TO 1.
; This is done by the macro 'DWA'.
; If the program is executed from an address < 0x8000
; take care to mask this bit in program part 'EXEC'.
;
; END OF TABLE IS AN ITEM WITH A JUMP ADDRESS ONLY.  IF THE
; STRING DOES NOT MATCH ANY OF THE OTHER ITEMS, IT WILL
; MATCH THIS NULL ITEM AS DEFAULT.
;
TAB1:           ;DIRECT COMMANDS
                .DB     "LIST"
                DWA(LIST)
                .DB     "RUN"
                DWA(RUN)
                .DB     "NEW"
                DWA(NEW)
;
TAB2:           ;DIRECT/STATEMENT
                .DB     "NEXT"
                DWA(NEXT)
                .DB     "LET"
                DWA(LET)
                .DB     "IF"
                DWA(IFF)
                .DB     "GOTO"
                DWA(GOTO)
                .DB     "GOSUB"
                DWA(GOSUB)
                .DB     "RETURN"
                DWA(RETURN)
                .DB     "REM"
                DWA(REM)
                .DB     "FOR"
                DWA(FOR)
                .DB     "INPUT"
                DWA(INPUT)
                .DB     "PRINT"
                DWA(PRINT)
                .DB     "?"
                DWA(PRINT)
                .DB     "STOP"
                DWA(STOP)
                DWA(DEFLT)      ;END OF LIST
;
TAB4:           ;FUNCTIONS
                .DB     "RND"
                DWA(RND)
                .DB     "ABS"
                DWA(ABS)
                .DB     "SIZE"
                DWA(SIZE)
                .DB     "PEEK"          ;get byte from memory
                DWA(PEEK)
                .DB     "DEEK"          ;get word from memory
                DWA(DEEK)
                DWA(XP40)       ;END OF LIST
;
TAB5:           ;"TO" IN "FOR"
                .DB     "TO"
                DWA(FR1)
                DWA(QWHAT)      ;END OF LIST
;
TAB6:           ;"STEP" IN "FOR"
                .DB     "STEP"
                DWA(FR2)
                DWA(FR3)        ;END OF LIST
;
TAB8:           ;RELATION OPERATORS
                .DB     ">="
                DWA(XP11)
                .DB     "#"
                DWA(XP12)
                .DB     ">"
                DWA(XP13)
                .DB     "="
                DWA(XP15)
                .DB     "<="
                DWA(XP14)
                .DB     "<"
                DWA(XP16)
                DWA(XP17)       ;END OF REL OPERATOR LIST
;
DIRECT:         LD      HL,TAB1-1       ;*** DIRECT ***
;
EXEC:           ;*** EXEC ***
EX0:            RST     RIGNBLK ;IGNORE LEADING BLANKS
                PUSH    DE      ;SAVE POINTER
EX1:            LD      A,(DE)  ;IF FOUND '.' IN STRING
                INC     DE      ;BEFORE ANY MISMATCH
                CP      2EH     ;WE DECLARE A MATCH
                JP      Z,EX3
                CP      'a'     ;< 'a' ?
                JP      C,EXN   ;NO ALPHA CHAR
                CP      'z'+1   ;> 'z'
                JP      NC,EXN  ;NO ALPHA CHAR
                AND     5FH     ;MASK LOWER CASE TO UPPER CASE
EXN:
                INC     HL      ;HL->TABLE
                CP      (HL)    ;IF MATCH, TEST NEXT
                JP      Z,EX1
                LD      A,07FH  ;ELSE SEE IF BIT 7
                DEC     DE      ;OF TABLE IS SET, WHICH
                CP      (HL)    ;IS THE JUMP ADDR. (HI)
                JP      C,EX5   ;C:YES, MATCHED
EX2:            INC     HL      ;NC:NO, FIND JUMP ADDR.
                CP      (HL)
                JP      NC,EX2
                INC     HL      ;BUMP TO NEXT TAB. ITEM
                POP     DE      ;RESTORE STRING POINTER
                JP      EX0     ;TEST AGAINST NEXT ITEM
EX3:            LD      A,07FH  ;PARTIAL MATCH, FIND
EX4:            INC     HL      ;JUMP ADDR., WHICH IS
                CP      (HL)    ;FLAGGED BY BIT 7
                JP      NC,EX4
EX5:            LD      A,(HL)  ;LOAD HL WITH THE JUMP
                INC     HL      ;ADDRESS FROM THE TABLE
                LD      L,(HL)
#IF             $ < 8000H
                AND     7FH     ;MASK OFF HIGH ADDRESS BIT
#ENDIF
                LD      H,A
                POP     AF      ;CLEAN UP THE GARBAGE
                JP      (HL)    ;AND WE GO DO IT
;
LSTROM:                         ;ALL ABOVE CAN BE ROM
;
;*************************************************************

;*************************************************************
;
                .ORG            RAMBGN          ;HERE DOWN MUST BE RAM
;
PNBASE:         .DS             1               ;BASE FOR PRTNUM
OCSW:           .DS             1               ;SWITCH FOR OUTPUT
CURRNT:         .DS             2               ;POINTS TO CURRENT LINE
STKGOS:         .DS             2               ;SAVES SP IN 'GOSUB'
VARNXT:         .DS             2               ;TEMP STORAGE
STKINP:         .DS             2               ;SAVES SP IN 'INPUT'
LOPVAR:         .DS             2               ;'FOR' LOOP SAVE AREA
LOPINC:         .DS             2               ;INCREMENT
LOPLMT:         .DS             2               ;LIMIT
LOPLN:          .DS             2               ;LINE NUMBER
LOPPT:          .DS             2               ;TEXT POINTER
RANPNT:         .DS             2               ;RANDOM NUMBER POINTER
TXTUNF:         .DS             2               ;->UNFILLED TEXT AREA
TXTBGN:         .EQU            $               ;TEXT SAVE AREA BEGINS

                .ORG            RAMBGN+RAMSZE-$0100
;
TXTEND:         .EQU            $               ;TEXT SAVE AREA ENDS
VARBGN:         .DS             2+2*26          ;VARIABLE '@(0)' FOLLOWED BY 'A'..'Z'
                                                ;'@(1), @(2), ... are stored top-down
                                                ;i.e. &@(i) = TXTEND-2*i
BUFFER:         .DS             64              ;INPUT BUFFER
BUFEND:         .DS             1               ;BUFFER ENDS
STKLMT:         .DS             1               ;TOP LIMIT FOR STACK

                .ORG            RAMBGN+RAMSZE
;
STACK:          .EQU            $               ;STACK STARTS HERE
;
                .END