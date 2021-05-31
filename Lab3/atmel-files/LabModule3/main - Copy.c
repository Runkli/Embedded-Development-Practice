#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define F_CPU 8000000
#define BaudRate 9600
#include <util/delay.h>

#define BR_Calc ((F_CPU/16/BaudRate )-1) //Baudrate calcuator

// globals
unsigned char data [5200];
unsigned short dataPointer = 0;
unsigned char TOS = 0; // 8 bit variable for top of stack
#define EMPTY  0
#define FULL   1
unsigned char TOS_STATE = EMPTY;
#include "../CRC.h"

char * pointer2 = 0;

void rollDataPointer(){
	dataPointer++;
	if(dataPointer > 5199)
		dataPointer = 0;
}

// the generator polynomial, 0b110101
#define RESET_REQUEST  0x00  // 0000 0000
#define REPEAT_REQUEST 0xE0  // 1110 0000
#define ACNKOWLEDGE    0x40  // 0100 0000
#define LOG_REQUEST    0x20  // 0010 0000

void TRANSMIT(unsigned char packet) {
	PORTE = packet;
}

// mem dump = pinf0
// last entry = pinf1
void SERVICE_READOUT() {
	unsigned char user_input = PINF;
	if( (1<<0)&user_input ) { // if pin 0 is one, start total mem dump
		for(int i = 0; i < dataPointer; i++ ) {
			PORTB = data[i];
			_delay_ms(1000);
		}
		} else if( (1<<1)&user_input ) { // if pin 1 is set, show last entry
		PORTB = data[dataPointer - 1];
	}
}

void SYS_CONFIG(){
	// Port configurations
	
	// XMEM
	MCUCR = 0X80; // 1000 0000
	
	// USART 1, BLUETOOTH
	UCSR0C = (1<<UCSZ01)|(1<<UCSZ00); // setting data width to 8
	UBRR0H = (BR_Calc>>8); // setting baud rate to 9600 by setting UBBR to 0x66
	UBRR0L = BR_Calc;
	//UCSR0B = (1<<RXEN0); // enable receiver
	UCSR0B = (1<<TXEN0); // enable transmitter
}

void LOG_REQUEST_FUNCTION(unsigned char packet_in){
	data[dataPointer] = packet_in;
	rollDataPointer();
}

// incomplete
void SENSOR_TRANSMIT(unsigned char sensor_packet_out) {
	sensor_packet_out = CRC3(sensor_packet_out);
}

// incomplete
void USER_TRANSMIT(unsigned char user_packet_out) {
	user_packet_out = CRC3(user_packet_out);
	
	while(!(UCSR0A & (1<<UDRE0)))
		_delay_ms(100);

	UDR0 = user_packet_out;
}

void INIT(){
	unsigned char packet = CRC3(RESET_REQUEST);
	SENSOR_TRANSMIT(packet);
	TOS = packet;
	TOS_STATE = FULL;
}

void TREAT_SENSOR_DATA(){
	unsigned char packet_in;
	// read from usart to packet_in
	
	if ((1<<7)&packet_in) {// if not command type, eg packet_in[7] == 1
		TOS = packet_in;
		TOS_STATE = FULL;
		} else { // it is a command
		if( (1<<7)&TOS ) { // if TOS has data packet
			unsigned char result = CRC_CHECK11(packet_in);
			if (result==0xFF) { // If CRC_CHECK11 PASS
				if ( (packet_in&0xE0) == LOG_REQUEST ) { // if packet in is log request
					LOG_REQUEST_FUNCTION(TOS);
					TOS_STATE = EMPTY;
					SENSOR_TRANSMIT(ACNKOWLEDGE);
				}
				else{
					INIT();
				}
				} else { // CRC_CHECK11 FAIL
				//TOS = NULL;
				TOS_STATE = EMPTY;
				SENSOR_TRANSMIT(REPEAT_REQUEST);
			}
			} else { // If TOS does not have Data packet
			unsigned char result = CRC_CHECK3(packet_in);
			if( result == 0xFF) { // if CRC_CHECK3 passes
				if ( (packet_in&0xE0) == ACNKOWLEDGE ) {
					TOS_STATE = EMPTY;
				}
				else{ // not acknowledge
					if ( (packet_in&0xE0) == REPEAT_REQUEST ){
						if (TOS_STATE == FULL)
						TRANSMIT(TOS);
					}
				}

				} else { // if CRC_CHECK3 fails
				SENSOR_TRANSMIT(REPEAT_REQUEST);
			}
		}
	}
}


// incomplete
void TREAT_USER_INPUT(){
	SERVICE_READOUT();
}

#if 0 // 1 for project, 0 for bottom main function to use as test bench
int main(void) {
	SYS_CONFIG();
	INIT();
	
	while(1){
		sleep_enable(); // arm sleep mode
		sei(); // global interrupt enable
		sleep_cpu(); // put CPU to sleep
		// sleep_disable(); // disable sleep once an interrupt wakes CPU up
	}
	
}

#else
int main(void) {
	SYS_CONFIG();
	// INIT();
	//DDRB = 255;
	//PORTB = 255;
	USER_TRANSMIT(0Xff);
}
#endif
