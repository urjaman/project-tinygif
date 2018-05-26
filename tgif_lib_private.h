#pragma once
/****************************************************************************
tgif_lib_private.h - internal details
****************************************************************************/

#define LZ_MAX_CODE         1023    /* Biggest code possible in 10 bits. */
#define LZ_BITS             10

#define FLUSH_OUTPUT        (LZ_MAX_CODE+1)    /* Impossible code, to signal flush. */
#define FIRST_CODE          (LZ_MAX_CODE+2)    /* Impossible code, to signal first. */
#define NO_SUCH_CODE        (LZ_MAX_CODE+3)    /* Impossible code, to signal empty. */

#define FILE_STATE_WRITE    0x01
#define FILE_STATE_SCREEN   0x02
