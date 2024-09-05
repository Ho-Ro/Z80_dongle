# Arduino and ZiLOG Z80

If you want to find out exactly what a venerable Z80 is doing on its bus
while executing instructions, in this post I outlined a dongle and the software
that will let you see that. Using just a few components and connecting them
to an Arduino Mega, you can trace instructions clock by clock and observe what’s
happening on the bus.

Start with a proto-board and solder down components following this schematics:

![Arduino + Z80 "schematics"](arduino-z80-schematics.jpg)

There is a capacitor between +5V and GND which should help keep away any voltage noise.
You can use anything: I had a 0.1uF tantalum cap that I used. The evil thing with not
putting bypass (or decoupling) caps in your designs is that you may never find out
why they might behave erratically.

There is also a push-button, which is optional. You might think it would be connected
to the reset pin but it is connected to the CLK – I wanted to be able to manually
clock the Z80 to see if it worked before hooking it up to Arduino. For the same reason,
every Z80 control input pin has a weak pull-up resistor. That should make it “alive and
kicking” right off the bat without the need for anything else externally driving it.

Now, the most interesting extension to the design is a tri-state bus detection.
Z80 occasionally puts its address and data buses into “High-Z”, or tri-state,
and I wanted to detect that (it also lets most of its control pins to Z,
but I was content to detect only the major buses.) There are 2+2 resistors
(each 10K) making up a weak resistor divider network connected to pins D0 and A0.
That way, whenever Z80 releases its data or address bus, the pins will assume 2.5V
pulled by resistor dividers. Since both buses are connected to analog input pins,
Arduino will be able to read the voltage and clearly detect that they are not
0V or 5V (logical 0 or 1) but somewhere in between.

After verifying that my 25-year old Z80 chip from my parts bin is still working (!),
I connected it to an Arduino Mega board. Mega is really useful here since it hosts
more I/O pins than you’d ever need, runs on +5V, and therefore needs no voltage
level translators to talk to Z80.

The connectors are conveniently clustered by their function: All eight data wires together,
address wires (I only used 8 for a max address space of 256 bytes), control signals
in two groups (one from each side of the Z80 package), and a few odd ones: clock,
which goes to pin 13 on Arduino and also powers the LED on it, and +5V and GND.
You can click on an image below to zoom on it —

The time is to write some software. Being a software engineer by trade comes in really useful
since many great hardware hobbyists totally drop the ball when it’s time to blow a breath of
life into their designs and write code, so they skim over that part. The Arduino software
that runs this dongle can be downloaded here.

Connected through a serial port, you have several commands available (type “?” or “h”
at the console):

```
s            - show simulation variables
s #var value - set simulation variable number to a value
sc           - clear simulation variables to their default values
r            - restart the simulation
:INTEL-HEX   - reload RAM buffer with a given data stream
m            - dump the content of the RAM buffer
mc           - clear the RAM buffer
```

There are several internal simulation variables that you can change in order to run your tests
on Z80 in various ways. The best way to create a Z80 test is to create a small test program.

<details>
<summary>For example, create a test like this:<summary>

```
; test for IM2
stack   equ $100
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
```
</summary>

Functionally, this sequence does not make much sense, but it lets us test several things
by inserting an NMI and INT at certain places we can trace what’s going on
when the CPU is servicing those interrupts.

<details>
<summary>Assembler output:<summary>

```
                        ; test for IM2
                        stack   equ $100
0000    21 00 00                ld hl, counter
0003    31 00 01                ld sp, stack
0006    3E 55                   ld a,$55
0008    77                      ld (hl),a
0009    0E 55                   ld c,$55
000B    ED 71                   out (c),0
000D    3E 00                   ld a,im2vector >> 8
000F    ED 47                   ld i,a ; high part of int vector
0011    ED 5E                   im 2
0013    FB                      ei
0014    18 FE                   jr $ ; wait for interrupt

0001 <- 16 00
                        counter
0016    00                      defs 1

0017    00 00 00 00             org $20 ; int routine
001B    00 00 00 00
001F    00
                        im2routine
0020    34                      inc (hl)
0021    FB                      ei
0022    ED 4D                   reti


0024    00 00 00 00             org $30 ; int vector
0028    00 00 00 00
002C    00 00 00 00
000E <- 00
                        im2vector
0030    20 00                   defw im2routine

0032    00 00 00 00             org $38 ; RST38 / IM 1 routine
0036    00 00
0038    34                      inc (hl)
0039    FB                      ei
003A    ED 4D                   reti

003C    00 00 00 00             org $66 ; NMI routine
0040    00 00 00 00
0044    00 00 00 00
0048    00 00 00 00
004C    00 00 00 00
0050    00 00 00 00
0054    00 00 00 00
0058    00 00 00 00
005C    00 00 00 00
0060    00 00 00 00
0064    00 00
0066    35                      dec (hl)
0067    FB                      ei
0068    ED 45                   retn

0020                    IM2ROUTINE
0016                    COUNTER
0030                    IM2VECTOR
0100                    STACK

Using RAM range [0x0000...0x0069]
```
</details>

Open an Intel-style hex file
that will show the code in hex; copy all and paste it into the Arduino serial terminal.

```
:200000002116003100013E55770E55ED713E00ED47ED5EFB18FE00000000000000000000DE
:2000200034FBED4D000000000000000000000000200000000000000034FBED4D00000000CE
:200040000000000000000000000000000000000000000000000000000000000000000000A0
:0A00600000000000000035FBED4534
:00000001FF
```

Arduino will happily respond that it stored the stream of bytes and you can issue a command “m”
to dump the buffer (which Z80 sees as its RAM) to confirm that it is there:

```
m 0 80
       00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F
--------------------------------------------------------+
0000 : 21 16 00 31 00 01 3E 55  77 0E 55 ED 71 3E 00 ED |
0010 : 47 ED 5E FB 18 FE 00 00  00 00 00 00 00 00 00 00 |
0020 : 34 FB ED 4D 00 00 00 00  00 00 00 00 00 00 00 00 |
0030 : 20 00 00 00 00 00 00 00  34 FB ED 4D 00 00 00 00 |
0040 : 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00 |
0050 : 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00 |
0060 : 00 00 00 00 00 00 35 FB  ED 45 00 00 00 00 00 00 |
0070 : 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00 |
--------------------------------------------------------+
```

Also possible as intel hex:

```
mx 0 80
:100000002116003100013E55770E55ED713E00ED91
:1000100047ED5EFB18FE000000000000000000003D
:1000200034FBED4D00000000000000000000000067
:10003000200000000000000034FBED4D0000000037
:1000400000000000000000000000000000000000B0
:1000500000000000000000000000000000000000A0
:1000600000000000000035FBED450000000000002E
:100070000000000000000000000000000000000080
:00000001FF
```

Typing command “s” will show simulation variables that are available to us:

```
------ Simulation variables ------
#0  Trace both clock phases  =  0
#1  Trace refresh cycles     =  1
#2  Pause for keypress every =  0
#3  Stop after clock #       = 40
#4  Stop after M1 cycle #    = -1
#5  Stop at HALT             =  1
#6  Issue INT at clock #     = -1
#7  Issue NMI at clock #     = -1
#8  Issue BUSRQ at clock #   = -1
#9  Issue RESET at clock #   = -1
#10 Issue WAIT at clock #    = -1
#11 Clear all at clock #     = -1
#12 Push IORQ vector #(hex)  = FF
```

The code evolved over time and so did the variables and multitudes of situations that can
be set up by cleverly combining those values. In fact, this blog probably does not show
the most up-to-date software version.

As it runs, the trace program counts clocks and, by setting those variables, you can toggle
specific control pins at determined times. For example, if you want to issue an INT at clock 120,
you would do “s 6 120”. You can optionally dump what’s happening on both clock phases and not
only on the positive phase (variable #0). Variable #1 will show or hide memory refresh cycles
that accompany M1.

```
------ Simulation variables ------
#0  Trace both clock phases  =  0
#1  Trace refresh cycles     =  1
#2  Pause for keypress every =  0
#3  Stop after clock #       = 200
#4  Stop after M1 cycle #    = -1
#5  Stop at HALT             =  1
#6  Issue INT at clock #     = 120
#7  Issue NMI at clock #     = -1
#8  Issue BUSRQ at clock #   = -1
#9  Issue RESET at clock #   = -1
#10 Issue WAIT at clock #    = -1
#11 Clear all at clock #     = 130
#12 Push IORQ vector #(hex)  = 30
```

Start the trace by issuing a command “r”. The Arduino starts the clocks and issues a RESET
sequence to Z80 after which your code runs and bus values are dumped out.

Notice the tri-state detection – when the address or data bus is being tri-stated by Z80,
the program outputs “–“. In fact, the data bus is being tri-stated most of the time!
This is a dump from parts of the run.

```
:Starting the clock
:Releasing RESET
--------------------------------------------------------------+
#000H T1    AB:---- DB:--                                     |
#000H T2    AB:---- DB:--                                     |
--------------------------------------------------------------+
#001H T1    AB:0000 DB:--  M1                                 |
#002H T2    AB:0000 DB:21  M1      MREQ RD                    | Opcode read from 0000 -> 21
#003H T3    AB:0000 DB:--     RFSH                            |
#004H T4    AB:0000 DB:--     RFSH MREQ                       | Refresh address  0000
#005H T5    AB:0001 DB:--                                     |
#006H T6    AB:0001 DB:16          MREQ RD                    | Memory read from 0001 -> 16
#007H T7    AB:0001 DB:16          MREQ RD                    | Memory read from 0001 -> 16
#008H T8    AB:0002 DB:--                                     |
#009H T9    AB:0002 DB:00          MREQ RD                    | Memory read from 0002 -> 00
#010H T10   AB:0002 DB:00          MREQ RD                    | Memory read from 0002 -> 00
--------------------------------------------------------------+
```

<details>
<summary>The dumps are normally longer, but you get the idea.</summary>

```
:Starting the clock
:Releasing RESET
--------------------------------------------------------------+
#000H T1    AB:---- DB:--                                     |
#000H T2    AB:---- DB:--                                     |
--------------------------------------------------------------+
#001H T1    AB:0000 DB:--  M1                                 |
#002H T2    AB:0000 DB:21  M1      MREQ RD                    | Opcode read from 0000 -> 21
#003H T3    AB:0000 DB:--     RFSH                            |
#004H T4    AB:0000 DB:--     RFSH MREQ                       | Refresh address  0000
#005H T5    AB:0001 DB:--                                     |
#006H T6    AB:0001 DB:16          MREQ RD                    | Memory read from 0001 -> 16
#007H T7    AB:0001 DB:16          MREQ RD                    | Memory read from 0001 -> 16
#008H T8    AB:0002 DB:--                                     |
#009H T9    AB:0002 DB:00          MREQ RD                    | Memory read from 0002 -> 00
#010H T10   AB:0002 DB:00          MREQ RD                    | Memory read from 0002 -> 00
--------------------------------------------------------------+
#011H T1    AB:0003 DB:--  M1                                 |
#012H T2    AB:0003 DB:31  M1      MREQ RD                    | Opcode read from 0003 -> 31
#013H T3    AB:0001 DB:--     RFSH                            |
#014H T4    AB:0001 DB:--     RFSH MREQ                       | Refresh address  0001
#015H T5    AB:0004 DB:--                                     |
#016H T6    AB:0004 DB:00          MREQ RD                    | Memory read from 0004 -> 00
#017H T7    AB:0004 DB:00          MREQ RD                    | Memory read from 0004 -> 00
#018H T8    AB:0005 DB:--                                     |
#019H T9    AB:0005 DB:01          MREQ RD                    | Memory read from 0005 -> 01
#020H T10   AB:0005 DB:01          MREQ RD                    | Memory read from 0005 -> 01
--------------------------------------------------------------+
#021H T1    AB:0006 DB:--  M1                                 |
#022H T2    AB:0006 DB:3E  M1      MREQ RD                    | Opcode read from 0006 -> 3E
#023H T3    AB:0002 DB:--     RFSH                            |
#024H T4    AB:0002 DB:--     RFSH MREQ                       | Refresh address  0002
#025H T5    AB:0007 DB:--                                     |
#026H T6    AB:0007 DB:55          MREQ RD                    | Memory read from 0007 -> 55
#027H T7    AB:0007 DB:55          MREQ RD                    | Memory read from 0007 -> 55
--------------------------------------------------------------+
#028H T1    AB:0008 DB:--  M1                                 |
#029H T2    AB:0008 DB:77  M1      MREQ RD                    | Opcode read from 0008 -> 77
#030H T3    AB:0003 DB:--     RFSH                            |
#031H T4    AB:0003 DB:--     RFSH MREQ                       | Refresh address  0003
#032H T5    AB:0016 DB:--                                     |
#033H T6    AB:0016 DB:55          MREQ                       |
#034H T7    AB:0016 DB:55          MREQ    WR                 | Memory write to  0016 <- 55
--------------------------------------------------------------+
#035H T1    AB:0009 DB:--  M1                                 |
#036H T2    AB:0009 DB:0E  M1      MREQ RD                    | Opcode read from 0009 -> 0E
#037H T3    AB:0004 DB:--     RFSH                            |
#038H T4    AB:0004 DB:--     RFSH MREQ                       | Refresh address  0004
#039H T5    AB:000A DB:--                                     |
#040H T6    AB:000A DB:55          MREQ RD                    | Memory read from 000A -> 55
#041H T7    AB:000A DB:55          MREQ RD                    | Memory read from 000A -> 55
--------------------------------------------------------------+
#042H T1    AB:000B DB:--  M1                                 |
#043H T2    AB:000B DB:ED  M1      MREQ RD                    | Opcode read from 000B -> ED
#044H T3    AB:0005 DB:--     RFSH                            |
#045H T4    AB:0005 DB:--     RFSH MREQ                       | Refresh address  0005
--------------------------------------------------------------+
#046H T1    AB:000C DB:--  M1                                 |
#047H T2    AB:000C DB:71  M1      MREQ RD                    | Opcode read from 000C -> 71
#048H T3    AB:0006 DB:--     RFSH                            |
#049H T4    AB:0006 DB:--     RFSH MREQ                       | Refresh address  0006
#050H T5    AB:0055 DB:--                                     |
#051H T6    AB:0055 DB:00                  WR IORQ            | I/O write to 0055 <- 00
#052H T7    AB:0055 DB:00                  WR IORQ            | I/O write to 0055 <- 00
#053H T8    AB:0055 DB:00                  WR IORQ            | I/O write to 0055 <- 00
--------------------------------------------------------------+
#054H T1    AB:000D DB:--  M1                                 |
#055H T2    AB:000D DB:3E  M1      MREQ RD                    | Opcode read from 000D -> 3E
#056H T3    AB:0007 DB:--     RFSH                            |
#057H T4    AB:0007 DB:--     RFSH MREQ                       | Refresh address  0007
#058H T5    AB:000E DB:--                                     |
#059H T6    AB:000E DB:00          MREQ RD                    | Memory read from 000E -> 00
#060H T7    AB:000E DB:00          MREQ RD                    | Memory read from 000E -> 00
--------------------------------------------------------------+
#061H T1    AB:000F DB:--  M1                                 |
#062H T2    AB:000F DB:ED  M1      MREQ RD                    | Opcode read from 000F -> ED
#063H T3    AB:0008 DB:--     RFSH                            |
#064H T4    AB:0008 DB:--     RFSH MREQ                       | Refresh address  0008
--------------------------------------------------------------+
#065H T1    AB:0010 DB:--  M1                                 |
#066H T2    AB:0010 DB:47  M1      MREQ RD                    | Opcode read from 0010 -> 47
#067H T3    AB:0009 DB:--     RFSH                            |
#068H T4    AB:0009 DB:--     RFSH MREQ                       | Refresh address  0009
#069H T5    AB:0009 DB:--                                     |
--------------------------------------------------------------+
#070H T1    AB:0011 DB:--  M1                                 |
#071H T2    AB:0011 DB:ED  M1      MREQ RD                    | Opcode read from 0011 -> ED
#072H T3    AB:000A DB:--     RFSH                            |
#073H T4    AB:000A DB:--     RFSH MREQ                       | Refresh address  000A
--------------------------------------------------------------+
#074H T1    AB:0012 DB:--  M1                                 |
#075H T2    AB:0012 DB:5E  M1      MREQ RD                    | Opcode read from 0012 -> 5E
#076H T3    AB:000B DB:--     RFSH                            |
#077H T4    AB:000B DB:--     RFSH MREQ                       | Refresh address  000B
--------------------------------------------------------------+
#078H T1    AB:0013 DB:--  M1                                 |
#079H T2    AB:0013 DB:FB  M1      MREQ RD                    | Opcode read from 0013 -> FB
#080H T3    AB:000C DB:--     RFSH                            |
#081H T4    AB:000C DB:--     RFSH MREQ                       | Refresh address  000C
--------------------------------------------------------------+
#082H T1    AB:0014 DB:--  M1                                 |
#083H T2    AB:0014 DB:18  M1      MREQ RD                    | Opcode read from 0014 -> 18
#084H T3    AB:000D DB:--     RFSH                            |
#085H T4    AB:000D DB:--     RFSH MREQ                       | Refresh address  000D
#086H T5    AB:0015 DB:--                                     |
#087H T6    AB:0015 DB:FE          MREQ RD                    | Memory read from 0015 -> FE
#088H T7    AB:0015 DB:FE          MREQ RD                    | Memory read from 0015 -> FE
#089H T8    AB:0015 DB:--                                     |
#090H T9    AB:0015 DB:--                                     |
#091H T10   AB:0015 DB:--                                     |
#092H T11   AB:0015 DB:--                                     |
#093H T12   AB:0015 DB:--                                     |
--------------------------------------------------------------+
#094H T1    AB:0014 DB:--  M1                                 |
#095H T2    AB:0014 DB:18  M1      MREQ RD                    | Opcode read from 0014 -> 18
#096H T3    AB:000E DB:--     RFSH                            |
#097H T4    AB:000E DB:--     RFSH MREQ                       | Refresh address  000E
#098H T5    AB:0015 DB:--                                     |
#099H T6    AB:0015 DB:FE          MREQ RD                    | Memory read from 0015 -> FE
#100H T7    AB:0015 DB:FE          MREQ RD                    | Memory read from 0015 -> FE
#101H T8    AB:0015 DB:--                                     |
#102H T9    AB:0015 DB:--                                     |
#103H T10   AB:0015 DB:--                                     |
#104H T11   AB:0015 DB:--                                     |
#105H T12   AB:0015 DB:--                                     |
--------------------------------------------------------------+
#106H T1    AB:0014 DB:--  M1                                 |
#107H T2    AB:0014 DB:18  M1      MREQ RD                    | Opcode read from 0014 -> 18
#108H T3    AB:000F DB:--     RFSH                            |
#109H T4    AB:000F DB:--     RFSH MREQ                       | Refresh address  000F
#110H T5    AB:0015 DB:--                                     |
#111H T6    AB:0015 DB:FE          MREQ RD                    | Memory read from 0015 -> FE
#112H T7    AB:0015 DB:FE          MREQ RD                    | Memory read from 0015 -> FE
#113H T8    AB:0015 DB:--                                     |
#114H T9    AB:0015 DB:--                                     |
#115H T10   AB:0015 DB:--                                     |
#116H T11   AB:0015 DB:--                                     |
#117H T12   AB:0015 DB:--                                     |
--------------------------------------------------------------+
#118H T1    AB:0014 DB:--  M1                                 |
#119H T2    AB:0014 DB:18  M1      MREQ RD                    | Opcode read from 0014 -> 18
#120H T3    AB:0010 DB:--     RFSH                            |
#121H T4    AB:0010 DB:--     RFSH MREQ                       |[INT] Refresh address  0010
#122H T5    AB:0015 DB:--                                     |[INT]
#123H T6    AB:0015 DB:FE          MREQ RD                    |[INT] Memory read from 0015 -> FE
#124H T7    AB:0015 DB:FE          MREQ RD                    |[INT] Memory read from 0015 -> FE
#125H T8    AB:0015 DB:--                                     |[INT]
#126H T9    AB:0015 DB:--                                     |[INT]
#127H T10   AB:0015 DB:--                                     |[INT]
#128H T11   AB:0015 DB:--                                     |[INT]
#129H T12   AB:0015 DB:--                                     |[INT]
--------------------------------------------------------------+
#130H T1    AB:0014 DB:--  M1                                 |[INT]
#131H T2    AB:0014 DB:--  M1                                 |
#132H T3    AB:0014 DB:--  M1                                 |
#133H T4    AB:0014 DB:30  M1                 IORQ            | Pushing vector 30
#134H T5    AB:0011 DB:--     RFSH                            |
#135H T6    AB:0011 DB:--     RFSH MREQ                       | Refresh address  0011
#136H T7    AB:0011 DB:--                                     |
#137H T8    AB:00FF DB:--                                     |
#138H T9    AB:00FF DB:00          MREQ                       |
#139H T10   AB:00FF DB:00          MREQ    WR                 | Memory write to  00FF <- 00
#140H T11   AB:00FE DB:--                                     |
#141H T12   AB:00FE DB:14          MREQ                       |
#142H T13   AB:00FE DB:14          MREQ    WR                 | Memory write to  00FE <- 14
#143H T14   AB:0030 DB:--                                     |
#144H T15   AB:0030 DB:20          MREQ RD                    | Memory read from 0030 -> 20
#145H T16   AB:0030 DB:20          MREQ RD                    | Memory read from 0030 -> 20
#146H T17   AB:0031 DB:--                                     |
#147H T18   AB:0031 DB:00          MREQ RD                    | Memory read from 0031 -> 00
#148H T19   AB:0031 DB:00          MREQ RD                    | Memory read from 0031 -> 00
--------------------------------------------------------------+
#149H T1    AB:0020 DB:--  M1                                 |
#150H T2    AB:0020 DB:34  M1      MREQ RD                    | Opcode read from 0020 -> 34
#151H T3    AB:0012 DB:--     RFSH                            |
#152H T4    AB:0012 DB:--     RFSH MREQ                       | Refresh address  0012
#153H T5    AB:0016 DB:--                                     |
#154H T6    AB:0016 DB:55          MREQ RD                    | Memory read from 0016 -> 55
#155H T7    AB:0016 DB:55          MREQ RD                    | Memory read from 0016 -> 55
#156H T8    AB:0016 DB:--                                     |
#157H T9    AB:0016 DB:--                                     |
#158H T10   AB:0016 DB:56          MREQ                       |
#159H T11   AB:0016 DB:56          MREQ    WR                 | Memory write to  0016 <- 56
--------------------------------------------------------------+
#160H T1    AB:0021 DB:--  M1                                 |
#161H T2    AB:0021 DB:FB  M1      MREQ RD                    | Opcode read from 0021 -> FB
#162H T3    AB:0013 DB:--     RFSH                            |
#163H T4    AB:0013 DB:--     RFSH MREQ                       | Refresh address  0013
--------------------------------------------------------------+
#164H T1    AB:0022 DB:--  M1                                 |
#165H T2    AB:0022 DB:ED  M1      MREQ RD                    | Opcode read from 0022 -> ED
#166H T3    AB:0014 DB:--     RFSH                            |
#167H T4    AB:0014 DB:--     RFSH MREQ                       | Refresh address  0014
--------------------------------------------------------------+
#168H T1    AB:0023 DB:--  M1                                 |
#169H T2    AB:0023 DB:4D  M1      MREQ RD                    | Opcode read from 0023 -> 4D
#170H T3    AB:0015 DB:--     RFSH                            |
#171H T4    AB:0015 DB:--     RFSH MREQ                       | Refresh address  0015
#172H T5    AB:00FE DB:--                                     |
#173H T6    AB:00FE DB:14          MREQ RD                    | Memory read from 00FE -> 14
#174H T7    AB:00FE DB:14          MREQ RD                    | Memory read from 00FE -> 14
#175H T8    AB:00FF DB:--                                     |
#176H T9    AB:00FF DB:00          MREQ RD                    | Memory read from 00FF -> 00
#177H T10   AB:00FF DB:00          MREQ RD                    | Memory read from 00FF -> 00
--------------------------------------------------------------+
#178H T1    AB:0014 DB:--  M1                                 |
#179H T2    AB:0014 DB:18  M1      MREQ RD                    | Opcode read from 0014 -> 18
#180H T3    AB:0016 DB:--     RFSH                            |
#181H T4    AB:0016 DB:--     RFSH MREQ                       | Refresh address  0016
#182H T5    AB:0015 DB:--                                     |
#183H T6    AB:0015 DB:FE          MREQ RD                    | Memory read from 0015 -> FE
#184H T7    AB:0015 DB:FE          MREQ RD                    | Memory read from 0015 -> FE
#185H T8    AB:0015 DB:--                                     |
#186H T9    AB:0015 DB:--                                     |
#187H T10   AB:0015 DB:--                                     |
#188H T11   AB:0015 DB:--                                     |
#189H T12   AB:0015 DB:--                                     |
--------------------------------------------------------------+
#190H T1    AB:0014 DB:--  M1                                 |
#191H T2    AB:0014 DB:18  M1      MREQ RD                    | Opcode read from 0014 -> 18
#192H T3    AB:0017 DB:--     RFSH                            |
#193H T4    AB:0017 DB:--     RFSH MREQ                       | Refresh address  0017
#194H T5    AB:0015 DB:--                                     |
#195H T6    AB:0015 DB:FE          MREQ RD                    | Memory read from 0015 -> FE
#196H T7    AB:0015 DB:FE          MREQ RD                    | Memory read from 0015 -> FE
#197H T8    AB:0015 DB:--                                     |
#198H T9    AB:0015 DB:--                                     |
#199H T10   AB:0015 DB:--                                     |
#200H T11   AB:0015 DB:--                                     |
--------------------------------------------------------------+
:Simulation stopped: 200 clocks reached
```
</details>

Immediately we can see that Z80 uses 2 clocks of not doing anything externally after the reset.
The clock phase can be high (H) or low (L) and dumping lows is enabled by setting a simulation
variable #0 (by default, it does not dump low phases). T-cycles are being automatically counted
starting at every M1 cycle. This greatly helps to cross-check each instruction against
documentation. Input and output pins that are active are also tagged.

The Arduino simulator software provides data bytes to Z80 on memory read operations and stores
bytes into the internal buffer on memory write operations. Simulating an IO map is much simpler
where variable #12 can be used to push an arbitrary IORQ vector when needed.

Overall, the dongle itself and the options implemented by the Arduino [software](Z80_dongle/Z80_dongle.ino)
provide a powerful way to examine and visualize Z80 behavior whether it is running undocumented
opcodes or responding to a sequence of external control pins like interrupts, bus requests, etc.
