all: convert testdec

convert: convert.c tegif_lib.c tgif_lib.h tgif_lib_private.h
	gcc -O2 -Wall -W -o convert convert.c tegif_lib.c -lgif


testdec: testdec.c tdgif_lib.c tdgif_lib.h
	gcc -O2 -Wall -W -o testdec testdec.c tdgif_lib.c
