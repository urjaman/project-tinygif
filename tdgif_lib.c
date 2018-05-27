/******************************************************************************
tdgif_lib.c - Tiny "GIF" decoding
*****************************************************************************/

#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>

#include "tdgif_lib.h"
#include "tgif_lib_private.h"

#ifdef __AVR
#include <avr/pgmspace.h>
#include <avr/io.h>
typedef __uint24 uint24_t;
#define printf()

#ifdef USE_ALLOCA
#include <alloca.h>
#define ALLOC(x) alloca_check(x) ? alloca(x) : 0;
#define FREE(x)

extern char _end;

static uint8_t alloca_check(uint16_t size) {
	const int margin = 64;
	char *stackp = (char*)SP;
	char *e = (char*)&_end;
	uint16_t avail = stackp - e;
	if (avail >= (size+margin)) return 1;
	return 0;
}

#else
#define ALLOC(x) malloc(x)
#define FREE(x) free(x)
#endif

#else
#include <stdio.h>
typedef uint32_t uint24_t;
#define PROGMEM
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define ALLOC(x) malloc(x)
#define FREE(x) free(x)
#endif

typedef struct TDGifPrivateType {
    TGifInfo *Info;
    uint16_t *Prefix;
    uint16_t
        ReadOffset,
        ClearCode,   /* The CLEAR LZ code. */
        RunningCode, /* The next code algorithm can generate. */
        MaxCode1,    /* 1 bigger than max. possible code, in RunningBits bits. */
        MaxCodePoint,
	DictBase,
	DictSize;
    uint24_t CrntShiftDWord;   /* For bytes decomposition into codes. */
    uint8_t
        RunningBits,
        InitCodeBits,
	MaxCodeBits,
        CrntShiftState;    /* Number of bits in CrntShiftDWord. */
} TDGifPrivateType;

/* Just byte access */
static uint8_t TDGifReadByte(const void* base, uint16_t offset) {
	const uint8_t *d = base;
	return pgm_read_byte(d+offset);
}


static int
TDGifInput(TDGifPrivateType *Private, uint8_t *NextByte)
{
    if (Private->ReadOffset >= Private->Info->MaxSz) {
	Private->Info->Error = D_TGIF_ERR_MAXSZ;
        return TGIF_ERROR;
    }

    *NextByte = TDGifReadByte(Private->Info->Data, Private->ReadOffset++);
    //printf("RB %04X:%02X ", Private->ReadOffset-1, *NextByte);
    return TGIF_OK;
}


static const unsigned short CodeMasks[] PROGMEM = {
	0x0000, 0x0001, 0x0003, 0x0007,
	0x000f, 0x001f, 0x003f, 0x007f,
	0x00ff, 0x01ff, 0x03ff
};



/******************************************************************************
 The LZ decompression input routine:
 This routine is responsable for the decompression of the bit stream from
 8 bits (bytes) packets, into the real codes.
 Returns GIF_OK if read successfully.
******************************************************************************/
static int
TDGifDecompressInput(TDGifPrivateType *Private, uint16_t *Code)
{
    /* Optimization note: AVRs suck at variable shifts, but fixed 8-bit
     * shifts are trivial, thus these optimizations to reduce-by-8
     * if possible */

    uint8_t NextByte;

    while (Private->CrntShiftState < Private->RunningBits) {
        /* Needs to get more bytes from input stream for next code: */
        if (TDGifInput(Private, &NextByte) == TGIF_ERROR) {
            return TGIF_ERROR;
        }
        uint24_t BigNextByte = NextByte;
	uint8_t BigShift = Private->CrntShiftState;
	if (BigShift >= 8) {
		BigShift -= 8;
		BigNextByte <<= 8;
	}
        Private->CrntShiftDWord |= BigNextByte << BigShift;
        Private->CrntShiftState += 8;
    }
    *Code = Private->CrntShiftDWord & pgm_read_word(&(CodeMasks[Private->RunningBits]));
    //printf("Co:%d/%d ", *Code, Private->RunningBits);
    uint8_t BigShift = Private->RunningBits;
    if (BigShift >= 8) {
        BigShift -= 8;
	Private->CrntShiftDWord >>= 8;
    }
    Private->CrntShiftDWord >>= BigShift;
    Private->CrntShiftState -= Private->RunningBits;

    /* If code cannot fit into RunningBits bits, must raise its size. Note
     * however that codes above 4095 are used for special signaling.
     * If we're using LZ_BITS bits already and we're at the max code, just
     * keep using the table as it is, don't increment Private->RunningCode.
     */
    if (Private->RunningCode < Private->MaxCodePoint + 2 &&
	++Private->RunningCode > Private->MaxCode1 &&
	Private->RunningBits < Private->MaxCodeBits) {
        Private->MaxCode1 <<= 1;
        Private->RunningBits++;
    }
    return TGIF_OK;
}



/******************************************************************************/
int TDGifGetInfo(const void *TGif, TGifInfo *Info, const uint16_t MaxW, const uint16_t MaxH,
const uint16_t MaxSz)
{
    if (!Info) return TGIF_ERROR; /* Umm, we want that info slot to give you the info... */

    /* Initial MaxSz check to see if we can even try to parse this. */
    if (MaxSz < 8) {
        /* 8: 4 bytes first header, 2 bytes one color, 1 byte code point count header, 1 byte LZW data */
        Info->Error = D_TGIF_ERR_MAXSZ;
        return TGIF_ERROR;
    }
    uint8_t ExtBits = TDGifReadByte(TGif, 0);
    Info->Width = TDGifReadByte(TGif, 1);
    Info->Width |= (ExtBits & 0xC) << 6;
    Info->Height = TDGifReadByte(TGif, 2);
    Info->Height |= (ExtBits & 0x3) << 8;
    Info->ColorCount = TDGifReadByte(TGif, 3);
    if (Info->ColorCount == 0) Info->ColorCount = 256;
    Info->SRAMLimit = (ExtBits & 0xF0) << 4;
    if (Info->SRAMLimit == 0) Info->SRAMLimit = 4096;

    if ((!Info->Width)||(!Info->Height)) {
        Info->Error = D_TGIF_ERR_ZWH;
        return TGIF_ERROR;
    }

    if ((Info->Width > MaxW)||(Info->Height > MaxH)) {
        Info->Error = D_TGIF_ERR_TOOBIG;
        return TGIF_ERROR;
    }

    Info->Colors = (const TGifColorType*)( ((const uint8_t*)TGif) + 4);

    unsigned int ColorTableSize = sizeof(TGifColorType) * Info->ColorCount;
    Info->Data = (const uint8_t*)TGif + 4 + ColorTableSize;

    /* Second MaxSz check */
    if (MaxSz < (6+ColorTableSize)) {
        Info->Error = D_TGIF_ERR_MAXSZ;
        return TGIF_ERROR;
    }

    /* Store maximum size of the data */
    Info->MaxSz = MaxSz - (ColorTableSize + 4);

    Info->Error = 0;
    return TGIF_OK;
}


static uint8_t BitSize(uint16_t n) {
	uint8_t i;
	uint16_t shv = 2;
	for (i = 1; i <= 12; i++) {
		if (shv > n) break;
		shv = shv << 1;
	}
	return i;
}

/******************************************************************************
 Routine to trace the Prefixes linked list until we get a prefix which is
 not code, but a pixel value (less than ClearCode). Returns that pixel value.
 If image is defective, we might loop here forever, so we limit the loops to
 the maximum possible if image O.k. - LZ_MAX_CODE times.
******************************************************************************/
static int
TDGifGetPrefixChar(TDGifPrivateType *Private, unsigned int Code, unsigned int ClearCode)
{
    int i = 0;

    while (Code > ClearCode && i++ <= LZ_MAX_CODE) {
        if (Code > Private->MaxCodePoint) {
            return NO_SUCH_CODE;
        }
        Code = Private->Prefix[Code - Private->DictBase];
    }
    return Code;
}




/******************************************************************************
 The LZ decompression routine:
 This version decompress the given GIF file into Line of length LineLen.
 This routine can be called few times (one per scan line, for example), in
 order the complete the whole image.
******************************************************************************/
int
TDGifDecompress(TGifInfo *Info, void(*OutputCB)(uint8_t) )
{

    TDGifPrivateType PrivateStuff;
    TDGifPrivateType *Private = &PrivateStuff;

    int CodeCount = TDGifReadByte(Info->Data, 0);
    if (CodeCount == 0) CodeCount = 256;

    Private->ReadOffset = 1;
    Private->Info = Info;
    Private->DictBase = CodeCount + 1;
    Private->DictSize = Info->SRAMLimit/4;
    if ((Private->DictSize+Private->DictBase) > (LZ_MAX_CODE+1)) {
	Private->DictSize = (LZ_MAX_CODE+1) - Private->DictBase;
    }
    Private->MaxCodePoint = Private->DictBase + (Private->DictSize-1); /* Maximum code actually used */
    Private->MaxCodeBits = BitSize(Private->MaxCodePoint);
    //Private->DictBase -= 2;
    //Private->DictSize += 2;

    //printf("CodeCount %d ", CodeCount);
    Private->ClearCode = CodeCount;
    Private->RunningCode = CodeCount + 1;
    Private->InitCodeBits = BitSize(Private->RunningCode);
    Private->RunningBits = Private->InitCodeBits;    /* Number of bits per code. */
    Private->MaxCode1 = 1 << Private->RunningBits;    /* Max. code + 1. */
    Private->CrntShiftState = 0;    /* No information in CrntShiftDWord. */
    Private->CrntShiftDWord = 0;

    uint8_t *Alloc = ALLOC(Private->DictSize * 4);
    if (!Alloc) {
	Info->Error = D_TGIF_ERR_NOT_ENOUGH_MEM;
	return TGIF_ERROR;
    }
    uint16_t *Prefix = (uint16_t*)Alloc;
    uint8_t *Suffix = Alloc + (Private->DictSize * 2);
    uint8_t *Stack = Alloc + (Private->DictSize * 3);

    Private->Prefix = Prefix;

    for (uint16_t i = 0; i < Private->DictSize; i++)
        Prefix[i] = NO_SUCH_CODE;

    uint16_t LastCode = NO_SUCH_CODE;
    uint16_t StackPtr = 0;
    uint16_t ClearCode = Private->ClearCode;
    uint16_t CrntPrefix, CrntCode;

    uint24_t i = 0;
    uint24_t PixelCount = (uint24_t)Info->Width * Info->Height;

    while (i < PixelCount) {    /* Decode all.. */
        if (TDGifDecompressInput(Private, &CrntCode) == TGIF_ERROR) {
            FREE(Alloc);
            return TGIF_ERROR;
        }

        if (CrntCode == ClearCode) {
            /* We need to start over again: */
            for (uint16_t j = 0; j < Private->DictSize; j++)
                Prefix[j] = NO_SUCH_CODE;
            Private->RunningCode = CodeCount+1;
            Private->RunningBits = Private->InitCodeBits;
            Private->MaxCode1 = 1 << Private->RunningBits;
            LastCode = NO_SUCH_CODE;
            continue;
        }

        /* Its regular code - if in pixel range simply add it to output
         * stream, otherwise trace to codes linked list until the prefix
         * is in pixel range: */
        if (CrntCode < ClearCode) {
	    //printf("S %d<%d ", CrntCode, ClearCode);
            /* This is simple - its pixel scalar, so add it to output. */
            OutputCB(CrntCode);
            i++;
        } else {
            /* Its a code to needed to be traced: trace the linked list
             * until the prefix is a pixel, while pushing the suffix
             * pixels on our stack. If we done, pop the stack in reverse
             * (thats what stack is good for!) order to output.  */
            if (Prefix[CrntCode - Private->DictBase] == NO_SUCH_CODE) {
                CrntPrefix = LastCode;

                /* Only allowed if CrntCode is exactly the running code:
                 * In that case CrntCode = XXXCode, CrntCode or the
                 * prefix code is last code and the suffix char is
                 * exactly the prefix of last code! */
                if (CrntCode == Private->RunningCode - 2) {
                    Suffix[(Private->RunningCode - 2) - Private->DictBase] =
                       Stack[StackPtr++] = TDGifGetPrefixChar(Private,
                                                             LastCode,
                                                             ClearCode);
                } else {
                    Suffix[(Private->RunningCode - 2) - Private->DictBase] =
                       Stack[StackPtr++] = TDGifGetPrefixChar(Private,
                                                             CrntCode,
                                                             ClearCode);
                }
            } else {
                CrntPrefix = CrntCode;
            }
            //printf("C%dP%dS%d", CrntCode, CrntPrefix, StackPtr);

            /* Now (if image is O.K.) we should not get a NO_SUCH_CODE
             * during the trace. As we might loop forever, in case of
             * defective image, we use StackPtr as loop counter and stop
             * before overflowing Stack[]. */
            while (StackPtr < Private->DictSize &&
                     CrntPrefix > ClearCode && CrntPrefix <= Private->MaxCodePoint) {
                Stack[StackPtr++] = Suffix[CrntPrefix - Private->DictBase];
                CrntPrefix = Prefix[CrntPrefix - Private->DictBase];
            }
            if (StackPtr >= Private->DictSize || CrntPrefix > Private->MaxCodePoint) {
		//printf("StackPtr %d CrntPrefix %d ", StackPtr, CrntPrefix);
                Info->Error = D_TGIF_ERR_IMAGE_DEFECT;
                FREE(Alloc);
                return TGIF_ERROR;
            }

            /* Output the last character (since it'd be first out the stack). */
            //Stack[StackPtr++] = CrntPrefix;
            OutputCB(CrntPrefix);
            i++;

            /* Now lets pop all the stack into output: */
            while (StackPtr != 0 && i < PixelCount) {
                OutputCB(Stack[--StackPtr]);
                i++;
            }
        }
        if (LastCode != NO_SUCH_CODE && Prefix[(Private->RunningCode - 2) - Private->DictBase] == NO_SUCH_CODE) {
            Prefix[(Private->RunningCode - 2) - Private->DictBase] = LastCode;

            if (CrntCode == Private->RunningCode - 2) {
                /* Only allowed if CrntCode is exactly the running code:
                 * In that case CrntCode = XXXCode, CrntCode or the
                 * prefix code is last code and the suffix char is
                 * exactly the prefix of last code! */
                Suffix[(Private->RunningCode - 2) - Private->DictBase] =
                   TDGifGetPrefixChar(Private, LastCode, ClearCode);
            } else {
                Suffix[(Private->RunningCode - 2) - Private->DictBase] =
                   TDGifGetPrefixChar(Private, CrntCode, ClearCode);
            }
        }
        LastCode = CrntCode;
    }

    FREE(Alloc);
    return TGIF_OK;
}




