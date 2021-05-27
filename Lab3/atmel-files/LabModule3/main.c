/*
 * LabModule3.c
 *
 * Created: 27-May-21 1:31:38 PM
 * Author : Ilknur
 */ 
unsigned char TOS; // 8 bit variable for top of stack

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include "../CRC.h"

//unsigned char gen = 0x35; // the generator polynomial, 0b110101



int main(void) {
	//unsigned char command = 0xE0;
	//command = CRC3(command);
	//int a = 2+3;
	DDRB = 0xFF;
	TOS = 0x80;
	unsigned char input = 0x80;
	PORTB = input;
	input = CRC_CHECK11(input);
	PORTB = input;
	//while (1) {
	//	PORTB = input;
	//}
	
 }

