CC=gcc

all: flash-led powerlogger

flash-led: flash-led.o
	$(CC) -Wall -o flash-led flash-led.o -lwiringPi

powerlogger: powerlogger.o
	$(CC) -Wall -o powerlogger powerlogger.o -lwiringPi


