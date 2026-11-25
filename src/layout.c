#include "layout.h"
#include "textbuf.h"
#include "markdown.h"

#define kParaBit  0x80             /* gPara: line starts a paragraph */
#define kLevelMask 0x07            /* gPara: heading level 0-6 */

static long          *gStart;     /* first char of each line */
static unsigned char *gPara;      /* kParaBit | heading level */
static long           gN, gCapLines;
static short          gCols[7];
static long           gCaret;
static Boolean gShowMarks;

static long          *gTmpStart;  /* scratch for re-wrapped lines */
static unsigned char *gTmpPara;
static long           gTmpN, gTmpCap;

static Boolean Grow(long **starts, unsigned char **paras, long *cap, long need)
{
    long newCap;
    long *ns;
    unsigned char *np;
    if (need <= *cap)
        return true;
    newCap = *cap ? *cap * 2 : 1024;
    while (newCap < need)
        newCap *= 2;
    ns = (long *)NewPtr(newCap * sizeof(long));
    np = (unsigned char *)NewPtr(newCap);
    if (!ns || !np) {
        if (ns) DisposePtr((Ptr)ns);
        if (np) DisposePtr((Ptr)np);
        return false;
    }
    if (*starts) {
        BlockMove(*starts, ns, *cap * sizeof(long));
        BlockMove(*paras, np, *cap);
        DisposePtr((Ptr)*starts);
        DisposePtr((Ptr)*paras);
    }
    *starts = ns;
    *paras = np;
    *cap = newCap;
    return true;
}

/* Returns the start of the line after the one starting at s, within the
   paragraph [ps, pe) whose styles are sty. Hidden characters take no
   column. *hard is true when the line ended with a paragraph break. */
static long WrapLine(long s, long len, long ps, long pe,
                     const unsigned char *sty, short cols, Boolean *hard)
{
    long p = s, lastBreak = -1;
    short col = 0;
    unsigned char c;

    *hard = false;
    while (p < len) {
        if (p == pe) {                   /* the paragraph's '\r' (or text end) */
            if (p < len) {
                *hard = true;
                return p + 1;
            }
            return len;
        }
        if (sty && (sty[p - ps] & MD_HIDDEN)) {
            p++;
            continue;
        }
        c = TB_Char(p);
        if (col >= cols) {
            if (c == ' ') {              /* trailing spaces hang past the edge */
                while (p < pe && TB_Char(p) == ' ')
                    p++;
                return p;
            }
            if (lastBreak > s)
                return lastBreak;        /* wrap after the last space */
            return p;                    /* one long word: hard break */
        }
        if (c == ' ')
            lastBreak = p + 1;
        col++;
        p++;
    }
    return len;
}

static Boolean TmpAdd(long start, unsigned char para)
{
    if (!Grow(&gTmpStart, &gTmpPara, &gTmpCap, gTmpN + 1))
        return false;
    gTmpStart[gTmpN] = start;
    gTmpPara[gTmpN] = para;
    gTmpN++;
    return true;
}

/* Wrap from s (a paragraph start) into the scratch arrays, stopping at
   position stop (a later paragraph start), or at the end when stop < 0. */
static Boolean Generate(long s, long stop)
{
    long len = TB_Len(), next, ps = s, pe = 0;
    const unsigned char *sty = 0;
    short level = 0;
    Boolean newPara = true, hard;

    gTmpN = 0;
    for (;;) {
        if (newPara) {                   /* parse each paragraph once */
            ps = s;
            pe = MD_ParaEnd(s);
            sty = MD_Parse(ps, pe, gShowMarks, &level);
        }
        if (!TmpAdd(s, (unsigned char)((newPara ? kParaBit : 0) | level)))
            return false;
        next = WrapLine(s, len, ps, pe, sty, gCols[level], &hard);
        if (stop >= 0 && next >= stop)   /* reached an untouched paragraph */
            break;
        if (next >= len) {
            if (hard && !TmpAdd(len, kParaBit))  /* empty last line after '\r' */
                return false;
            break;
        }
        newPara = hard;
        s = next;
    }
    return true;
}

Boolean LY_Init(const short *cols)
{
    short i;
    for (i = 0; i < 7; i++)
        gCols[i] = cols[i];
    return LY_Rebuild();
}

void LY_SetCaret(long pos) { gCaret = pos; }
void LY_SetShowMarks(Boolean on) { gShowMarks = on; }

Boolean LY_Rebuild(void)
{
    if (!Generate(0, -1))
        return false;
    if (!Grow(&gStart, &gPara, &gCapLines, gTmpN))
        return false;
    BlockMove(gTmpStart, gStart, gTmpN * sizeof(long));
    BlockMove(gTmpPara, gPara, gTmpN);
    gN = gTmpN;
    return true;
}

Boolean LY_Update(long from, long oldLen, long newLen,
                  long *first, long *oldCount, long *newCount)
{
    long delta = newLen - oldLen;
    long a, b, stop, i, newN, tail;

    /* first affected line: the paragraph start at or before `from` */
    a = LY_LineOf(from);
    while (a > 0 && !(gPara[a] & kParaBit))
        a--;
    /* first untouched paragraph after the edited range (old positions) */
    b = a + 1;
    while (b < gN && !((gPara[b] & kParaBit) && gStart[b] > from + oldLen))
        b++;
    stop = (b < gN) ? gStart[b] + delta : -1;

    if (!Generate(gStart[a], stop))
        return false;

    newN = gN - (b - a) + gTmpN;
    if (!Grow(&gStart, &gPara, &gCapLines, newN))
        return false;
    tail = gN - b;
    if (tail > 0) {
        BlockMove(gStart + b, gStart + a + gTmpN, tail * sizeof(long));
        BlockMove(gPara + b, gPara + a + gTmpN, tail);
    }
    BlockMove(gTmpStart, gStart + a, gTmpN * sizeof(long));
    BlockMove(gTmpPara, gPara + a, gTmpN);
    for (i = a + gTmpN; i < newN; i++)
        gStart[i] += delta;

    *first = a;
    *oldCount = b - a;
    *newCount = gTmpN;
    gN = newN;
    return true;
}

long  LY_Count(void) { return gN; }
short LY_Level(long line) { return gPara[line] & kLevelMask; }
Boolean LY_IsParaStart(long line) { return (gPara[line] & kParaBit) != 0; }

long LY_Start(long line)
{
    return gStart[line];
}

long LY_End(long line)
{
    long e = (line + 1 < gN) ? gStart[line + 1] : TB_Len();
    if (e > gStart[line] && TB_Char(e - 1) == '\r')
        e--;
    return e;
}

long LY_LineOf(long pos)
{
    long lo = 0, hi = gN - 1, mid;
    while (lo < hi) {                    /* last line starting at or before pos */
        mid = (lo + hi + 1) / 2;
        if (gStart[mid] <= pos)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}
