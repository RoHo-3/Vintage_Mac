/*
 * Gap buffer: the document text in one block of memory with a movable
 * gap at the edit point, so typing is O(1) and only edits far from the
 * last one move memory. Positions are logical (0 .. TB_Len()).
 * Paragraph breaks are stored as '\r' (Mac native; matches the Return key
 * and the clipboard). Line endings are converted at load/save time.
 */
#ifndef TEXTBUF_H
#define TEXTBUF_H

#include <Memory.h>

Boolean TB_Init(long capacity);
long    TB_Len(void);
long    TB_Capacity(void);
unsigned char TB_Char(long pos);

/* Copy len chars starting at pos into dst. */
void    TB_Copy(long pos, long len, char *dst);

/* Replace [pos, pos+oldLen) with newLen bytes from src (src may be NULL
   when newLen is 0). Returns false if the buffer is full. */
Boolean TB_Replace(long pos, long oldLen, const char *src, long newLen);

/* The text of [pos, pos+len) as at most two contiguous runs. */
void    TB_Runs(long pos, long len,
                const char **p1, long *n1, const char **p2, long *n2);

#endif
