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
// The analysis part of the SW is: Copyright 2014 by Goran Devic
// This source code is released under the GPL v2 software license.
//
// The z80retroshield part of the SW is under the MIT License (MIT)
// Copyright (c) 2019 Erturk Kocalar, http://8Bitforce.com/
// Copyright (c) 2019 Steve Kemp, https://steve.kemp.fi/


//------------------------------------------------------------------------

#include <stdarg.h>
#include <stdint.h>

#include "WString.h"
#include "z80retroshield.h"

#include "NascomBasic.h"


// Define Arduino Mega pins that are connected to a Z80 dongle board.

// Address bus pins from Z80 are connected to PA (AL = A0..A7) and PC (AH = A8..A15) on Arduino
#define P_AL PA
#define DDR_AL DDRA
#define PORT_AL PORTA
#define PIN_AL PINA
#define P_AH PC
#define DDR_AH DDRC
#define PORT_AH PORTC
#define PIN_AH PINC
#define AB0_sense A15 // read analog value from Z80 A0 to detect floating bus

// Data bus pins from Z80 are connected to PL (D0..D7) on Arduino
#define P_DB PL
#define DDR_DB DDRL
#define PORT_DB PORTL
#define PIN_DB PINL
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
#define HI_Z_LOW 100  // Upper "0" value; low tri-state boundary
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
uint16_t ab;
uint8_t db;

// Clock counter after reset
int clkCount;
int clkCountHi;

// T-cycle counter
int T;
int m1Prev;

// M1-cycle counter
int m1Count;

// Detection if the address or data bus is tri-stated
bool abTristated = false;
bool dbTristated = false;

// Simulation control variables
bool running = 0;        // Simulation is running or is stopped
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
const uint16_t ramLen = 0x1000;
// more RAM for Basic program
// reuse as additional space for input and extra, tmp, ftmp
uint8_t ram[ ramLen + 2560 ];
const uint16_t ramMask = 0xFFF;

static uint16_t ramBegin = 0;
static uint16_t ramEnd = ramBegin + ramLen - 1;

const uint16_t romLen = 0x2000;
static uint16_t romBegin = 0;
static uint16_t romEnd = romBegin + romLen - 1;

// Temp buffer to store input line at the end of Basic RAM, unused at analyser
static const int INPUT_SIZE = 1024; // enough room for ~400 byte intel hex code input
char *input = (char *)ram + sizeof( ram ) - INPUT_SIZE;

// Temp buffer for extra dump info, at the end of input buffer (unused during output)
static const int EXTRA_SIZE = 64;
char *extraInfo = (char *)ram + sizeof( ram ) - EXTRA_SIZE;

// Buffer simulating IO space for Z80 to access, at unused Basic RAM before input buffer
const unsigned int ioLen = 0x100;
byte *io = ram + sizeof( ram ) - INPUT_SIZE - ioLen;
const unsigned int ioMask = 0xFF;

const unsigned int tmpLen = 256;
const unsigned int ftmpLen = 256;
char *tmp = (char *)ram + sizeof( ram ) - INPUT_SIZE - ioLen - tmpLen;
char *ftmp = (char *)ram + sizeof( ram ) - INPUT_SIZE - ioLen - tmpLen - ftmpLen;

int doEcho = 0;      // Echo received commands
int verboseMode = 0; // Enable debugging output

bool runBasic = false;

//
// Our helper
//
Z80RetroShield cpu;


// -----------------------------------------------------------
// Arduino initialization entry point
// -----------------------------------------------------------
void setup() {
    Serial.begin( 115200 );
    Serial.flush();
    Serial.setTimeout( 1000L * 60 * 60 );

    pf( F( "\r\nZ80 Analyser V 1.0\r\n" ) );

    //
    // Empty the serial-buffer before we launch.
    //
    while ( Serial.available() )
        Serial.read();

    //
    // Setup callbacks: RAM & ROM
    //
    cpu.set_memory_read( memory_read );
    cpu.set_memory_write( memory_write );

    //
    // Setup callbacks: Port I/O
    //
    cpu.set_io_read( io_read );
    cpu.set_io_write( io_write );

    //
    // We're now configured, show a pre-boot
    // message.
    //
    // Serial.println("Launching Nascom ROM Basic:");

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

    // Set all Z80 control inputs
    WriteControlPins();
    extraInfo[ 0 ] = 0;

    // Perform a Z80 CPU reset
    // DoReset();
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
    m1Prev = 1;
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
    if ( verboseMode > 1 )
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
    if ( verboseMode > 1 )
        pf( F( "AB0 sense: %4d\n" ), sense );
    // These numbers might need to be adjusted for each Arduino board
    abTristated = sense > HI_Z_LOW && sense < HI_Z_HIGH;

    ab = abTristated ? 0xFFFF : ( PIN_AH << 8 ) + PIN_AL;
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
        pf( F( "#%03d%c T%-4d AB:%s DB:%s  %s %s %s %s %s %s %s %s |%s%s%s%s %s\r\n" ), clkCount < 0 ? 0 : clkCount,
            clkCountHi ? 'H' : 'L', T, abStr, dbStr, m1 ? "  " : "M1", rfsh ? "    " : "RFSH", mreq ? "    " : "MREQ",
            rd ? "  " : "RD", wr ? "  " : "WR", iorq ? "    " : "IORQ", busak ? "     " : "BUSAK", halt ? "    " : "HALT",
            zint ? "" : "[INT]", nmi ? "" : "[NMI]", busrq ? "" : "[BUSRQ]", wait ? "" : "[WAIT]", extraInfo );
    }
    extraInfo[ 0 ] = 0;
}


//////////////////////////////////////////////////////////////////////
// retroshield CB functions
//////////////////////////////////////////////////////////////////////
//
// If the address is "low" read from our ROM.
//
// Otherwise read from our RAM-region.
//
//
uint8_t memory_read( uint16_t address ) {
    if ( address <= romEnd )
        return ( pgm_read_byte_near( rom + address ) );
    else if ( address <= ramEnd )
        return ( ram[ address - ramBegin ] );
    else {
        if ( verboseMode )
            pf( F( "*** Read error: %04X\n" ), address );
        return ( 0 );
    }
}

//
// If the address is "high" write to our RAM.
//
//
void memory_write( uint16_t address, uint8_t value ) {
    if ( address >= ramBegin && address <= ramEnd )
        ram[ address - ramBegin ] = value;
    else if ( verboseMode )
        pf( F( "*** Write error: %4X <- %2X\n" ), address, value );
}

//
// I'm porting the (unchanged) BASIC here.
//
// So addresses are:
//
//   0  -> STDIN / Serial.read
//   1  -> Meta / serial-console setup.
//
uint8_t io_read( uint16_t address ) {
    //
    // If the address is port 0 then the Z80
    // is making a request for serial-data.
    //
    // This might be optimistic, or as a result
    // of an interrupt.
    //
    // Ensure that the interrupt pin is disabled
    // (i.e. set high, low is active), and return
    // a byte of serial-input.
    //
    if ( address == 0 ) {
        // Terminate interrupt.
        digitalWrite( INT, HIGH );

        char c = Serial.read();

        if ( c < 0 )
            return 0x00;
        else
            return (uint8_t)c;
    }

    //
    // Read from an unhandled I/O port.
    //
    return 0x00;
}

//
// I/O function handler: Port writing.
//
//
void io_write( uint16_t address, uint8_t byte ) {
    //
    // Write the byte to the serial-port.
    //
    if ( address == 0 ) {
        Serial.write( byte );
        return;
    }

    //
    // Unknown address
    //
}
//
//////////////////////////////////////////////////////////////////////


// -----------------------------------------------------------
// Main loop routine runs over and over again forever
// -----------------------------------------------------------
void loop() {
    //--------------------------------------------------------
    // Trace/simulation control handler
    //--------------------------------------------------------
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
                ramBegin = 0;
                ramEnd = ramBegin + sizeof( ram ) - 1;

                memset( input, 0, INPUT_SIZE );
                if ( !readBytesUntilEOL( input, INPUT_SIZE - 1 ) )
                    continue;

                if ( input[ 0 ] == 'B' ) {
                    ramBegin = 0x2000;
                    ramEnd = ramBegin + sizeof( ram ) - 1;
                    runBasic = input[ 1 ] != '?';
                    DoReset();
                    running = true;
                }

                if ( input[ 0 ] == 'e' ) {
                    if ( input[ 1 ] == '0' )
                        doEcho = false;
                    else if ( input[ 1 ] == '1' )
                        doEcho = true;
                    else
                        continue;
                    pf( "Echo %s\r\n", doEcho ? "on" : "off" );
                    continue;
                }

                if ( input[ 0 ] == 'v' ) {
                    if ( isdigit( input[ 1 ] ) )
                        verboseMode = input[ 1 ] - '0';
                    continue;
                }

                // Option ":"  : this is not really a user option.
                // This is used to input Intel HEX format values into the RAM buffer
                // Multiple lines may be pasted. They are separated by a space character.
                char *pTemp = input;
                while ( *pTemp == ':' || *pTemp == '.' ) {
                    bool isRam = *pTemp == ':';
                    byte bytes = hex( ++pTemp ); // skip ':', start with hex number
                    if ( bytes > 0 ) {
                        adr = ( hex( pTemp + 2 ) << 8 ) + hex( pTemp + 4 );
                        // byte recordType = hex( pTemp + 6 );
                        if ( verboseMode )
                            pf( F( "%04X:" ), adr );
                        for ( int i = 0; i < bytes; i++ ) {
                            if ( isRam )
                                ram[ ( adr + i ) & ramMask ] = hex( pTemp + 8 + 2 * i );
                            else
                                io[ ( adr + i ) & ioMask ] = hex( pTemp + 8 + 2 * i );
                            if ( verboseMode )
                                pf( F( " %02X" ), hex( pTemp + 8 + 2 * i ) );
                        }
                        if ( verboseMode )
                            pf( F( "\r\n" ) );
                    }
                    pTemp += 2 * bytes + 10; // Skip to the next possible line of hex entry
                    while ( *pTemp && isspace( *pTemp ) )
                        ++pTemp; // skip leading space
                }

                // Option "r"  : reset and run the simulation
                if ( input[ 0 ] == 'r' ) {
                    // If the variable 9 (Issue RESET) is not set, perform a RESET and run the simulation.
                    // If the variable was set, skip reset sequence since we might be testing it.
                    if ( resetAtClk < 0 )
                        DoReset();
                    running = true;
                }

                // Option "sR" : reset simulation variables to their default values
                if ( input[ 0 ] == 's' && input[ 1 ] == 'R' ) {
                    ResetSimulationVars();
                    input[ 1 ] = 0; // Proceed to dump all variables...
                }

                // Option "s"  : show and set internal control variables
                if ( input[ 0 ] == 's' ) {
                    // Show or set the simulation parameters
                    int var = 0, value;
                    int args = sscanf( &input[ 1 ], "%d %d", &var, &value );
                    // Parameter for the option #12 is read in as a hex; others are decimal by default
                    if ( var == 12 )
                        args = sscanf( &input[ 1 ], "%d %x", &var, &value );
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

                if ( input[ 0 ] == 'm' && input[ 1 ] == 'R' ) {
                    // Option "mR"  : reset RAM memory
                    memset( ram, 0, ramLen );
                    pf( F( "RAM reset to 00\r\n" ) );
                } else if ( input[ 0 ] == 'i' && input[ 1 ] == 'R' ) {
                    // Option "iR"  : reset IO memory
                    memset( io, 0, ioLen );
                    pf( F( "IO reset to 00\r\n" ) );
                } else if ( ( input[ 0 ] == 'm' || input[ 0 ] == 'i' ) && input[ 1 ] == 's' ) {
                    // Option "ms"  : set RAM memory from ADR to byte(s) B
                    // Option "is"  : set IO memory from ADR to byte(s) B
                    // ms/is ADR B B B ...
                    bool isRam = input[ 0 ] == 'm';
                    int i = 2;
                    if ( !isxdigit( input[ 2 ] ) )
                        i = nextHex( input, 0 ); // skip to start of adr
                    unsigned val;
                    if ( i && sscanf( pTemp + i, "%04X ", &adr ) == 1 ) {
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
                        pTemp[ 1 ] = 0; // "fall through" to mem/io dump
                    }
                }

                if ( input[ 0 ] == 'm' || input[ 0 ] == 'i' ) {
                    // Option "m START END"  : dump RAM memory
                    // Option "i START END"  : dump IO memory
                    // START and END are optional
                    // START defaults to 0 or ADR from "ms adr ..."
                    // END defaults to START + 0x100
                    // Option "mx" : same in intel hex format
                    bool isRam = input[ 0 ] == 'm';
                    bool isHex = false;
                    int i = 1, rc = 0;
                    if ( input[ 1 ] == 'x' ) {
                        isHex = true;
                        i = 2;
                    }
                    if ( !isxdigit( input[ i ] ) )
                        i = nextHex( input, 0 ); // skip to start of adr
                    if ( i )
                        rc = sscanf( input + i, "%04X", &adr );
                    adr &= 0xFF0;                                    // fit to line
                    if ( !end && rc && ( i = nextHex( input, i ) ) ) // one more argument
                        rc = sscanf( input + i, "%04X", &end );
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
                            cs -= (aaa)&0xFF;
                            cs -= ( aaa >> 8 ) & 0x0F;
                            for ( int j = 0; j < 0x10; j++ ) {
                                unsigned char val;
                                if ( isRam )
                                    val = ram[ ( aaa + j ) & ramMask ];
                                else
                                    val = io[ ( aaa + j ) & ioMask ];
                                pf( "%02X", val );
                                cs -= val;
                            }
                            pf( "%02X\r\n", cs );
                        }
                        if ( isRam )
                            pf( F( ":00000001FF\r\n" ) );
                        else
                            pf( F( ".00000001FF\r\n" ) );
                    } else { // normal dump, RAM or IO
                        pf( F( " %-3s   00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F\r\n" ), isRam ? "MEM" : " IO" );
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
                if ( input[ 0 ] == '?' || input[ 0 ] == 'h' ) {
                    pf( F( "B               - execute Basic interpreter\r\n" ) );
                    pf( F( "B?              - analyse Basic interpreter\r\n" ) );
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
        } // while ( !running )
    }     // if ( !runing )

    if ( runBasic ) {
        //
        // Do we have any pending serial-input?  If so
        // trigger an interrupt to the processor.
        //
        // (Active is "low".)
        //
        if ( Serial.available() )
            digitalWrite( INT, LOW );
        //
        // Tickle the CPU.
        //
        cpu.Tick();

    } else { // analyse RAM or ROM

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
        if ( m1Prev == 1 && m1 == 0 )
            T = 1, m1Count++;
        m1Prev = m1;
        bool suppressDump = false;
        if ( !traceRefresh & !rfsh )
            suppressDump = true;

        // If the number of M1 cycles has been reached, skip the rest since we dont
        // want to execute this M1 phase
        if ( stopAtM1 >= 0 && m1Count > stopAtM1 ) {
            sprintf( extraInfo, "%d M1 cycles reached", stopAtM1 ), running = false;
            pf( F( "--------------------------------------------------------------+\r\n" ) );
            goto nextLoop;
        }

        // If the address is tri-stated, skip checking various combinations of
        // control signals since they may also be floating and we can't detect that
        if ( !abTristated ) {
            uint8_t data = 0;
            // Simulate read from RAM
            if ( !mreq && !rd ) {
                if ( ab < ramBegin )
                    data = pgm_read_byte_near( rom + ab );
                else if ( ab <= ramEnd )
                    data = ram[ ab - ramBegin ];
                SetDataToDB( data );
                if ( !m1 )
                    sprintf( extraInfo, "Opcode read from %04X -> %02X", ab, data );
                else
                    sprintf( extraInfo, "Memory read from %04X -> %02X", ab, data );
            }

            // Simulate interrupt requesting a vector
            else if ( !m1 && !iorq ) {
                SetDataToDB( iorqVector );
                sprintf( extraInfo, "Pushing vector %02X", iorqVector );
            }

            // Simulate write to RAM
            else if ( !mreq && !wr ) {
                if ( ( ab >= ramBegin ) && ( ab <= ramEnd ) )
                    ram[ ab - ramBegin ] = db;
                sprintf( extraInfo, "Memory write to  %04X <- %02X", ab, db );
            }

            // Simulate I/O read
            else if ( !iorq && !rd ) {
                data = io[ ab & ioMask ];
                SetDataToDB( data );
                sprintf( extraInfo, "I/O read from %04X -> %02X", ab, data );
            }

            // Simulate I/O write
            else if ( !iorq && !wr ) {
                io[ ab & ioMask ] = db;
                sprintf( extraInfo, "I/O write to %04X <- %02X", ab, db );
            }

            // Capture memory refresh cycle
            else if ( !mreq && !rfsh ) {
                sprintf( extraInfo, "Refresh address  %04X", ab );
            }

            else
                GetDataFromDB();
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
        // delayMicroseconds( 4 );
        digitalWrite( CLK, LOW );
        // delayMicroseconds( 4 );

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
    nextLoop:;
    } // else ( !runBasic )
} // loop()


// Utility function to provide a meaningful printf to a serial port
// Function for fmt string in ram
void pf( const char *fmt, ... ) {
    // char tmp[ 256 ]; // resulting string limited to 256 chars
    va_list args;
    va_start( args, fmt );
    vsnprintf( tmp, tmpLen, fmt, args );
    va_end( args );
    Serial.print( tmp );
}


// Utility function to provide a meaningful printf to a serial port
// Overloaded function for fmt string in flash
void pf( const __FlashStringHelper *fmt, ... ) {
    // move string from flash to ram
    // char ftmp[ 256 ];
    memset( ftmp, 0, ftmpLen );
    strlcpy_P( ftmp, (const char PROGMEM *)fmt, 256 );
    // char tmp[ 256 ]; // resulting string limited to 256 chars
    va_list args;
    va_start( args, fmt );                // get args from input
    vsnprintf( tmp, tmpLen, ftmp, args ); // use format string in ram
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
    while ( s[ i ] && s[ i ] != ' ' )
        ++i;
    while ( s[ i ] && s[ i ] == ' ' )
        ++i;
    return ( isxdigit( s[ i ] ) ? i : 0 );
}


int readBytesUntilEOL( char *buf, int maxlen ) {
    int c, n = 0;
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
