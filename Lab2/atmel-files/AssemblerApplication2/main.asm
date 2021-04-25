
.equ CRC_POL = 0x35 ;110101 ; x^5 + x^4 + 0*x^3 + x^2 + x^1 1
; Replace with your application code

.org 0x00
jmp start

CONF_PORTS:
	push r17 ; store r17
	push r18 ; r18 so we can manipulate
	ldi r17, 0x00	; constant zero register
	ldi r18, 0xFF	; constant one register
	sts ddrf, r17	; user input
	out ddrb, r17	; ram dump input ???
	out ddrd, r17	; command input
	out ddre, r18	; command output
	pop r18	; retreive r18
	pop r17	; retreive r17
	ret

INIT:	
	; Initialize stack pointer
	push r16
	ldi r16,low(RAMEND)
	out spl,r16
	ldi r16,high(RAMEND)
	out sph,r16
	pop r16
	;-----------
	ldi r20, 0x00	; reset request

CRC3:
	/* Modulo 2 example
	11100000 110101
	111000  / 110101 = 1, xor = 001101
	 11010  / 110101 = 0
	 110100 / 110101 = 1, xor = 000001
	 if size(packet) == size(code)
		xor(packet,code)
	 else
		left_shift_logical(packet)
	*/
	;pop r20
	push r16
	push r17
	push r21
	
.def packet = r20
.def reference = r21
.def code = r16
.def compare = r17

	mov reference, packet
	; Packet_Out <- Reset Req. assume Packet_Out is in arbitrary register

	; r20 = 1110 0000
	lsr packet
	lsr packet ; right shift twice to align with generator code
	ldi code, CRC_POL	; load CRC generator code
	mov compare, code

	cpi packet, 0x20
	brlo s1
	eor packet,code
s1:
	lsl packet
	cpi packet, 0x20
	brlo s2
	eor packet,code
s2:
	lsl packet
	cpi packet, 0x20
	brlo cont
	eor packet,code
cont:
	or packet, reference
	pop r21
	pop r17
	pop r16
	;push r20
.undef packet
.undef reference
.undef code
.undef compare
	ret

CRC_CHECK3:
	.def original_packet=r21
	.def packet = r20
	mov original_packet, packet
	rcall CRC3 ; reads and writes to r20
	cp packet, original_packet
	breq CRC_CHECK3_pass	; if they are equal, store 0xFF (1, true) return value to r16
	ldi r16, 0x00	; if not true, then store 0, false in r16
	rjmp CRC_CHECK3_finish
CRC_CHECK3_pass:
	ldi r16, 0xFF	; store true
CRC_CHECK3_finish:	
	ret


.org 0x900
start:
	; put your argument in r20
	jmp CONF_PORTS	; will also do INIT since it is right beneath it
	
	ldi r20, 0x40 ; 0 00 00000 [command packet, 2-bit command, 5 itu code 
    call CRC3
	
	//call SERVICE_READOUT
	
	sbis pinf,0	; bit 0 of pinf for START
	; check if bit 0 is set, if START is asserted, do not go to INIT, continue
	jmp INIT

	// Ready <- ON
	ldi r16, 0x00
	sts ddrg, r16	; set bit 0 to Ready led
	ldi r16,0xFF	
	sts ping, r16 ; turn on the Ready led
rec:
	sbis pinf, 1	; bit 0 of pinf for Receive
	rjmp rec	; loop until receive has been assserted

	ldi r16, 0x00
	sts ping, r16 ; turn off Ready

	in r16, pind	; Packet_In
	sbrs r16, 7		; if bit 7 in Packet_In is set, that means it's a Data Packet
	jmp subr_c3_NO	; if it's Data Packet, then we go to "Yes" route in the diagram
	; if not, then it's a Command Packet, and we go with the "No" route	
branch1:
	// <TOS has Data packet?>



subr_c3_NO:
	push r17
	push r18
	ldi r18, SPL
	ldi r17, low(RAMEND)
	cp r18, r17	; if stack pointer is at the bottom, stack is empty

	breq subr_c3_NO_y
	pop r18
	pop r17
subr_c3_NO_y:
	// packet in is in r16, put it to top of stack
	push r16


