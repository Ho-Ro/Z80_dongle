#include <cstdio>
#include <cstdint>


#define romsize 0x800

int main( int argc, char *argv[] ) {
    FILE *in;
    uint8_t rom[ romsize ] = {0};
    if( argc < 2 ) {
        fprintf( stderr, "usage: obj2h <OBJFILE>\n" );
        return -1;
    }
    in = fopen( argv[1], "rb" );
    if( !in ) {
        fprintf( stderr, "error: cannot open %s\n", argv[1] );
        return -1;
    }
    fread( rom, 1, sizeof( rom ), in );
    fclose( in );
    printf( "#ifndef ROM_TB2_H\n#define ROM_TB2_H\n\n" );
    printf( "const unsigned char rom_tb2[] PROGMEM = {" );

    for ( int iii = 0; iii < romsize; ++iii ) {
        if ( (iii & 0x07) == 0 )
            printf( "\n    /* %04x */", iii );
        printf( " 0x%02x%c", rom[ iii ], iii == romsize - 1 ? '\n' : ',' );
    }
    printf( "};\n\n" );
    printf( "#endif\n" );
    return 0;
}
