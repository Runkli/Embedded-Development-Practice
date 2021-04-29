.include "m128def.inc"

.equ CRC_POL = 0x35 ;110101 ; x^5 + x^4 + 0*x^3 + x^2 + x^1 1

.equ NEWRAMEND = 0x10FF

.def PACKET_IN_REGISTER = R21

; MAIN BRANCH
.org 0x00
; program starts here linearly
CONF:
	; constant registers
	ldi r17, 0x00	; zero register
	ldi r18, 0xFF	; one register
	mov r1, r17
	mov r2, r18
	
	; configure ports
	ldi r16, 0x08
	sts ddrf, r16	; user input and sensor interaction
	out ddrb, r2	; ram dump output
	out ddrd, r1	; command input
	out ddre, r2	; command output

	; configure xmem mode
    ldi r16, 0x80
    out mcucr, r16

	; Y will be used as memory pointer
	ldi yl, 0x00
	ldi yh, 0x01
	

INIT: ; Transmits a Reset request in a Packet_Out to reset the remote sensor state

	; Initialize stack pointer, since no flow reaches before here, stack pointer is in INIT
	ldi r16,low(NEWRAMEND)
	out spl, r16
	ldi r16,high(NEWRAMEND)
	out sph, r16

	;-----------
	ldi r20, 0x00	; reset request
    call CRC3
	call TRANSMIT

	push r20

PRE_SERVICE_READOUT:
	call SERVICE_READOUT
	
	; start == 1?
	sbis pinf, 2	; if bit 2 in io register is cleard, continue
	;no
	rjmp INIT
	;yes
	ldi r16, 0x08
	sts portf, r16

	; receive == 1?
	sbis pinf, 4	; if bit 4 in io register is set, continue
	;no
	rjmp PRE_SERVICE_READOUT
	;yes
	; receive == 0?
POLL_RECV:
	sbic pinf, 4	; if bit 4 in io register is cleard, continue
	;no
	rjmp POLL_RECV
	;yes
	sts portf, r1 ; portf = 0
	in PACKET_IN_REGISTER, PIND

	; Packet_in commmand type? (if bit 7 == 0, it is command)
	sbrc PACKET_IN_REGISTER, 7
	; no
	rjmp TREAT_DATA ; first horizontal branch
	; yes (command)

	;tos has data packet?
	; load tos to register
	pop r16
	in r16, spl
	dec r16
	out spl, r16 
	sbrs r16, 7 ; if bit 7 == 1, it is a data packet (yes)
	;no
	rjmp CRC_CHECK3_PATH ; second horizontal branch
	;yes
	call CRC_CHECK11 ; expects return on r24
	cpi r24, 0xFF
	;true
	breq LOG_REQUEST_PATH
	;false (fail)
	pop r16
PRE_REPEAT_REQUEST:
	call REPEAT_REQUEST
	rjmp PRE_SERVICE_READOUT
	;END

TREAT_DATA: ; (HORIZONTAL BRANCH)
	; stack empty? (if sp == ramend)
	call IS_STACK_EMPTY
	breq skip_TD
	;no
	pop r16
skip_TD:
	;yes
	push PACKET_IN_REGISTER
	rjmp PRE_SERVICE_READOUT

CRC_CHECK3_PATH:
	call CRC_CHECK3 ; expects return on r24
	cpi r24, 0xFF
	breq check3_pass
	rjmp PRE_REPEAT_REQUEST

	check3_pass:
	; acknowledge in packet_in?
	mov r16, PACKET_IN_REGISTER
	andi r16, 0x60 ; mask it
	cpi r16, 0x40 ;equal to 0100 0000 (acknowledge)?
	;no
	brne THIRD_H_BRANCH
	;yes
	;stack empty? (if stack == newramend)
	call IS_STACK_EMPTY
	;yes
	breq PRE_SERVICE_READOUT
	;no
	pop r16
	rjmp PRE_SERVICE_READOUT

	rjmp PRE_SERVICE_READOUT

THIRD_H_BRANCH:
	;repeat?
	mov r16, PACKET_IN_REGISTER
	andi r16, 0x60
	cpi r16, 0x60 ;repeat?
	;no
	brne PRE_SERVICE_READOUT
	;yes
	;stack emtpy?
	call IS_STACK_EMPTY
	;yes
	breq PRE_SERVICE_READOUT
	;no
PRE_RETRANSMIT:
	pop r20
	call TRANSMIT
	rjmp PRE_SERVICE_READOUT

LOG_REQUEST_PATH:
	mov r16, PACKET_IN_REGISTER
	andi r16, 0x60
	cpi r16, 0x20;log request?
	;no
	breq LOG
	rjmp INIT
	;yes
LOG:
	pop r16
	st Y+, r16
	call CHECK_MEM_POINTER
	ldi r20, 0x40 ; TOS <- acknowledge
	call CRC3
	push r20
	rjmp PRE_RETRANSMIT

;---- custom subroutines ----
CHECK_MEM_POINTER:
	in r16, spl
	in r17, sph
	;if sp < y, don't reset y
	cp r16, yl
	cpc r17, yh
	brsh skip
	ldi yl, low(0x100)
	ldi yh, high(0x100)
	skip:
	ret

IS_STACK_EMPTY: ; sets the z using cp
	ldi r16, LOW(RAMEND)
	in r17, spl
	cp r17, r16
	ldi r16, HIGH(RAMEND)
	in r17, sph
	cpc r17, r16
	ret

WAIT_ONE_SECOND:
	push r16
	push r17
	push r18

	ldi r16, 60
	loop1:
		dec r16
		ldi r17, 0xFF
		loop2:
			dec r17
			ldi r18, 0xFF
			;loop3: ; removed because simulation is slow
				;dec r18
				;cpse r18, r1
				;rjmp loop3
			cpse r17, r1
			rjmp loop2
		cpse r16, r1
		rjmp loop1

	pop r18
	pop r17
	pop r16
	ret

;---- subroutines specified in prompt ----
TRANSMIT: ; transmits r20 to packet_out
	out porte, r20
	ret

REPEAT_REQUEST:
	push r20
	ldi r20, 0b01100000
	call CRC3
	call TRANSMIT
	pop r20
	ret

SERVICE_READOUT:
	push r16

	lds r16, pinf
	sbrc r16, 0
	rjmp start_mem_dump
	
	lds r16, pinf
	sbrc r16, 1
	rjmp transmit_last_entry

	pop r16
	ret

	start_mem_dump:
		push r20
		ldi xl, low(0x100)
		ldi xh, high(0x100)
		;while x < y
		mem_dump_loop:
			cp xl, yl
			cpc xh, yh
			breq end
			; do
			ld r20, X+
			out portb, r20
			call WAIT_ONE_SECOND
			rjmp mem_dump_loop

	transmit_last_entry:
		push r20
		mov xl, yl
		mov xh, yh
		sbiw X, 1
		ld r20, X
		call TRANSMIT
	end:
		pop r20
		pop r16
		ret

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
	ldi r20, 0x00	; if not true, then store 0, false in r16
	rjmp CRC_CHECK3_finish
CRC_CHECK3_pass:
	ldi r20, 0xFF	; store true
CRC_CHECK3_finish:	
	ret ; returns true or false in r20 (stand)

CRC_CHECK11: ; (UNFINISHED)
	ldi r24, 0x00
	ret
