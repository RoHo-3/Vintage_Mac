/*
 * Text encoding for files: UTF-8 on disk, MacRoman in memory, with an
 * exact round trip ("never damage a file you didn't write").
 *
 * - Line endings: CRLF, LF and CR are read; paragraphs are '\r' in memory;
 *   the file's original style is written back.
 * - A UTF-8 byte-order mark is remembered and written back.
 * - A character MacRoman can't show (emoji, CJK, …), a control character,
 *   or an invalid byte is kept as an escape in memory:
 *       0x1B  hex digits of the code point  0x1C
 *   drawn as one placeholder box. Invalid bytes use 0xDC00 + byte (0xDC80-0xDCFF) (as in
 *   Python's surrogateescape) and are written back as the same raw byte.
 */
#ifndef ENCODING_H
#define ENCODING_H

#include <Memory.h>

#define kEscStart 0x1B
#define kEscEnd   0x1C

enum { kEndCR = 0, kEndLF = 1, kEndCRLF = 2 };

typedef struct {
    short   lineEnding;
    Boolean bom;
} FileFormat;

/* Decode n bytes of UTF-8 into dst (MacRoman + escapes). Returns the
   decoded length, or -1 if it doesn't fit in dstCap. */
long Enc_Decode(const unsigned char *src, long n, char *dst, long dstCap,
                FileFormat *fmt);

/* Encode the whole text buffer as UTF-8 into dst. Returns the byte count,
   or -1 if it doesn't fit. Enc_MaxEncoded gives a safe dst size. */
long Enc_Encode(char *dst, long dstCap, const FileFormat *fmt);
long Enc_MaxEncoded(void);
/* A piece of text (n bytes, the app's form) as UTF-8 with LF endings;
   at most 4 bytes per byte in. -1 if it doesn't fit. */
long Enc_EncodeText(const char *src, long n, char *dst, long cap);

/* Escapes behave as one character: these return the escape's bounds if
   pos is inside or at one, else pos unchanged. */
long Enc_EscapeStart(long pos);   /* pos inside/after-start → start */
long Enc_EscapeEnd(long pos);     /* pos inside → just past kEscEnd */

#endif
