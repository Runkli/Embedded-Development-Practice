/*
 * UserModule.c
 *
 * Created: 18-Jun-21 9:45:35 PM
 * Author : Ilknur
 */ 

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

void command_LCD(unsigned char);
void data_LCD(unsigned char);
void send_LCD(unsigned char);
void write_string_LCD(char*, char);
char keypad_to_char();

char keypad[4][3] = {
	'1', '2', '3',
	'4', '5', '6',
	'7', '8', '9',
	'.', '0', '#' 
};

ISR(INT0_vect) {
	clear_LCD();
	char result = keypad_to_char();
	char row, col;
	row = result & 0x0F;
	col = (result & 0xF0) >> 4;
	
	result = keypad[row][col];
	data_LCD(result);
}


char keypad_to_char() {
	char column = 0;
	switch (PINC & 0x0E) {
		case 2:
			column = 2;
			break;
		case 4:
			column = 1;
			break;
		case 8:
			column = 0;
			break;
	}
	
	char row = 0;
	for ( row = 0; row < 4; row++ ) {
		PORTC &= ~(1<<(4+row));
		_delay_ms(5);
		if ( (PINC&(1<<(1+column))) == ~(1<<(1+column))) {
			PORTC |= (1<<(4+row));
			break;
		}
		
		_delay_ms(5);
		PORTC |= (1<<(4+row));
		
	}
	
	return row | (column<<4);
	
}

void configure() {
	
	// BlueTooth USART
	UCSR1C = (1<<UCSZ11)|(1<<UCSZ10); // setting data width to 8
	UBRR1H = (BR_Calc>>8); // setting baud rate to 9600 by setting UBBR
	UBRR1L = BR_Calc;
	UCSR1B = (1<<TXEN1) | (1<<RXCIE1) | (1<<RXEN1);; // enable transmitter, receiver, and receive and transmit complete interrupts	
	sei();
	
	// LCD config
	DDRE = 0xFF; // data pins
	DDRD = 0xE0; // 11100000 for RS/RW/E
	
	command_LCD(0x38); // 00111000 sets 8 bit mode
	command_LCD(0x0C); // 00001100 display on cursor off
	command_LCD(0x06); // shift cursor to the right command
	
	
	// Keypad config
	DDRC = 0xF0; // keypad inputs
	PORTC = 0xF0; // pull up
	
	
	// keypad interrupt
	EIMSK = 0x01; // normal mode, falling edge
	

	
	
}

void write_string_LCD(char str[], char line) {
	int i = 0;
	switch (line){
		case 0:
			command_LCD(0x80);
			break;
		case 1:
			command_LCD(0xC0);
			break;
		case 2:
			command_LCD(0x94);
			break;
		case 3:
			command_LCD(0xD4);
			break;
	}
	for(i = 0; str[i]!='\0' && i < 20; i++) {
		data_LCD(str[i]);
	}
	
}

void clear_LCD() {
	command_LCD(0x01); // clear display screen
	_delay_ms(5);
	//command_LCD(0x80); // return cursor home
}

void command_LCD(unsigned char command) {
	PORTD &= ~(1<<5); // 
	send_LCD(command); // 0000 0000 RS=0, RW=0
}

void data_LCD(unsigned char command) {
	PORTD |= 0x20;
	send_LCD(command); // 0010 0000
}

void send_LCD(unsigned char data) {
	PORTE = data;
	PORTD |= 0x80; // 1000 0000 send pulse to E
	_delay_us(200);
	PORTD &= ~(1<<7); // turn E pulse off
	_delay_us(200);
}

int main(void) {
	configure();
    /* Replace with your application code */
    while (1) 
    {
		
    }
}

