#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdint.h>

#include <gif_lib.h>
#include "tegif_lib.h"

TColorMapObject TGifColors;

static uint8_t MapColor(uint8_t r, uint8_t g, uint8_t b) {
	uint16_t c = (((r<<8)&0xF800) | ((g<<3)&0x07E0) | (b>>3));
	for(int i=0;i < TGifColors.ColorCount;i++) {
		if (c == TGifColors.Colors[i]) return i;
	}
	int idx = TGifColors.ColorCount;
	TGifColors.Colors[TGifColors.ColorCount++] = c;
	return idx;
}

static void PrintGifError(int error) {
	fprintf(stderr,"[T]GIF Error: %d\n", error);
}

int main(int argc, char** argv) {
	GifFileType *GifFile;
	uint16_t sram_limit = 3072;
	if ((argc < 3)||(argc > 4)) {
		fprintf(stderr, "%s <in.gif> <out.bin> [SRAM]", argv[0]);
		return 1;
	}

	int Error;
	if ((GifFile = DGifOpenFileName(argv[1], &Error)) == NULL) {
		PrintGifError(Error);
		exit(EXIT_FAILURE);
	}

	if (DGifSlurp(GifFile) == GIF_ERROR) {
		PrintGifError(GifFile->Error);
		exit(EXIT_FAILURE);
	}

	TGifFileType *TGif = TEGifOpenFileName(argv[2], &Error);

	if (!TGif) {
		/* This is crude hack, but the codes should actually match :P */
		PrintGifError(Error);
		exit(EXIT_FAILURE);
	}

	const ColorMapObject *InputColors = 0;
	if (GifFile->SavedImages->ImageDesc.ColorMap) InputColors = GifFile->SavedImages->ImageDesc.ColorMap;
	else InputColors = GifFile->SColorMap;

	const int Width  = GifFile->SavedImages->ImageDesc.Width;
	const int Height = GifFile->SavedImages->ImageDesc.Height;
	const uint8_t * InPixels = GifFile->SavedImages->RasterBits;
	const int PixelCount = Width*Height;
	uint8_t *OutPixels = malloc(PixelCount);

	/* We perform a palette remapping to:
	 * 1. Only include the colors that are used in the image
	 * 2. Make sure the palette is not sparse
	 * 3. Combine any RGB888 colors that are the same in RGB565 */

	int16_t PaletteMap[256];
	memset(PaletteMap, 0xFF, 256 * sizeof(int16_t));

	TGifColors.ColorCount = 0;

	for (int i=0;i < PixelCount;i++) {
		uint8_t p = InPixels[i];
		if (PaletteMap[p] < 0) {
			PaletteMap[p] = MapColor(InputColors->Colors[p].Red, InputColors->Colors[p].Green, InputColors->Colors[p].Blue);
		}
		OutPixels[i] = PaletteMap[p];
	}

	if (argc==4) {
		sram_limit = atoi(argv[3]);
		if (!sram_limit) {
			fprintf(stderr, "Invalid SRAM bytes number\n");
			exit(EXIT_FAILURE);
		}
	}


	printf("Processing %dx%d image with %d colors\n", Width, Height, TGifColors.ColorCount);
	printf("Setting up to encode for a decoder with %d bytes of SRAM\n", sram_limit);

	if (TEGifPutScreenDesc(TGif, Width, Height, &TGifColors, sram_limit) == TGIF_ERROR) {
		PrintGifError(TGif->Error);
		exit(EXIT_FAILURE);
	}

	if (TEGifPutLine(TGif, OutPixels, PixelCount) == TGIF_ERROR) {
		PrintGifError(TGif->Error);
		exit(EXIT_FAILURE);
	}
	int MaxCode = TGif->MaxCodeUsed;
	if (TEGifCloseFile(TGif, &Error) == TGIF_ERROR) {
		PrintGifError(Error);
		exit(EXIT_FAILURE);
	}

	printf("Everything is ok (max code used=%d)\n", MaxCode);
	return 0;
}
