//------------------------------------------------------------------------
// This Arduino sketch should be used with a Mega board connected to a
// dongle hosting a Z80 CPU. The Arduino fully controls and senses all
// Z80 CPU pins. This software runs physical Z80 CPU by providing clock
// ticks and setting various control pins.
//
// There is a limited RAM buffer simulated by this sketch. All Z80 memory
// accesses are directed to use that buffer.
//
// Address and data buses from Z80 are connected to analog Arduino pins.
// Along with a resistor network on the dongle, this allows the software
// to sense when Z80 tri-states those two buses.
//
// Notes:
//      - Use serial set to 115200
//      - In the Arduino serial monitor window, set line ending to "CR"
//      - Memory access is simulated using a 256-byte pseudo-RAM memory
//      - I/O map is _not_ implemented. Reads will return whatever happens
//        to be on the data bus
//
// Copyright 2014 by Goran Devic
// This source code is released under the GPL v2 software license.
//------------------------------------------------------------------------
#include <stdarg.h>
#include "WString.h"

// Define Arduino Mega pins that are connected to a Z80 dongle board.

// Address bus pins from Z80 are connected to PA (AL = A0..A7) and PC (AH = A8..A15) on Arduino
#define P_AL PA
#define DDR_AL  DDRA
#define PORT_AL PORTA
#define PIN_AL  PINA
#define P_AH PC
#define DDR_AH  DDRC
#define PORT_AH PORTC
#define PIN_AH  PINC
#define AB0_sense A15 // read analog value from Z80 A0 to detect floating bus

// Data bus pins from Z80 are connected to PL (D0..D7) on Arduino
#define P_DB PL
#define DDR_DB  DDRL
#define PORT_DB PORTL
#define PIN_DB  PINL
#define DB0_sense A8 // read analog value from Z80 D0 to detect floating bus


#define INT 41 // This is a block of control signals from the
#define NMI 4  // bottom-left corner of Z80
#define HALT A11
#define MREQ A9
#define IORQ A10

#define RFSH A14 // This is a block of control signals from the
#define M1 A13   // bottom-right corner of Z80
#define RESET 40
#define BUSRQ 17
#define WAIT 39
#define BUSAK A12
#define WR 18 //
#define RD 19

#define CLK 10 // PB4 / OC2A (allows to drive the clock also from pwm output)

// Tri-state detection values: the values that are read on analog pins
// sensing the "high-Z" will differ based on the resistor values that make
// up your voltage divider. Print your particular readings and adjust these:
#define HI_Z_LOW  100 // Upper "0" value; low tri-state boundary
#define HI_Z_HIGH 600 // Low "1" value; upper tri-state boundary

// Control *output* pins of Z80, we read them into these variables
int halt;
int mreq;
int iorq;
int rfsh;
int m1;
int busak;
int wr;
int rd;

// Control *input* pins of Z80, we write them into the dongle
int zint = 1;
int nmi = 1;
int reset = 1;
int busrq = 1;
int wait = 1;

// Content of address and data wires
int ab;
byte db;

// Clock counter after reset
int clkCount;
int clkCountHi;

// T-cycle counter
int T;
int Mlast;

// M1-cycle counter
int m1Count;

// Detection if the address or data bus is tri-stated
bool abTristated = false;
bool dbTristated = false;

// Simulation control variables
bool running = 1;        // Simulation is running or is stopped
int traceShowBothPhases; // Show both phases of a clock cycle
int traceRefresh;        // Trace refresh cycles
int tracePause;          // Pause for a key press every so many clocks
int tracePauseCount;     // Current clock count for tracePause
int stopAtClk;           // Stop the simulation after this many clocks
int stopAtM1;            // Stop at a specific M1 cycle number
int stopAtHalt;          // Stop when HALT signal gets active
int intAtClk;            // Issue INT signal at that clock number
int nmiAtClk;            // Issue NMI signal at that clock number
int busrqAtClk;          // Issue BUSRQ signal at that clock number
int resetAtClk;          // Issue RESET signal at that clock number
int waitAtClk;           // Issue WAIT signal at that clock number
int clearAtClk;          // Clear all control signals at that clock number
byte iorqVector;         // Push IORQ vector (default is FF)

// Buffer containing RAM memory for Z80 to access
const unsigned int ramLen = 0x1000;
byte ram[ ramLen ];
const unsigned int ramMask = 0xFFF;

// Buffer simulating IO space for Z80 to access
const unsigned int ioLen = 0x100;
byte io[ ioLen ];
const unsigned int ioMask = 0xFF;

// Temp buffer to store input line
static const int TEMP_SIZE = 3 * 256; // enough room for 256 byte intel hex code
char temp[ TEMP_SIZE ];

// Temp buffer to store extra dump information
char extraInfo[ 64 ] = { "" };

int doEcho = 0;          // Echo received commands
int verboseMode;         // Enable debugging output

// -----------------------------------------------------------
// Arduino initialization entry point
// -----------------------------------------------------------
void setup() {
    Serial.begin( 115200 );
    Serial.flush();
    Serial.setTimeout( 1000L * 60 * 60 );

    pf( F( "\r\nZ80 Analyser V 1.0\r\n" ) );

    ResetSimulationVars();

    // By default, all Arduino pins are set as inputs
    DDR_AL = 0x00;
    DDR_AH = 0x00;
    DDR_DB = 0x00;
    pinMode( HALT, INPUT_PULLUP );
    pinMode( MREQ, INPUT_PULLUP );
    pinMode( IORQ, INPUT_PULLUP );
    pinMode( RD, INPUT_PULLUP );
    pinMode( WR, INPUT_PULLUP );
    pinMode( BUSAK, INPUT_PULLUP );
    pinMode( M1, INPUT_PULLUP );
    pinMode( RFSH, INPUT_PULLUP );
    // Configure all output pins, *inputs* into Z80
    digitalWrite( CLK, LOW );
    pinMode( CLK, OUTPUT );
    digitalWrite( INT, zint = HIGH );
    pinMode( INT, OUTPUT );
    digitalWrite( NMI, nmi = HIGH );
    pinMode( NMI, OUTPUT );
    digitalWrite( RESET, reset = HIGH );
    pinMode( RESET, OUTPUT );
    digitalWrite( BUSRQ, busrq = HIGH );
    pinMode( BUSRQ, OUTPUT );
    digitalWrite( WAIT, wait = HIGH );
    pinMode( WAIT, OUTPUT );
    WriteControlPins();

    // Perform a Z80 CPU reset
    DoReset();
}

// Resets all simulation variables to their defaults
void ResetSimulationVars() {
    traceShowBothPhases = 0; // Show both phases of a clock cycle
    traceRefresh = 1;        // Trace refresh cycles
    tracePause = 0;          // Pause for a keypress every so many clocks
    stopAtClk = 40;          // Stop the simulation after this many clocks
    stopAtM1 = -1;           // Stop at a specific M1 cycle number
    stopAtHalt = 1;          // Stop when HALT signal gets active
    intAtClk = -1;           // Issue INT signal at that clock number
    nmiAtClk = -1;           // Issue NMI signal at that clock number
    busrqAtClk = -1;         // Issue BUSRQ signal at that clock number
    resetAtClk = -1;         // Issue RESET signal at that clock number
    waitAtClk = -1;          // Issue WAIT signal at that clock number
    clearAtClk = -1;         // Clear all control signals at that clock number
    iorqVector = 0xFF;       // Push IORQ vector
    verboseMode = 0;         // Enable debugging output
}

// Issue a RESET sequence to Z80 and reset internal counters
void DoReset() {
    pf( F( "\r\n:Starting the clock\r\n" ) );
    digitalWrite( RESET, LOW );
    delayMicroseconds( 4 );
    // Reset should be kept low for 3 full clock cycles
    for ( int i = 0; i < 3; i++ ) {
        digitalWrite( CLK, HIGH );
        delayMicroseconds( 4 );
        digitalWrite( CLK, LOW );
        delayMicroseconds( 4 );
    }
    pf( F( ":Releasing RESET\r\n" ) );
    digitalWrite( RESET, HIGH );
    delayMicroseconds( 4 );
    // Do not count initial 2 clocks after the reset
    clkCount = -2;
    T = 0;
    Mlast = 1;
    tracePauseCount = 0;
    m1Count = 0;
}

// Write all control pins into the Z80 dongle
void WriteControlPins() {
    digitalWrite( INT, zint ? HIGH : LOW );
    digitalWrite( NMI, nmi ? HIGH : LOW );
    digitalWrite( RESET, reset ? HIGH : LOW );
    digitalWrite( BUSRQ, busrq ? HIGH : LOW );
    digitalWrite( WAIT, wait ? HIGH : LOW );
}

// Set new data value into the Z80 data bus
void SetDataToDB( byte data ) {
    DDR_DB = 0xFF; // output
    PORT_DB = data;
    db = data;
    dbTristated = false;
}

// Read Z80 data bus (PINL) and store into db variable
void GetDataFromDB() {
    DDR_DB = 0x00;  // input
    PORT_DB = 0x00; // disable int. PU

    // Detect if the data bus is tri-stated
    delayMicroseconds( 4 );
    int sense = analogRead( DB0_sense );
    if ( verboseMode )
        pf( F( "DB0 sense: %4d\n" ), sense );
    // These numbers might need to be adjusted for each Arduino board
    dbTristated = sense > HI_Z_LOW && sense < HI_Z_HIGH;

    db = dbTristated ? 0xFF : PIN_DB;
}

// Read a value of Z80 address bus (PC:PA) and store it into the ab variable.
// In addition, try to detect when a bus is tri-stated and write 0xFFF if so.
void GetAddressFromAB() {
    // Detect if the address bus is tri-stated
    int sense = analogRead( AB0_sense );
    if ( verboseMode )
        pf( F( "AB0 sense: %4d\n" ), sense );
    // These numbers might need to be adjusted for each Arduino board
    abTristated = sense > HI_Z_LOW && sense < HI_Z_HIGH;

    ab = abTristated ? 0xFFFF : (PIN_AH << 8) + PIN_AL;
}

// Read all control pins on the Z80 and store them into internal variables
void ReadControlState() {
    halt = digitalRead( HALT );
    mreq = digitalRead( MREQ );
    iorq = digitalRead( IORQ );
    rfsh = digitalRead( RFSH );
    m1 = digitalRead( M1 );
    busak = digitalRead( BUSAK );
    wr = digitalRead( WR );
    rd = digitalRead( RD );
}

// Dump the Z80 state as stored in internal variables
void DumpState( bool suppress ) {
    if ( !suppress ) {
        // Select your character for tri-stated bus
        char abStr[ 5 ] = { "----" };
        char dbStr[ 3 ] = { "--" };
        if ( !abTristated )
            sprintf( abStr, "%04X", ab );
        if ( !dbTristated )
            sprintf( dbStr, "%02X", db );
        if ( T == 1 && clkCountHi )
            pf( F( "--------------------------------------------------------------+\r\n" ) );
        pf( F( "#%03d%c T%-4d AB:%s DB:%s  %s %s %s %s %s %s %s %s |%s%s%s%s %s\r\n" ),
            clkCount < 0 ? 0 : clkCount, clkCountHi ? 'H' : 'L', T, abStr, dbStr,
            m1 ? "  " : "M1", rfsh ? "    " : "RFSH", mreq ? "    " : "MREQ",
            rd ? "  " : "RD", wr ? "  " : "WR", iorq ? "    " : "IORQ",
            busak ? "     " : "BUSAK", halt ? "    " : "HALT", zint ? "" : "[INT]",
            nmi ? "" : "[NMI]", busrq ? "" : "[BUSRQ]", wait ? "" : "[WAIT]", extraInfo );
    }
    extraInfo[ 0 ] = 0;
}

// -----------------------------------------------------------
// Main loop routine runs over and over again forever
// -----------------------------------------------------------
void loop() {
    //--------------------------------------------------------
    // Clock goes high
    //--------------------------------------------------------
    delayMicroseconds( 4 );
    digitalWrite( CLK, HIGH );
    delayMicroseconds( 4 );

    clkCountHi = 1;
    clkCount++;
    T++;
    tracePauseCount++;
    ReadControlState();
    GetAddressFromAB();
    if ( Mlast == 1 && m1 == 0 )
        T = 1, m1Count++;
    Mlast = m1;
    bool suppressDump = false;
    if ( !traceRefresh & !rfsh )
        suppressDump = true;

    // If the number of M1 cycles has been reached, skip the rest since we dont
    // want to execute this M1 phase
    if ( stopAtM1 >= 0 && m1Count > stopAtM1 ) {
        sprintf( extraInfo, "%d M1 cycles reached", stopAtM1 ), running = false;
        pf( F( "--------------------------------------------------------------+\r\n" ) );
        goto control;
    }

    // If the address is tri-stated, skip checking various combinations of
    // control signals since they may also be floating and we can't detect that
    if ( !abTristated ) {
        // Simulate read from RAM
        if ( !mreq && !rd ) {
            SetDataToDB( ram[ ab & ramMask ] );
            if ( !m1 )
                sprintf( extraInfo, "Opcode read from %04X -> %02X", ab, ram[ ab & ramMask ] );
            else
                sprintf( extraInfo, "Memory read from %04X -> %02X", ab, ram[ ab & ramMask ] );
        } else
            // Simulate interrupt requesting a vector
            if ( !m1 && !iorq ) {
                SetDataToDB( iorqVector );
                sprintf( extraInfo, "Pushing vector %02X", iorqVector );
            } else
                GetDataFromDB();

        // Simulate write to RAM
        if ( !mreq && !wr ) {
            ram[ ab & ramMask ] = db;
            sprintf( extraInfo, "Memory write to  %04X <- %02X", ab, db );
        }

        // Simulate I/O read
        if ( !iorq && !rd ) {
            SetDataToDB( io[ ab & ioMask ] );
            sprintf( extraInfo, "I/O read from %04X -> %02X", ab, io[ ab & ioMask ] );
        }

        // Simulate I/O write
        if ( !iorq && !wr ) {
            io[ ab & ioMask ] = db;
            sprintf( extraInfo, "I/O write to %04X <- %02X", ab, db );
        }

        // Capture memory refresh cycle
        if ( !mreq && !rfsh ) {
            sprintf( extraInfo, "Refresh address  %04X", ab );
        }
    } else
        GetDataFromDB();

    DumpState( suppressDump );

    // If the user wanted to pause simulation after a certain number of
    // clocks, handle it here. If the key pressed to continue was not Enter,
    // stop the simulation to issue that command
    if ( tracePause == tracePauseCount ) {
        while ( Serial.available() == 0 )
            ;
        if ( Serial.peek() != '\r' )
            sprintf( extraInfo, "Continue keypress was not Enter" ), running = false;
        else
            Serial.read();
        tracePauseCount = 0;
    }

    //--------------------------------------------------------
    // Clock goes low
    //--------------------------------------------------------
    delayMicroseconds( 4 );
    digitalWrite( CLK, LOW );
    delayMicroseconds( 4 );

    clkCountHi = 0;
    if ( traceShowBothPhases ) {
        ReadControlState();
        GetAddressFromAB();
        DumpState( suppressDump );
    }

    // Perform various actions at the requested clock number
    // if the count is positive (we start it at -2 to skip initial 2T)
    if ( clkCount >= 0 ) {
        if ( clkCount == intAtClk )
            zint = 0;
        if ( clkCount == nmiAtClk )
            nmi = 0;
        if ( clkCount == busrqAtClk )
            busrq = 0;
        if ( clkCount == resetAtClk )
            reset = 0;
        if ( clkCount == waitAtClk )
            wait = 0;
        // De-assert all control pins at this clock number
        if ( clkCount == clearAtClk )
            zint = nmi = busrq = reset = wait = 1;
        WriteControlPins();

        // Stop the simulation under some conditions
        if ( clkCount == stopAtClk )
            sprintf( extraInfo, "%d clocks reached", clkCount ), running = false;
        if ( stopAtHalt & !halt )
            sprintf( extraInfo, "HALT instruction" ), running = false;
    }

    //--------------------------------------------------------
    // Trace/simulation control handler
    //--------------------------------------------------------
control:
    if ( !running ) {
        pf( F( "--------------------------------------------------------------+\r\n" ) );
        pf( F( ":Simulation stopped: %s\r\n" ), extraInfo );
        extraInfo[ 0 ] = 0;
        digitalWrite( CLK, HIGH );
        zint = nmi = busrq = wait = 1;
        WriteControlPins();

        while ( !running ) {
            // Expect a command from the serial port
            if ( Serial.available() > 0 ) {
                unsigned adr = 0;
                unsigned end = 0;

                memset( temp, 0, TEMP_SIZE );
                if ( !readBytesUntilEOL( temp, TEMP_SIZE - 1 ) )
                    continue;

                if ( temp[ 0 ] == 'e' ) {
                    if ( temp[ 1 ] == '0' )
                        doEcho = false;
                    else if ( temp[ 1 ] == '1' )
                        doEcho = true;
                    else
                        continue;
                    pf( "Echo %s\r\n", doEcho ? "on" : "off" );
                    continue;
                }

                if ( temp[ 0 ] == 'v' ) {
                    if ( isdigit( temp[ 1 ] ) )
                        verboseMode = temp[ 1 ] - '0';
                    continue;
                }

                // Option ":"  : this is not really a user option.
                // This is used to input Intel HEX format values into the RAM buffer
                // Multiple lines may be pasted. They are separated by a space character.
                char *pTemp = temp;
                while ( *pTemp == ':' || *pTemp == '.' ) {
                    bool isRam = *pTemp == ':';
                    byte bytes = hex( ++pTemp ); // skip ':', start with hex number
                    if ( bytes > 0 ) {
                        adr = ( hex( pTemp + 2 ) << 8 ) + hex( pTemp + 4 );
                        // byte recordType = hex( pTemp + 6 );
                        if ( verboseMode ) pf( F( "%04X:" ), adr );
                        for ( int i = 0; i < bytes; i++ ) {
                            if ( isRam )
                                ram[ ( adr + i ) & ramMask ] = hex( pTemp + 8 + 2 * i );
                            else
                                io[ ( adr + i ) & ioMask ] = hex( pTemp + 8 + 2 * i );
                            if ( verboseMode ) pf( F( " %02X" ), hex( pTemp + 8 + 2 * i ) );
                        }
                        if ( verboseMode ) pf( F( "\r\n" ) );
                    }
                    pTemp += 2 * bytes + 10; // Skip to the next possible line of hex entry
                    while ( *pTemp && isspace( *pTemp ) )
                        ++pTemp; // skip leading space
                }

                // Option "r"  : reset and run the simulation
                if ( temp[ 0 ] == 'r' ) {
                    // If the variable 9 (Issue RESET) is not set, perform a RESET and run the simulation.
                    // If the variable was set, skip reset sequence since we might be testing it.
                    if ( resetAtClk < 0 )
                        DoReset();
                    running = true;
                }

                // Option "sR" : reset simulation variables to their default values
                if ( temp[ 0 ] == 's' && temp[ 1 ] == 'R' ) {
                    ResetSimulationVars();
                    temp[ 1 ] = 0; // Proceed to dump all variables...
                }

                // Option "s"  : show and set internal control variables
                if ( temp[ 0 ] == 's' ) {
                    // Show or set the simulation parameters
                    int var = 0, value;
                    int args = sscanf( &temp[ 1 ], "%d %d", &var, &value );
                    // Parameter for the option #12 is read in as a hex; others are decimal by default
                    if ( var == 12 )
                        args = sscanf( &temp[ 1 ], "%d %x", &var, &value );
                    if ( args == 2 ) {
                        if ( var == 0 )
                            traceShowBothPhases = value;
                        if ( var == 1 )
                            traceRefresh = value;
                        if ( var == 2 )
                            tracePause = value;
                        if ( var == 3 )
                            stopAtClk = value;
                        if ( var == 4 )
                            stopAtM1 = value;
                        if ( var == 5 )
                            stopAtHalt = value;
                        if ( var == 6 )
                            intAtClk = value;
                        if ( var == 7 )
                            nmiAtClk = value;
                        if ( var == 8 )
                            busrqAtClk = value;
                        if ( var == 9 )
                            resetAtClk = value;
                        if ( var == 10 )
                            waitAtClk = value;
                        if ( var == 11 )
                            clearAtClk = value;
                        if ( var == 12 )
                            iorqVector = value & 0xFF;
                        if ( var == 13 )
                            verboseMode = value;
                    }
                    pf( F( "------ Simulation variables ------\r\n" ) );
                    pf( F( "#0  Trace both clock phases  = %2d\r\n" ), traceShowBothPhases );
                    pf( F( "#1  Trace refresh cycles     = %2d\r\n" ), traceRefresh );
                    pf( F( "#2  Pause for keypress every = %2d\r\n" ), tracePause );
                    pf( F( "#3  Stop after clock #       = %2d\r\n" ), stopAtClk );
                    pf( F( "#4  Stop after M1 cycle #    = %2d\r\n" ), stopAtM1 );
                    pf( F( "#5  Stop at HALT             = %2d\r\n" ), stopAtHalt );
                    pf( F( "#6  Issue INT at clock #     = %2d\r\n" ), intAtClk );
                    pf( F( "#7  Issue NMI at clock #     = %2d\r\n" ), nmiAtClk );
                    pf( F( "#8  Issue BUSRQ at clock #   = %2d\r\n" ), busrqAtClk );
                    pf( F( "#9  Issue RESET at clock #   = %2d\r\n" ), resetAtClk );
                    pf( F( "#10 Issue WAIT at clock #    = %2d\r\n" ), waitAtClk );
                    pf( F( "#11 Clear all at clock #     = %2d\r\n" ), clearAtClk );
                    pf( F( "#12 Push IORQ vector #(hex)  = %02X\r\n" ), iorqVector );
                }

                if ( temp[ 0 ] == 'm' && temp[ 1 ] == 'R' ) {
                    // Option "mR"  : reset RAM memory
                    memset( ram, 0, sizeof( ram ) );
                    pf( F( "RAM reset to 00\r\n" ) );
                } else if ( temp[ 0 ] == 'i' && temp[ 1 ] == 'R' ) {
                    // Option "iR"  : reset IO memory
                    memset( io, 0, sizeof( io ) );
                    pf( F( "IO reset to 00\r\n" ) );
                } else if ( ( temp[0] == 'm' || temp[ 0 ] == 'i' ) && temp[1] == 's' ) {
                    // Option "ms"  : set RAM memory from ADR to byte(s) B
                    // Option "is"  : set IO memory from ADR to byte(s) B
                    // ms/is ADR B B B ...
                    bool isRam = temp[ 0 ] == 'm';
                    int i = 2;
                    if ( !isxdigit( temp[2] ) )
                        i = nextHex( temp, 0 ); // skip to start of adr
                    unsigned val;
                    if ( i &&  sscanf( pTemp + i, "%04X ", &adr ) == 1 ) {
                        end = adr;
                        while ( ( i = nextHex( pTemp, i ) ) ) {
                            if ( end >= ( isRam ? ramLen : ioLen ) )
                                break;
                            if ( sscanf( pTemp + i, "%2X", &val ) == 1 ) {
                                if ( isRam )
                                    ram[ end++ ] = val;
                                else
                                    io[ end++ ] = val;
                            } else
                                break;
                        }
                        pTemp[1] = 0; // "fall through" to mem/io dump
                    }
                }

                if ( temp[ 0 ] == 'm' || temp[ 0 ] == 'i' ) {
                    // Option "m START END"  : dump RAM memory
                    // Option "i START END"  : dump IO memory
                    // START and END are optional
                    // START defaults to 0 or ADR from "ms adr ..."
                    // END defaults to START + 0x100
                    // Option "mx" : same in intel hex format
                    bool isRam = temp[ 0 ] == 'm';
                    bool isHex = false;
                    int i = 1, rc = 0;
                    if ( temp[1] == 'x' ) {
                        isHex = true;
                        i = 2;
                    }
                    if ( !isxdigit( temp[i] ) )
                        i = nextHex( temp, 0 ); // skip to start of adr
                    if ( i )
                        rc = sscanf( temp + i, "%04X", &adr );
                    adr &= 0xFF0; // fit to line
                    if ( !end && rc && ( i = nextHex( temp, i ) ) ) // one more argument
                        rc = sscanf( temp + i, "%04X", &end );
                    if ( isRam ) {
                        if ( !end )
                            end = adr + 0x100;
                        if ( end > ramLen )
                            end = ramLen - 1;
                    } else { // IO
                        if ( !end )
                            end = ioLen - 1;
                    }
                    if ( isHex ) { //
                        for ( unsigned aaa = adr; aaa < end; aaa += 0x10 ) {
                            if ( isRam )
                                pf( F( ":10%04X00" ), aaa & ramMask );
                            else
                                pf( F( ".10%04X00" ), aaa & ioMask );
                            unsigned char cs = 0;
                            cs -= 0x10; // datalen
                            cs -= (aaa) & 0xFF;
                            cs -= (aaa >> 8) & 0x0F;
                            for ( int j = 0; j < 0x10; j++ ) {
                                unsigned char val;
                                if ( isRam )
                                    val = ram[ ( aaa + j ) & ramMask ];
                                else
                                    val = io[ ( aaa + j ) & ioMask ];
                                pf( "%02X",  val );
                                cs -= val;
                            }
                            pf( "%02X\r\n", cs );
                        }
                        if ( isRam )
                            pf( F( ":00000001FF\r\n" ) );
                        else
                            pf( F( ".00000001FF\r\n" ) );
                    } else { // normal dump, RAM or IO
                        pf( F( " %-3s   00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F\r\n" ),
                               isRam ? "MEM" : " IO" );
                        pf( F( "--------------------------------------------------------+\r\n" ) );
                        for ( unsigned aaa = adr; aaa < end; aaa += 0x10 ) {
                            if ( isRam )
                                pf( F( "%04X : " ), aaa & ramMask );
                            else
                                pf( F( "%04X : " ), aaa & ioMask );
                            for ( int j = 0; j < 16; j++ ) {
                                if ( isRam )
                                    pf( F( "%02X " ), ram[ ( aaa + j ) & ramMask ] );
                                else
                                    pf( F( "%02X " ), io[ ( aaa + j ) & ioMask ] );
                                if ( j == 7 )
                                    pf( F( " " ) );
                            }
                            pf( F( "|\r\n" ) );
                        }
                        pf( F( "--------------------------------------------------------+\r\n" ) );
                    }
                }

                // Option "?"  : print help
                if ( temp[ 0 ] == '?' || temp[ 0 ] == 'h' ) {
                    pf( F( "e0              - set echo off (default)\r\n" ) );
                    pf( F( "e1              - set echo on\r\n" ) );
                    pf( F( "s               - show simulation variables\r\n" ) );
                    pf( F( "s N VALUE       - set simulation variable number N to a VALUE\r\n" ) );
                    pf( F( "sR              - reset simulation variables to their default values\r\n" ) );
                    pf( F( "r               - restart the simulation\r\n" ) );
                    pf( F( ":INTEL-HEX      - reload RAM buffer with ihex data stream\r\n" ) );
                    pf( F( ".INTEL-HEX      - reload IO buffer with a modified ihex data stream\r\n" ) );
                    pf( F( "m START END     - dump the RAM buffer from START to END\r\n" ) );
                    pf( F( "mx START END    - dump the RAM buffer as ihex from START to END\r\n" ) );
                    pf( F( "mR              - reset the RAM buffer to 00\r\n" ) );
                    pf( F( "ms ADR B B B .. - set RAM buffer at ADR with data byte(s) B\r\n" ) );
                    pf( F( "i START END     - dump the IO buffer from START to END\r\n" ) );
                    pf( F( "ix START END    - dump the IO buffer as modified ihex from START to END\r\n" ) );
                    pf( F( "iR              - reset the IO buffer to 00\r\n" ) );
                    pf( F( "is ADR B B B .. - set IO buffer at ADR with data byte(s) B\r\n" ) );
                    pf( F( "vN              - set verboseMode to N (default = 0)\r\n" ) );
                }
            }
        }
    }
}


// Utility function to provide a meaningful printf to a serial port
// Function for fmt string in ram
void pf( const char *fmt, ... ) {
    char tmp[ 256 ]; // resulting string limited to 256 chars
    va_list args;
    va_start( args, fmt );
    vsnprintf( tmp, 256, fmt, args );
    va_end( args );
    Serial.print( tmp );
}


// Utility function to provide a meaningful printf to a serial port
// Overloaded function for fmt string in flash
void pf( const __FlashStringHelper *fmt, ... ) {
    // move string from flash to ram
    char f[ 256 ];
    memset( f, 0, 256 );
    strlcpy_P( f, (const char PROGMEM *)fmt, 256 );
    char tmp[ 256 ]; // resulting string limited to 256 chars
    va_list args;
    va_start( args, fmt ); // get args from input
    vsnprintf( tmp, 256, f, args ); // use format string in ram
    va_end( args );
    Serial.print( tmp );
}


// Read and return one ASCII hex byte value from a string
byte hex( char *s ) {
    byte nibbleH = ( *s - '0' ) & ~( 1 << 5 );
    byte nibbleL = ( *( s + 1 ) - '0' ) & ~( 1 << 5 );
    if ( nibbleH > 9 )
        nibbleH -= 7;
    if ( nibbleL > 9 )
        nibbleL -= 7;
    return ( nibbleH << 4 ) | nibbleL;
}


// return index of next hex number in string s starting from index i
static int nextHex( char *s, int i ) {
    while ( s[i] && s[i] != ' ' )
        ++i;
    while ( s[i] && s[i] == ' ' )
        ++i;
    return( isxdigit( s[i] ) ? i : 0 );
}


int readBytesUntilEOL( char *buf, int maxlen ) {
    int c, n=0;
    while ( maxlen ) {
        c = Serial.read();
        if ( c == -1 )
            continue;
        if ( c == '\r' || c == '\n' )
            break;
        *buf++ = c;
        --maxlen;
        ++n;
        if ( doEcho )
            Serial.write( c );
    }
    if ( doEcho )
        Serial.write( "\r\n" );
    *buf = 0;
    return n;
}


// --- ATTIC ---

#if 0

// Define Arduino Mega pins that are connected to a Z80 dongle board.

// Address bus pins from Z80 are connected to PA (A0..A7) and PC (A8..A15) on Arduino.
// #define AB0 22 // AB low is PA
// #define AB1 23
// #define AB2 24
// #define AB3 25
// #define AB4 26
// #define AB5 27
// #define AB6 28
// #define AB7 29
//
// #define AB8 37 // AB high is PC
// #define AB9 36
// #define AB10 35
// #define AB11 34
// #define AB12 33
// #define AB13 32
// #define AB14 31
// #define AB15 30

// Data bus pins from Z80 are connected to PL (D0..D7) on Arduino
// #define DB0 49 // DB is PL
// #define DB1 48
// #define DB2 47
// #define DB3 46
// #define DB4 45
// #define DB5 44
// #define DB6 43
// #define DB7 42


// Set new data value into the Z80 data bus
void SetDataToDB_( byte data ) {
    pinMode( DB0, OUTPUT );
    pinMode( DB1, OUTPUT );
    pinMode( DB2, OUTPUT );
    pinMode( DB3, OUTPUT );
    pinMode( DB4, OUTPUT );
    pinMode( DB5, OUTPUT );
    pinMode( DB6, OUTPUT );
    pinMode( DB7, OUTPUT );

    digitalWrite( DB0, ( data & ( 1 << 0 ) ) ? HIGH : LOW );
    digitalWrite( DB1, ( data & ( 1 << 1 ) ) ? HIGH : LOW );
    digitalWrite( DB2, ( data & ( 1 << 2 ) ) ? HIGH : LOW );
    digitalWrite( DB3, ( data & ( 1 << 3 ) ) ? HIGH : LOW );
    digitalWrite( DB4, ( data & ( 1 << 4 ) ) ? HIGH : LOW );
    digitalWrite( DB5, ( data & ( 1 << 5 ) ) ? HIGH : LOW );
    digitalWrite( DB6, ( data & ( 1 << 6 ) ) ? HIGH : LOW );
    digitalWrite( DB7, ( data & ( 1 << 7 ) ) ? HIGH : LOW );
    db = data;
    dbTristated = false;
}

// Read Z80 data bus (PINL) and store into db variable
void GetDataFromDB_() {
    pinMode( DB0, INPUT );
    pinMode( DB1, INPUT );
    pinMode( DB2, INPUT );
    pinMode( DB3, INPUT );
    pinMode( DB4, INPUT );
    pinMode( DB5, INPUT );
    pinMode( DB6, INPUT );
    pinMode( DB7, INPUT );

    digitalWrite( DB0, LOW );
    digitalWrite( DB1, LOW );
    digitalWrite( DB2, LOW );
    digitalWrite( DB3, LOW );
    digitalWrite( DB4, LOW );
    digitalWrite( DB5, LOW );
    digitalWrite( DB6, LOW );
    digitalWrite( DB7, LOW );

    // Detect if the data bus is tri-stated
    delayMicroseconds( 4 );
    int sense = analogRead( DB0_sense );
    if ( verboseMode )
        pf( F( "DB0 sense: %4d\n" ), sense );
    // These numbers might need to be adjusted for each Arduino board
    dbTristated = sense > HI_Z_LOW && sense < HI_Z_HIGH;

    byte d0 = digitalRead( DB0 );
    byte d1 = digitalRead( DB1 );
    byte d2 = digitalRead( DB2 );
    byte d3 = digitalRead( DB3 );
    byte d4 = digitalRead( DB4 );
    byte d5 = digitalRead( DB5 );
    byte d6 = digitalRead( DB6 );
    byte d7 = digitalRead( DB7 );
    db = ( d7 << 7 ) | ( d6 << 6 ) | ( d5 << 5 ) | ( d4 << 4 ) | ( d3 << 3 ) | ( d2 << 2 ) | ( d1 << 1 ) | d0;
}

// Read a value of Z80 address bus and store it into the ab variable.
// In addition, try to detect when a bus is tri-stated and write 0xFFF if so.
void GetAddressFromAB_() {
    // Detect if the address bus is tri-stated
    int sense = analogRead( AB0_sense );
    if ( verboseMode )
    // These numbers might need to be adjusted for each Arduino board
    abTristated = sense > HI_Z_LOW && sense < HI_Z_HIGH;

    int a0 = digitalRead( AB0 );
    int a1 = digitalRead( AB1 );
    int a2 = digitalRead( AB2 );
    int a3 = digitalRead( AB3 );
    int a4 = digitalRead( AB4 );
    int a5 = digitalRead( AB5 );
    int a6 = digitalRead( AB6 );
    int a7 = digitalRead( AB7 );
    ab = ( a7 << 7 ) | ( a6 << 6 ) | ( a5 << 5 ) | ( a4 << 4 ) | ( a3 << 3 ) | ( a2 << 2 ) | ( a1 << 1 ) | a0;

    if ( verboseMode )
        pf( F( "AB0 sense: %4d, Address bus: %04X\n" ), sense, ab );
}

#endif
