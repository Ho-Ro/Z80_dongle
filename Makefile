# make Z80_dongle, include changes to the GS Basic intmini

PRJ = Z80_dongle

FQBN = arduino:avr:mega

PORT = /dev/ttyACM0


INO = $(PRJ)/$(PRJ).ino
ASM = Basic_GS/intmini.asm Basic_GS/basic.asm TinyBasic2/tinybasic2.asm

BUILD = $(PRJ)/build/$(subst :,.,$(FQBN))

HEX = $(BUILD)/$(PRJ).ino.hex

INC = $(PRJ)/*.h


$(HEX): $(INO) $(ASM) $(INC) Makefile
	make -C Basic_GS
	make -C TinyBasic2
	arduino-cli compile --export-binaries --warnings all --fqbn $(FQBN) $<


.PHONY: upload
upload: $(HEX)
	-killall picocom
	arduino-cli upload -v -p $(PORT) -i $<


.PHONY: touch
touch:
	touch $(INO)

