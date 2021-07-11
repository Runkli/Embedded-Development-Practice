#define F_CPU 4000000
#include <util/delay.h>

void command_LCD(unsigned char);
void data_LCD(unsigned char);
void send_LCD(unsigned char);
void write_string_LCD(char*, char);

void set_line_LCD(char line){
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
	_delay_us(1000);
}

void write_string_LCD(char str[], char line) {
	set_line_LCD(line);
	int i = 1;
	for(; str[i]!='\0'; i++) {
		if(str[i] == '~'){
			data_LCD(str[++i] - '0');
		}
		else if(str[i] == '\n'){
			set_line_LCD(++line);
		}else{
			data_LCD(str[i]);
		}
	}
}


void createCG(unsigned char array[], unsigned char character){
	unsigned char pointer = character * 8 + 0x40;
	for(unsigned char x = 0; x < 8; x++){
		command_LCD(pointer + x);
		data_LCD(array[x]);
	}
}

void clear_LCD() {
	command_LCD(0x01); // clear display screen
	_delay_us(1000);
}

void command_LCD(unsigned char command) {
	PORTD &= ~(1<<5); // setting rs to 0
	send_LCD(command); // 0000 0000 RS=0, RW=0
}

void data_LCD(unsigned char command) {
	PORTD |= (1<<5); // setting rs to 1
	send_LCD(command); // 0010 0000
}

void send_LCD(unsigned char data) {
	PORTE = data;
	PORTD |= (1<<7); // 1000 0000 send pulse to E
	_delay_us(100);
	PORTD &= ~(1<<7); // turn E pulse off
	_delay_us(100);
}