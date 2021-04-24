; This is a simple ATmega128 assembly firmware to solve the
; problem of receiving 3 unsigned 8-bit parameters using
; parallel ports through simple Request/Acknowledge protocol,
; sanitizing the parameters against predetermined expected ranges,
; outputting them to parallel ports, and logging the values to
; internal data memory (SRAM).
; Code
.include "m128def.inc"
.EQU ZEROS = 0x00
.EQU ONES = 0xFF
.EQU T_LO_LMT = 0x0A
.EQU T_HI_LMT =0xF0
.EQU MEM_START = 0x1000
.EQU MEM_END = 0x10FF
.CSEG
; Start using the Program Memory at the following address
	jmp 0x050 ;jump to program
.ORG 0x050 ;set program start address
	ldi XH, high(MEM_START)
	ldi XL, low(MEM_START)
	
	ldi r17, zeros	; temprature:	10 (0xA) <= T <= 240 (0xF0)
	ldi r18, zeros	; moisture:		20 (0x14) <= M <= 200 (0xC8) 
	ldi r19, zeros	; water level	5 (0x5) <= W <= 250 (0xFA)
	ldi r20, zeros	; zero constant as register
	
	ldi r21, 0x01	; for input/output config of ddrg 00001, one bit for output
	sts ddrg, r21	;setting the ddrg
	ldi r22, 1
	ldi r23, 0xFF
	out ddrd, r23
	out ddre, r23
	sts ddrf, r23
	ldi r16, zeros	; overwrite default state to off
buttonwait:	; Button wait routine	
	lds r16, ping	; read button pin a into register 0
	
	sbrs r16, 4
	rjmp buttonwait	; jump and wait for input
	sts ping, r22
	sts ping, r16 ; acknowledge led output
; If button is pressed
	; Read data from 3 pins
	; r17 Temprature, r18 Moisture, r19 Water level
	in r17, pina
	in r18, pinb
	in r19, pinc
	
; Sanitizing
checkTemp:
	cpi r17, 0xA	; check if less than 10
	brlo tempRange
	cpi r17, 0xF1	; if equal or bigger than 241
	brsh tempRange
checkMoist:
	cpi r18, 0x14	; check if less than 20
	brlo moistRange
	cpi r18, 0xC9	; check if equal or bigger than 201
	brsh moistRange
checkLevel:
	cpi r19, 0x5
	brlo levelRange
	cpi r19, 0xFB	; if equal or more than 251
	brsh levelRange
	jmp exit

; flow control and next states
tempRange:
	ldi r17, 0xFF
	jmp checkMoist

moistRange:
	ldi r18, 0xFF
	jmp checkLevel

levelRange:
	ldi r19, 0xFF
	jmp exit
exit:
	; output to io
	out portd, r17
	out porte, r18
	sts portf, r19
	; output to memory
	st X+, r17
	st X+, r18
	st X+, r19
	st X+, r20
	
	
buttonwait2:	; Button wait routine
	lds r16, ping	; read button pin a into register 16
	sbrc r16, 4
	rjmp buttonwait2	; jump and wait for input
	sts ping, r16
	
	; if no space left in SRAM,
	; set the pointer to the beginning of memory
	cpi XL, 0xfc
	brne buttonwait

	ldi XH, high(MEM_START)
	ldi XL, low(MEM_START)
	
	jmp buttonwait
	