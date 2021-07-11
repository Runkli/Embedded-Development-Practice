#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <avr/sleep.h>
#define F_CPU 4000000
#include <util/delay.h>
#include "../../../Lab4_UserModule/UserModule/UserModule/LCDC.c"
#include "../../../Lab4/atmel-files/atmel-files/CRC.h"
#define BR_Calc 26
char reset = 0;
char TOS = 0;
char TOS_STATE = 0;
#define EMPTY  0
#define FULL   1

int sensors[4];

#define RESET_REQUEST  0x00  // 0000 0000
#define REPEAT_REQUEST 0xE0  // 1110 0000
#define ACNKOWLEDGE    0x40  // 0100 0000
#define LOG_REQUEST    0x20  // 0010 0000

// transmits a single byte of data to the sensor
void SendData(unsigned char sensor_packet_out) {
	while(!(UCSR1A & (1<<UDRE1)))
	_delay_ms(10);

	UDR1 = sensor_packet_out;
}

// receive data
ISR(USART1_RX_vect) {
	sleep_disable(); // disable sleep once an interrupt wakes CPU up
	unsigned char data = UDR1;
	
	// crc check data and send repeat if necessary
	if(CRC_CHECK3(data) == 0x00){
		SendData(CRC3(REPEAT_REQUEST));
		_delay_ms(50);
		return;
	}
	
	// removes crc
	data = data & 0xE0;
	
	// if reset
	if(data == RESET_REQUEST){
		// skip first reset request, since already configured
		if(reset)
			configure();
		// set first reset bit
		reset = 1;
		
		// empty the stack
		TOS = 0;
		TOS_STATE = EMPTY;
		
		// respond with ack
		SendData(ACNKOWLEDGE);
		
	// if repeat request
	}else if (data == REPEAT_REQUEST){
		// resend tos
		SendData(TOS);
	// if ack
	}else if (data == ACNKOWLEDGE){
		// empty stack, used as a flag when sending multiple sensors, sending waits for stack to empty
		TOS = 0;
		TOS_STATE = EMPTY;
	}
}

// sets the adc conversion flag and wakes mcu up
char sensor_read = 0;
ISR(ADC_vect){
	sleep_disable();
	sensor_read = 1;
}

// reads a single pin sensor and converts to digital then returns the 10 bit number
int read_sensor(char i){
	sensor_read = 0;
	ADMUX &= 0xE0; // clear mux bits
	ADMUX |= i; // select the pin to read
	
	ADCSRA |= (1<<ADSC); // starts conversion
	// sleep and await for conversion to end, using while in case mcu wakes up
	// due to another interrupt
	while(!sensor_read){
		sleep_enable(); // arm sleep mode
		sleep_cpu(); // put CPU to sleep
	}
	
	// after sensor is read
	// divided by 32 to map 10 bit number to 5 bits, lossy conversion
	return ADC/32;
}

// initializes the analog to digital components
void init_adc(){
	// sets inputs and outputs
	DDRF = 0xF0;
	PORTF = 0xF0;
	
	ADCSRA |= (1<<ADEN) | (1<<ADIE) | 0x06; // enable adc, enable interrupts, and select prescaler = 64
	// to maintain adc clk frequency between 50kHz and 200kHz
	ADMUX = 0x40; // select vref = 5V 0100 
	
	// initiate the first conversion to ensure that subsequent conversions are 13 clock cycles
	read_sensor(0);
}

// reads all the 4 connected sensors and write their values to the related buffers
void read_sensors(){
	for(char i = 0; i < 4; i++){
		sensors[i] = read_sensor(i); // reading each sensor;
	}
}

// prints the sensor values from buffer
void print_sensors(){
	clear_LCD();
	char buffer[50];
	
	_delay_ms(200);
	if( sensors[3] < 0x14 ){ // if less than 3.2 volts
		write_string_LCD("<~1 Change battery\n  immediately!", 0);
	}else{
		sprintf(buffer, "<T=%x   M=%x\nW=%x   B=%x", sensors[0], sensors[1], sensors[2], sensors[3]);
		write_string_LCD(buffer, 0);
	}
}

// sends all sensor values from buffer to central logger
void send_sensors(){
	clear_LCD();
	// informs user
	write_string_LCD("<~0Sending sensor\n data", 0);
	// foreach sensor
	for(char i = 0; i < 4; i++){
		// write data value on tos along with data bit and parameter id
		// i = 0, temperature
		// i = 1, moisture
		// i = 2, water level
		// i = 3, battery
		TOS = (1<<7) | (i<<5) | sensors[i];
		TOS_STATE = FULL;
		
		// sends the data packet
		SendData(TOS);
		_delay_us(10);
		
		// sends the log request and crc bits
		SendData(LOG_REQUEST | CRC11(LOG_REQUEST, TOS));
		
		// awaits an ack by waiting for tos to empty up
		while(TOS_STATE == FULL){
			sleep_enable(); // arm sleep mode
			sleep_cpu(); // put CPU to sleep
		}
	}
	// print the sensor values to the lcd
	print_sensors();
}

// sets the pwm settings for the motor speed
void set_motor_speed(){
	// calculating the duty cycle ranged from 20% to 80% based on moisture level
	// moisture = 0, sets dutycycle to 80%
	// moisture = 0x1f, sets dutycycle to 20%
	char dutyCycle = (100 - 60*(((float)sensors[1] / (float)0x1F))) - 20;
	// inverting ocr
	//dutyCycle = 50;
	char ocr = -1 * ((dutyCycle * 256 / 100) - 255);
	//ocr = 127;
	OCR0 = ocr;
	TCCR0 |= 0x01; // start with prescaler = 1
}

// starts the motor
void start_watering(){
	set_motor_speed();
	// sets the direction and turns on the motor
	PORTB |= (1<<2);
	PORTB &= ~(1<<3);
	TCCR0 |= (1); // start pwm, unecssary but for speeding up simulation
}

// stops the motor
void stop_watering(){
	// turns off the motor
	PORTB &= ~(1<<2);
	PORTB &= ~(1<<3);
	
	TCCR0 &= ~(1); // stop pwm, unecssary but for speeding up simulation
}

ISR(TIMER3_OVF_vect){
	// stop watering
	TCCR3B = 0x00; // stopping timer
	stop_watering();
}

ISR(TIMER1_OVF_vect){
	TCCR1B = 0x00; // stopping timer
	
	// read sensors
	sei();
	read_sensors();
	send_sensors();
	
	// start watering
	TCNT3 = 26474*2; // setting timer 3 to run for 5 seconds
	TCCR3B = 0x05; // starting timer3 with scaler = 1024
	start_watering();
	
	TCNT1 = 26464; // setting counter1 to 26464 for 10 seconds
	TCCR1B = 0x05; // starting timer with scaler = 1024
}

void start_timer(){
	// watering interval sensing interval (since they both use the same timer of 10 seconds)
	TCNT1 = 26464; // setting counter1 to 26464 for 10 seconds
	TIMSK = 0x04; // enabling interrupt for timer 1
	ETIMSK = 0x04; // enabling interrupt for timer 3
	TCCR1B = 0x05; // starting timer with scaler = 1024
}

void init_motor(){
	DDRB = 0xFF; // setting in1 and in2 to output, determines direction of dc motor
	
	// using timer0 for pwm
	TCCR0 = 0x78; // enable fast inverting pwm 
	// 0111 1000
}

void configure() {
	
	// XBEE USART
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
	
	// custom character
	// envelop
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
	unsigned char battery[] = {
		0b01110,
		0b11011,
		0b10001,
		0b10001,
		0b10001,
		0b11111,
		0b11111,
		0b11111
	};

	createCG(battery, 1);
	
	init_adc(); // initialize the Analog to digital converter
	
	// set sensors
	read_sensors();
	print_sensors();
	
	init_motor();
}

int main(void)
{
	configure();
	while(!reset){
		sleep_enable(); // arm sleep mode
		sleep_cpu(); // put CPU to sleep
	}
	start_timer();

    while (1) 
    {
	    sleep_enable(); // arm sleep mode
	    sleep_cpu(); // put CPU to sleep
    }
}

