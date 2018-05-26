#pragma once
#include "tgif_lib.h"

typedef struct TColorMapObject {
    int ColorCount;
    TGifColorType Colors[256];
} TColorMapObject;

typedef struct TGifFileType {
    int Error;			     /* Last error condition reported */
    int MaxCodeUsed;
    void *Private;                   /* Don't mess with this! */
} TGifFileType;


/******************************************************************************
 GIF encoding routines
******************************************************************************/

/* Main entry points, you basically just run through them in this order. */
TGifFileType *TEGifOpenFileName(const char *GifFileName, int *Error);
int TEGifPutScreenDesc(TGifFileType *GifFile,
                  const uint16_t Width,
                  const uint16_t Height,
                  const TColorMapObject *ColorMap, uint16_t SRAMLimit);
/* "Line" can be whatever you want from 1 pixel to all pixels */
int TEGifPutLine(TGifFileType *GifFile, TGifPixelType *GifLine,
                int GifLineLen);
int TEGifCloseFile(TGifFileType *GifFile, int *ErrorCode);


#define E_TGIF_SUCCEEDED          0
#define E_TGIF_ERR_OPEN_FAILED    1    /* And TEGif possible errors. */
#define E_TGIF_ERR_WRITE_FAILED   2
#define E_TGIF_ERR_HAS_SCRN_DSCR  3
#define E_TGIF_ERR_HAS_IMAG_DSCR  4
#define E_TGIF_ERR_NO_COLOR_MAP   5
#define E_TGIF_ERR_DATA_TOO_BIG   6
#define E_TGIF_ERR_NOT_ENOUGH_MEM 7
#define E_TGIF_ERR_DISK_IS_FULL   8
#define E_TGIF_ERR_CLOSE_FAILED   9
#define E_TGIF_ERR_NOT_WRITEABLE  10


