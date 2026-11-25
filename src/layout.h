/*
 * Line layout for a fixed-width font: word-wraps the text into display
 * lines of at most `cols` characters. Only the paragraph(s) touched by an
 * edit are re-wrapped; later lines just shift.
 */
#ifndef LAYOUT_H
#define LAYOUT_H

#include <Memory.h>

Boolean LY_Init(short cols);
Boolean LY_Rebuild(void);

/* Call after TB_Replace(from, oldLen, ..., newLen). Reports which lines
   changed: lines [first, first+newCount) replace the old [first,
   first+oldCount). Returns false if out of memory. */
Boolean LY_Update(long from, long oldLen, long newLen,
                  long *first, long *oldCount, long *newCount);

short LY_Cols(void);
long  LY_Count(void);
long  LY_Start(long line);
long  LY_End(long line);       /* display end: excludes the '\r' */
long  LY_LineOf(long pos);

#endif
