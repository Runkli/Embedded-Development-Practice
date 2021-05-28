unsigned char TOS = NULL; // 8 bit variable for top of stack

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include "../CRC.h"

// the generator polynomial, 0b110101
#define RESET_REQUEST 0x00

void TRANSMIT(unsigned char packet) {
	PORTE = packet;
}


#if 1 // 1 for project, 0 for bottom main function to use as test bench
int main(void) {
	// Port configurations
	DDRF = 0x08;
	DDRB = 0xFF;
	DDRD = 0X00;
	DDRE = 0XFF;
	
	// XMEM
	MCUCR = 0X80; // 1000 0000
	
	// pre_init
	PORTF = 0x00; 

	// INIT
	/* 
	init stack pointer
	*/
	unsigned char start = PINF;
	
	// Init
	unsigned char packet = CRC3(RESET_REQUEST);
	TRANSMIT(packet);
	TOS = packet;
	
	/* 
	Service readout
	*/
	
	
	// Assert ready
	PORTF = 0x08;
	
	if ( !(1<<4)&PINF) { // if receive FALSE, do something, otherwise continue
		
	}
	PORTF = 0x00;
	unsigned char packet_in = PIND;
	
	if ((1<<7)&packet_in) { // packet_in is command, if bit 7 is set, it is command
		if (!(1<<7)&TOS) { // if TOS has data packet, if bit 7 is not set, it is data
			CRC_CHECK11(packet_in);
		} else {
			if (CRC_CHECK3(packet_in)) { // if CRC_CHECK3 pass
				// acknowledge
			} else {
				// repeat request
			}
			
		}
	} else {
		if(TOS){
			TOS = NULL;
		}
		TOS = packet_in;
		// JUMP PRE-SERVICE READOUT
	}
	
}

#else
int main(void) {
	DDRB = 0xFF;
	TOS = 0x80;
	unsigned char input = 0x80;
	PORTB = input;
	input = CRC_CHECK11(input);
	PORTB = input;
	
 }
#endif
