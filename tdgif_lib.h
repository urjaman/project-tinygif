#pragma once

#include "tgif_lib.h"

typedef struct TGifInfo {
    uint16_t Width;
    uint16_t Height;
    uint16_t SRAMLimit;
    int ColorCount;
    const TGifColorType *Colors;
    const void* Data;
    int Error;			     /* Last error condition reported */
    uint16_t MaxSz;
} TGifInfo;

#define D_TGIF_ERR_MAXSZ          20 /* Maximum size too small / file truncated or corrupt */
#define D_TGIF_ERR_ZWH            21 /* Zero Width or Height */
#define D_TGIF_ERR_TOOBIG         22 /* MaxW or MaxH exceeded */
#define D_TGIF_ERR_NOT_ENOUGH_MEM 23
#define D_TGIF_ERR_IMAGE_DEFECT   24

int TDGifGetInfo(const void *TGif, TGifInfo *Info, const uint16_t MaxW,
	const uint16_t MaxH, const uint16_t MaxSz);
int TDGifDecompress(TGifInfo *Info, void(*OutputCB)(uint8_t) );
