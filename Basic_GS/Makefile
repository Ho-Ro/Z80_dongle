# create include file from intmini.asm

#../Z80_dongle/intmini.h: intmini.cim obj2h
#	./obj2h > $@

../Z80_dongle/NascomBasic_GS.h: intmini.cim basic.cim obj2h
	./obj2h > $@

basic.cim: basic.asm
	zmac -c $< -o $@ -o basic.lst

intmini.cim: intmini.asm
	zmac -c $< -o $@ -o intmini.lst

obj2h: obj2h.cpp
	gcc -Wall -o $@ $<
