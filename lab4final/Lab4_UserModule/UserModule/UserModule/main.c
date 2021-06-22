#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <avr/sleep.h>
#include "LCDC.c"

#define BR_Calc 26

char keypad_to_char();

// inverted since the connections of the columns are inverted in the schematic
char keypad[4][3] = {
	'3', '2', '1',
	'6', '5', '4',
	'9', '8', '7',
	'#', '0', '.' 
};

// interrupt that wakes mcu up, triggered by external pin when keypad is pressed
ISR(INT0_vect) {
	sleep_disable(); // disable sleep once an interrupt wakes CPU up
	// get row and column from keypad
	char result = keypad_to_char();
	char row, col;
	row = result & 0x0F;
	col = (result & 0xF0) >> 4;
	
	// get char based on row and column
	result = keypad[row][col];
	
	// send character to main logger
	SendData(result);
	// display feedback to user
	write_string_LCD(">>", 3);
	data_LCD(result);
}

// function reads the keypad
char keypad_to_char() {
	// we already know which column is being pressed based on which pin is asserted
	char column = 0;
	switch (PINC & 0x0E) {
		case 2:
			column = 0;
			break;
		case 4:
			column = 1;
			break;
		case 8:
			column = 2;
			break;
	}
	
	// we turn off one row at a time until the pin is deasserted, when the pin is deasserted, we know
	// that the row we just toggled was the responsible row
	char row;
	for ( row = 0; row < 4; row++ ) {
		PORTC &= ~(1<<(4+row));
		if ( (PINC&(1<<(1+column))) == 0) {
			PORTC |= (1<<(4+row));
			break;
		}
		
		PORTC |= (1<<(4+row));
		
	}
	return row | (column<<4);
}

// transmits a single byte of data to the sensor
void SendData(unsigned char sensor_packet_out) {
	while(!(UCSR1A & (1<<UDRE1)))
	_delay_ms(1);

	UDR1 = sensor_packet_out;
}

char char_number = 0, line_number = 0;

char rec_buffer[255];
char rec_buffer_pointer = 0;
char mem_dump_pointer = 0;
char mem_dump_line = 1;

// receive data
ISR(USART1_RX_vect) {
	sleep_disable(); // disable sleep once an interrupt wakes CPU up
	char character = UDR1;
	// if '>' then this is the last character of the string being received
	if(character == '>'){
		// reset buffer pointer
		rec_buffer[rec_buffer_pointer] = '\0';
		rec_buffer_pointer = 0;
		// if first character is '<', clear the screen and prep for mem dump
		if(rec_buffer[0] == '<'){
			clear_LCD();
			write_string_LCD(rec_buffer, 0);
			mem_dump_pointer = 0;
			mem_dump_line = 1;
		}
		// if '<' then we are writing to lcd without formatting, only used when memdump or last entry
		else{// memdump sepecific
			data_LCD(rec_buffer[1]);
			data_LCD(rec_buffer[2]);
			data_LCD(rec_buffer[3]);
			// used to rollover the lcd cursor
			mem_dump_pointer += 3;
			if(mem_dump_pointer > 15){
				mem_dump_pointer = 0;
				set_line_LCD(++mem_dump_line);
				if(mem_dump_line > 3){
					clear_LCD();
					mem_dump_line = 0;
				}
			}
		}
	// if neither, then is is a normal character to be inserted into the buffer
	}else{
		rec_buffer[rec_buffer_pointer++] = character;
	}
}

// function that configures the mcu
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
	EICRA |= (1<<1);
	
	// custom character
	// arrow
	unsigned char arrow[] = {
		0b00000,
		0b00100,
		0b00010,
		0b11111,
		0b11111,
		0b00010,
		0b00100,
		0b00000
	 };
	createCG(arrow, 0);
}

int main(void) {
	configure();
    while (1) {
	    sei();// waits for user or sensor interrupts
	    sleep_enable(); // arm sleep mode
	    sleep_cpu(); // put CPU to sleep
    }
}

