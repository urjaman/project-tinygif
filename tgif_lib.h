#pragma once

/******************************************************************************
tgif_lib.h - service library for encoding Tiny "GIF" images
*****************************************************************************/

#define TGIF_ERROR   0
#define TGIF_OK      1

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef unsigned char TGifPixelType;
typedef unsigned char TGifByteType;
typedef int TGifWord;
typedef uint16_t TGifColorType;
