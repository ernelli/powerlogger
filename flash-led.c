#include <stdio.h>
#include <wiringPi.h>

int main (void)
{
  printf("calling wiringPi setup\n");
  wiringPiSetup () ;
  printf("Setting pinMode\n");
  pinMode (8, OUTPUT) ;
  printf("Start flashing LED\n");
  for (;;)
  {
    digitalWrite (8, HIGH) ; 
    delay (2) ;
    digitalWrite (8,  LOW) ; 
    delay (999) ;
  }
  return 0 ;
}
