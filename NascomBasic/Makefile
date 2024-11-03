# create include file from intmini.asm and basic.asm


../Z80_dongle/NascomBasic_GS.h: intmini.cim basic.cim obj2h_gs
	./obj2h_gs > $@

basic.cim: basic.asm
	zmac -c $< -o $@ -o basic.lst

intmini.cim: intmini.asm
	zmac -c $< -o $@ -o intmini.lst

obj2h_gs: obj2h_gs.cpp
	gcc -Wall -o $@ $<

.PHONY: clean
clean:
	rm -f *.cim *.lst *.hex
