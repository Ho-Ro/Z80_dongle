
H = ../Z80_dongle/TinyBasic2.h
HEX = tinybasic2.hex


all: $(H) $(HEX)


../Z80_dongle/TinyBasic2.h: tinybasic2_rom.obj obj2h
	./obj2h $< > $@

tinybasic2.hex: tinybasic2.obj
	srec_cat $< -binary -output $@ -intel

tinybasic2.obj: tinybasic2.asm Makefile
	uz80as $<

tinybasic2_rom.obj: tinybasic2.asm Makefile
	uz80as -dMAKE_ROM $< $@ tinybasic2_rom.lst

obj2h: obj2h.cpp
	gcc -Wall -o $@ $<
