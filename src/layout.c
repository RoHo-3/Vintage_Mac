#include "layout.h"
#include "textbuf.h"
#include "markdown.h"

#define kParaBit  0x80             /* gPara: line starts a paragraph */
#define kLevelMask 0x07            /* gPara: heading level 0-6 */

static long          *gStart;     /* first char of each line */
static unsigned char *gPara;      /* kParaBit | heading level */
static unsigned char *gBlk;       /* MD_Block() of the line's paragraph */
static long           gN, gCapLines;
static short          gWidth;             /* the text column, in pixels */
static LY_Measure     gMeasure;           /* pixel width of one character */
static long           gCaret;
static Boolean gShowMarks;

static long          *gTmpStart;  /* scratch for re-wrapped lines */
static unsigned char *gTmpPara, *gTmpBlk;
static long           gTmpN, gTmpCap;

static Boolean Grow(long **starts, unsigned char **paras, unsigned char **blks,
                    long *cap, long need)
{
    long newCap;
    long *ns;
    unsigned char *np, *nb;
    if (need <= *cap)
        return true;
    newCap = *cap ? *cap * 2 : 1024;
    while (newCap < need)
        newCap *= 2;
    ns = (long *)NewPtr(newCap * sizeof(long));
    np = (unsigned char *)NewPtr(newCap);
    nb = (unsigned char *)NewPtr(newCap);
    if (!ns || !np || !nb) {
        if (ns) DisposePtr((Ptr)ns);
        if (np) DisposePtr((Ptr)np);
        if (nb) DisposePtr((Ptr)nb);
        return false;
    }
    if (*starts) {
        BlockMove(*starts, ns, *cap * sizeof(long));
        BlockMove(*paras, np, *cap);
        BlockMove(*blks, nb, *cap);
        DisposePtr((Ptr)*starts);
        DisposePtr((Ptr)*paras);
        DisposePtr((Ptr)*blks);
    }
    *starts = ns;
    *paras = np;
    *blks = nb;
    *cap = newCap;
    return true;
}

/* Returns the start of the line after the one starting at s, within the
   paragraph [ps, pe) whose styles are sty. Hidden characters take no
   column. *hard is true when the line ended with a paragraph break. */
static long WrapLine(long s, long len, long ps, long pe,
                     const unsigned char *sty, short level, short width,
                     Boolean *hard)
{
    long p = s, lastBreak = -1;
    short px = 0, w;
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
        w = gMeasure(level, sty ? sty[p - ps] : 0, c);
        if (px + w > width && px > 0) {
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
        px += w;
        p++;
    }
    return len;
}

static Boolean TmpAdd(long start, unsigned char para, unsigned char blk)
{
    if (!Grow(&gTmpStart, &gTmpPara, &gTmpBlk, &gTmpCap, gTmpN + 1))
        return false;
    gTmpStart[gTmpN] = start;
    gTmpPara[gTmpN] = para;
    gTmpBlk[gTmpN] = blk;
    gTmpN++;
    return true;
}

/* Wrap from s (a paragraph start) into the scratch arrays, stopping at
   position stop (a later paragraph start), or at the end when stop < 0.
   fence: a ``` block is open before s; *fenceOut: open after the last
   paragraph wrapped. */
static Boolean Generate(long s, long stop, Boolean fence, Boolean *fenceOut)
{
    long len = TB_Len(), next, ps = s, pe = 0;
    const unsigned char *sty = 0;
    short level = 0, width = gWidth;
    unsigned char blk = 0;
    Boolean newPara = true, hard, ok = true;

    gTmpN = 0;
    for (;;) {
        if (newPara) {                   /* parse each paragraph once */
            ps = s;
            pe = MD_ParaEnd(s);
            MD_ForceFence(fence);
            sty = MD_Parse(ps, pe, gShowMarks, &level);
            blk = MD_Block();
            fence = MD_FENCEOPEN(blk);
            width = gWidth - MD_Indent(blk);
        }
        if (!TmpAdd(s, (unsigned char)((newPara ? kParaBit : 0) | level), blk)) {
            ok = false;
            break;
        }
        next = WrapLine(s, len, ps, pe, sty, level, width, &hard);
        if (stop >= 0 && next >= stop)   /* reached an untouched paragraph */
            break;
        if (next >= len) {
            if (hard) {                  /* empty last line after '\r' */
                MD_ForceFence(fence);
                MD_Parse(len, len, gShowMarks, &level);
                if (!TmpAdd(len, kParaBit, MD_Block()))
                    ok = false;
            }
            break;
        }
        newPara = hard;
        s = next;
    }
    MD_ForceFence(-1);
    *fenceOut = fence;
    return ok;
}

/* Is a ``` block open before this paragraph start? */
static Boolean FenceBefore(long start)
{
    long line;
    if (start <= 0 || gN == 0) return false;
    line = LY_LineOf(start);
    if (line == 0) return false;
    return MD_FENCEOPEN(gBlk[line - 1]);
}

Boolean LY_Init(short width, LY_Measure measure)
{
    gWidth = width;
    gMeasure = measure;
    MD_SetFenceQuery(FenceBefore);
    return LY_Rebuild();
}

void LY_SetCaret(long pos) { gCaret = pos; MD_SetCaret(pos); }
void LY_SetShowMarks(Boolean on) { gShowMarks = on; }

Boolean LY_Rebuild(void)
{
    Boolean f;
    if (!Generate(0, -1, false, &f))
        return false;
    if (!Grow(&gStart, &gPara, &gBlk, &gCapLines, gTmpN))
        return false;
    BlockMove(gTmpStart, gStart, gTmpN * sizeof(long));
    BlockMove(gTmpPara, gPara, gTmpN);
    BlockMove(gTmpBlk, gBlk, gTmpN);
    gN = gTmpN;
    return true;
}

Boolean LY_Update(long from, long oldLen, long newLen,
                  long *first, long *oldCount, long *newCount)
{
    long delta = newLen - oldLen;
    long a, b, stop, i, newN, tail;
    Boolean fenceIn, fenceOut;

    /* first affected line: the paragraph start at or before `from`, and
       one paragraph more (a setext underline styles the line above it) */
    a = LY_LineOf(from);
    while (a > 0 && !(gPara[a] & kParaBit))
        a--;
    if (a > 0) {
        a--;
        while (a > 0 && !(gPara[a] & kParaBit))
            a--;
    }
    /* first untouched paragraph after the edited range (old positions),
       skipping one more (its underline test looks at the line above) */
    b = a + 1;
    while (b < gN && !((gPara[b] & kParaBit) && gStart[b] > from + oldLen))
        b++;
    if (b < gN) {
        b++;
        while (b < gN && !(gPara[b] & kParaBit))
            b++;
    }
    stop = (b < gN) ? gStart[b] + delta : -1;

    fenceIn = a > 0 && MD_FENCEOPEN(gBlk[a - 1]);
    if (!Generate(gStart[a], stop, fenceIn, &fenceOut))
        return false;
    if (stop >= 0 && fenceOut != MD_FENCEOPEN(gBlk[b - 1])) {
        /* a ``` opened or closed: everything after changes */
        if (!Generate(gStart[a], -1, fenceIn, &fenceOut))
            return false;
        b = gN;
    }

    newN = gN - (b - a) + gTmpN;
    if (!Grow(&gStart, &gPara, &gBlk, &gCapLines, newN))
        return false;
    tail = gN - b;
    if (tail > 0) {
        BlockMove(gStart + b, gStart + a + gTmpN, tail * sizeof(long));
        BlockMove(gPara + b, gPara + a + gTmpN, tail);
        BlockMove(gBlk + b, gBlk + a + gTmpN, tail);
    }
    BlockMove(gTmpStart, gStart + a, gTmpN * sizeof(long));
    BlockMove(gTmpPara, gPara + a, gTmpN);
    BlockMove(gTmpBlk, gBlk + a, gTmpN);
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
unsigned char LY_Block(long line) { return gBlk[line]; }
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
