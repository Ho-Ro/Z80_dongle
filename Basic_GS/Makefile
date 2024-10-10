# create include file from intmini.asm

../Z80_dongle/intmini.h: intmini.obj obj2h
	./obj2h > $@

intmini.obj: intmini.asm
	uz80as $<

obj2h: obj2h.cpp
	gcc -Wall -o $@ $<
