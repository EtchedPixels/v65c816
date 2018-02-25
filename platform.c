#include "config.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>
#include <termios.h>

#include <lib65816/cpu.h>
#include <lib65816/cpuevent.h>

#include <SDL2/SDL.h>

#define MAX_DISK	8

static uint8_t ram[1024 * 8192];

static uint8_t timer_int;
static uint16_t blk;
static uint8_t disk;
static uint8_t diskstat;
static uint8_t dma = 0x34;	/* For the moment */

FILE *diskfile[MAX_DISK];


/*
 *	This is a fairly dumb way to implement an emulated frame buffer but
 *	on the other hand it didn't take very long to write.
 */

static SDL_Window *screen;
static SDL_Renderer *rend;
static SDL_Texture *texture;
static uint32_t pixbuf[16384 * 8];
static uint8_t vram[16384];
static uint8_t key;


static void video_shutdown(void)
{
	SDL_Quit();
}

static int video_init(void)
{
	SDL_Init(SDL_INIT_EVERYTHING);

	screen = SDL_CreateWindow("v65C816",
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			656, 416, SDL_WINDOW_RESIZABLE);

	if (screen == NULL)
		return -1;

	rend = SDL_CreateRenderer(screen, -1, 0);

	if (rend == NULL)
		return -1;

	texture = SDL_CreateTexture(rend, SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_STREAMING, 640, 200);

	if (texture == NULL)
		return -1;

	SDL_SetRenderDrawColor(rend, 0, 0, 0, 255);
	SDL_RenderClear(rend);
	SDL_RenderPresent(rend);

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_RenderSetLogicalSize(rend, 656, 416);
//	SDL_RenderSetScale(rend, 1.0, 2.0);

	SDL_StartTextInput();

	return 0;
}


void video_write_ram(uint16_t addr, uint8_t val)
{
	int i;
	uint32_t taddr;

	addr &= 0x3FFF;
	vram[addr] = val;

	taddr = addr << 3;

	for (i = 0; i < 8; i++) {
		if (val & 0x80)
			pixbuf[taddr] = 0xFFFFFFFF;
		else
			pixbuf[taddr] = 0x00000000;
		taddr++;
		val <<= 1;
	}
}

static uint8_t video_read_ram(uint16_t addr)
{
	addr &= 0x3FFF;
	return vram[addr];
}

static void video_update(void)
{
	SDL_Rect sr;
	sr.x = 8;
	sr.y = 8;
	sr.w = 640;
	sr.h = 400;
	SDL_UpdateTexture(texture, NULL, pixbuf, 640 * 4);
	SDL_RenderClear(rend);
	SDL_RenderCopy(rend, texture, NULL, &sr);
	SDL_RenderPresent(rend);
}

static int video_event(void)
{
	SDL_Event event;

	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT)
			return -1;
		if (event.type == SDL_TEXTINPUT) {
			key = *event.text.text;
			printf("Typed '%c'\n", key);
		}
	}
	return 0;
}


static uint8_t check_chario(void)
{
	fd_set i, o;
	struct timeval tv;
	unsigned int r = 0;

	FD_ZERO(&i);
	FD_SET(0, &i);
	FD_ZERO(&o);
	FD_SET(1, &o);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (select(2, &i, NULL, NULL, &tv) == -1) {
		perror("select");
		exit(1);
	}
	if (FD_ISSET(0, &i))
		r |= 1;
	if (FD_ISSET(1, &o))
		r |= 2;
	return r;
}

static unsigned int next_char(void)
{
	char c;
	if (read(0, &c, 1) != 1) {
		printf("(tty read without ready byte)\n");
		return 0xFF;
	}
	return c;
}

static uint8_t check_keyio(void)
{
	if (key == 0xFF)
		return 0;
	return 1;
}

static uint8_t next_key(void)
{
	uint8_t r = key;
	key = 0xFF;
	return r;
}

static uint8_t io_read(uint32_t addr)
{
	addr &= 0xFF;
	if (addr == 0x10) {
		uint8_t v = timer_int;
		timer_int = 0;
		CPU_clearIRQ(1);
		return v;
	}
	if (addr == 0x11)
		return dma;
	if (addr == 0x20)
		return next_char();
	if (addr == 0x21)
		return check_chario();
	if (addr == 0x22)
		return next_key();
	if (addr == 0x23)
		return check_keyio();
	if (addr == 0x30)
		return disk;
	if (addr == 0x31)
		return blk >> 8;
	if (addr == 0x32)
		return blk & 0xFF;
	if (addr == 0x34) {
		int c;
		if (disk >= MAX_DISK || diskfile[disk] == NULL) {
			diskstat = 0x02;
			return 0xFF;
		} else {
			c = fgetc(diskfile[disk]);
			if (c == EOF)
				diskstat = 0x02;
			return (uint8_t) c;
		}
	}
	if (addr == 0x35) {
		uint8_t v = diskstat;
		diskstat = 0;
		return v;
	}
	fprintf(stderr, "Invalid I/O read %x\n", addr);
	CPU_abort();
	return 0xFF;
}


static void io_write(uint32_t addr, uint8_t value)
{
	addr &= 0xFF;
	if (addr == 0x11) {
		dma = value;
		return;
	}
	if (addr == 0x20) {
		putchar(value);
		fflush(stdout);
		return;
	}
	if (addr == 0x30) {
		if (value >= MAX_DISK || diskfile[value] == NULL)
			diskstat = 0x02;
		else
			disk = value;
		return;
	}
	if (addr == 0x31) {
		blk &= 0xFF;
		blk |= value << 8;
		return;
	}
	if (addr == 0x32) {
		blk &= 0xFF00;
		blk |= value;
		return;
	}
	if (addr == 0x33) {
		if (disk >= MAX_DISK || diskfile[disk] == NULL)
			diskstat = 0x02;
		else
			diskstat = (fseek(diskfile[disk], blk << 9, SEEK_SET) == -1) ? 0x01 : 0;
		return;
	}
	if (addr == 0x34) {
		if (disk >= MAX_DISK || diskfile[disk] == NULL)
			diskstat = 0x02;
		else
			fputc(value, diskfile[disk]);
		return;
	}
	if (addr == 0x40 && value == 0xA5)
		exit(0);
	if (addr == 0x41) {
		CPU_setTrace(value);
		return;
	}
	fprintf(stderr, "Invalid I/O write %x\n", addr);
	CPU_abort();
}

uint8_t read65c816(uint32_t addr, uint8_t debug)
{
	if ((addr >> 16) == 0xFF)
		addr = 0xFE00 + dma;
	if ((addr >> 16) == 0xFE)
		return video_read_ram(addr);
	if (addr >= 0xFE00 && addr < 0xFF00) {
		/* Don't cause I/O when using debug trace read */
		if (debug)
			return 0xFF;
		else
			return io_read(addr);
	} else if (addr < sizeof(ram))
		return ram[addr];
	else {
		if (!debug) {
			fprintf(stderr, "Invalid mem read %x\n", addr);
			CPU_abort();
		}
		return 0xFF;
	}
}

void write65c816(uint32_t addr, uint8_t value)
{
	if ((addr >> 16) == 0xFF)
		addr = 0xFE00 + dma;
	if ((addr >> 16) == 0xFE)
		video_write_ram(addr, value);
	else if (addr >= 0xFE00 && addr < 0xFF00)
		io_write(addr, value);
	else if (addr < sizeof(ram))
		ram[addr] = value;
	else {
		fprintf(stderr, "Invalid mem write %x\n", addr);
		CPU_abort();
	}
}

static void take_a_nap(void)
{
	struct timespec t;
	t.tv_sec = 0;
	t.tv_nsec = 10000000;
	if (nanosleep(&t, NULL))
		perror("nanosleep");
}

void system_process(void)
{
	video_update();
	if (video_event())
		exit(0);
	take_a_nap();
	timer_int++;
	CPU_addIRQ(1);
}

void wdm(void)
{
}

static struct termios saved_term, term;

static void cleanup(int sig)
{
	ioctl(0, TCSETS, &saved_term);
	exit(1);
}

static void exit_cleanup(void)
{
	ioctl(0, TCSETS, &saved_term);
}

static char diskname[16] = "disk";

int main(int argc, char *argv[])
{
	int i;
	int debug = 0;

	if (argc == 2 && strcmp(argv[1], "-t") == 0) {
		CPU_setTrace(1);
		argc--;
		argv++;
		debug = 1;
	}
	if (argc != 1) {
		fprintf(stderr, "%s [-t]\n", argv[0]);
		exit(1);
	}

	if (video_init() < 0)
		exit(1);

	if (ioctl(0, TCGETS, &term) == 0) {
		saved_term = term;
		atexit(exit_cleanup);
		signal(SIGINT, cleanup);
		signal(SIGQUIT, cleanup);
		term.c_lflag &= ~(ICANON | ECHO);
		term.c_cc[VMIN] = 1;
		term.c_cc[VTIME] = 1;
		if (!debug)
			term.c_lflag &= ~ISIG;
		ioctl(0, TCSETS, &term);
	}

	for (i = 0; i < MAX_DISK; i++) {
		sprintf(diskname + 4, "%d", i);
		diskfile[i] = fopen(diskname, "r+");
	}
	if (diskfile[0] == NULL) {
		perror("disk0");
		exit(1);
	}
	/* This doesn't go via the memory routines so we can load over
	   the I/O space with ignored bytes just fine */
	if (fread(ram + 0xFC00, 512, 1, diskfile[0]) != 1) {
		fprintf(stderr, "Unable to read boot image.\n");
		exit(1);
	}
	ram[0xfffd] = 0xFC;
	CPUEvent_initialize();
	CPU_setUpdatePeriod(41943);	/* 4MHz */
	CPU_reset();
	CPU_run();
	video_shutdown();
	exit(0);
}
