#include <cstdio>
#include <cstdint>


#define romsize 0x150

int main( int argc, char *argv[] ) {
	FILE *in;
	uint8_t rom[ romsize ];
	in = fopen( "intmini.obj", "rb" );
	fread( rom, 1, sizeof( rom ), in );
	fclose( in );
	printf( "{" );
	for ( int iii = 0; iii < romsize; ++iii ) {
		if ( (iii & 0x07) == 0 )
			printf( "\n    /* %04x */ ", iii );
		printf( "0x%02x%s", rom[ iii ], iii < romsize-1 ? ", " : "" );
	}
	printf( "\n};\n" );
	return 0;
}