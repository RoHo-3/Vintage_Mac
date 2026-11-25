/*
 * Line layout for fixed-width fonts: word-wraps each paragraph into
 * display lines of at most cols[level] visible characters (hidden
 * Markdown markers take no space). Only the paragraph(s) touched by an
 * edit are re-wrapped; later lines just shift.
 */
#ifndef LAYOUT_H
#define LAYOUT_H

#include <Memory.h>

/* cols[level] = characters per line for body (0) and headings 1-6. */
Boolean LY_Init(const short *cols);
Boolean LY_Rebuild(void);

/* The paragraph holding this position shows its Markdown markers. */
void    LY_SetCaret(long pos);

/* Call after TB_Replace(from, oldLen, ..., newLen). Reports which lines
   changed: lines [first, first+newCount) replace the old [first,
   first+oldCount). Returns false if out of memory. */
Boolean LY_Update(long from, long oldLen, long newLen,
                  long *first, long *oldCount, long *newCount);

long  LY_Count(void);
short LY_Level(long line);     /* 0 body, 1-6 heading */
Boolean LY_IsParaStart(long line);
long  LY_Start(long line);
long  LY_End(long line);       /* display end: excludes the '\r' */
long  LY_LineOf(long pos);

#endif
