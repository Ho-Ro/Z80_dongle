
// The MIT License (MIT)

// Copyright (c) 2019 Erturk Kocalar, http://8Bitforce.com/
// Copyright (c) 2019 Steve Kemp, https://steve.kemp.fi/

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#include "z80retroshield.h"
#include <Arduino.h>
#include <stdlib.h>

#include <avr/pgmspace.h>

/* Digital Pin Assignments */
static inline void DATA_OUT( uint8_t data ) { ( PORTL ) = data; }
static inline uint8_t DATA_IN( void ) { return ( PINL ); }
static inline uint8_t ADDR_H( void ) { return ( PINC ); }
static inline uint8_t ADDR_L( void ) { return ( PINA ); }
static inline unsigned int ADDR( void ) { return ( (unsigned int)( ADDR_H() << 8 | ADDR_L() ) ); }

static const uint8_t uP_RESET_N = 40; // RG1, was 38/PD7
static const uint8_t uP_M1_N = A13;   // PK5, was --
static const uint8_t uP_RFSH_N = A14; // PK6, was --
static const uint8_t uP_MREQ_N = A9;  // PK1, was 41/PG0
static const uint8_t uP_IORQ_N = A10; // PK2, was 39/PG2
static const uint8_t uP_RD_N = 19;    // PD2, was 53/RB0
static const uint8_t uP_WR_N = 18;    // PD3, was 40/RG1
static const uint8_t uP_NMI_N = 4;    // PG5, was 51/RB2
static const uint8_t uP_INT_N = 41;   // PG0, was 50/RB3
static const uint8_t uP_CLK = 10;     // PB4, was 52/PB1

//
// Fast routines to drive clock signals high/low; faster than digitalWrite
// required to meet >100kHz clock
//

static inline void SET_CLK( uint8_t state ) {
    if ( state )
        PORTB |= ( 1 << 4 );
    else
        PORTB &= ~( 1 << 4 );
}
static inline void CLK_HIGH( void ) { PORTB = PORTB | ( 1 << 4 ); }
static inline void CLK_LOW( void ) { PORTB = PORTB & ~( 1 << 4 ); }
static inline uint8_t STATE_RD_N( void ) { return ( PIND & ( 1 << 2 ) ); }
static inline uint8_t STATE_WR_N( void ) { return ( PIND & ( 1 << 3 ) ); }
static inline uint8_t STATE_M1_N( void ) { return ( PINK & ( 1 << 5 ) ); }
static inline uint8_t STATE_RFSH_N( void ) { return ( PINK & ( 1 << 6 ) ); }
static inline uint8_t STATE_MREQ_N( void ) { return ( PINK & ( 1 << 1 ) ); }
static inline uint8_t STATE_IORQ_N( void ) { return ( PINK & ( 1 << 2 ) ); }

static const uint8_t DIR_IN = 0x00;
static const uint8_t DIR_OUT = 0xFF;
static inline void DATA_DIR( uint8_t dir ) { DDRL = dir; }
static inline void ADDR_H_DIR( uint8_t dir ) { DDRC = dir; }
static inline void ADDR_L_DIR( uint8_t dir ) { DDRA = dir; }


/*
 * Constructor
 */
Z80RetroShieldClassName::Z80RetroShieldClassName() {
    //
    // Callbacks are all empty by default.
    //
    m_on_memory_read = NULL;
    m_on_memory_write = NULL;
    m_on_io_read = NULL;
    m_on_io_write = NULL;
    m_debug_output = NULL;
    m_debug = 0;
    m_cycle = -2;

    //
    // Set directions
    //
    DATA_DIR( DIR_IN );
    ADDR_H_DIR( DIR_IN );
    ADDR_L_DIR( DIR_IN );

    //
    // Handle other pins.
    //
    pinMode( uP_RESET_N, OUTPUT );
    pinMode( uP_WR_N, INPUT );
    pinMode( uP_RD_N, INPUT );
    pinMode( uP_M1_N, INPUT );
    pinMode( uP_RFSH_N, INPUT );
    pinMode( uP_MREQ_N, INPUT );
    pinMode( uP_IORQ_N, INPUT );
    pinMode( uP_INT_N, OUTPUT );
    pinMode( uP_NMI_N, OUTPUT );
    pinMode( uP_CLK, OUTPUT );

    Reset();
    digitalWrite( uP_CLK, LOW );
}

/*
 * Destructor
 */
Z80RetroShieldClassName::~Z80RetroShieldClassName() {}

void Z80RetroShieldClassName::show_status( const char *header ) {
    if ( m_debug_output == nullptr ) {
        return;
    }
    char buf[ 100 ];
    uint8_t m1_n = STATE_M1_N();
    uint8_t rfsh_n = STATE_RFSH_N();
    uint8_t mreq_n = STATE_MREQ_N();
    uint8_t iorq_n = STATE_IORQ_N();
    if ( ( m_debug & DEBUG_FLAG_CYCLE ) || ( ( m_debug & DEBUG_FLAG_IO ) && iorq_n == 0 ) ||
         ( ( m_debug & DEBUG_FLAG_MEM ) && mreq_n == 0 ) ) {
        sprintf( buf, "%s%4ld%c A: %04X D: %02X  %3s %5s %5s %5s  %s%s", header, m_cycle > 0 ? m_cycle : 0,
                 m_cycle_state ? 'H' : 'L', ADDR(), DATA_IN(), m1_n ? "" : "/M1", rfsh_n ? "" : "/RFSH", mreq_n ? "" : "/MREQ",
                 iorq_n ? "" : "/IORQ", STATE_RD_N() ? "" : "/RD", STATE_WR_N() ? "" : "/WR" );
        m_debug_output( buf );
    } else {
        return;
    }
    if ( m_debug & DEBUG_FLAG_VERBOSE ) {
        sprintf( buf, "IN    ABL: %02X  ABH: %02X  DB: %02X", PINA, PINC, PINL );
        m_debug_output( buf );
        sprintf( buf, "OUT   ABL: %02X  ABH: %02X  DB: %02X", PORTA, PORTC, PORTL );
        m_debug_output( buf );
        sprintf( buf, "DIR   ABL: %02X  ABH: %02X  DB: %02X", DDRA, DDRC, DDRL );
        m_debug_output( buf );
    }
}

/*
 * Run the processor.
 *
 * This will step the processor by a single clock-tick.
 */
void Z80RetroShieldClassName::Tick( int cycles ) {
    for ( int cycle = 0; cycle < cycles; cycle++ ) {

        /*
         * The memory address we're reading/writing to.
         */
        static unsigned int uP_ADDR = 0;

        /*
         * The I/O address we're reading/writing to.
         */
        static uint8_t prevRD = 0;
        static uint8_t prevWR = 0;
        // static uint8_t prevIORQ = 0;
        // static uint8_t prevIORQ = 0;

        m_cycle_state = 2;
        while ( m_cycle_state-- ) { // clock HIGH, clock LOW
            SET_CLK( m_cycle_state );

            // Store the contents of the address-bus in case we're going to use it.
            uP_ADDR = ADDR();

            //////////////////////////////////////////////////////////////////////
            // Read Access?
            if ( !STATE_RD_N() ) {
                // change DATA port to output to uP:
                if ( prevRD ) { // neg edge
                    DATA_DIR( DIR_OUT );

                    // RAM Read?
                    if ( !STATE_MREQ_N() ) {
                        if ( m_on_memory_read )
                            DATA_OUT( m_on_memory_read( uP_ADDR ) );
                        else
                            DATA_OUT( 0 );
                        // IO Read?
                    } else if ( !STATE_IORQ_N() ) {
                        if ( m_on_io_read )
                            DATA_OUT( m_on_io_read( ADDR_L() ) );
                        else
                            DATA_OUT( 0 );
                    }
                }
            } else if ( !STATE_WR_N() ) { // Write Access
                if ( prevWR ) {           // neg edge
                    // change DATA port to input from uP:
                    DATA_DIR( DIR_IN );
                    // RAM Write?
                    if ( !STATE_MREQ_N() ) {
                        if ( m_on_memory_write != NULL )
                            m_on_memory_write( uP_ADDR, DATA_IN() );
                    } else if ( !STATE_IORQ_N() ) {
                        if ( m_on_io_write != NULL )
                            m_on_io_write( ADDR_L(), DATA_IN() );
                    }
                }
            }

            prevRD = STATE_RD_N();
            prevWR = STATE_WR_N();

            //////////////////////////////////////////////////////////////////////

            // natural delay for DATA Hold time (t_HR)
            if ( STATE_RD_N() )
                DATA_DIR( DIR_IN );
            else
                DATA_DIR( DIR_OUT );
        }
        // start next cycle
        debug_count_cycle();
    } // for (int cycle = 0; cycle < cycles; cycle++)
}


/*
 * Reset the processor.
 *
 * This will run the clock a few cycles to ensure that the processor
 * is reset fully.
 */
void Z80RetroShieldClassName::Reset() {
    // Drive RESET conditions
    digitalWrite( uP_RESET_N, LOW );
    digitalWrite( uP_INT_N, HIGH );
    digitalWrite( uP_NMI_N, HIGH );

    // Run for a few cycles.
    for ( int i = 0; i < 4; i++ )
        Tick();

    // Drive RESET conditions
    digitalWrite( uP_RESET_N, HIGH );
}
