#!/bin/sh

test:
	arduino-cli compile -b arduino:avr:mega Z80_dongle/Z80_dongle.ino

o.new: o.cpp
	g++ -Wall -Wpedantic -o o.new o.cpp

