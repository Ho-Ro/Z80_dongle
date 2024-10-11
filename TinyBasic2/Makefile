../Z80_dongle/TinyBasic2.h: tinybasic2.obj obj2h
	./obj2h > $@

tinybasic2.hex: tinybasic2.obj
	srec_cat $< -binary -output $@ -intel

tinybasic2.obj: tinybasic2.asm
	uz80as $<

obj2h: obj2h.cpp
	gcc -Wall -o $@ $<

