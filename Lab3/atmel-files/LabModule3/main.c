#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#define F_CPU 8000000
#define BaudRate 9600
#include <util/delay.h>

#define BR_Calc 51

// globals
unsigned char data [520];
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
void SERVICE_READOUT(unsigned char user_input) {
	if( user_input == 0 ) { //(1<<0)&user_input ) { // if pin 0 is one, start total mem dump
		for(int i = 0; i < dataPointer; i++ ) {
			PORTB = data[i];
			_delay_ms(1000);
		}
		} else if( user_input == 1 ) {// (1<<1)&user_input ) { // if pin 1 is set, show last entry
		PORTB = data[dataPointer - 1];
	}
}

void SYS_CONFIG(){
	// Port configurations
	
	// XMEM
	MCUCR = 0X80; // 1000 0000
	
	// USART 0, BLUETOOTH
	UCSR0C = (1<<UCSZ01)|(1<<UCSZ00); // setting data width to 8
	UBRR0H = (BR_Calc>>8); // setting baud rate to 9600 by setting UBBR to 0x33
	UBRR0L = BR_Calc;
	UCSR0B = (1<<TXEN0) | (1<<RXCIE0)| (1<<TXCIE0) | (1<<RXEN0); // enable transmitter, receiver, and receive and transmit complete interrupts
	
	
	// USART 1, XBEE
	UCSR1C = (1<<UCSZ11)|(1<<UCSZ10); // setting data width to 8
	UBRR1H = (BR_Calc>>8); // setting baud rate to 9600 by setting UBBR to 0x33
	UBRR1L = BR_Calc;
	UCSR1B = (1<<TXEN1) | (1<<RXCIE1) | (1<<RXEN1);; // enable transmitter, receiver, and receive and transmit complete interrupts
}

void LOG_REQUEST_FUNCTION(unsigned char packet_in){
	data[dataPointer] = packet_in;
	rollDataPointer();
}

// incomplete
void SENSOR_TRANSMIT(unsigned char sensor_packet_out) {
	while(!(UCSR1A & (1<<UDRE1)))
	_delay_ms(100);

	UDR1 = sensor_packet_out;
}

// incomplete
void USER_TRANSMIT(unsigned char user_packet_out) {
	while(!(UCSR0A & (1<<UDRE0)))
	_delay_ms(100);

	UDR0 = user_packet_out;
}

void INIT(){
	TOS = CRC3(RESET_REQUEST);
	SENSOR_TRANSMIT(TOS);
	TOS_STATE = FULL;
}

void TREAT_SENSOR_DATA(unsigned char packet_in){
	SENSOR_TRANSMIT(packet_in);
	
	if (((1<<7)&packet_in) == 0x80) {// if not command type, eg packet_in[7] == 1
		TOS = packet_in;
		SENSOR_TRANSMIT(0xCC);
		TOS_STATE = FULL;
	} else { // it is a command
		if( ((1<<7)&TOS) == 0x80) { // if TOS has data packet
			unsigned char result = CRC_CHECK11(packet_in);
			if (result==0xFF) { // If CRC_CHECK11 PASS
				if ( (packet_in&0xE0) == LOG_REQUEST ) { // if packet in is log request
					LOG_REQUEST_FUNCTION(TOS);
					TOS_STATE = EMPTY;
					SENSOR_TRANSMIT(CRC3(ACNKOWLEDGE));
				}
				else{
					INIT();
				}
			} else { // CRC_CHECK11 FAIL
				TOS_STATE = EMPTY;
				SENSOR_TRANSMIT(0xAA);
				SENSOR_TRANSMIT(CRC3(REPEAT_REQUEST));
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
				SENSOR_TRANSMIT(0xBB);
				SENSOR_TRANSMIT(CRC3(REPEAT_REQUEST));
			}
		}
	}
}


char user_input_buffer [255];
char user_input_buffer_ptr = 0;

// bluetooth rx
// data register empty
ISR(USART0_RX_vect) {
	sleep_disable(); // disable sleep once an interrupt wakes CPU up
	user_input_buffer[user_input_buffer_ptr++] = UDR0;
	if (user_input_buffer[user_input_buffer_ptr - 1] == '.') {
		char number = user_input_buffer[user_input_buffer_ptr -2];
		switch (number){
			case '1':
			SERVICE_READOUT(0);
			break;
			case '2':
			SERVICE_READOUT(1);
			break;
			case '3':
			SYS_CONFIG();
			INIT();
			break;
			case '4':
			USER_TRANSMIT(0xFF);
			break;
		}
		user_input_buffer_ptr = 0;
	}
}

char user_output_buffer [255];
char user_output_buffer_ptr = 0;
char bluetoothSending = 0;
// bluetooth tx
ISR(USART0_TX_vect) {
	if(user_output_buffer[user_output_buffer_ptr] != '.' && bluetoothSending){
		USER_TRANSMIT(user_output_buffer[user_output_buffer_ptr++]);
	}
	else{
		user_output_buffer_ptr = 0;
		bluetoothSending = 0;
	}
}


char sensor_input_buffer [2];
char sensor_input_buffer_ptr = 0;

// xbee rx
// data register empty
ISR(USART1_RX_vect) {
	sleep_disable(); // disable sleep once an interrupt wakes CPU up
	if(sensor_input_buffer_ptr > 2)
		sensor_input_buffer_ptr = 0;
	sensor_input_buffer[sensor_input_buffer_ptr++] = UDR1;
	if (sensor_input_buffer_ptr == 2) {
		// ['0', '0']
		unsigned char number = 0;
		if(sensor_input_buffer[0] <= '9' && sensor_input_buffer[0] >= '0')
		number += (sensor_input_buffer[0] - '0') * 16;
		else if(sensor_input_buffer[0] >= 'a' && sensor_input_buffer[0] <= 'f')
		number += (sensor_input_buffer[0] - 'a' + 10) * 16;
		else if(sensor_input_buffer[0] >= 'A' && sensor_input_buffer[0] <= 'F')
		number += (sensor_input_buffer[0] - 'A' + 10) * 16;
		
		if(sensor_input_buffer[1] <= '9' && sensor_input_buffer[1] >= '0')
		number += (sensor_input_buffer[1] - '0');
		else if(sensor_input_buffer[1] >= 'a' && sensor_input_buffer[1] <= 'f')
		number += (sensor_input_buffer[1] - 'a' + 10);
		else if(sensor_input_buffer[1] >= 'A' && sensor_input_buffer[1] <= 'F')
		number += (sensor_input_buffer[1] - 'A' + 10);
		
		sensor_input_buffer_ptr = 0;
		TREAT_SENSOR_DATA(number);
	}
}

void USER_TRANSMIT_START(const char string[]){
	bluetoothSending = 1;
	strcpy(user_output_buffer, string);
	USER_TRANSMIT('\n');
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
	INIT();
	TOS = 0x80;
	SENSOR_TRANSMIT(CRC_CHECK11(0x80));
}
#endif
