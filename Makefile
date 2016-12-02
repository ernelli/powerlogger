CC=gcc

all: flash-led led-trigger

flash-led: flash-led.o
	$(CC) -Wall -o flash-led flash-led.o -lwiringPi

led-trigger: led-trigger.o
	$(CC) -Wall -o led-trigger led-trigger.o -lwiringPi


