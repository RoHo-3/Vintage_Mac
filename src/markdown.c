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

static unsigned char gBlock, gCBlock;      /* block info of the last parse */
static short   gForceFence = -1;           /* layout: fence state, or -1 */
static short   gCFence = -1;
static MD_FenceQuery gFenceQuery;

void MD_ForceFence(short inFence) { gForceFence = inFence; }
void MD_SetFenceQuery(MD_FenceQuery q) { gFenceQuery = q; }
unsigned char MD_Block(void) { return gBlock; }

short MD_Indent(unsigned char blk)
{
    short in = MD_QUOTE(blk) * kQuoteStep;
    short k = MD_KIND(blk);
    if (k >= MD_BULLET && k <= MD_TASKDONE)
        in += (MD_LISTDEPTH(blk) + 1) * kListStep;
    else if (k == MD_CODELINE)
        in += kCodeIndent;
    return in;
}

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

static Boolean IsPunct(char c)
{
    return (c >= '!' && c <= '/') || (c >= ':' && c <= '@')
        || (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

static Boolean IsAlnum(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || (unsigned char)c >= 0x80;
}

/* Find a closing run of `n` copies of c at or after i (not already a
   marker, and for n == 1 not part of a longer run). */
static Boolean IsBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* The marker run starting at i (n characters) can open emphasis: it is not
   followed by a space, as in standard Markdown ("** b **" stays plain).
   A run at the very end is still a possible opener while it is typed. */
static Boolean CanOpen(long i, long n, long len)
{
    return i + n >= len || !IsBlank(gTxt[i + n]);
}

/* One bit per marker kind, set when a run of it was turned down as an
   opener because a space followed ("~~ b"). */
static short SpacedBit(char c, long n)
{
    short b = (c == '*') ? 1 : (c == '_') ? 4 : 16;
    return n == 2 ? b << 1 : b;
}

/* A run at the end of the paragraph, after a space, when an earlier run of
   the same kind was turned down: the writer is closing a spaced pair
   ("~~ b ~~"), so leave it showing rather than treat it as a new opener. */
static Boolean LooksClosing(long i, long n, long len, short spaced)
{
    return i + n >= len && i > 0 && IsBlank(gTxt[i - 1])
        && (spaced & (SpacedBit(gTxt[i], 1) | SpacedBit(gTxt[i], 2))) != 0;
}

static long FindClose(long i, long n, char c, long len)
{
    long j, limit = i + kSearchLimit;
    if (limit > len) limit = len;
    for (j = i; j + n <= limit; j++) {
        if (gSty[j] & (MD_MARK | MD_CODE))
            continue;
        if (gTxt[j] != c)
            continue;
        if (IsBlank(gTxt[j - 1])) {      /* a closer can't follow a space */
            if (n == 1 && j + 1 < len && gTxt[j + 1] == c)
                j++;
            continue;
        }
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

/* Is there a run of n c's at k (exactly n for single markers) that opens
   emphasis: after a space, not before one, not hidden or code? */
static Boolean InnerOpener(long k, long n, char c, long len)
{
    if (gSty[k] & (MD_MARK | MD_CODE)) return false;
    if (gTxt[k] != c || (n == 2 && gTxt[k + 1] != c)) return false;
    if (!IsBlank(gTxt[k - 1]) || !CanOpen(k, n, len)) return false;
    if (n == 1 && k + 1 < len && gTxt[k + 1] == c) return false;
    return true;
}

/* Closer for the opener at i. As in standard Markdown a closer belongs to
   the nearest opener before it, so in "**a **b**" the last ** closes the
   second one; the first then looks for a closer further on. */
static long FindPair(long i, long n, char c, long len)
{
    long j = FindClose(i + n + 1, n, c, len), k, from = i + n + 1;
    while (j > 0) {
        for (k = from; k + n <= j; k++)  /* openers paired before 'from' */
            if (InnerOpener(k, n, c, len))     /* are already used up */
                break;
        if (k + n > j)
            return j;
        from = j + n;
        j = FindClose(j + n + 1, n, c, len);
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

/* ---- block structure: raw-text tests on whole lines --------------- */

/* Line [a, b) is a fence: up to 3 spaces, then 3+ backticks. */
static Boolean IsFence(long a, long b)
{
    long i = a, n = 0;
    while (i < b && i - a < 3 && TB_Char(i) == ' ') i++;
    while (i < b && TB_Char(i) == '`') { i++; n++; }
    return n >= 3;
}

/* Line [a, b) is a thematic break: 3+ of one of - * _, spaces allowed. */
static Boolean IsRule(long a, long b)
{
    long i, n = 0;
    char m = 0, c;
    for (i = a; i < b; i++) {
        c = TB_Char(i);
        if (c == ' ' || c == '\t') continue;
        if (c != '-' && c != '*' && c != '_') return false;
        if (m && c != m) return false;
        m = c;
        n++;
    }
    return n >= 3;
}

/* Line [a, b) is a setext underline: up to 3 spaces, then only = or only -
   (no inner spaces), then optional spaces. Returns 1 for =, 2 for -. */
static short Underline(long a, long b)
{
    long i = a;
    char m;
    while (i < b && i - a < 3 && TB_Char(i) == ' ') i++;
    if (i >= b) return 0;
    m = TB_Char(i);
    if (m != '=' && m != '-') return 0;
    while (i < b && TB_Char(i) == m) i++;
    while (i < b && TB_Char(i) == ' ') i++;
    if (i < b) return 0;
    return m == '=' ? 1 : 2;
}

/* Could the line at a be an underline: its first non-space is - or =? */
static Boolean StartsUnderline(long a)
{
    long i = a, len = TB_Len();
    char c;
    while (i < len && i - a < 3 && TB_Char(i) == ' ') i++;
    if (i >= len) return false;
    c = TB_Char(i);
    return c == '-' || c == '=';
}

Boolean MD_MaybeUnderline(long a) { return StartsUnderline(a); }

/* A setext underline of a single - or = ("-", "- "): while the writer is
   on that line it's most likely a list item being started, so it doesn't
   turn the line above into a heading until a second - (or the cursor
   leaves). */
static Boolean LoneUnderline(long a, long b)
{
    long i, n = 0;
    for (i = a; i < b; i++)
        if (TB_Char(i) != ' ') n++;
    return n == 1;
}

/* Length of a list marker at i ("- ", "12. ", "3) "), or 0. */
static long ListMarker(long i, long b, Boolean *ordered)
{
    long j = i;
    char c;
    if (i >= b) return 0;
    c = TB_Char(i);
    *ordered = false;
    if (c == '-' || c == '*' || c == '+')
        j = i + 1;
    else {
        while (j < b && j - i < 9 && TB_Char(j) >= '0' && TB_Char(j) <= '9') j++;
        if (j == i || j >= b || (TB_Char(j) != '.' && TB_Char(j) != ')')) return 0;
        j++;
        *ordered = true;
    }
    if (j >= b || TB_Char(j) != ' ') return 0;
    return j + 1 - i;
}

/* Line [a, b) is plain paragraph text that a setext underline below could
   turn into a heading: not blank, not a heading, quote, list, fence or
   rule, and not inside a code block. */
static Boolean IsParaText(long a, long b, Boolean inFence)
{
    long i = a;
    Boolean ord;
    if (inFence) return false;
    while (i < b && i - a < 3 && TB_Char(i) == ' ') i++;
    if (i >= b) return false;
    if (b - i >= 4 && TB_Char(i) == ' ') return false;   /* indented code-ish */
    {
        char c = TB_Char(i);
        if (c == '#' || c == '>') return false;
    }
    if (IsFence(a, b) || IsRule(a, b) || ListMarker(i, b, &ord)) return false;
    if (Underline(a, b)) return false;
    return true;
}

static Boolean FenceBefore(long start)
{
    if (gForceFence >= 0) return gForceFence != 0;
    return gFenceQuery ? gFenceQuery(start) : false;
}

const unsigned char *MD_Parse(long start, long end, Boolean active,
                              short *level)
{
    long len = end - start, i, j, p;
    long cpos = (gCaretAbs >= start && gCaretAbs <= end) ? gCaretAbs - start : -1;
    unsigned char mark = MD_MARK | (active ? 0 : MD_HIDDEN);
    char c;
    short spaced, quote = 0, kind = 0, ldepth = 0;
    Boolean fenceIn = FenceBefore(start), fenceOut = false, ord;
    long q, m;

    if (start == gCStart && end == gCEnd && active == gCActive
        && (short)fenceIn == gCFence) {
        *level = gCLevel;
        gBlock = gCBlock;
        return gSty;
    }
    if (!Reserve(len + 1))
        return 0;
    TB_Copy(start, len, gTxt);
    for (i = 0; i < len; i++)
        gSty[i] = 0;
    *level = 0;
    p = 0;

    if (fenceIn) {                       /* inside a ``` block: all code */
        if (IsFence(start, end)) {
            Mark(0, len, mark);
            kind = MD_FENCE;
        } else {
            ApplyStyle(0, len, MD_CODE);
            kind = MD_CODELINE;
            fenceOut = true;
        }
        goto finish;
    }

    /* block quote: "> ", up to three deep */
    for (;;) {
        q = p;
        while (q < len && q - p < 3 && gTxt[q] == ' ') q++;
        if (q < len && gTxt[q] == '>' && quote < 3) {
            q++;
            if (q < len && gTxt[q] == ' ') q++;
            p = q;
            quote++;
        } else
            break;
    }
    if (p > 0)
        Mark(0, p, MD_PREFIX | MD_HIDDEN);

    if (quote == 0 && IsFence(start, end)) {   /* opening ``` (and info) */
        Mark(0, len, mark);
        kind = MD_FENCE;
        fenceOut = true;
        goto finish;
    }
    if (quote == 0 && start > 0 && Underline(start, end)) {   /* under text? */
        Boolean pending = cpos >= 0 && LoneUnderline(start, end);   /* "- " being typed */
        if (!pending && IsParaText(MD_ParaStart(start - 1), start - 1, false)) {
            Mark(0, len, mark);
            kind = MD_FENCE;             /* a syntax-only line: nothing drawn */
            goto finish;
        }
    }
    if (IsRule(start + p, end)) {        /* --- *** ___ */
        Mark(p, len - p, mark);
        kind = MD_RULE;
        goto finish;
    }

    /* list item: indent, marker, optional task box */
    q = p;
    while (q < len && gTxt[q] == ' ') q++;
    m = ListMarker(start + q, end, &ord);
    if (m > 0) {
        ldepth = (short)((q - p) / 2);
        if (ldepth > 3) ldepth = 3;
        kind = ord ? MD_ORDERED : MD_BULLET;
        q += m;
        if (!ord && q + 3 <= len && gTxt[q] == '['
            && (gTxt[q + 1] == ' ' || gTxt[q + 1] == 'x' || gTxt[q + 1] == 'X')
            && gTxt[q + 2] == ']' && (q + 3 == len || gTxt[q + 3] == ' ')) {
            kind = gTxt[q + 1] == ' ' ? MD_TASK : MD_TASKDONE;
            q += (q + 3 < len) ? 4 : 3;
        }
        Mark(p, q - p, MD_PREFIX | MD_HIDDEN);
        p = q;
    }

    /* ATX heading: 1-6 '#' then a space */
    q = p;
    for (; p < len && p - q < 6 && gTxt[p] == '#'; p++)
        ;
    if (p > q && p < len && gTxt[p] == ' ') {
        *level = (short)(p - q);
        while (p < len && gTxt[p] == ' ')
            p++;
        Mark(0, p, MD_PREFIX | MD_HIDDEN);
        {   /* optional closing run, "## Title ##": only after a space (or
               alone); "Title##" stays text, as in every Markdown app */
            long e = len, q, k;
            while (e > p && gTxt[e - 1] == ' ') e--;
            for (q = e; q > p && gTxt[q - 1] == '#'; q--)
                ;
            if (q < e && (q == p || gTxt[q - 1] == ' ')) {
                /* only after a space, as in CommonMark: "Title##" stays text */
                for (k = q; k > p && gTxt[k - 1] == ' '; k--)
                    ;
                Mark(k, len - k, MD_MARK | (active ? 0 : MD_HIDDEN));
            }
        }
    } else
        p = q;

    /* setext heading: plain text with === or --- on the next line */
    if (*level == 0 && kind == 0 && quote == 0 && end < TB_Len()
        && StartsUnderline(end + 1)) {   /* cheap test before scanning the line */
        long ne = MD_ParaEnd(end + 1);
        short u = Underline(end + 1, ne);
        Boolean pending = gCaretAbs >= end + 1 && gCaretAbs <= ne && LoneUnderline(end + 1, ne);
        if (u && !pending && IsParaText(start, end, false))
            *level = u;
    }

    /* hard line break: a backslash at the end of a line */
    if (len > 0 && gTxt[len - 1] == '\\' && end < TB_Len()
        && (len < 2 || gTxt[len - 2] != '\\'))
        Mark(len - 1, 1, mark);

    for (i = 0; i < len; i++)            /* escapes: one placeholder box */
        if (gTxt[i] == kEscStart)
            for (j = i + 1; j < len && j < i + 8; j++) {
                gSty[j] |= MD_HIDDEN | MD_MARK;
                if (gTxt[j] == kEscEnd)
                    break;
            }

    spaced = 0;
    for (i = p; i < len; i++) {
        if (gSty[i] & (MD_MARK | MD_CODE))
            continue;
        c = gTxt[i];
        if (c == '\\' && i + 1 < len && IsPunct(gTxt[i + 1])) {
            Mark(i, 1, mark);            /* \* is a literal star */
            i++;
            continue;
        }
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
            if (!CanOpen(i, 2, len)) { spaced |= SpacedBit(c, 2); i++; continue; }
            if (LooksClosing(i, 2, len, spaced)) { i++; continue; }
            j = FindPair(i, 2, c, len);
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
        } else if (c == '~' && i + 1 < len && gTxt[i + 1] == '~') {
            if (!CanOpen(i, 2, len)) { spaced |= SpacedBit(c, 2); i++; continue; }
            if (LooksClosing(i, 2, len, spaced)) { i++; continue; }
            j = FindPair(i, 2, c, len);          /* ~~strike~~ */
            if (j > 0) {
                Mark(i, 2, mark);
                Mark(j, 2, mark);
                ApplyStyle(i + 2, j, MD_STRIKE);
                i += 1;
            } else if (cpos == i + 2 && i + 3 < len
                       && gTxt[i + 2] == c && gTxt[i + 3] == c) {
                Mark(i, 4, mark);                     /* ~~|~~ */
                i += 3;
            } else if (cpos >= i + 2) {  /* open ~~ */
                Mark(i, 2, mark);
                ApplyStyle(i + 2, len, MD_STRIKE);
                i += 1;
            }
        } else if (c == '*' || c == '_') {
            if (c == '_' && i > 0 && IsAlnum(gTxt[i - 1]))
                continue;                /* intra-word underscore */
            if (!CanOpen(i, 1, len)) {
                spaced |= SpacedBit(c, 1);
                continue;                /* "* " is a bullet or plain text */
            }
            j = FindPair(i, 1, c, len);
            if (j > 0) {
                Mark(i, 1, mark);
                Mark(j, 1, mark);
                ApplyStyle(i + 1, j, MD_ITALIC);
            } else if (cpos > i && !LooksClosing(i, 1, len, spaced)) {  /* open * or _ */
                Mark(i, 1, mark);
                ApplyStyle(i + 1, len, MD_ITALIC);
            }
        }
    }

finish:
    gBlock = (unsigned char)(kind | (quote << 3) | (ldepth << 5) | (fenceOut ? 0x80 : 0));
    gCBlock = gBlock;
    gCFence = fenceIn;
    gCStart = start;
    gCEnd = end;
    gCActive = active;
    gCLevel = *level;
    return gSty;
}
