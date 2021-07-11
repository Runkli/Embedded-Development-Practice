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
#include <avr/wdt.h>

#define BR_Calc 51
#define EMPTY  0
#define FULL   1

// globals
unsigned char data [3200]; // logged data
unsigned short dataPointer = 0; // pointer to next position in array
unsigned char TOS = 0; // 8 bit variable for top of stack
unsigned char TOS_STATE = EMPTY; // state of stack
#include "../CRC.h" // has to be after TOS
unsigned short timeOutCount = 0; // timeout counter, if 0 it is disabled
char saved; // if timeout counter is already saved in eeprom
char wdSaved; // if timeout counter is already saved in eeprom
char watchdogSetting;

// increment the data pointer, if it is over the limit, set back to 0
void rollDataPointer(){
	dataPointer++;
	if(dataPointer > 3199)
	dataPointer = 0;
}

// the generator polynomial, 0b110101

// define requests
#define RESET_REQUEST  0x00  // 0000 0000
#define REPEAT_REQUEST 0xE0  // 1110 0000
#define ACNKOWLEDGE    0x40  // 0100 0000
#define LOG_REQUEST    0x20  // 0010 0000

// start a timer
void START_TIMER(char timer){
	// timeout timer 1
	if(timer == 0 && timeOutCount > 0){
		TCNT1H = (timeOutCount&0xFF00)>>8;
		TCNT1L = timeOutCount&0x00FF;
		TCCR1A = 0;
		TCCR1B = (1<<CS10) | (1<<CS12);
		TIMSK = (1<<TOIE1);
	}
	// timeout timer 3
	else if(timeOutCount > 0){
		TCNT3H = (timeOutCount&0xFF00)>>8;
		TCNT3L = timeOutCount&0x00FF;
		TCCR3A = 0;
		TCCR3B = (1<<CS30) | (1<<CS32);
		ETIMSK = (1<<TOIE3);
	}
}

// stop a timer
void STOP_TIMER(char timer){
	if(timer == 0){
		TCCR1B &= ~(1<<CS10);
		TCCR1B &= ~(1<<CS12);
		TIMSK &= ~(1<<TOIE1);
	}
	else{
		TCCR3B &= ~(1<<CS30);
		TCCR3B &= ~(1<<CS32);
		ETIMSK &= ~(1<<TOIE3);
	}
}

// timeout timer 1 triggered, send reset to sensor
ISR (TIMER1_OVF_vect){
	sleep_disable();
	INIT();
}

// timeout timer 3 triggered, send repeat request to sensor
ISR (TIMER3_OVF_vect){
	sleep_disable();
	SENSOR_TRANSMIT((REPEAT_REQUEST));
	TOS = 0;
	TOS_STATE = EMPTY;
}

// helper function to convert a byte sized number to 2 characters
void hex_to_chars(unsigned char hex, unsigned char * buffer){
	buffer[0] = 0;
	buffer[1] = 0;
	
	buffer[0] = hex>>4;
	if(buffer[0] >= 0 && buffer[0] <= 9)
	buffer[0] += '0';
	else
	buffer[0] += 'A' - 10;
	
	buffer[1] = hex&0x0F;
	if(buffer[1] >= 0 && buffer[1] <= 9)
	buffer[1] += '0';
	else
	buffer[1] += 'A' - 10;
	
	buffer[2] = ',';
	buffer[3] = '\0';
}

// function that transmits the content of the logged data to the user
void SERVICE_READOUT(char user_input) {
	if(dataPointer == 0){
		USER_TRANSMIT_START("No data ");
		return;
	}
	unsigned char tempBuffer[4]; // used to convert numbers to characters

	if(user_input == 0) { // mem dump
		USER_TRANSMIT_START("STARTING MEM DUMP: ");
		for(int i = 0; i < dataPointer; i++ ) {
			hex_to_chars(data[i], tempBuffer);
			USER_TRANSMIT_START(tempBuffer);
			_delay_ms(1000);
		}
	}
	else{ // last entry
		USER_TRANSMIT_START("Last Entry: ");
		hex_to_chars(data[dataPointer - 1], tempBuffer);
		USER_TRANSMIT_START(tempBuffer);
	}
}

// configures the system, runs only once or when reset
void SYS_CONFIG(){
	// Port configurations
	DDRB |= 0x01; // used to show watchdoge timer
	
	// XMEM
	MCUCR = 0X80; // 1000 0000
	
	// USART 0, BLUETOOTH
	UCSR0C = (1<<UCSZ01)|(1<<UCSZ00); // setting data width to 8
	UBRR0H = (BR_Calc>>8); // setting baud rate to 9600 by setting UBBR
	UBRR0L = BR_Calc;
	UCSR0B = (1<<TXEN0) | (1<<RXCIE0)| (1<<TXCIE0) | (1<<RXEN0); // enable transmitter, receiver, and receive and transmit complete interrupts
	
	
	// USART 1, XBEE
	UCSR1C = (1<<UCSZ11)|(1<<UCSZ10); // setting data width to 8
	UBRR1H = (BR_Calc>>8); // setting baud rate to 9600 by setting UBBR
	UBRR1L = BR_Calc;
	UCSR1B = (1<<TXEN1) | (1<<RXCIE1) | (1<<RXEN1);; // enable transmitter, receiver, and receive and transmit complete interrupts
	
	sei(); // enable global interrupts
}

// function that takes in a byte sized number and logs it in data then rolls the pointer forward
void LOG_REQUEST_FUNCTION(unsigned char packet_in){
	data[dataPointer] = packet_in;
	rollDataPointer();
}

// transmits a single byte of data to the sensor
void SENSOR_TRANSMIT(unsigned char sensor_packet_out) {
	while(!(UCSR1A & (1<<UDRE1)))
	_delay_ms(1);

	UDR1 = sensor_packet_out;
}

// transmits a single byte of data to the user -- DO NOT USE!!: Use "USER_TRANSMIT_START" instead
void USER_TRANSMIT(unsigned char user_packet_out) {
	while(!(UCSR0A & (1<<UDRE0)))
	_delay_ms(1);

	UDR0 = user_packet_out;
}

// user input buffer
char user_input_buffer [5];
char user_input_buffer_ptr = 0;

// bluetooth rx
// data register empty
ISR(USART0_RX_vect) {
	sleep_disable(); // disable sleep once an interrupt wakes CPU up
	
	// reset wd timer
	wdt_reset();
	
	// read one byte from the usart register and roll the buffer pointer
	user_input_buffer[user_input_buffer_ptr++] = UDR0;
	// if the read byte is a '.' command is issued
	if (user_input_buffer[user_input_buffer_ptr - 1] == '.') {
		// checks if timeout and wd timers are setup, if not, it expects them to be the first inputs by the user
		if(saved){
			// checks the validity of user input
			if(user_input_buffer[0] < '9' && user_input_buffer[0] >= '0'){
				// notify user, calculate the timer counter and set saved
				USER_TRANSMIT_START("RECEIVED");
				timeOutCount = (user_input_buffer[0] - '0') * 8 * 1000000 / 1024;
				saved = 0;
			}
		}
		else if(wdSaved){ // expects wd setup second// checks the validity of user input
			if(user_input_buffer[0] < '9' && user_input_buffer[0] >= '0'){
				// notify user, calculate the timer counter and set saved
				USER_TRANSMIT_START("RECEIVED");
				watchdogSetting = user_input_buffer[0] - '0';
				wdSaved = 0;
			}
		}
		else{ // normal operation command
			char number = user_input_buffer[0];
			switch (number){
				// mem dump
				case '1':
				sei(); // interrupts are reenabled since service readout relies on them
				SERVICE_READOUT(0);
				break;
				// last entry
				case '2':
				sei(); // interrupts are reenabled since service readout relies on them
				SERVICE_READOUT(1);
				break;
				// reset
				case '3':
				startI();
				break;
				// unknown command
				default:
				USER_TRANSMIT_START("Invalid command");
				break;
			}
		}
		// reset pointer to 0
		user_input_buffer_ptr = 0;
	}
}

// user output buffer, should not be used by other than USER_TRANSMIT_START, and user transmit tx interrupt
char user_output_buffer [255];
char user_output_buffer_ptr = 0;
char bluetoothSending = 0;
char user_output_buffer_size = 0;

// bluetooth tx
ISR(USART0_TX_vect) {
	sleep_disable(); // disable sleep
	if(user_output_buffer[user_output_buffer_ptr] != '\0' && bluetoothSending){ // if still sending data
		USER_TRANSMIT(user_output_buffer[user_output_buffer_ptr++]); // send next byte of data from buffer to usart
		if(user_output_buffer_ptr == user_output_buffer_size || user_output_buffer[user_output_buffer_ptr] == '\0') // if just sent last byte, set state to not sending
			bluetoothSending = 0;
	}
	else{ // if not sending, reset pointers
		user_output_buffer_ptr = 0;
		user_output_buffer_size = 0;
	}
}

// sensor input buffers, only sized 2 bytes
unsigned char sensor_input_buffer [2];
unsigned char sensor_input_buffer_ptr = 0;

// helper function that converts 2 ascii characters into a byte number
unsigned char chars_to_hex(unsigned char buffer[]){
	unsigned char number = 0;
	if(buffer[0] <= '9' && buffer[0] >= '0')
	number += (buffer[0] - '0') * 16;
	else if(buffer[0] >= 'a' && buffer[0] <= 'f')
	number += (buffer[0] - 'a' + 10) * 16;
	else if(buffer[0] >= 'A' && buffer[0] <= 'F')
	number += (buffer[0] - 'A' + 10) * 16;
	
	if(buffer[1] <= '9' && buffer[1] >= '0')
	number += (buffer[1] - '0');
	else if(buffer[1] >= 'a' && buffer[1] <= 'f')
	number += (buffer[1] - 'a' + 10);
	else if(buffer[1] >= 'A' && buffer[1] <= 'F')
	number += (buffer[1] - 'A' + 10);
	
	return number;
}

// xbee rx, sensor receive
ISR(USART1_RX_vect) {
	sleep_disable(); // disable sleep once an interrupt wakes CPU up
	
	sensor_input_buffer[sensor_input_buffer_ptr++] = UDR1; // read data into buffer
	if (sensor_input_buffer_ptr == 2) { // once read both nibbles (full byte)
		sensor_input_buffer_ptr = 0; // reset pointer
		TREAT_SENSOR_DATA(chars_to_hex(sensor_input_buffer)); // send to treat data function
	}
}

// function used to transmit a string of bytes to the user
void USER_TRANSMIT_START(char string[]){
	while(bluetoothSending){ // if a previous transmission is going on, wait, woken up by last tx interrupt
		sleep_enable(); // arm sleep mode
		sleep_cpu(); // put CPU to sleep
	}
		
	bluetoothSending = 1; // set transmission state so next calls need to wait until this is done
	strcpy(user_output_buffer, string); // copy the string to the buffer
	user_output_buffer_size = strlen(string)+1; // set the length of the buffer
	user_output_buffer[user_output_buffer_size-1] = '\0';
	user_output_buffer_ptr = 0; // reset the buffer pointer
	USER_TRANSMIT(' '); // start the first byte transmission, the rest is handled by the tx interrupt
}

// soft initialize, used to reset sensors
void INIT(){
	TOS = CRC3(RESET_REQUEST);
	SENSOR_TRANSMIT(TOS); // reset sensor
	TOS_STATE = FULL;
	START_TIMER(0); // start timeout timer 1
	USER_TRANSMIT_START("RESETTING SENSOR");
}

// main function that deals with the sensor input protocol
void TREAT_SENSOR_DATA(unsigned char packet_in){
	SENSOR_TRANSMIT(packet_in);
	
	if (((1<<7)&packet_in) == 0x80) {// if not command type, eg packet_in[7] == 1
		TOS = packet_in;
		TOS_STATE = FULL;
		START_TIMER(1);
	} else { // it is a command
		STOP_TIMER(1);
		if( ((1<<7)&TOS) == 0x80) { // if TOS has data packet
			unsigned char result = CRC_CHECK11(packet_in);
			if (result==0xFF) { // If CRC_CHECK11 PASS
				if ( (packet_in&0xE0) == LOG_REQUEST ) { // if packet in is log request
					LOG_REQUEST_FUNCTION(TOS & 0x1F);
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
					STOP_TIMER(0);
				}
				else{ // not acknowledge
					if ( (packet_in&0xE0) == REPEAT_REQUEST ){
						if (TOS_STATE == FULL)
						SENSOR_TRANSMIT(TOS);
					}
				}

			} else { // if CRC_CHECK3 fails
				SENSOR_TRANSMIT(0xBB);
				SENSOR_TRANSMIT(CRC3(REPEAT_REQUEST));
			}
		}
	}
}

// reads and returns a byte from eeprom at address
char READ_FROM_EEPROM(short address){
	while((EECR & (1<<EEWE)) == 2)
	_delay_ms(1);
	
	EEARH = address&0xFF00;
	EEARL = address&0x00FF;
	EECR = (1<<EERE);
	return EEDR;
}

// writes a byte to eeprom at address
void SAVE_TO_EEPROM(unsigned short address, char data){
	while((EECR & (1<<EEWE)) == 2)
	_delay_ms(1);
	
	EEARH = address&0xFF00;
	EEARL = address&0x00FF;
	
	EEDR = data;
	EECR = (1<<EEMWE);
	EECR = (1<<EEWE);
}

// prompts the user to setup timeout and watchdog timers
void PROMPT_USER_WD(){
	unsigned short timer = 0;
	
	saved = READ_FROM_EEPROM(0x00); // read whether already setup on eeprom or not
	wdSaved = saved;
	
	// reads previous timer
	for(char i = 0; i < 2; i++){
		timer |= READ_FROM_EEPROM(0x01 + i);
		if(i == 0)
			timer = timer<<8;
	}
	
	watchdogSetting = READ_FROM_EEPROM(0x02);
	
	if(saved){ // if not saved
		USER_TRANSMIT_START("Please enter the timeout duration wanted in (0-8)s (0 to disable): ");
		timeOutCount = 0;
		}else{ // if saved, use previous read counter timer
		timeOutCount = timer;
	}
	
	while(saved){ // waits until user inputs and sets up timers
		sleep_enable(); // arm sleep mode
		sleep_cpu(); // put CPU to sleep
	}
	
	if(wdSaved){ // if not saved
		USER_TRANSMIT_START("Please enter the watchdog timer duration wanted in (0-8)s (0 to disable): ");
	}
	
	while(wdSaved){ // waits until user inputs and sets up timers
		sleep_enable(); // arm sleep mode
		sleep_cpu(); // put CPU to sleep
	}
	
	// saves settings to eeprom
	USER_TRANSMIT_START("Saving settings");
	// writing saved and timer settings
	SAVE_TO_EEPROM(0x00, 0x00);
	SAVE_TO_EEPROM(0x01, (timeOutCount&0xFF00)>>8);
	SAVE_TO_EEPROM(0x02, (timeOutCount&0x00FF));
	SAVE_TO_EEPROM(0x03, watchdogSetting);
	timeOutCount = 65536 - timeOutCount;
}

void enableWD(){
	char sub;
	switch (watchdogSetting){
		case 0:
		sub = 0;
		break;
		case 1:
		sub = WDTO_15MS;
		break;
		case 2:
		sub = WDTO_30MS;
		break;
		case 3:
		sub = WDTO_60MS;
		break;
		case 4:
		sub = WDTO_120MS;
		break;
		case 5:
		sub = WDTO_250MS;
		break;
		case 6:
		sub = WDTO_500MS;
		break;
		case 7:
		sub = WDTO_1S;
		break;
		case 8:
		sub = WDTO_2S;
		break;
	}
	if(sub){
		PORTB |= 0x01; // shows you that watchdog is armed
		wdt_enable(sub);
	}
}

// reset and restart procedure
void startI(void){
	SYS_CONFIG(); // configure the system
	PROMPT_USER_WD(); // prompt the user for timer setup if not setup
	INIT(); // initializes and reset the sensors
	USER_TRANSMIT_START("\rEnter choice (and period): 1-Mem Dump 2-Last Entry 3-Restart \0"); // prints menu
}

// main function
int main(void) {
	// reset eeprom, ONLY for demo purposes!
	/*SAVE_TO_EEPROM(0x00, 0xFF);
	SAVE_TO_EEPROM(0x01, 0x00);
	SAVE_TO_EEPROM(0x02, 0x00);*/
	
	startI();
	
	enableWD(); // enables watchdog timer if setup
	while(1){ // waits for user or sensor interrupts
		sleep_enable(); // arm sleep mode
		sleep_cpu(); // put CPU to sleep
	}
}
