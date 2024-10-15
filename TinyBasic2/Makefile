
H = ../Z80_dongle/TinyBasic2.h
HEX = tinybasic2.hex
8080 = tinybasic2_8080.hex


.PHONY:	all
all: $(H) $(HEX)


.PHONY:	clean
clean:
	rm *.obj *.lst *.hex


../Z80_dongle/TinyBasic2.h: tinybasic2_rom.obj obj2h
	./obj2h $< > $@

$(HEX): tinybasic2.obj
	srec_cat $< -binary -output $@ -intel --address-length=2

tinybasic2.obj: tinybasic2.asm Makefile
	uz80as $<

$(8080): tinybasic2_8080.obj
	srec_cat $< -binary -output $@ -intel --address-length=2

tinybasic2_8080.obj: tinybasic2_8080.asm Makefile
	uz80as -ti8080 $<

tinybasic2_rom.obj: tinybasic2.asm Makefile
	uz80as -dMAKE_ROM $< $@ tinybasic2_rom.lst

obj2h: obj2h.cpp
	gcc -Wall -o $@ $<
