// SPDX-License-Identifier: GPL-3.0-or-later
//
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
//      - Memory access is simulated using a 4096-byte pseudo-RAM memory
//      - I/O map is implemented with a buffer of 256 byte.
//      - I/O reads will return what was written in the I/O memory.
//      - I/O writes will modify the I/O memory.
//
// The analysis part of the SW is: Copyright 2014 by Goran Devic
// This source code is released under the GPL v2 software license.
//
// The updates to the original BASIC within this project are copyright Grant Searle:
// ; You have permission to use this for NON COMMERCIAL USE ONLY
// ; If you wish to use it elsewhere, please include an acknowledgement to myself (G.S.).
//
// My derived work is copyright 2024 M.Ho-Ro and provided under GPL v.3
//
//------------------------------------------------------------------------

#include <stdarg.h>
#include <stdint.h>


// Grant Searle's Basic ROM
#include "NascomBasic_GS.h"

// Hein Pragt's Basic ROM
#include "NascomBasic_HP.h"

//  Li Chen Wang's Tiny Basic ROM
#include "TinyBasic2.h"

//  Will Stevens' 1K Basic ROM
#include "basic1K.h"

#include "opcodes.h"

const uint32_t BAUDRATE = 115200;

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
static int halt;
static int mreq;
static int iorq;
static int rfsh;
static int m1;
static int busak;
static int wr;
static int rd;

// Control *input* pins of Z80, we write them into the dongle
static int zint = 1;
static int nmi = 1;
static int reset = 1;
static int busrq = 1;
static int wait = 1;

// Content of address and data wires
static uint16_t ab;
static uint8_t db;

// Clock counter after reset
static int clkCount;
static int clkCountHi;

// T-cycle counter
static int T;
static int m1Prev;

// M1-cycle counter
static int m1Count;

// Detection if the address or data bus is tri-stated
static bool abTristated = false;
static bool dbTristated = false;

// Simulation control variables
bool running = 0;        // Simulation is running or is stopped
int traceAlsoLowPhase; // Show both phases of a clock cycle
int traceRefresh;        // Trace refresh cycles
int tracePause;          // Pause for a key press every so many clocks
int tracePauseCount;     // Current clock count for tracePause
int stopAtClk;           // Stop the simulation after this many clocks
int stopAtM1;            // Stop at a specific M1 cycle number
int stopAtHalt;          // Stop when HALT signal gets active
int intAtClk;            // Issue INT signal at that clock number
int intOffAtClk;         // Reset INT signal at that clock number
int nmiAtClk;            // Issue NMI signal at that clock number
int nmiOffAtClk;         // Reset NMI signal at that clock number
int busrqAtClk;          // Issue BUSRQ signal at that clock number
int busrqOffAtClk;       // Reset BUSRQ signal at that clock number
int resetAtClk;          // Set RESET signal at that clock number
int resetOffAtClk;       // Reset RESET signal at that clock number
int waitAtClk;           // Issue WAIT signal at that clock number
int waitOffAtClk;        // Reset WAIT signal at that clock number
int clearAtClk;          // Clear all control signals at that clock number
byte iorqVector;         // Push IORQ vector (default is FF)
uint16_t pauseAddress;   // Pause at M1

// Buffer containing RAM memory for Z80 to access
const uint16_t ramLen = 0x1000; // 4 K
// more RAM for Basic program
// reuse as additional space for input and extra, tmp, ftmp
static uint8_t RAM[ ramLen + 0xA00 ]; // 6.5 K
const uint16_t ramMask = 0x0FFF;

static uint16_t ramBegin = 0;
static uint8_t ramBeginHi = 0;
static uint16_t ramEnd = ramBegin + ramLen - 1;
static uint8_t ramEndHi = ramEnd >> 8;

const uint16_t romLen = 0x2000;
static uint16_t romBegin = 0;
static uint8_t romBeginHi = 0;
static uint16_t romEnd = romBegin + romLen - 1;
static uint8_t romEndHi = romEnd >> 8;

uint8_t const *ROM = rom_gs;

/* RAM layout:
 * RAM -> 6656 (6.5K = 0x1A00) byte = 26 256 byte blocks
 *                    ftIiiii
 * End of RAM is used in parallel by other buffer
 *  RAM   26 blocks: |RRRRRRRRRRRRRRRRRRRRRRRRRR|
 *  ftmp   1 block:  |                      #   |
 *  tmp    1 block:  |                       #  |
 *  IO     1 block:  |                        # |
 *  input  1 block:  |                         #|
 *
 */
// Temp buffer to store input line at the end of Basic RAM, unused at analyser
static const int INPUT_SIZE = 256;
static char *input = (char *)RAM + sizeof( RAM ) - INPUT_SIZE;

// Temp buffer for extra dump info, at the end of input buffer (unused during output)
static const int EXTRA_SIZE = 64;
static char *extraInfo = (char *)RAM + sizeof( RAM ) - EXTRA_SIZE;

// Buffer simulating IO space for Z80 to access, at unused Basic RAM before input buffer
static const unsigned int ioLen = 0x100;
static uint8_t *IO = RAM + sizeof( RAM ) - INPUT_SIZE - ioLen;
static const unsigned int ioMask = 0xFF;

static const unsigned int tmpLen = 256;
static const unsigned int ftmpLen = 256;
static char *tmp = (char *)RAM + sizeof( RAM ) - INPUT_SIZE - ioLen - tmpLen;
static char *ftmp = (char *)RAM + sizeof( RAM ) - INPUT_SIZE - ioLen - tmpLen - ftmpLen;

static int doEcho = 1;      // Echo received commands
static int verboseMode = 0; // Enable debugging output

static uint8_t cmd = 0;
static uint8_t opt = 0;

// default 6850 ACIA port addresses
static int ioDataPort = 1;
static int ioStatPort = 2;

// the default Arduino LED
const int ledZ80isRunning = 13;


// -----------------------------------------------------------
// Arduino initialization entry point
// -----------------------------------------------------------
void setup() {
    Serial.begin( BAUDRATE );
    Serial.flush();
    Serial.setTimeout( 1000L * 60 * 60 );

    pf( F( "\r\nZ80 Analyser V 1.0\r\n" ) );

    //
    // Empty the serial-buffer before we launch.
    //
    while ( Serial.available() )
        Serial.read();

    ResetSimulationVars();

    pinMode( ledZ80isRunning, OUTPUT );
    digitalWrite( ledZ80isRunning, LOW );

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
}


// Resets all simulation variables to their defaults
void ResetSimulationVars() {
    traceAlsoLowPhase = 0;   // Show both phases of a clock cycle
    traceRefresh = 1;        // Trace refresh cycles
    tracePause = 100;        // Pause for a keypress every so many clocks
    stopAtClk = -1;          // Stop the simulation after this many clocks
    stopAtM1 = -1;           // Stop at a specific M1 cycle number
    stopAtHalt = 1;          // Stop when HALT signal gets active
    intAtClk = -1;           // Set INT signal at that clock number
    intOffAtClk = -1;        // Reset INT signal at that clock number
    nmiAtClk = -1;           // Set NMI signal at that clock number
    nmiOffAtClk = -1;        // Reset NMI signal at that clock number
    busrqAtClk = -1;         // Set BUSRQ signal at that clock number
    busrqOffAtClk = -1;      // Reset BUSRQ signal at that clock number
    resetAtClk = -1;         // Set RESET signal at that clock number
    resetOffAtClk = -1;      // Reset RESET signal at that clock number
    waitAtClk = -1;          // Set WAIT signal at that clock number
    waitOffAtClk = -1;       // Reset WAIT signal at that clock number
    clearAtClk = -1;         // Clear all control signals at that clock number
    iorqVector = 0xFF;       // Push IORQ vector
    pauseAddress = 0;        // Pause at M1 at this address
    verboseMode = 0;         // Enable debugging output
}


static bool CB = false;
static bool DD = false;
static bool ED = false;
static bool FD = false;

static bool isPrefix( uint8_t data ) { return data == 0xCB || data == 0xDD || data == 0xED || data == 0xFD; }

static bool isCB() { return CB; }
static bool isDD() { return DD; }
static bool isDDCB() { return DD && CB; }
static bool isED() { return ED; }
static bool isFD() { return FD; }
static bool isFDCB() { return FD && CB; }


// Issue a RESET sequence to Z80 and reset internal counters
void DoReset() {
    CB = DD = ED = FD = false; // reset prefixes
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
    tracePauseCount = -2;
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
// In addition, try to detect when a bus is tri-stated and write 0xFFFF if so.
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
            pf( F( "+-----------------------------------------------------------------+\r\n" ) );
        pf( F( "| #%05d%c T%-4d AB:%s DB:%s  %s %s %s %s %s %s %s %s |%s%s%s%s%s %s\r\n" ), clkCount < 0 ? 0 : clkCount,
            clkCountHi ? 'H' : 'L', T, abStr, dbStr, m1 ? "  " : "M1", rfsh ? "    " : "RFSH", mreq ? "    " : "MREQ",
            rd ? "  " : "RD", wr ? "  " : "WR", iorq ? "    " : "IORQ", busak ? "     " : "BUSAK", halt ? "    " : "HALT",
            reset ? " " : "R", nmi ? " " : "N", zint ? " " : "I", busrq ? " " : "B", wait ? " " : "W", extraInfo );
    }
    extraInfo[ 0 ] = 0;
}

//--------------------------------------------------------
// Trace/simulation control handler
//--------------------------------------------------------
int controlHandler() {
    bool singleStep = false;
    if ( *extraInfo ) {
        pf( F( "  ----------------------------------------------------------------+\r\n" ) );
        pf( F( "Simulation stopped: %s\r\n" ), extraInfo );
        *extraInfo = 0;
    }
    digitalWrite( CLK, HIGH );
    zint = nmi = busrq = wait = 1;
    WriteControlPins();

    while ( !running ) {
        pf( F( "$ " ) );
        // Expect a command from the serial port
        while ( !Serial.available() )
            ;
        unsigned adr = 0;
        unsigned end = 0;
        memset( input, 0, INPUT_SIZE );

        if ( !readBytesUntilEOL( input, INPUT_SIZE - 1 ) )
            continue;

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
                        RAM[ ( adr + i ) & ramMask ] = hex( pTemp + 8 + 2 * i );
                    else
                        IO[ ( adr + i ) & ioMask ] = hex( pTemp + 8 + 2 * i );
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
        cmd = input[ 0 ];
        opt = input[ 1 ];
        switch ( toupper( cmd ) ) {
        // this entry selects one of the three Basic setups
        // either for analysis (A) or execution (B)
        case 'A':
        case 'B':
            // "move" the RAM on top of ROM
            if ( toupper( opt ) == 'X' ) { // Hein's Basic
                ROM = rom_hp;
                ramBegin = 0x2000;
            } else if ( toupper( opt ) == 'T' ) { // Tiny Basic
                ROM = rom_tb2;
                ramBegin = 0x0800;
            } else if ( toupper( opt ) == '1' ) { // 1K Basic
                ROM = rom_b1k;
                ramBegin = 0x0400;
            } else { // default, Grant's Basic
                ROM = rom_gs;
                ramBegin = 0x2000;
                opt = 'B';
            }

            if ( toupper( cmd ) == 'A' ) { // analyse
                ramEnd = ramBegin + ramLen - 1;
                if ( toupper( opt ) == 'X' ) { // Hein's 8 K BASIC need RAM size info
                    // signal 4K memory size to Basic
                    RAM[ 0 ] = ( ramEnd + 1 ) & 0xFF;
                    RAM[ 1 ] = ( ramEnd + 1 ) >> 8;
                }
                singleStep = running = true;
            } else { // run Basic
                memset( RAM, 0, sizeof( RAM ) );
                ramEnd = ramBegin + sizeof( RAM ) - 1;
                if ( toupper( opt ) == 'X' ) { // Hein's 8 K BASIC needs RAM size info
                    // signal memory size to Basic
                    RAM[ 0 ] = ( ramEnd + 1 ) & 0xFF;
                    RAM[ 1 ] = ( ramEnd + 1 ) >> 8;
                    runWithInterrupt( 0x2000, sizeof( RAM ) ); // returns after 'MONITOR' cmd
                } else if (toupper( opt ) == 'B') {            // NASCOM ROM BASIC
                    runWithInterrupt( 0x2000, sizeof( RAM ) );  // returns after 'MONITOR' cmd
                } else if (toupper( opt ) == 'T') {            // Tiny BASIC
                    runWithInterrupt( 0x800, sizeof( RAM ) );  // returns after 'HALT' cmd
                } else {                                       // 1K BASIC
                    runWithInterrupt( 0x400, sizeof( RAM ) );  // does not return w/o tricks
                }
                while ( Serial.available() )
                    Serial.read(); // clear buffer
                pf( F( "BASIC halted\r\n" ) );
            }
            // DoReset();
            break;

        case 'X': // execute program from RAM
            ioDataPort = 1; // default 6850 data port
            ioStatPort = 2; // default 6850 status port
            sscanf( input + 2, "%d %d", &ioDataPort, &ioStatPort );
            // no ROM, RAM starts at 0x0000
            ramBegin = 0x0000;
            ramEnd = ramBegin + sizeof( RAM ) - 1;       // full size ram
            runWithInterrupt( ramBegin, sizeof( RAM ) ); // returns with 'HALT' status
            while ( Serial.available() )                 // clear buffer
                Serial.read();
            break; // go to analyser loop

        case 'E':
            if ( opt == '0' )
                doEcho = false;
            else if ( opt == '1' )
                doEcho = true;
            else
                break;
            pf( "Echo %s\r\n", doEcho ? "on" : "off" );
            break;

        case 'V':
            if ( isdigit( opt ) )
                verboseMode = opt - '0';
            break;

        case 'C': // continue
            // Continue from a paused simulation in single step mode
            running = singleStep = true;
            break;

        case 'R': // reset
            // Perform a RESET and start the simulation in single step mode.
            ramBegin = 0;
            ramBeginHi = 0;
            ramEnd = ramBegin + ramLen - 1;
            ramEndHi = ramEnd >> 8;
            DoReset();
            running = singleStep = true;
            break;

        case '#':
            // Option "##" : reset simulation variables to their default values
            if ( opt == '#' ) {
                ResetSimulationVars();
                opt = 0; // Proceed to dump all variables...
            }
            else {
                // Show or set the simulation parameters
                int value, value2;
                int args = 0;
                // Parameter for the option V and A is read in as a hex; others are decimal by default
                if ( toupper( opt ) == 'V' || toupper( opt ) == 'A' )
                    args = sscanf( input + 2, "%x", &value );
                else if ( toupper( opt ) == 'I' || toupper( opt ) == 'N' || toupper( opt ) == 'B'
                       || toupper( opt ) == 'W' || toupper( opt ) == 'R' )
                    args = sscanf( input + 2, "%d %d", &value, &value2 );
                else
                    args = sscanf( input + 2, "%d", &value );
                if ( args > 0 ) {
                  switch ( toupper( opt ) ) {
                    case 'L':
                        traceAlsoLowPhase = value;
                        break;
                    case 'F':
                        traceRefresh = value;
                        break;
                    case 'P':
                        tracePause = value;
                        break;
                    case 'C':
                        stopAtClk = value;
                        break;
                    case 'M':
                        stopAtM1 = value;
                        break;
                    case 'H':
                        stopAtHalt = value;
                        break;
                    case 'I':
                        intAtClk = value;
                        if ( args == 2 )
                            intOffAtClk = value2;
                        break;
                    case 'N':
                        nmiAtClk = value;
                        if ( args == 2 )
                            nmiOffAtClk = value2;
                        break;
                    case 'B':
                        busrqAtClk = value;
                        if ( args == 2 )
                            busrqOffAtClk = value2;
                        break;
                    case 'R':
                        resetAtClk = value;
                        if ( args == 2 )
                            resetOffAtClk = value2;
                        break;
                    case 'W':
                        waitAtClk = value;
                        if ( args == 2 )
                            waitOffAtClk = value2;
                        break;
                    case '-':
                        clearAtClk = value;
                        break;
                    case 'V':
                        iorqVector = value & 0xFF;
                        break;
                    case 'A':
                        pauseAddress = value & 0xFFFF;
                        break;
                  }
                }
            }
            pf( F( "  ------ Simulation variables --------\r\n" ) );
            pf( F( "  #L  Trace also Low clock     = %4d\r\n" ), traceAlsoLowPhase );
            pf( F( "  #F  Trace reFresh cycles     = %4d\r\n" ), traceRefresh );
            pf( F( "  #K  Pause for keypress every = %4d\r\n" ), tracePause );
            pf( F( "  #A  Pause at M1 Addr. #(hex  = %04X\r\n" ), pauseAddress );
            pf( F( "  #C  Stop after Clock #       = %4d\r\n" ), stopAtClk );
            pf( F( "  #M  Stop after M1 cycle #    = %4d\r\n" ), stopAtM1 );
            pf( F( "  #H  Stop at HALT             = %4d\r\n" ), stopAtHalt );
            pf( F( "  #V  IORQ vector #(hex)       =   %02X\r\n" ), iorqVector );
            pf( F( "  #I  INT at clock #           = %4d %4d\r\n" ), intAtClk, intOffAtClk );
            pf( F( "  #N  NMI at clock #           = %4d %4d\r\n" ), nmiAtClk, nmiOffAtClk );
            pf( F( "  #B  BUSRQ at clock #         = %4d %4d\r\n" ), busrqAtClk, busrqOffAtClk );
            pf( F( "  #R  RESET at clock #         = %4d %4d\r\n" ), resetAtClk, resetOffAtClk );
            pf( F( "  #W  WAIT at clock #          = %4d %4d\r\n" ), waitAtClk, waitOffAtClk );
            pf( F( "  #-  Clear all at clock #     = %4d\r\n" ), clearAtClk );
            break;

        case 'M':   // memory
        case 'I': { // IO
            if ( toupper( cmd ) == 'M' && toupper( opt ) == 'R' ) {
                // Option "MR"  : reset RAM memory
                memset( RAM, 0, ramLen );
                pf( F( "RAM reset to 00\r\n" ) );
            } else if ( toupper( cmd ) == 'I' && toupper( opt ) == 'R' ) {
                // Option "IR"  : reset IO memory
                memset( IO, 0, ioLen );
                pf( F( "IO reset to 00\r\n" ) );
            } else if ( ( toupper( cmd ) == 'M' || toupper( cmd ) == 'I' )
                       && toupper( opt ) == 'S' ) {
                // Option "MS"  : set RAM memory from ADR to byte(s) B
                // Option "IS"  : set IO memory from ADR to byte(s) B
                // ms/is ADR B B B ...
                bool isRam = toupper( cmd ) == 'M';
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
                                RAM[ end++ ] = val;
                            else
                                IO[ end++ ] = val;
                        } else
                            break;
                    }
                    pTemp[ 1 ] = 0; // "fall through" to mem/IO dump
                }
            }

            // Option "M START END"  : dump RAM memory
            // Option "I START END"  : dump IO memory
            // START and END are optional
            // START defaults to 0 or ADR from "ms adr ..."
            // END defaults to START + 0x100
            // Option "MX" : same in intel hex format
            bool isRam = toupper( cmd ) == 'M';
            bool isHex = false;
            bool asASCII = false;
            int i = 1, rc = 0;
            if ( toupper( opt ) == 'X' ) {
                isHex = true;
                i = 2;
            } else if ( toupper( opt ) == 'A' ) {
                asASCII = true;
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
                const int hexLen = 0x10;
                for ( unsigned aaa = adr; aaa < end; aaa += hexLen ) {
                    if ( isRam )
                        pf( F( ":%02X%04X00" ), hexLen, aaa & ramMask );
                    else
                        pf( F( ".%02X%04X00" ), hexLen, aaa & ioMask );
                    unsigned char cs = 0;
                    cs -= hexLen; // datalen
                    cs -= aaa & 0xFF;
                    cs -= ( aaa >> 8 ) & 0x0F;
                    for ( int j = 0; j < hexLen; j++ ) {
                        unsigned char val;
                        if ( isRam )
                            val = RAM[ ( aaa + j ) & ramMask ];
                        else
                            val = IO[ ( aaa + j ) & ioMask ];
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
                pf( F( "   %-3s   00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F\r\n" ), isRam ? "MEM" : " IO" );
                pf( F( "+---------------------------------------------------------+\r\n" ) );
                for ( unsigned aaa = adr; aaa < end; aaa += 0x10 ) {
                    if ( isRam )
                        pf( F( "| %04X : " ), aaa & ramMask );
                    else
                        pf( F( "| %04X : " ), aaa & ioMask );
                    for ( int j = 0; j < 16; j++ ) {
                        if ( isRam )
                            pf( F( "%02X " ), RAM[ ( aaa + j ) & ramMask ] );
                        else
                            pf( F( "%02X " ), IO[ ( aaa + j ) & ioMask ] );
                        if ( j == 7 )
                            pf( F( " " ) );
                    }
                    if ( isRam && asASCII ) {
                        pf( F( "|  " ) );
                        for ( int j = 0; j < 16; j++ ) {
                            uint8_t c = RAM[ ( aaa + j ) & ramMask ];
                            Serial.write( isprint( c ) ? c : '.' );
                        }
                        Serial.println();
                    } else
                        pf( F( "|\r\n" ) );
                }
                pf( F( "+---------------------------------------------------------+\r\n" ) );
            }
        } break;

        case '?':
        case 'H':
            // Print help
            pf( F( "A                - analyse Grant Searle's ROM Basic\r\n" ) );
            pf( F( "AT               - analyse Li Chen Wang's tiny ROM Basic\r\n" ) );
            pf( F( "AX               - analyse Hein Pragt's ROM Basic\r\n" ) );
            pf( F( "B                - execute Grant Searle's ROM Basic\r\n" ) );
            pf( F( "BT               - execute Li Chen Wang's tiny ROM Basic\r\n" ) );
            pf( F( "BX               - execute Hein Pragt's ROM Basic\r\n" ) );
            pf( F( "X [DATA [STAT]]  - execute the RAM content, opt. data and status port\r\n" ) );
            pf( F( "R                - reset CPU, start the simulation at RAM address 0x0000\r\n" ) );
            pf( F( "C                - continue the simulation after halt\r\n" ) );
            pf( F( ":INTEL-HEX       - reload RAM buffer with ihex data stream\r\n" ) );
            pf( F( ".INTEL-HEX       - reload IO buffer with a modified ihex data stream\r\n" ) );
            pf( F( "M [START [END]]  - dump the RAM buffer from START to END\r\n" ) );
            pf( F( "MA [START [END]] - dump the RAM buffer from START to END (with ASCII)\r\n" ) );
            pf( F( "MX [START [END]] - dump the RAM buffer as ihex from START to END\r\n" ) );
            pf( F( "MR               - reset the RAM buffer to 00\r\n" ) );
            pf( F( "MS ADR B B B ..  - set RAM buffer at ADR with data byte(s) B\r\n" ) );
            pf( F( "I [START [END]]  - dump the IO buffer from START to END\r\n" ) );
            pf( F( "IX [START [END]] - dump the IO buffer as modified ihex from START to END\r\n" ) );
            pf( F( "IR               - reset the IO buffer to 00\r\n" ) );
            pf( F( "IS ADR B B B ..  - set IO buffer at ADR with data byte(s) B\r\n" ) );
            pf( F( "#                - show simulation variables\r\n" ) );
            pf( F( "#N VALUE         - set simulation variable number N to a VALUE\r\n" ) );
            pf( F( "#R               - reset simulation variables to their default values\r\n" ) );
            pf( F( "E0               - set echo off\r\n" ) );
            pf( F( "E1               - set echo on (default)\r\n" ) );
            pf( F( "VN               - set verboseMode to N (default = 0)\r\n" ) );
            break;
        } // switch ( toupper( cmd ) )
    }     // while ( !running )
    return singleStep;
}


// -----------------------------------------------------------
// Main loop routine runs over and over again forever
// -----------------------------------------------------------
void loop() {
    uint16_t m1Address = 0;
    static bool singleStep = false;

    if ( !running ) {
        singleStep = controlHandler();
    }

    //--------------------------------------------------------
    // Clock goes high
    //--------------------------------------------------------
    digitalWrite( CLK, HIGH );

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
        pf( F( "  --------------------------------------------------------------+\r\n" ) );
        return;
    }

    // If the address is tri-stated, skip checking various combinations of
    // control signals since they may also be floating and we can't detect that
    if ( !abTristated ) {
        static bool ioRdPrev = true;
        static bool ioWrPrev = true;
        uint8_t data = 0;
        // Simulate read from RAM
        if ( !mreq && !rd ) {
            if ( ab < ramBegin )
                data = pgm_read_byte_near( ROM + ab );
            else if ( ab <= ramEnd )
                data = RAM[ ab - ramBegin ];
            SetDataToDB( data );
            if ( !m1 ) {
                const char *opc;
                m1Address = ab;
                sprintf( extraInfo, "Opcode read from %s %04X -> %02X  ", ab < ramBegin ? "ROM" : "RAM", ab, data );
                if ( data == 0xCB ) {
                    CB = true;
                    ED = false;
                } else if ( data == 0xDD ) {
                    CB = false;
                    DD = true;
                    ED = false;
                    FD = false;
                } else if ( data == 0xED ) {
                    CB = false;
                    DD = false;
                    ED = true;
                    FD = false;
                } else if ( data == 0xFD ) {
                    CB = false;
                    ED = false;
                    DD = false;
                    FD = true;
                }
                if ( isPrefix( data ) )
                    opc = opcode[ data ];
                else if ( isDDCB() )
                    opc = opcodeDDCB[ data ];
                else if ( isFDCB() )
                    opc = opcodeFDCB[ data ];
                else if ( isCB() )
                    opc = opcodeCB[ data ];
                else if ( isDD() )
                    opc = opcodeDD[ data ];
                else if ( isED() )
                    opc = opcodeED[ data ];
                else if ( isFD() )
                    opc = opcodeFD[ data ];
                else
                    opc = opcode[ data ];
                strlcpy_P( extraInfo + strlen( extraInfo ), (PROGMEM const char *)opc, 16 );
                if ( !isPrefix( data ) ) // clear all prefixes
                    CB = DD = ED = FD = false;
            } else {
                sprintf( extraInfo, "Memory read from %s %04X -> %02X", ab < ramBegin ? "ROM" : "RAM", ab, data );
            }
        }

        // Simulate interrupt requesting a vector
        else if ( !m1 && !iorq ) {
            SetDataToDB( iorqVector );
            sprintf( extraInfo, "Pushing vector %02X", iorqVector );
        }

        // Simulate write to RAM
        else if ( !mreq && !wr ) {
            GetDataFromDB();
            if ( ( ab >= ramBegin ) && ( ab <= ramEnd ) )
                RAM[ ab - ramBegin ] = db;
            sprintf( extraInfo, "Memory write to %s  %04X <- %02X", ab < ramBegin ? "ROM" : "RAM", ab, db );
        }

        // Simulate I/O read
        else if ( !iorq && !rd ) {
            static char key = 0;
            if ( ioRdPrev && ( ( ab & 0xFF ) == 2 ) ) {
                pf( "KBD: " );
                while ( !Serial.available() )
                    ;
                key = Serial.read();
                if ( key == '!' )
                    key = 0x0D;
            } else if ( ioRdPrev && ( ( ab & 0xFF ) == 1 ) ) {
                SetDataToDB( key );
                key = 0;
            } else {
                data = IO[ ab & ioMask ];
                sprintf( extraInfo, "I/O read from %04X -> %02X", ab, data );
            }
        }

        // Simulate I/O write
        else if ( !iorq && !wr ) {
            GetDataFromDB();
            IO[ ab & ioMask ] = db;
            sprintf( extraInfo, "I/O write to %04X <- %02X", ab, db );
            if ( ( toupper( opt ) == 'X' && ioWrPrev && ( ( ab & 0xFF ) == 0 ) ) ||
                 ( toupper( opt ) == 'T' && ioWrPrev && ( ( ab & 0xFF ) == 1 ) ) ||
                 ( toupper( opt ) == 'B' && ioWrPrev && ( ( ab & 0xFF ) == 1 ) ) ) // console out
                sprintf( extraInfo + strlen( extraInfo ), "  CONOUT: %c", isprint( db ) ? db : ' ' );
        }

        // Capture memory refresh cycle
        else if ( !mreq && !rfsh ) {
            sprintf( extraInfo, "Refresh address  %04X", ab );
        }

        else
            GetDataFromDB();
        ioRdPrev = iorq || rd;
        ioWrPrev = iorq || wr;
    } else
        GetDataFromDB();

    DumpState( suppressDump );

    // If the user wanted to pause simulation after a certain number of
    // clocks, handle it here.
    // Enter resumes the simulation, Space advances one clock.
    // Other keys stop the simulation to issue that command
    if ( singleStep || ( tracePause > 0 && tracePause == tracePauseCount ) || ( pauseAddress && pauseAddress == m1Address ) ) {
        singleStep = false;
        while ( Serial.available() == 0 )
            ;                          // wait
        if ( Serial.peek() == '\r' ) { // step
            Serial.read();
            singleStep = true;
        } else if ( Serial.peek() == ' ' ) { // run
            Serial.read();                   // consume SPACE
            while ( Serial.peek() == '\r' )  // remove CR if buffered terminal
                Serial.read();
        } else { // command char
            running = false;
            singleStep = controlHandler();
        }
        if ( tracePause == tracePauseCount )
            tracePauseCount = 0;
    }

    //--------------------------------------------------------
    // Clock goes low
    //--------------------------------------------------------
    digitalWrite( CLK, LOW );

    clkCountHi = 0;
    if ( traceAlsoLowPhase ) {
        ReadControlState();
        GetAddressFromAB();
        DumpState( suppressDump );
    }

    // Perform various actions at the requested clock number
    // if the count is positive (we start it at -2 to skip initial 2T)
    if ( clkCount >= 0 ) {
        if ( clkCount == intAtClk )
            zint = 0;
        if ( clkCount == intOffAtClk )
            zint = 1;
        if ( clkCount == nmiAtClk )
            nmi = 0;
        if ( clkCount == nmiOffAtClk )
            nmi = 1;
        if ( clkCount == busrqAtClk )
            busrq = 0;
        if ( clkCount == busrqOffAtClk )
            busrq = 1;
        if ( clkCount == resetAtClk )
            reset = 0;
        if ( clkCount == resetOffAtClk )
            reset = 1;
        if ( clkCount == waitAtClk )
            wait = 0;
        if ( clkCount == waitOffAtClk )
            wait = 1;
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
} // loop()
// -----------------------------------------------------------
// End of main loop routine
// -----------------------------------------------------------


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
        if ( c != 0x08 && c != 0x7F ) { // BS or DEL
            if ( doEcho )
                Serial.write( c );
            *buf++ = c;
            --maxlen;
            ++n;
        } else if ( n ) {
            *--buf = 0;
            ++maxlen;
            --n;
            if ( doEcho ) {
                Serial.write( 0x08 );
                Serial.write( ' ' );
                Serial.write( 0x08 );
            }
        }
    }
    if ( doEcho )
        Serial.write( "\r\n" );
    *buf = 0;
    return n;
}


///////////////////////////////////////////////////////////////////////////////////////
// here comes Hein Pragts Basic interface idea that runs the Z80 with a clk of ~200 kHz
// /RD and /WR trigger ATmega interrupts and the ISR stops the Z80 via /WAIT until done
///////////////////////////////////////////////////////////////////////////////////////

// Pin defines

#define RD_PIN PD2
#define WR_PIN PD3

// Z80 RD or WR pin status
#define zRD ( ( PIND & ( 1 << RD_PIN ) ) == 0 )
#define zWR ( ( PIND & ( 1 << WR_PIN ) ) == 0 )

#define MREQ_PIN PK1
#define IORQ_PIN PK2
#define HALT_PIN PK3
#define BUSAK_PIN PK4
#define M1_PIN PK5
#define RFSH_PIN PK6

// Test MREQ or IOREQ pin
#define zMREQ ( ( PINK & ( 1 << MREQ_PIN ) ) == 0 )
#define zIORQ ( ( PINK & ( 1 << IORQ_PIN ) ) == 0 )
// Test HALT or BUSAK pin
#define zHALT ( ( PINK & ( 1 << HALT_PIN ) ) == 0 )
#define zBUSAK ( ( PINK & ( 1 << BUSAK_PIN ) ) == 0 )
// Test M1 or RFSH pin
#define zM1 ( ( PINK & ( 1 << M1_PIN ) ) == 0 )
#define zRFSH ( ( PINK & ( 1 << RFSH_PIN ) ) == 0 )


// A0..A7
#define ADDR_LO PINA
// A8..A15
#define ADDR_HI PINC

#define CLK_PIN_NAME PB4

#define INT_PIN_NAME PG0
#define RESET_PIN_NAME PG1
#define WAIT_PIN_NAME PG2
#define NMI_PIN_NAME PG5
#define BUSRQ_PIN_NAME PH0

// ctrl out on port G
inline void WAIT_LOW() { PORTG &= ~( 1 << WAIT_PIN_NAME ); }
inline void WAIT_HIGH() { PORTG |= ( 1 << WAIT_PIN_NAME ); }
inline void RESET_LOW() { PORTG &= ~( 1 << RESET_PIN_NAME ); }
inline void RESET_HIGH() { PORTG |= ( 1 << RESET_PIN_NAME ); }
inline void INT_LOW() { PORTG &= ~( 1 << INT_PIN_NAME ); }
inline void INT_HIGH() { PORTG |= ( 1 << INT_PIN_NAME ); }
inline void NMI_LOW() { PORTG &= ~( 1 << NMI_PIN_NAME ); }
inline void NMI_HIGH() { PORTG |= ( 1 << NMI_PIN_NAME ); }

// ctrl out on PORTH
inline void BUSRQ_LOW() { PORTH &= ~( 1 << BUSRQ_PIN_NAME ); }
inline void BUSRQ_HIGH() { PORTH |= ( 1 << BUSRQ_PIN_NAME ); }


const long fClk = 200000; // Z80 clock speed must be <= 200 kHz


void runWithInterrupt( uint16_t romLen, uint16_t ramLen ) {
    // Serial.begin( BAUDRATE );
    Serial.println( "Start Z80" );

    digitalWrite( ledZ80isRunning, HIGH );

    // Z80 CTRL OUT
    pinMode( M1, INPUT_PULLUP );   // M1
    pinMode( IORQ, INPUT_PULLUP ); // IOREQ
    pinMode( MREQ, INPUT_PULLUP ); // MREQ
    pinMode( WR, INPUT_PULLUP );   // WR
    pinMode( RD, INPUT_PULLUP );   // RD

    // Data bus default in
    DDR_DB = 0x00;  // Data Bus in
    PORT_DB = 0x00; // No pull-up

    // Address bus default in
    DDR_AL = 0x00;  // Address Bus Low in
    PORT_AL = 0x00; // No pull-up
    DDR_AH = 0x00;  // Address Bus High in
    PORT_AH = 0x00; // No pull-up

    // Z80 CTRL IN
    pinMode( WAIT, OUTPUT );
    digitalWrite( WAIT, HIGH );
    pinMode( RESET, OUTPUT );
    digitalWrite( RESET, HIGH );
    pinMode( INT, OUTPUT );
    digitalWrite( INT, HIGH );
    pinMode( NMI, OUTPUT );
    digitalWrite( NMI, HIGH );
    pinMode( BUSRQ, OUTPUT );
    digitalWrite( BUSRQ, HIGH );

    RESET_LOW();

    // Pin Interrupts RD and WR Trigger falling edge
    EICRA |= ( 1 << ISC31 ) + ( 0 << ISC30 ) + ( 1 << ISC21 ) + ( 0 << ISC20 );
    EIMSK |= ( 1 << RD_PIN ) + ( 1 << WR_PIN );

    // Clock Timer2 -> Z80 ClK
    DDRB |= ( 1 << CLK_PIN_NAME );                          // Output
    TCCR2A = ( 0 << COM2A1 ) + ( 1 << COM2A0 );             // Toggle OC2A on Compare Match
    TCCR2A |= ( 1 << WGM21 );                               // CTC Mode
    TCCR2B = ( 0 << CS22 ) + ( 0 << CS21 ) + ( 1 << CS20 ); // No Prescaling
    OCR2A = 16000000L / fClk - 1;

    romBegin = 0;
    romEnd = romLen - 1;
    ramBegin = romLen;
    ramEnd = ramBegin + ramLen - 1;

    // values for 8 bit comparison
    romBeginHi = romBegin >> 8;
    romEndHi = romEnd >> 8;
    ramBeginHi = ramBegin >> 8;
    ramEndHi = ramEnd >> 8;

    delay( 1 );
    RESET_HIGH(); // ready to go

    while ( true ) { // background tasks can be done here
        ;            // the Z80 interrupts this loop
        if ( 0 == digitalRead( HALT ) ) {
            EIMSK &= ~( ( 1 << RD_PIN ) + ( 1 << WR_PIN ) ); // DI ext INT
            TCCR2A = 0;                                      // stop CLK
            TCCR2B = 0;
            digitalWrite( CLK, LOW );
            digitalWrite( ledZ80isRunning, LOW );
            return;
        }
    }
}


// -------------------------------------
// Handle Arduino interups
// -------------------------------------

// ------------------------
// Read data from Data Bus
//-------------------------

inline uint8_t DATA_GET() {
    DDRL = 0;    // Set in
    return PINL; // Read
}

// ------------------------
// Write data to Data Bus
// ------------------------

inline void DATA_PUT( uint8_t d ) {
    DDRL = 0xff; // Set out
    PORTL = d;   // Write
}

// -----------------------------
// Z80 CPU Reads -> RD interrupt
// -----------------------------

ISR( INT2_vect ) { // Handle RD
    WAIT_LOW();
    if ( zMREQ ) {                     // Handle MREQ
        if ( ADDR_HI >= ramBeginHi ) { // RAM read
            if ( ADDR_HI <= ramEndHi ) {
                DATA_PUT( RAM[ (uint16_t)( ADDR_LO | ( ADDR_HI << 8 ) ) - ramBegin ] );
            } else {
                DATA_PUT( 0 );
            }
        } else { // ROM read
            DATA_PUT( pgm_read_byte( ROM + (uint16_t)( ADDR_LO | ( ADDR_HI << 8 ) ) ) );
        }
    } else if ( zIORQ ) {     // Handle IORQ - Simulate 6850 at addresses 1 and 2
        if ( ADDR_LO == ioDataPort ) { // Port 1 returns character from keyboard
            if ( Serial.available() == 0 ) {
                DATA_PUT( 0 );
            } else {
                DATA_PUT( islower( cmd ) ? (char)Serial.read() : toupper( (char)Serial.read() ) );
            }
        } else if ( ADDR_LO == ioStatPort ) { // Port 2 returns bit 0 set if a key has been pressed.
            if ( Serial.available() > 0 ) {
                DATA_PUT( 0x03 ); // RDRF=1, TDRE=1
            } else {
                DATA_PUT( 0x02 ); // RDRF=0, TDRE=1
            }
        } else             // other ports
            DATA_PUT( 0 ); // Otherwise, it returns 0
    }
    WAIT_HIGH();
}


// ------------------------------
// Z80 CPU Writes -> WR Interrupt
// ------------------------------

ISR( INT3_vect ) { // Handle WR
    WAIT_LOW();
    if ( zMREQ ) { // Handle MREQ
        if ( ADDR_HI >= ramBeginHi && ADDR_HI <= ramEndHi ) {
            RAM[ (uint16_t)( ADDR_LO | ( ADDR_HI << 8 ) ) - ramBegin ] = DATA_GET();
        }
    } else if ( zIORQ ) {                      // Handle IORQ
        if ( ADDR_LO == 1 ) {                  // "Serial" OUT
            Serial.write( DATA_GET() & 0x7F ); // 7 bit ASCII
        }
    }
    WAIT_HIGH();
}
