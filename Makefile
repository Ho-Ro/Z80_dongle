
PRJ = Z80_dongle

FQBN = arduino:avr:mega


PORT = /dev/ttyACM0

INO = $(PRJ)/$(PRJ).ino
CPP = $(PRJ)/*.cpp
H = $(PRJ)/*.h

BUILD = $(PRJ)/build/$(subst :,.,$(FQBN))

HEX = $(BUILD)/$(PRJ).ino.hex


$(HEX): $(INO) $(CPP) $(H)
	arduino-cli compile --export-binaries --warnings all --fqbn $(FQBN) $<


.PHONY: upload
upload: $(HEX)
	-killall picocom
	arduino-cli upload -v -p $(PORT) -i $<


.PHONY: touch
touch:
	touch $(INO)


o.new: o.cpp
	g++ -Wall -Wpedantic -o o.new o.cpp

