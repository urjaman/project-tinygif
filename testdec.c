#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

#include "tdgif_lib.h"


static TGifInfo Info;

static int output_calls = 0;

static char output_xt[256];

void MakeXT(void) {
	int n;
	for (n=0;n<Info.ColorCount;n++) {
		uint16_t col = Info.Colors[n];
		uint8_t r,g,b,ch;
		r = (col >> 11) & 0x1F;
		g = (col >> 6)  & 0x1F;
		b = col & 0x1F;
		if ((r < 7)&&(g < 7)&&(b < 7)) {
			ch = ' ';
		} else if ((r > g)&&(r > b)) {
			if (r > 16) ch = 'R';
			else ch = 'r';
		} else if ((g > r)&&(g > b)) {
			if (g > 16) ch = 'G';
			else ch = 'g';
		} else if ((b > r)&&(b > g)) {
			if (b > 16) ch = 'B';
			else ch = 'b';
		} else if ((r > 24)&&(g > 24)&&(b > 24)) {
			if (g > 29) ch = 'W';
			else ch = 'w';
		} else {
			if (g > 16) ch = 'X';
			else ch = 'x';
		}
		output_xt[n] = ch;
	}
	for (;n<256;n++) {
		output_xt[n] = '!';
	}
}


void Output(uint8_t c) {
	printf("%c", output_xt[c]);
	output_calls++;
	if ((output_calls % Info.Width)==0) printf("\n");
}


static void PrintError(int error) {
	fflush(stdout);
	fprintf(stderr,"\n[T]GIF Error: %d (after %d output calls)\n", error, output_calls);
}


int main(int argc, char** argv) {
	if (argc != 2) {
		fprintf(stderr, "%s <tgif.bin>", argv[0]);
		return 1;
	}
	int fd = open(argv[1], O_RDONLY);
	if (fd<0) {
		fprintf(stderr, "open '%s' failed\n", argv[1]);
		return 2;
	}
	int len = lseek(fd, 0, SEEK_END);
	if (len<0) {
		fprintf(stderr, "cannot determine file size");
		return 3;
	}
	const void *data = mmap(0, len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (!data) {
		fprintf(stderr, "failed to mmap file");
		return 4;
	}

	if (TDGifGetInfo(data, &Info, 1023, 1023, len) == TGIF_ERROR) {
		PrintError(Info.Error);
		return 5;
	}

	printf("%dx%d image with %d colors, requires %d bytes of SRAM to decode (len=%d)\n",
		Info.Width, Info.Height, Info.ColorCount, Info.SRAMLimit, len);

	MakeXT();

	if (TDGifDecompress(&Info, Output) == TGIF_ERROR) {
		PrintError(Info.Error);
		return 6;
	}

	printf("Decode success with %d output calls\n", output_calls);
	return 0;
}
