#include "textbuf.h"
#include <Memory.h>

static char *gBuf;
static long  gCap, gGapStart, gGapEnd;   /* gap is [gGapStart, gGapEnd) */

Boolean TB_Init(long capacity)
{
    gBuf = NewPtr(capacity);
    if (!gBuf)
        return false;
    gCap = capacity;
    gGapStart = 0;
    gGapEnd = capacity;
    return true;
}

long TB_Len(void)      { return gCap - (gGapEnd - gGapStart); }
long TB_Capacity(void) { return gCap; }

unsigned char TB_Char(long pos)
{
    return (unsigned char)(pos < gGapStart ? gBuf[pos]
                                           : gBuf[pos + (gGapEnd - gGapStart)]);
}

static void MoveGap(long pos)
{
    if (pos < gGapStart) {
        long n = gGapStart - pos;
        BlockMove(gBuf + pos, gBuf + gGapEnd - n, n);
        gGapStart -= n;
        gGapEnd -= n;
    } else if (pos > gGapStart) {
        long n = pos - gGapStart;
        BlockMove(gBuf + gGapEnd, gBuf + gGapStart, n);
        gGapStart += n;
        gGapEnd += n;
    }
}

void TB_Runs(long pos, long len,
             const char **p1, long *n1, const char **p2, long *n2)
{
    long gap = gGapEnd - gGapStart;
    if (pos + len <= gGapStart) {
        *p1 = gBuf + pos; *n1 = len; *p2 = 0; *n2 = 0;
    } else if (pos >= gGapStart) {
        *p1 = gBuf + pos + gap; *n1 = len; *p2 = 0; *n2 = 0;
    } else {
        *p1 = gBuf + pos;     *n1 = gGapStart - pos;
        *p2 = gBuf + gGapEnd; *n2 = len - *n1;
    }
}

void TB_Copy(long pos, long len, char *dst)
{
    const char *p1, *p2;
    long n1, n2;
    TB_Runs(pos, len, &p1, &n1, &p2, &n2);
    if (n1) BlockMove(p1, dst, n1);
    if (n2) BlockMove(p2, dst + n1, n2);
}

Boolean TB_Replace(long pos, long oldLen, const char *src, long newLen)
{
    if (newLen - oldLen > gGapEnd - gGapStart)
        return false;
    MoveGap(pos);
    gGapEnd += oldLen;                   /* delete: widen the gap */
    if (newLen) {
        BlockMove(src, gBuf + gGapStart, newLen);
        gGapStart += newLen;
    }
    return true;
}
