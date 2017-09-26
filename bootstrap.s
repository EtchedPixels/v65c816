;
;	Load an image from the disk into low memory and run it in 65C816 mode
;	Support loading the image into whatever bank we hardcode
;
	.segment "BOOT"

	.P816
	.A8
	.I16

ptr1	=	$FE
kernel	=	$000000
kbank	=	$00

	; At 0xFC00

reset:
	sei
	clc
	xce
	sep #$20
	rep #$10

	;
	; 	We are now into 65c816 mode so we can hit arbitrary banks
	; 	and running with 16bit index, 8 bit data
	;

	ldx #hello
	jsr print

	lda #0
	tay
	sta ptr1
	sta $FE30	;	disk 0
	lda #$FF
	sta $FE31	;	block high
	lda #$80	;	block low (last 64K of our 32MB disk)
	sta $FE32
	lda #1
	sta $FE33

	ldx #$0000	; going to load [XX]0000 to [XX]FBFF
diskload_2:
	lda $FE35
	cmp #0
	bne diskfault
diskload_1:
	lda $FE34
	sta kernel,x
	inx
	cpx #$FC00
	beq done
	txa		; see if low byte is 0 and if so print a *
	bne diskload_1
	lda #'*'
	sta $FE20
	bra diskload_2	; check for error and carry on
done:
	lda #13
	sta $FE20
	lda #10
	sta $FE20
	lda #kbank
	pha
	plb
	lda $0300
	cmp #65
	bne badcode
	lda $0301
	cmp #81
	bne badcode
	ldx #booting
	jsr print
	jmp kernel+$0302

print:
	lda 0,x
	cmp #0
	beq printed
	sta $FE20
	inx
	bra print
printed:lda #13
	sta $FE20
	lda #10
	sta $FE20
	rts

diskfault:
	ldx #disk
	jsr print
spin:	bra spin

badcode:
	ldx #bad
	jsr print
	bra spin

hello:
	.byte "Disk Loader 0.1",13,10,"Loading...",0
booting:
	.byte "Booting...",0
bad:
	.byte "No loadable image found.",0
disk:
	.byte "Disk error.",0
