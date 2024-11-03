#!/usr/bin/python


def mkh( file, inc, pos ):
    with open( file, "r") as f:
        print( f"PROGMEM const char { inc }[][14] = ", end='' )
        print( "{" )
        lastop = -1
        for line in f.readlines():
            line = line.strip()
            # print( line )
            op = line[0:13]
            # print( op, line )
            num = 16 * int( line[26+pos*3],16 ) + int( line[27+pos*3],16 )
            for n in range(lastop+1,num):
                print( '    /*%02X*/ "",' % n )
            print( '    /*%02X*/ "%s"' % (num, op), end='' )
            lastop = num
            if num < 0xFF:
                print( ',' )
            else:
                print()
        if num < 0xFF:
            for n in range( lastop+1, 0x100 ):
                print( '    /*%02X*/ ""' % n, end='' )
                if n < 0xFF:
                    print( ',' )
                else:
                    print()
        print( "};\n" );

mkh( "opcode.txt",     "opcode",     0 )
mkh( "opcodeCB.txt",   "opcodeCB",   1 )
mkh( "opcodeDD.txt",   "opcodeDD",   1 )
mkh( "opcodeDDCB.txt", "opcodeDDCB", 2 )
mkh( "opcodeED.txt",   "opcodeED",   1 )
mkh( "opcodeFD.txt",   "opcodeFD",   1 )
mkh( "opcodeFDCB.txt", "opcodeFDCB", 2 )
