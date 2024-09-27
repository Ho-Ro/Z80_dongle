
all: intmini.h

intmini.h: intmini.obj bin2h
	./bin2h > intmini.h

intmini.obj: intmini.asm
	uz80as $<

bin2h: bin2h.cpp
	gcc -Wall -o $@ $<
