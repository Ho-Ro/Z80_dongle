#include <cstdio>
#include <cstdint>



int main( int argc, char *argv[] ) {
	int intminisize = 0x150;
	int basicsize = 0x2000-intminisize;
	FILE *in;
	uint8_t intmini[ intminisize ] = { 0 };
	uint8_t basic[ basicsize ] = { 0 };
	in = fopen( "intmini.cim", "rb" );
	fread( intmini, 1, sizeof( intmini ), in );
	fclose( in );
	in = fopen( "basic.cim", "rb" );
	basicsize = fread( basic, 1, sizeof( basic ), in );
	fclose( in );

	printf( "#ifndef ROM_GS_H\n#define ROM_GS_H\n\n" );

	printf( "// Credits\n//\n" );
	printf( "// * Original BASIC code was written by Microsoft.\n" );
	printf( "// * Updates were made by Grant Searle.\n" );
	printf( "// * Further updates from 8bitforce.\n\n" );

	printf( "const uint8_t rom_gs[] PROGMEM = {" );


	for ( int iii = 0; iii < intminisize; ++iii ) {
		if ( (iii & 0x0F) == 0 )
			printf( "\n    /* %04x */", iii );
		printf( " 0x%02x,", intmini[ iii ] );
	}
	printf( "\n" );

	for ( int iii = 0; iii < basicsize; ++iii ) {
		if ( (iii & 0x0F) == 0 )
			printf( "\n    /* %04x */", iii + intminisize );
		printf( " 0x%02x%c", basic[ iii ],iii != basicsize-1 ? ',' : '\n' );
	}
	printf( "};\n\n" );
	printf( "#endif\n" );

	return 0;
}
