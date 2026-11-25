#include "markdown.h"
#include "textbuf.h"
#include "encoding.h"

#define kSearchLimit 2000          /* how far to look for a closing marker */

static unsigned char *gSty;        /* per-char styles */
static char          *gTxt;        /* copy of the paragraph text */
static long           gCap;

static long    gCStart = -1, gCEnd = -1;   /* cached parse */
static Boolean gCActive;
static short   gCLevel;

static long    gCaretAbs = -1;             /* where the writer is typing */

void MD_Invalidate(void) { gCStart = -1; }

void MD_SetCaret(long pos)
{
    if (pos != gCaretAbs) {
        gCaretAbs = pos;
        gCStart = -1;                    /* open markers depend on it */
    }
}

long MD_ParaStart(long pos)
{
    while (pos > 0 && TB_Char(pos - 1) != '\r')
        pos--;
    return pos;
}

long MD_ParaEnd(long pos)
{
    long len = TB_Len();
    while (pos < len && TB_Char(pos) != '\r')
        pos++;
    return pos;
}

static Boolean Reserve(long n)
{
    unsigned char *s;
    char *t;
    long cap;
    if (n <= gCap)
        return true;
    cap = gCap ? gCap : 512;
    while (cap < n)
        cap *= 2;
    s = (unsigned char *)NewPtr(cap);
    t = NewPtr(cap);
    if (!s || !t) {
        if (s) DisposePtr((Ptr)s);
        if (t) DisposePtr(t);
        return false;
    }
    if (gSty) DisposePtr((Ptr)gSty);
    if (gTxt) DisposePtr(gTxt);
    gSty = s;
    gTxt = t;
    gCap = cap;
    return true;
}

static Boolean IsAlnum(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || (unsigned char)c >= 0x80;
}

/* Find a closing run of `n` copies of c at or after i (not already a
   marker, and for n == 1 not part of a longer run). */
static long FindClose(long i, long n, char c, long len)
{
    long j, limit = i + kSearchLimit;
    if (limit > len) limit = len;
    for (j = i; j + n <= limit; j++) {
        if (gSty[j] & (MD_MARK | MD_CODE))
            continue;
        if (gTxt[j] != c)
            continue;
        if (n == 2) {
            if (gTxt[j + 1] == c && !(gSty[j + 1] & MD_MARK))
                return j;
        } else if (j + 1 >= len || gTxt[j + 1] != c) {
            if (c == '_' && j + 1 < len && IsAlnum(gTxt[j + 1]))
                continue;                /* intra-word underscore */
            return j;
        } else
            j++;                         /* skip a double run */
    }
    return -1;
}

static void Mark(long a, long n, unsigned char m)
{
    while (n-- > 0)
        gSty[a++] |= m;
}

static void ApplyStyle(long a, long b, unsigned char s)
{
    for (; a < b; a++)
        gSty[a] |= s;
}

const unsigned char *MD_Parse(long start, long end, Boolean active,
                              short *level)
{
    long len = end - start, i, j, p;
    long cpos = (gCaretAbs >= start && gCaretAbs <= end) ? gCaretAbs - start : -1;
    unsigned char mark = MD_MARK | (active ? 0 : MD_HIDDEN);
    char c;

    if (start == gCStart && end == gCEnd && active == gCActive) {
        *level = gCLevel;
        return gSty;
    }
    if (!Reserve(len + 1))
        return 0;
    TB_Copy(start, len, gTxt);
    for (i = 0; i < len; i++)
        gSty[i] = 0;

    /* ATX heading: 1-6 '#' then a space */
    *level = 0;
    for (p = 0; p < len && p < 6 && gTxt[p] == '#'; p++)
        ;
    if (p > 0 && p < len && gTxt[p] == ' ') {
        *level = (short)p;
        while (p < len && gTxt[p] == ' ')
            p++;
        Mark(0, p, MD_PREFIX | MD_HIDDEN);
    } else
        p = 0;

    for (i = 0; i < len; i++)            /* escapes: one placeholder box */
        if (gTxt[i] == kEscStart)
            for (j = i + 1; j < len && j < i + 8; j++) {
                gSty[j] |= MD_HIDDEN | MD_MARK;
                if (gTxt[j] == kEscEnd)
                    break;
            }

    for (i = p; i < len; i++) {
        if (gSty[i] & (MD_MARK | MD_CODE))
            continue;
        c = gTxt[i];
        if (c == '`') {
            j = i + 1;
            while (j < len && gTxt[j] != '`')
                j++;
            if (j < len && j > i + 1) {
                Mark(i, 1, mark);
                Mark(j, 1, mark);
                ApplyStyle(i + 1, j, MD_CODE);
                i = j;
            } else if (j >= len && cpos > i) {   /* typing after an open ` */
                Mark(i, 1, mark);
                ApplyStyle(i + 1, len, MD_CODE);
                i = len;
            }
        } else if ((c == '*' || c == '_') && i + 1 < len && gTxt[i + 1] == c) {
            j = FindClose(i + 3, 2, c, len);
            if (j > 0) {
                Mark(i, 2, mark);
                Mark(j, 2, mark);
                ApplyStyle(i + 2, j, MD_BOLD);
                i++;
            } else if (cpos == i + 2 && i + 3 < len
                       && gTxt[i + 2] == c && gTxt[i + 3] == c) {
                Mark(i, 4, mark);        /* empty pair, cursor inside: **|** */
                i += 3;
            } else if (cpos >= i + 2) {  /* typing after an open ** */
                Mark(i, 2, mark);
                ApplyStyle(i + 2, len, MD_BOLD);
                i++;
            }
        } else if (c == '*' || c == '_') {
            if (c == '_' && i > 0 && IsAlnum(gTxt[i - 1]))
                continue;                /* intra-word underscore */
            if (i + 1 < len && gTxt[i + 1] == ' ')
                continue;                /* "* " is a list bullet, not emphasis */
            j = FindClose(i + 2, 1, c, len);
            if (j > 0) {
                Mark(i, 1, mark);
                Mark(j, 1, mark);
                ApplyStyle(i + 1, j, MD_ITALIC);
            } else if (cpos > i) {       /* typing after an open * or _ */
                Mark(i, 1, mark);
                ApplyStyle(i + 1, len, MD_ITALIC);
            }
        }
    }

    gCStart = start;
    gCEnd = end;
    gCActive = active;
    gCLevel = *level;
    return gSty;
}
