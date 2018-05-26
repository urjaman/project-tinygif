/******************************************************************************
tegif_lib.c - Tiny "GIF" encoding
*****************************************************************************/

#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "tegif_lib.h"

/* Hash table details */
#define HT_SIZE			8192	   /* 12bits = 4096 or twice as big! */
#define HT_KEY_MASK		0x1FFF			      /* 13bits keys */
#define HT_KEY_NUM_BITS		13			      /* 13bits keys */
#define HT_MAX_KEY		8191	/* 13bits - 1, maximal code possible */
#define HT_MAX_CODE		4095	/* Biggest code possible in 12 bits. */

/* The 32 bits of the long are divided into two parts for the key & code:   */
/* 1. The code is 12 bits as our compression algorithm is limited to 12bits */
/* 2. The key is 12 bits Prefix code + 8 bit new char or 20 bits.	    */
/* The key is the upper 20 bits.  The code is the lower 12. */
#define HT_GET_KEY(l)	(l >> 12)
#define HT_GET_CODE(l)	(l & 0x0FFF)
#define HT_PUT_KEY(l)	(l << 12)
#define HT_PUT_CODE(l)	(l & 0x0FFF)

typedef struct TGifHashTableType {
    uint32_t HTable[HT_SIZE];
} TGifHashTableType;

static TGifHashTableType *_InitHashTable(void);
static void _ClearHashTable(TGifHashTableType *HashTable);
static void _InsertHashTable(TGifHashTableType *HashTable, uint32_t Key, int Code);
static int _ExistsHashTable(TGifHashTableType *HashTable, uint32_t Key);

/* Other private stuff */

#include "tgif_lib_private.h"

/* Private details for the encoder */
typedef struct TGifFilePrivateType {
    TGifWord FileState,  /* Where all this data goes to! */
      ColorCount,
      InitCodeBits,     /* Codes uses at least this. */
      ClearCode,   /* The CLEAR LZ code. */
      RunningCode, /* The next code algorithm can generate. */
      RunningBits, /* The number of bits required to represent RunningCode. */
      MaxCode1,    /* 1 bigger than max. possible code, in RunningBits bits. */
      MaxCodePoint, /* Maximum code actually used ever, for decoder SRAM limiting. */
      CrntCode,    /* Current algorithm code. */
      CrntShiftState;    /* Number of bits in CrntShiftDWord. */
    unsigned long CrntShiftDWord;   /* For bytes decomposition into codes. */
    unsigned long PixelCount;   /* Number of pixels in image. */
    FILE *File;    /* File as stream. */
    TGifByteType Buf[256];   /* Compressed input is buffered here. */
    TGifHashTableType *HashTable;
} TGifFilePrivateType;


/* Hash table impl. */
static int KeyItem(uint32_t Item);

/******************************************************************************
 Initialize HashTable - allocate the memory needed and clear it.	      *
******************************************************************************/
static TGifHashTableType *_InitHashTable(void)
{
    TGifHashTableType *HashTable;

    if ((HashTable = (TGifHashTableType *) malloc(sizeof(TGifHashTableType)))
	== NULL)
	return NULL;

    _ClearHashTable(HashTable);

    return HashTable;
}

/******************************************************************************
 Routine to clear the HashTable to an empty state.			      *
 This part is a little machine depended. Use the commented part otherwise.   *
******************************************************************************/
static void _ClearHashTable(TGifHashTableType *HashTable)
{
    memset(HashTable -> HTable, 0xFF, HT_SIZE * sizeof(uint32_t));
}

/******************************************************************************
 Routine to insert a new Item into the HashTable. The data is assumed to be  *
 new one.								      *
******************************************************************************/
static void _InsertHashTable(TGifHashTableType *HashTable, uint32_t Key, int Code)
{
    int HKey = KeyItem(Key);
    uint32_t *HTable = HashTable -> HTable;

    while (HT_GET_KEY(HTable[HKey]) != 0xFFFFFL) {
	HKey = (HKey + 1) & HT_KEY_MASK;
    }
    HTable[HKey] = HT_PUT_KEY(Key) | HT_PUT_CODE(Code);
}

/******************************************************************************
 Routine to test if given Key exists in HashTable and if so returns its code *
 Returns the Code if key was found, -1 if not.				      *
******************************************************************************/
static int _ExistsHashTable(TGifHashTableType *HashTable, uint32_t Key)
{
    int HKey = KeyItem(Key);
    uint32_t *HTable = HashTable -> HTable, HTKey;

    while ((HTKey = HT_GET_KEY(HTable[HKey])) != 0xFFFFFL) {
	if (Key == HTKey) return HT_GET_CODE(HTable[HKey]);
	HKey = (HKey + 1) & HT_KEY_MASK;
    }

    return -1;
}

/******************************************************************************
 Routine to generate an HKey for the hashtable out of the given unique key.  *
 The given Key is assumed to be 20 bits as follows: lower 8 bits are the     *
 new postfix character, while the upper 12 bits are the prefix code.	      *
 Because the average hit ratio is only 2 (2 hash references per entry),      *
 evaluating more complex keys (such as twin prime keys) does not worth it!   *
******************************************************************************/
static int KeyItem(uint32_t Item)
{
    return ((Item >> 12) ^ Item) & HT_KEY_MASK;
}

static int TEGifSetupCompress(TGifFileType * GifFile, uint16_t sram_limit);
static int TEGifCompressLine(TGifFileType * GifFile, TGifPixelType * Line,
                            int LineLen);
static int TEGifCompressOutput(TGifFileType * GifFile, int Code);
static int TEGifBufferedOutput(TGifFileType * GifFile, TGifByteType * Buf,
                              int c);


/******************************************************************************
 Open a new GIF file for write, specified by name.
 Returns a dynamically allocated TGifFileType pointer which serves as the GIF
 info record.
******************************************************************************/
TGifFileType *
TEGifOpenFileName(const char *FileName, int *Error)
{

    TGifFileType *GifFile;
    FILE *f = fopen(FileName, "wb");

    if (!f) {
        if (Error != NULL)
	    *Error = E_TGIF_ERR_OPEN_FAILED;
        return NULL;
    }

    GifFile = (TGifFileType *) malloc(sizeof(TGifFileType));
    if (GifFile == NULL) {
        return NULL;
    }

    memset(GifFile, '\0', sizeof(TGifFileType));

    TGifFilePrivateType* Private = (TGifFilePrivateType *)malloc(sizeof(TGifFilePrivateType));
    if (Private == NULL) {
        free(GifFile);
        if (Error != NULL)
	    *Error = E_TGIF_ERR_NOT_ENOUGH_MEM;
        return NULL;
    }
    /*@i1@*/memset(Private, '\0', sizeof(TGifFilePrivateType));
    if ((Private->HashTable = _InitHashTable()) == NULL) {
        free(GifFile);
        free(Private);
        if (Error != NULL)
	    *Error = E_TGIF_ERR_NOT_ENOUGH_MEM;
        return NULL;
    }

    GifFile->Private = (void *)Private;
    Private->File = f;
    Private->FileState = FILE_STATE_WRITE;

    GifFile->Error = 0;

    return GifFile;
}


/******************************************************************************
 All writes to the GIF should go through this.
******************************************************************************/
static int InternalWrite(TGifFileType *GifFileOut,
		   const void *buf, size_t len)
{
    TGifFilePrivateType *Private = (TGifFilePrivateType*)GifFileOut->Private;
    int r = fwrite(buf, 1, len, Private->File);
    fflush(Private->File);
    return r;
}

/* return smallest bitfield size n will fit in */
static int BitSize(int n) {
	int i;
	for (i = 1; i <= 12; i++)
		if ((1 << i) > n) break;
	return i;
}

/******************************************************************************
 This routine should be called before any other TEGif calls, immediately
 following the GIF file opening.
******************************************************************************/
int
TEGifPutScreenDesc(TGifFileType *GifFile,
                  const uint16_t Width,
                  const uint16_t Height,
                  const TColorMapObject *ColorMap, uint16_t SRAMLimit)
{
    TGifByteType Buf[4];
    TGifFilePrivateType *Private = (TGifFilePrivateType *) GifFile->Private;

    if (Private->FileState & FILE_STATE_SCREEN) {
        /* If already has screen descriptor - something is wrong! */
        GifFile->Error = E_TGIF_ERR_HAS_SCRN_DSCR;
        return TGIF_ERROR;
    }

    if (!ColorMap)
	return TGIF_ERROR;

    /* Main header: Compress SRAM limit, dimensions and Color Count */
    SRAMLimit &= ~0xFF;
    if (!SRAMLimit)
	return TGIF_ERROR;

    Buf[0] = ((SRAMLimit >> 4) & 0xF0) | ((Width >> 6) & 0x0C) | ((Height >> 8) & 0x03);
    Buf[1] = Width;
    Buf[2] = Height;
    Buf[3] = ColorMap->ColorCount;

    /* We need this later */
    Private->ColorCount = ColorMap->ColorCount;

    InternalWrite(GifFile, Buf, 4);
    /* And the color map */
    InternalWrite(GifFile, ColorMap->Colors, sizeof(TGifColorType)*ColorMap->ColorCount);

    Private->PixelCount = (int)Width * (int)Height;
    /* Reset compress algorithm parameters. */
    (void)TEGifSetupCompress(GifFile, SRAMLimit);

    /* Mark this file as has screen descriptor, and no pixel written yet: */
    Private->FileState |= FILE_STATE_SCREEN;

    return TGIF_OK;
}

/******************************************************************************
 Put one full scanned line (Line) of length LineLen into GIF file.
******************************************************************************/
int
TEGifPutLine(TGifFileType * GifFile, TGifPixelType *Line, int LineLen)
{
    TGifFilePrivateType *Private = (TGifFilePrivateType *) GifFile->Private;

    if (Private->PixelCount < (unsigned)LineLen) {
        GifFile->Error = E_TGIF_ERR_DATA_TOO_BIG;
        return TGIF_ERROR;
    }
    Private->PixelCount -= LineLen;

    return TEGifCompressLine(GifFile, Line, LineLen);
}

/******************************************************************************
 This routine should be called last, to close the GIF file.
******************************************************************************/
int
TEGifCloseFile(TGifFileType *GifFile, int *ErrorCode)
{
    TGifFilePrivateType *Private;
    FILE *File;

    if (GifFile == NULL)
        return TGIF_ERROR;

    Private = (TGifFilePrivateType *) GifFile->Private;
    if (Private == NULL)
	return TGIF_ERROR;

    File = Private->File;

    if (Private) {
        if (Private->HashTable) {
            free((char *) Private->HashTable);
        }
	free((char *) Private);
    }

    if (File && fclose(File) != 0) {
	if (ErrorCode != NULL)
	    *ErrorCode = E_TGIF_ERR_CLOSE_FAILED;
	free(GifFile);
        return TGIF_ERROR;
    }

    free(GifFile);
    if (ErrorCode != NULL)
	*ErrorCode = E_TGIF_SUCCEEDED;
    return TGIF_OK;
}


/******************************************************************************
 Setup the LZ compression for this image:
******************************************************************************/
static int
TEGifSetupCompress(TGifFileType *GifFile, uint16_t SRAMLimit)
{
    TGifByteType Buf;
    TGifFilePrivateType *Private = (TGifFilePrivateType *) GifFile->Private;

    Buf = Private->ColorCount;
    InternalWrite(GifFile, &Buf, 1);    /* Write the Code size to file. */

    /* Decoder needs 4 bytes per actual dictionary entry, so compute the maximum emitted code. */
    Private->MaxCodePoint = Private->ColorCount + 1 + (SRAMLimit/4); /* Maximum code actually used */
    if (Private->MaxCodePoint > LZ_MAX_CODE) Private->MaxCodePoint = LZ_MAX_CODE;

    Private->Buf[0] = 0;    /* Nothing was output yet. */
    Private->ClearCode = Private->ColorCount;
    Private->RunningCode = Private->ClearCode + 1;
    Private->RunningBits = BitSize(Private->RunningCode);    /* Number of bits per code. */
    Private->InitCodeBits = Private->RunningBits;
    Private->MaxCode1 = 1 << Private->RunningBits;    /* Max. code + 1. */
    Private->CrntCode = FIRST_CODE;    /* Signal that this is first one! */
    Private->CrntShiftState = 0;    /* No information in CrntShiftDWord. */
    Private->CrntShiftDWord = 0;

   /* Clear hash table */
    _ClearHashTable(Private->HashTable);

    return TGIF_OK;
}

/******************************************************************************
 The LZ compression routine:
 This version compresses the given buffer Line of length LineLen.
 This routine can be called a few times (one per scan line, for example), in
 order to complete the whole image.
******************************************************************************/
static int
TEGifCompressLine(TGifFileType *GifFile,
                 TGifPixelType *Line,
                 const int LineLen)
{
    int i = 0, CrntCode, NewCode;
    unsigned long NewKey;
    TGifPixelType Pixel;
    TGifHashTableType *HashTable;
    TGifFilePrivateType *Private = (TGifFilePrivateType *) GifFile->Private;

    HashTable = Private->HashTable;

    if (Private->CrntCode == FIRST_CODE)    /* Its first time! */
        CrntCode = Line[i++];
    else
        CrntCode = Private->CrntCode;    /* Get last code in compression. */

    while (i < LineLen) {   /* Decode LineLen items. */
        Pixel = Line[i++];  /* Get next pixel from stream. */
        /* Form a new unique key to search hash table for the code combines
         * CrntCode as Prefix string with Pixel as postfix char.
         */
        NewKey = (((uint32_t) CrntCode) << 8) + Pixel;
        if ((NewCode = _ExistsHashTable(HashTable, NewKey)) >= 0) {
            /* This Key is already there, or the string is old one, so
             * simple take new code as our CrntCode:
             */
            CrntCode = NewCode;
        } else {
            /* Put it in hash table, output the prefix code, and make our
             * CrntCode equal to Pixel.
             */
            if (TEGifCompressOutput(GifFile, CrntCode) == TGIF_ERROR) {
                GifFile->Error = E_TGIF_ERR_DISK_IS_FULL;
                return TGIF_ERROR;
            }
            CrntCode = Pixel;

            /* If however the HashTable if full, we send a clear first and
             * Clear the hash table.
             */
            if (Private->RunningCode >= Private->MaxCodePoint) {
                GifFile->MaxCodeUsed = Private->MaxCodePoint;
                /* Time to do some clearance: */
                if (TEGifCompressOutput(GifFile, Private->ClearCode)
                        == TGIF_ERROR) {
                    GifFile->Error = E_TGIF_ERR_DISK_IS_FULL;
                    return TGIF_ERROR;
                }
                Private->RunningCode = Private->ClearCode + 1;
                Private->RunningBits = Private->InitCodeBits;
                Private->MaxCode1 = 1 << Private->RunningBits;
                _ClearHashTable(HashTable);
            } else {
                /* Put this unique key with its relative Code in hash table: */
                _InsertHashTable(HashTable, NewKey, Private->RunningCode++);
            }
        }

    }

    /* Preserve the current state of the compression algorithm: */
    Private->CrntCode = CrntCode;

    if (Private->PixelCount == 0) {
        if (GifFile->MaxCodeUsed < (Private->RunningCode-1)) GifFile->MaxCodeUsed = Private->RunningCode-1;

        /* We are done - output last Code and flush output buffers: */
        if (TEGifCompressOutput(GifFile, CrntCode) == TGIF_ERROR) {
            GifFile->Error = E_TGIF_ERR_DISK_IS_FULL;
            return TGIF_ERROR;
        }
        if (TEGifCompressOutput(GifFile, FLUSH_OUTPUT) == TGIF_ERROR) {
            GifFile->Error = E_TGIF_ERR_DISK_IS_FULL;
            return TGIF_ERROR;
        }
    }

    return TGIF_OK;
}

/******************************************************************************
 The LZ compression output routine:
 This routine is responsible for the compression of the bit stream into
 8 bits (bytes) packets.
 Returns TGIF_OK if written successfully.
******************************************************************************/
static int
TEGifCompressOutput(TGifFileType *GifFile,
                   const int Code)
{
    TGifFilePrivateType *Private = (TGifFilePrivateType *) GifFile->Private;
    int retval = TGIF_OK;

    if (Code == FLUSH_OUTPUT) {
        while (Private->CrntShiftState > 0) {
            /* Get Rid of what is left in DWord, and flush it. */
            if (TEGifBufferedOutput(GifFile, Private->Buf,
                                 Private->CrntShiftDWord & 0xff) == TGIF_ERROR)
                retval = TGIF_ERROR;
            Private->CrntShiftDWord >>= 8;
            Private->CrntShiftState -= 8;
        }
        Private->CrntShiftState = 0;    /* For next time. */
        if (TEGifBufferedOutput(GifFile, Private->Buf,
                               FLUSH_OUTPUT) == TGIF_ERROR)
            retval = TGIF_ERROR;
    } else {
	printf("Co:%d/%d ", Code, Private->RunningBits);
        Private->CrntShiftDWord |= ((long)Code) << Private->CrntShiftState;
        Private->CrntShiftState += Private->RunningBits;
        while (Private->CrntShiftState >= 8) {
            /* Dump out full bytes: */
            if (TEGifBufferedOutput(GifFile, Private->Buf,
                                 Private->CrntShiftDWord & 0xff) == TGIF_ERROR)
                retval = TGIF_ERROR;
            Private->CrntShiftDWord >>= 8;
            Private->CrntShiftState -= 8;
        }
    }

    /* If code cannt fit into RunningBits bits, must raise its size. Note */
    /* however that codes above 4095 are used for special signaling.      */
    if (Private->RunningCode >= Private->MaxCode1 && Code <= LZ_MAX_CODE) {
       Private->MaxCode1 = 1 << ++Private->RunningBits;
    }

    return retval;
}

/******************************************************************************
 This routines buffers the given characters until 255 characters are ready
 to be output.
 Returns TGIF_OK if written successfully.
******************************************************************************/
static int
TEGifBufferedOutput(TGifFileType *GifFile,
                   TGifByteType *Buf,
                   int c)
{
    if (c == FLUSH_OUTPUT) {
        /* Flush everything out. */
        if (Buf[0] != 0
            && InternalWrite(GifFile, Buf+1, Buf[0]) != Buf[0]) {
            GifFile->Error = E_TGIF_ERR_WRITE_FAILED;
            return TGIF_ERROR;
        }
    } else {
        if (Buf[0] == 255) {
            /* Dump out this buffer - it is full: */
            if (InternalWrite(GifFile, Buf+1, Buf[0]) != Buf[0]) {
                GifFile->Error = E_TGIF_ERR_WRITE_FAILED;
                return TGIF_ERROR;
            }
            Buf[0] = 0;
        }
        Buf[++Buf[0]] = c;
    }

    return TGIF_OK;
}

