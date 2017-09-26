# Virtual 65C816

This is a simple emulator designed to help bring up Fuzix on the 65C816
processor. The I/O interface matches that of the v65 virtual platform.

The 65C816 emulator itself is taken from lib65816.

### Licence

GNU GPL v2.

### Memory Management

The system is laid out with RAM from 0-0xFDFF, a 1 page I/O window at FExx
and RAM for the FFxx page and vectors. Above 64K RAM continues for the
rest of the 512K.

All management is done via the processor.

### I/O Address Space (FE00-FEFF)

I/O space is 0xFE00 plus

| Address | R/W | Description
| ------- | --- | -----------
| FE00-7  | r/w	| No-op (bank registers on v6502)
| FE10    | ro	| 50HZ Timer int clear (reports interrupts since last)
| FE20    | r   | Next input byte
| FE20    | w   | Output byte
| FE21    | ro  | Input status (bit 0 - input pending bit 1 - write ready)
| FE30    | r/w | Disk number
| FE31    | r/w | Block high
| FE32    | r/w | Block low	(512 byte blocks)
| FE33    | w   | Trigger disk action (sets diskstat, uses disk/block)
| FE34    | r/w | Read or write next byte
| FE35    | ro  | Disk status (clear on read)
| FE40    | w   | Write 0xA5 to halt system

### Bootstrap

The imaginary boot ROM loads block 0 from disk to $FC00 and jumps to it in
6502 mode.

### Notes

Currently one large disc is implemented. It's meant to provide a simple
API.
