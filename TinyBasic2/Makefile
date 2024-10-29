
H = ../Z80_dongle/TinyBasic2.h
HEX = tinybasic2.hex
ROMSIZE = 0x0800

.PHONY:	all
all: $(H) $(HEX)


.PHONY:	clean
clean:
	rm *.cim *.lst *.hex


../Z80_dongle/TinyBasic2.h: tinybasic2_rom.cim obj2h
	./obj2h $< $(ROMSIZE) rom_tb2 > $@

tinybasic2.hex: tinybasic2.cim
	srec_cat $< -binary -output $@ -intel --address-length=2

tinybasic2_rom.cim: tinybasic2.asm Makefile
	zmac -c -DMAKE_ROM -o $@ -o tinybasic2_rom.lst $<

tinybasic2.cim: tinybasic2.asm Makefile
	zmac -c -o $@ -o tinybasic2.lst $<

obj2h: obj2h.cpp
	gcc -Wall -o $@ $<
