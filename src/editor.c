#include "editor.h"
#include "textbuf.h"
#include "layout.h"
#include "markdown.h"
#include "encoding.h"
#include "chrome.h"
#include "journal.h"

#include <Fonts.h>
#include <Memory.h>
#include <ToolUtils.h>
#include <OSUtils.h>

/* ---- geometry (see docs/DESIGN.md) ------------------------------------ */

#define kFontMDW      200        /* MDW Mono; +1 bold, +2 italic, +3 both */
#define kFontMonaco   4          /* fallback if our font is missing */
#define kLeft         46         /* left margin */
#define kColumnW      420        /* text column width */
#define kTopPad       24
#define kBottomPad    14
#define kBlinkTicks   30
#define kDoubleTicks  30
#define kBufferSize   (384L * 1024)
#define kMaxUndo      64
#define kRunMax       256

/* body (0) and heading levels 1-6: size, then metrics measured at init */
/* One font and body size for the whole app; headings scale from the body
   and snap to sizes the font really has (crisp bitmaps, or TrueType). */
static short gSize[7] = { 10, 20, 16, 14, 12, 12, 10 };
static short gFontID;                    /* 0: MDW Mono (our families) */
#define kSize gSize
static short gAsc[7], gLH[7], gCW[7];
static Boolean gHave[7];                 /* that size measured yet? */
static short gOffH;                      /* the offscreen strip's height */
static void Need(short level);
#define Asc(l) (Need(l), gAsc[l])
#define LH(l)  (Need(l), gLH[l])
#define CWm(l) (Need(l), gCW[l])
static short CW(short level, unsigned char sty, unsigned char c);

static WindowPtr gWin;
static Boolean   gShowMarks;            /* per document: Markdown markers visible */
static long  gTop;                       /* first visible line */
static long  gAnchor, gCaret;            /* selection = [min, max) */
static short gGoalX = -1;                /* pixel column kept on up/down */
static Boolean gCaretOn, gActive = true;
static unsigned long gCaretTick, gLastClick;
static long  gLastClickPos = -1;
static Boolean gTyping;                  /* coalesce typing into one undo */
static Boolean gFallbackFont;
static long    gWeight;                 /* autosave change tracking */
static unsigned long gFirstChange, gLastEdit;
static char  gRun[kRunMax];

/* Offscreen strip one line tall: each line is drawn here, then copied to
   the screen in one CopyBits, so lines never flash blank while typing. */
static GrafPort gOffPort;
static BitMap   gOffBits;
static Boolean  gOffOK;

#define SelStart() (gAnchor < gCaret ? gAnchor : gCaret)
#define SelEnd()   (gAnchor < gCaret ? gCaret : gAnchor)
#define HasSel()   (gAnchor != gCaret)

static short Bottom(void) { return gWin->portRect.bottom - kBottomPad; }
static void ShowCaret(void);

/* ---- undo -------------------------------------------------------------- */

typedef struct {
    char  kind;          /* 'i' inserted text, 'd' deleted text */
    short group;         /* records of one user action share a group */
    long  pos, len, cap;
    char *data;
} UndoRec;

static UndoRec gUndo[kMaxUndo], gRedo[kMaxUndo];
static short gUndoN, gRedoN, gGroup;

static void FreeRec(UndoRec *r)
{
    if (r->data) DisposePtr(r->data);
    r->data = 0;
}

static void ClearRedo(void)
{
    while (gRedoN > 0)
        FreeRec(&gRedo[--gRedoN]);
}

static void PushRec(UndoRec *stack, short *n, UndoRec *r)
{
    if (*n == kMaxUndo) {                /* drop the oldest */
        FreeRec(&stack[0]);
        BlockMove(&stack[1], &stack[0], (kMaxUndo - 1) * sizeof(UndoRec));
        (*n)--;
    }
    stack[(*n)++] = *r;
}

static Boolean MakeRec(UndoRec *r, char kind, long pos, long len, const char *src)
{
    r->kind = kind;
    r->group = gGroup;
    r->pos = pos;
    r->len = len;
    r->cap = len < 32 ? 32 : len;
    r->data = NewPtr(r->cap);
    if (!r->data)
        return false;
    if (src)
        BlockMove(src, r->data, len);
    else
        TB_Copy(pos, len, r->data);
    return true;
}

/* ---- lines and their Markdown styles ----------------------------------- */

typedef struct {
    long  s, e;          /* line text [s, e) */
    long  ps, pe;        /* its paragraph */
    long  prefixEnd;     /* end of a heading's "## " (== ps if none) */
    short level;
    unsigned char blk;   /* MD_Block(): quote, list, rule, code */
    short x0, right;     /* text column for this paragraph, pixels */
    Boolean active;      /* paragraph holds the cursor: markers shown */
    const unsigned char *sty;   /* styles, indexed from ps */
} LineInfo;

static short LineH(long line)
{
    return LH(line < LY_Count() ? LY_Level(line) : 0);
}

static void GetLine(long line, LineInfo *li)
{
    long pl = line;
    short lvl;
    li->s = LY_Start(line);
    li->e = LY_End(line);
    while (pl > 0 && !LY_IsParaStart(pl))
        pl--;
    li->ps = LY_Start(pl);
    li->pe = MD_ParaEnd(li->s);
    li->active = gShowMarks;
    li->sty = MD_Parse(li->ps, li->pe, li->active, &lvl);
    li->level = lvl;
    li->blk = MD_Block();
    li->x0 = kLeft + MD_Indent(li->blk);
    li->right = kLeft + kColumnW;
    li->prefixEnd = li->ps;
    if (li->sty)
        while (li->prefixEnd < li->pe && (li->sty[li->prefixEnd - li->ps] & MD_PREFIX))
            li->prefixEnd++;
}

static unsigned char StyleAt(LineInfo *li, long p)
{
    return li->sty ? li->sty[p - li->ps] : 0;
}

/* Pixel x of position p on this line (prefix positions hang left). */
static short XOf(LineInfo *li, long p)
{
    short x = 0, w;
    long q;
    unsigned char st;
    if (p < li->prefixEnd) {             /* in the hanging "## " */
        for (q = p; q < li->prefixEnd; q++)
            x += CW(li->level, 0, TB_Char(q));
        return li->x0 - x;
    }
    for (q = li->s; q < p && q < li->e; q++) {
        st = StyleAt(li, q);
        if (st & MD_HIDDEN) continue;
        w = CW(li->level, st, TB_Char(q));
        if (li->x0 + x + w > li->right) break;
        x += w;
    }
    return li->x0 + x;
}

/* Text position nearest pixel x on this line. */
static long PosAt(LineInfo *li, short x)
{
    short at = li->x0, w;
    long p;
    unsigned char st;
    if (x < li->x0) {
        if (li->active && li->prefixEnd > li->ps && li->s == li->ps) {
            for (p = li->prefixEnd; p > li->ps; p--) {   /* the hanging "##" */
                w = CW(li->level, 0, TB_Char(p - 1));
                if (x >= at - w / 2) return p;
                at -= w;
            }
            return li->ps;
        }
        x = li->x0;
    }
    for (p = li->s; p < li->e; p++) {
        st = StyleAt(li, p);
        if (st & MD_HIDDEN)
            continue;
        w = CW(li->level, st, TB_Char(p));
        if (x < at + w / 2)
            return p;
        at += w;
    }
    return li->e;
}

/* ---- drawing ----------------------------------------------------------- */

static short LineTop(long line)
{
    short y = kTopPad;
    long i;
    for (i = gTop; i < line; i++)
        y += LineH(i);
    return y;
}

static Boolean gCodeRun;                  /* SetFont: inline code (Monaco) */
static Boolean gBackRun;                  /* a run of Backspaces is going on */
static Boolean gMarkRun;                  /* SetFont: Markdown markers (no underline) */

static void SetFont(short level, Boolean isBold, Boolean isItalic)
{
    short f;
    Style face = 0;
    if (level > 0) isBold = true;
    if (gCodeRun && gFontID != 0 && gFontID != kFontMonaco) {
        f = kFontMonaco;                 /* code reads as code in any font */
        if (isBold) face |= bold;
    } else if (gFontID != 0 || gFallbackFont) {   /* a system font */
        f = gFontID != 0 ? gFontID : kFontMonaco;
        if (isBold) face |= bold;
        if (isItalic) face |= italic;
    } else
        f = kFontMDW + (isBold ? 1 : 0) + (isItalic ? 2 : 0);
    if (level == 4 && !gMarkRun)
        face |= underline;               /* H4: the words, not their markers */
    TextFont(f);
    TextSize(kSize[level]);
    TextFace(face);
}

/* ---- measuring: every character's real width (fonts may be proportional) */

static unsigned char *gWTab[7][5];       /* [level][plain,b,i,bi,code] -> 256 widths */

static void ForgetWidths(void)
{
    short l, f;
    for (l = 0; l < 7; l++)
        for (f = 0; f < 5; f++)
            if (gWTab[l][f]) { DisposePtr((Ptr)gWTab[l][f]); gWTab[l][f] = NULL; }
}

static short FaceIndex(unsigned char sty)
{
    if (sty & MD_CODE) return 4;
    return ((sty & MD_BOLD) ? 1 : 0) + ((sty & MD_ITALIC) ? 2 : 0);
}

static unsigned char *Widths(short level, short fi)
{
    unsigned char *t = gWTab[level][fi];
    GrafPtr port;
    short i;
    if (t) return t;
    t = (unsigned char *)NewPtr(256);
    if (!t) return NULL;
    GetPort(&port);
    SetPort(gWin);
    gCodeRun = fi == 4;
    SetFont(level, fi == 1 || fi == 3, fi == 2 || fi == 3);
    gCodeRun = false;
    {   /* a fixed-width font (MDW Mono, Monaco, Courier): one width for all,
           instead of 256 CharWidth calls */
        short w = CharWidth('m');
        if (CharWidth('i') == w && CharWidth('W') == w && CharWidth('.') == w
            && CharWidth(' ') == w) {
            for (i = 0; i < 256; i++) t[i] = (unsigned char)(w > 255 ? 255 : w);
        } else
            for (i = 0; i < 256; i++) {
                short cw = CharWidth((short)i);
                t[i] = (unsigned char)(cw > 255 ? 255 : cw);
            }
    }
    SetPort(port);
    gWTab[level][fi] = t;
    return t;
}

/* The width the screen gives character c (H6 draws capitals). */
static short CW(short level, unsigned char sty, unsigned char c)
{
    unsigned char *t;
    if (level == 6 && c >= 'a' && c <= 'z') c -= 'a' - 'A';
    t = Widths(level, FaceIndex(sty));
    return t ? t[c] : CWm(level);
}

static void DrawRun(short level, unsigned char style, short x, short base, short n)
{
    if (n <= 0) return;
    gCodeRun = (style & MD_CODE) != 0;
    gMarkRun = (style & MD_MARK) != 0;
    SetFont(level, (style & MD_BOLD) != 0, (style & MD_ITALIC) != 0);
    gCodeRun = gMarkRun = false;
    MoveTo(x, base);
    DrawText(gRun, 0, n);
    if (style & MD_STRIKE) {             /* QuickDraw has no strike style: draw it */
        Point e;
        short y = base - (Asc(level) * 3) / 10;
        GetPen(&e);
        MoveTo(x, y);
        LineTo(e.h - 1, y);
    }
}

/* Block decorations, drawn when the markers are hidden: quote bars on
   every line, the list bullet, number or task box on an item's first line,
   a rule, a fence's dotted line. */
static void DrawBlock(LineInfo *li, short y, short h, short base)
{
    unsigned char b = li->blk;
    short k = MD_KIND(b), d, x, cy;
    Rect r;
    Boolean first = li->s == li->ps;

    for (d = 0; d < MD_QUOTE(b); d++) {
        x = kLeft + d * kQuoteStep;
        SetRect(&r, x, y, x + 2, y + h);
        PaintRect(&r);
    }
    x = li->x0;
    cy = base - (Asc(li->level) * 35) / 100;      /* middle of a lowercase letter */
    if (first && (k == MD_BULLET)) {
        SetRect(&r, x - 11, cy - 2, x - 6, cy + 3);
        if (MD_LISTDEPTH(b) & 1) FrameOval(&r); else PaintOval(&r);
    } else if (first && k == MD_ORDERED) {
        long q = li->ps, e;
        short n = 0, wsum = 0;
        while (q < li->prefixEnd && (TB_Char(q) == ' ' || TB_Char(q) == '>')) q++;
        for (e = q; e < li->prefixEnd && TB_Char(e) != ' ' && n < 12; e++)
            gRun[n++] = TB_Char(e);
        for (d = 0; d < n; d++)
            wsum += CW(li->level, 0, (unsigned char)gRun[d]);
        DrawRun(li->level, 0, x - wsum - 4, base, n);
    } else if (first && (k == MD_TASK || k == MD_TASKDONE)) {
        short sz = Asc(li->level) < 9 ? 9 : Asc(li->level);
        if (sz > 13) sz = 13;
        SetRect(&r, x - sz - 5, base - sz + 1, x - 5, base + 1);
        FrameRect(&r);
        if (k == MD_TASKDONE) {          /* a check mark */
            MoveTo(r.left + 2, r.top + sz / 2);
            LineTo(r.left + sz / 2 - 1, r.bottom - 3);
            LineTo(r.right - 3, r.top + 2);
        }
    } else if (k == MD_RULE) {
        MoveTo(kLeft, y + h / 2);
        LineTo(kLeft + kColumnW - 1, y + h / 2);
    } else if (k == MD_FENCE && li->ps < li->pe) {
        long q = li->ps;
        while (q < li->pe && TB_Char(q) == ' ') q++;
        if (q < li->pe && TB_Char(q) == '`')   /* a code fence, not an underline */
            for (x = kLeft + kCodeIndent; x < kLeft + kColumnW; x += 4) {
                MoveTo(x, y + h / 2);
                LineTo(x, y + h / 2);
            }
    }
}

static void DrawLine(long line, short y)
{
    Rect r, dst;
    LineInfo li;
    short h = LineH(line), base, x, runX, n = 0, w;
    short screenY = y;
    long p, a, b, next;
    unsigned char st, runSt = 0;
    char c;
    Boolean off = gOffOK && h <= gOffH;  /* a line too tall draws directly */

    if (off) {                           /* compose offscreen at y = 0 */
        SetPort(&gOffPort);
        y = 0;
    }
    SetRect(&r, 0, y, gWin->portRect.right - 2, y + h);
    EraseRect(&r);
    if (line >= LY_Count())
        goto done;

    GetLine(line, &li);
    base = y + Asc(li.level);

    /* the heading prefix hangs in the margin while the cursor is here */
    if (li.active && li.s == li.ps && li.prefixEnd > li.ps) {
        n = (short)(li.prefixEnd - li.ps);
        if (n > kRunMax) n = kRunMax;
        TB_Copy(li.ps, n, gRun);
        DrawRun(li.level, MD_MARK, XOf(&li, li.ps), base, n);
        n = 0;
    }

    if (!li.active)
        DrawBlock(&li, y, h, base);

    x = runX = li.x0;
    for (p = li.s; p < li.e; p++) {
        st = StyleAt(&li, p);
        if (st & MD_HIDDEN)
            continue;
        st &= (MD_BOLD | MD_ITALIC | MD_CODE | MD_MARK | MD_STRIKE);
        w = CW(li.level, st, TB_Char(p));
        if (x + w > li.right)
            break;                       /* trailing spaces hang past the edge */
        if (n > 0 && (st != runSt || n == kRunMax)) {
            DrawRun(li.level, runSt, runX, base, n);
            runX = x;
            n = 0;
        }
        c = (char)TB_Char(p);
        if (li.level == 6 && c >= 'a' && c <= 'z')
            c -= 'a' - 'A';              /* H6: small caps stand-in */
        gRun[n++] = c;
        runSt = st;
        x += w;
    }
    DrawRun(li.level, runSt, runX, base, n);

    if (HasSel()) {                      /* reverse the selected part */
        next = (line + 1 < LY_Count()) ? LY_Start(line + 1) : TB_Len() + 1;
        a = SelStart() > li.s ? SelStart() : li.s;
        b = SelEnd() < next ? SelEnd() : next;
        if (a < b) {
            short x1 = XOf(&li, a);
            short x2 = (b >= next && line + 1 < LY_Count())
                       ? li.right                           /* past line end */
                       : XOf(&li, b);
            if (x2 > x1) {
                SetRect(&r, x1, y, x2, y + h);
                InvertRect(&r);
            }
        }
    }
done:
    if (off) {
        SetPort(gWin);
        SetRect(&r, 0, 0, gWin->portRect.right - 2, h);
        SetRect(&dst, 0, screenY, gWin->portRect.right - 2, screenY + h);
        CopyBits(&gOffPort.portBits, &gWin->portBits, &r, &dst, srcCopy, NULL);
    }
}

/* Draw lines [from, to) that are on screen. */
static Boolean gHold;                    /* the Library is up: draw nothing */

void Ed_Hold(Boolean hold) { gHold = hold; if (hold) gCaretOn = false; }

static void DrawLines(long from, long to)
{
    short y, bottom = Bottom();
    Rect r;
    if (gHold) return;
    if (from < gTop) from = gTop;
    y = LineTop(from);
    for (; from < to && y < bottom; from++) {
        DrawLine(from, y);
        y += LineH(from);
    }
    if (from >= LY_Count() && y < bottom) {  /* clear below the last line */
        SetRect(&r, 0, y, gWin->portRect.right - 2, gWin->portRect.bottom - kFooterH);
        EraseRect(&r);
    }
}

#define kToEnd 0x7FFFFFFFL

void Ed_DrawAll(void)
{
    Rect r;
    gCaretOn = false;
    if (gHold) return;
    SetPort(gWin);
    if (!gOffOK) {                       /* no offscreen strip: blank, then draw */
        SetRect(&r, 0, kStripH, gWin->portRect.right, gWin->portRect.bottom - kFooterH);
        EraseRect(&r);
    } else {
        /* each line replaces itself in one CopyBits, full width but the last
           2 pixels: erase only what no line covers, so the page never goes
           blank first (opening and closing the drawer, the Library) */
        SetRect(&r, 0, kStripH, gWin->portRect.right, LineTop(gTop));
        EraseRect(&r);
        SetRect(&r, gWin->portRect.right - 2, kStripH, gWin->portRect.right,
                gWin->portRect.bottom - kFooterH);
        EraseRect(&r);
        SetRect(&r, 0, Bottom(), gWin->portRect.right, gWin->portRect.bottom - kFooterH);
        EraseRect(&r);
    }
    DrawLines(gTop, kToEnd);             /* (clears below the last line too) */
    ShowCaret();
}

/* last line that fits entirely on screen */
static long LastVisible(void)
{
    short y = kTopPad, bottom = Bottom();
    long i = gTop;
    while (i < LY_Count() - 1 && y + LineH(i) + LineH(i + 1) <= bottom) {
        y += LineH(i);
        i++;
    }
    return i;
}

static void CaretRect(Rect *r)
{
    LineInfo li;
    long line = LY_LineOf(gCaret);
    short x, y = LineTop(line);
    GetLine(line, &li);
    x = XOf(&li, gCaret);
    SetRect(r, x - 1, y, x, y + LineH(line));
}

static Boolean CaretVisible(void)
{
    long line = LY_LineOf(gCaret);
    return line >= gTop && line <= LastVisible();
}

static void ToggleCaret(void)
{
    Rect r;
    if (gHold || !CaretVisible()) { gCaretOn = false; return; }
    CaretRect(&r);
    InvertRect(&r);
    gCaretOn = !gCaretOn;
}

static void HideCaret(void)
{
    if (gCaretOn)
        ToggleCaret();
}

void Ed_HideCaret(void) { HideCaret(); }

static void ShowCaret(void)
{
    if (!gCaretOn && !HasSel() && gActive)
        ToggleCaret();
    gCaretTick = TickCount();
}

/* Scroll so the caret line is on screen; returns true if it redrew. */
static Boolean ScrollToCaret(void)
{
    long line = LY_LineOf(gCaret), old = gTop;
    if (line < gTop)
        gTop = line;
    else
        while (line > LastVisible() && gTop < line)
            gTop++;
    if (gTop != old) {
        Ed_DrawAll();
        return true;
    }
    return false;
}

/* Re-wrap the paragraph holding pos (an open marker may have started or
   ended there). Unchanged lines redraw in place, off screen: no shift. */
static void Rewrap(long pos)
{
    long first, oldCount, newCount;
    if (!LY_Update(pos, 0, 0, &first, &oldCount, &newCount))
        return;
    if (oldCount == newCount)
        DrawLines(first, first + newCount);
    else
        DrawLines(first, kToEnd);
}

/* The cursor moved from oldPos: if it changed paragraph, the old one hides
   its Markdown markers and the new one shows them. */
static void Activate(long oldPos)
{
    LY_SetCaret(gCaret);
    if (MD_ParaStart(oldPos) == MD_ParaStart(gCaret)) {
        /* same paragraph: an unclosed marker the cursor crossed starts or
           stops styling what follows it */
        long a = oldPos < gCaret ? oldPos : gCaret, b = oldPos ^ gCaret ^ a, q;
        char c;
        for (q = a; q < b && q < TB_Len(); q++) {
            c = TB_Char(q);
            if (c == '*' || c == '_' || c == '`' || c == '~') {
                Rewrap(gCaret);
                break;
            }
        }
        return;
    }
    Rewrap(oldPos);                      /* an unclosed marker there ends */
    Rewrap(gCaret);
}

/* A Markdown marker the writer can't see (markers hidden): the cursor
   steps over it and delete never removes it by accident. Escapes are
   excluded; they move and delete as one character already. */
static Boolean Skippable(long pos)
{
    long ps, pe;
    short lvl;
    const unsigned char *sty;
    if (gShowMarks || pos < 0 || pos >= TB_Len()) return false;
    if (Enc_EscapeStart(pos) != pos) return false;
    ps = MD_ParaStart(pos);
    pe = MD_ParaEnd(pos);
    if (pos >= pe) return false;
    sty = MD_Parse(ps, pe, false, &lvl);
    return sty && (sty[pos - ps] & MD_HIDDEN);
}

/* End of the hidden heading prefix ("### ") that starts at p, or p. */
static long PrefixEnd(long p)
{
    long ps, pe, q;
    short lvl;
    const unsigned char *sty;
    if (gShowMarks || p >= TB_Len() || MD_ParaStart(p) != p) return p;
    ps = p;
    pe = MD_ParaEnd(p);
    sty = MD_Parse(ps, pe, false, &lvl);
    if (!sty) return p;
    for (q = p; q < pe && (sty[q - ps] & MD_PREFIX); q++)
        ;
    return q;
}

static long SkipLeft(long p)  { while (p > 0 && Skippable(p - 1)) p--; return p; }
static long SkipRight(long p) { while (p < TB_Len() && Skippable(p)) p++; return p; }

/* ---- selection --------------------------------------------------------- */

static void SetSel(long anchor, long caret)
{
    long oldS = SelStart(), oldE = SelEnd(), oldCaret = gCaret, lo, hi;
    long len = TB_Len();
    if (anchor < 0) anchor = 0;
    if (caret < 0) caret = 0;
    if (anchor > len) anchor = len;
    if (caret > len) caret = len;

    HideCaret();
    gAnchor = anchor;
    gCaret = caret;
    Activate(oldCaret);
    lo = oldS < SelStart() ? oldS : SelStart();
    hi = oldE > SelEnd() ? oldE : SelEnd();
    if (!ScrollToCaret())
        DrawLines(LY_LineOf(lo), LY_LineOf(hi) + 1);
    ShowCaret();
}

/* ---- editing ----------------------------------------------------------- */

/* The one primitive every change goes through. */
static Boolean Replace(long from, long oldLen, const char *src, long newLen,
                       long newCaret)
{
    long first, oldCount, newCount, oldCaret = gCaret;
    short oldLevel = LY_Count() > 0 ? LY_Level(LY_LineOf(from)) : 0;

    HideCaret();
    if (oldLen > 0 || newLen > 0)        /* journal sees the change first */
        JR_Op(from, oldLen, src, newLen);
    if (!TB_Replace(from, oldLen, src, newLen)) {
        JR_Reset();                      /* keep the journal consistent */
        SysBeep(1);                      /* document full */
        return false;
    }
    MD_Invalidate();
    /* where the old cursor sits in the new text */
    if (oldCaret >= from + oldLen) oldCaret += newLen - oldLen;
    else if (oldCaret > from) oldCaret = from;

    gAnchor = gCaret = newCaret;
    LY_SetCaret(gCaret);
    if (!LY_Update(from, oldLen, newLen, &first, &oldCount, &newCount)) {
        SysBeep(1);
        LY_Rebuild();
        first = 0;
        oldCount = 0;
        newCount = 1;
    }
    if (!ScrollToCaret()) {
        if (oldCount == newCount && LY_Level(LY_LineOf(from)) == oldLevel)
            DrawLines(first, first + newCount);
        else
            DrawLines(first, kToEnd);    /* lines below moved */
    }
    if (MD_ParaStart(oldCaret) != MD_ParaStart(gCaret))
        Rewrap(oldCaret);                /* its open marker, if any, ends */
    ShowCaret();
    return true;
}

static void Touch(long weight)
{
    Chrome_TextChanged();
    if (gWeight == 0)
        gFirstChange = TickCount();
    gWeight += weight;
    gLastEdit = TickCount();
}

long          Ed_ChangeWeight(void)    { return gWeight; }
unsigned long Ed_FirstChangeTick(void) { return gFirstChange; }
unsigned long Ed_LastEditTick(void)    { return gLastEdit; }
void          Ed_ClearChanges(void)    { gWeight = 0; }
long          Ed_Caret(void)           { return gCaret; }

/* A user edit: records undo, then replaces. */
static void Edit(long from, long oldLen, const char *src, long newLen,
                 Boolean typing)
{
    UndoRec r;
    Boolean coalesce = typing && gTyping && oldLen == 0 && gUndoN > 0
        && gUndo[gUndoN - 1].kind == 'i'
        && gUndo[gUndoN - 1].pos + gUndo[gUndoN - 1].len == from;

    if (coalesce) {
        UndoRec *t = &gUndo[gUndoN - 1];
        if (t->len + newLen > t->cap) {
            long cap = t->cap * 2 + newLen;
            char *d = NewPtr(cap);
            if (d) {
                BlockMove(t->data, d, t->len);
                DisposePtr(t->data);
                t->data = d;
                t->cap = cap;
            } else
                coalesce = false;
        }
        if (coalesce) {
            BlockMove(src, t->data + t->len, newLen);
            t->len += newLen;
        }
    }
    if (!coalesce) {
        gGroup++;
        if (oldLen > 0 && MakeRec(&r, 'd', from, oldLen, 0))
            PushRec(gUndo, &gUndoN, &r);
        if (newLen > 0 && MakeRec(&r, 'i', from, newLen, src))
            PushRec(gUndo, &gUndoN, &r);
    }
    ClearRedo();
    gTyping = typing;
    gGoalX = -1;
    Touch(typing ? newLen : (oldLen + newLen < 50 ? 50 : oldLen + newLen));
    Replace(from, oldLen, src, newLen, from + newLen);
}

static void DeleteRange(long a, long b)
{
    a = Enc_EscapeStart(a);              /* escapes delete as one character */
    b = Enc_EscapeEnd(b);
    if (b > a)
        Edit(a, b - a, 0, 0, false);
}

/* After a delete: an empty pair left around the cursor (**|**, __|__,
   *|*, _|_, `|`) goes too, so the next word isn't styled by markers the
   writer can't see. Markers shown (Cmd-M): left alone, they're visible. */
static void CollapseEmptyPair(void)
{
    long c = gCaret, len = TB_Len();
    short k, i;
    char m;
    if (gShowMarks || HasSel() || c == 0) return;
    /* an opener left with nothing after it ("**|" at a line or word start,
       its text deleted) goes too */
    m = TB_Char(c - 1);
    if ((m == '*' || m == '_' || m == '`' || m == '~') && (c >= len || TB_Char(c) == '\r' || TB_Char(c) == ' ')) {
        long a = c;
        while (a > 0 && TB_Char(a - 1) == m && c - a < 3) a--;
        if (a == MD_ParaStart(c) || TB_Char(a - 1) == ' ') {
            DeleteRange(a, c);
            return;
        }
    }
    if (c >= len) return;
    m = TB_Char(c - 1);
    if (m != '*' && m != '_' && m != '`' && m != '~') return;
    for (k = (m == '`') ? 1 : 2; k >= (m == '~' ? 2 : 1); k--) {
        Boolean ok = c - k >= 0 && c + k <= len;
        for (i = 1; ok && i <= k; i++)
            ok = TB_Char(c - i) == m && TB_Char(c + i - 1) == m;
        if (ok && k == 1)                /* exactly one each side */
            ok = (c - 2 < 0 || TB_Char(c - 2) != m) && (c + 1 >= len || TB_Char(c + 1) != m);
        if (ok) {
            DeleteRange(c - k, c + k);
            return;
        }
    }
}

/* A hidden opener left alone at the cursor ("**|" at a line start or after
   a space, nothing after it) that the writer didn't just type: they can't
   see it, so it goes before typing there, rather than silently styling
   what comes next and showing up as "**" once the cursor leaves. */
static void DropLoneOpener(void)
{
    long c = gCaret, len = TB_Len(), a = c;
    char m;
    if (gShowMarks || HasSel() || c == 0) return;
    if (c < len && TB_Char(c) != '\r') return;      /* text after it */
    m = TB_Char(c - 1);
    if (m != '*' && m != '_' && m != '`' && m != '~') return;
    while (a > 0 && TB_Char(a - 1) == m && c - a < 3) a--;
    if (!(a == MD_ParaStart(c) || TB_Char(a - 1) == ' ')) return;
    if (!Skippable(a)) return;                       /* shown: it's plain text */
    if (gTyping && gUndoN > 0 && gUndo[gUndoN - 1].kind == 'i'
        && gUndo[gUndoN - 1].pos <= a
        && gUndo[gUndoN - 1].pos + gUndo[gUndoN - 1].len == c)
        return;                                      /* just typed: on purpose */
    DeleteRange(a, c);
}

static void InsertText(const char *s, long n, Boolean typing)
{
    Edit(SelStart(), SelEnd() - SelStart(), s, n, typing && !HasSel());
    if (typing)
        gTyping = true;                  /* later keystrokes join this undo */
}

void Ed_Undo(void)
{
    short g;
    UndoRec r;
    if (gUndoN == 0) { SysBeep(1); return; }
    g = gUndo[gUndoN - 1].group;
    while (gUndoN > 0 && gUndo[gUndoN - 1].group == g) {
        r = gUndo[--gUndoN];
        if (r.kind == 'i')
            Replace(r.pos, r.len, 0, 0, r.pos);
        else
            Replace(r.pos, 0, r.data, r.len, r.pos + r.len);
        PushRec(gRedo, &gRedoN, &r);
    }
    gTyping = false;
    Touch(50);
}

void Ed_Redo(void)
{
    short g;
    UndoRec r;
    if (gRedoN == 0) { SysBeep(1); return; }
    g = gRedo[gRedoN - 1].group;
    while (gRedoN > 0 && gRedo[gRedoN - 1].group == g) {
        r = gRedo[--gRedoN];
        if (r.kind == 'i')
            Replace(r.pos, 0, r.data, r.len, r.pos + r.len);
        else
            Replace(r.pos, r.len, 0, 0, r.pos);
        PushRec(gUndo, &gUndoN, &r);
    }
    gTyping = false;
    Touch(50);
}

/* ---- clipboard --------------------------------------------------------- */

/* The markers a style needs, in nesting order (outermost first). */
static short Markers(unsigned char st, char *out)
{
    short n = 0;
    if (st & MD_BOLD)   { out[n++] = '*'; out[n++] = '*'; }
    if (st & MD_ITALIC) out[n++] = '*';
    if (st & MD_STRIKE) { out[n++] = '~'; out[n++] = '~'; }
    if (st & MD_CODE)   out[n++] = '`';
    return n;
}

/* Style of the text at pos (0 if it's a marker or outside text). */
static unsigned char TextStyleAt(long pos)
{
    long ps, pe;
    short lvl;
    const unsigned char *sty;
    if (pos < 0 || pos >= TB_Len()) return 0;
    ps = MD_ParaStart(pos);
    pe = MD_ParaEnd(pos);
    if (pos >= pe) return 0;
    sty = MD_Parse(ps, pe, false, &lvl);
    if (!sty || (sty[pos - ps] & MD_MARK)) return 0;
    return sty[pos - ps] & (MD_BOLD | MD_ITALIC | MD_CODE | MD_STRIKE);
}

/* Copy keeps Markdown balanced: a selection that starts or ends inside a
   styled phrase gets the missing opening / closing markers added. */
void Ed_Copy(void)
{
    long a = SelStart(), b = SelEnd(), n = b - a, k;
    char pre[8], post[8];
    short np, nq, i;
    Ptr p;
    if (n <= 0) return;
    np = Markers(TextStyleAt(a), pre);
    nq = Markers(TextStyleAt(b - 1), post);
    for (i = 0; i < nq / 2; i++) {       /* close in reverse order */
        char t = post[i]; post[i] = post[nq - 1 - i]; post[nq - 1 - i] = t;
    }
    p = NewPtr(n + np + nq);
    if (!p) { SysBeep(1); return; }
    for (k = 0; k < np; k++) p[k] = pre[k];
    TB_Copy(a, n, p + np);
    for (k = 0; k < nq; k++) p[np + n + k] = post[k];
    ZeroScrap();
    PutScrap(n + np + nq, 'TEXT', p);
    DisposePtr(p);
}

/* Cmd-B / Cmd-I / Cmd-E: wrap the selection in marker m (k of them), or
   unwrap it if it's already wrapped; with no selection, put an empty pair
   at the cursor so what's typed next has the style. One undo step. */
void Ed_Format(char m, short k)
{
    long a = SelStart(), b = SelEnd(), len = TB_Len(), n, q;
    short i;
    Boolean wrapped;
    char *buf;
    if (a == b) {
        char pair[4];
        for (i = 0; i < 2 * k; i++) pair[i] = m;
        Edit(a, 0, pair, 2 * k, false);
        SetSel(a + k, a + k);
        return;
    }
    for (q = a; q < b; q++)
        if (TB_Char(q) == '\r') { SysBeep(1); return; }   /* one paragraph */
    while (a < b && TB_Char(a) == ' ') a++;      /* "** text**" isn't Markdown */
    while (b > a && TB_Char(b - 1) == ' ') b--;
    n = b - a;
    if (n == 0) return;
    wrapped = a >= k && b + k <= len;
    for (i = 1; wrapped && i <= k; i++)
        wrapped = TB_Char(a - i) == m && TB_Char(b + i - 1) == m;
    if (wrapped && k == 1)               /* "*x*", not the edge of "**x**" */
        wrapped = (a < 2 || TB_Char(a - 2) != m) && (b + 1 >= len || TB_Char(b + 1) != m);
    buf = NewPtr(n + 2 * k);
    if (!buf) { SysBeep(1); return; }
    if (wrapped) {
        TB_Copy(a, n, buf);
        Edit(a - k, n + 2 * k, buf, n, false);
        SetSel(a - k, b - k);
    } else {
        for (i = 0; i < k; i++) buf[i] = buf[k + n + i] = m;
        TB_Copy(a, n, buf + k);
        Edit(a, n, buf, n + 2 * k, false);
        SetSel(a + k, b + k);
    }
    DisposePtr(buf);
}

void Ed_Cut(void)
{
    if (!HasSel()) return;
    Ed_Copy();
    DeleteRange(SelStart(), SelEnd());
}

void Ed_Paste(void)
{
    Handle h = NewHandle(0);
    long offset, n;
    if (!h) return;
    n = GetScrap(h, 'TEXT', &offset);
    if (n > 0) {
        HLock(h);
        InsertText(*h, n, false);
        HUnlock(h);
    }
    DisposeHandle(h);
}

void Ed_SelectAll(void)
{
    gTyping = false;
    SetSel(0, TB_Len());
}

/* ---- block prefixes: quotes, lists, headings --------------------------- */

typedef struct {
    long ps, pe;
    long quoteEnd;       /* after the "> > " part */
    long markStart;      /* the list marker, after its indent */
    long listEnd;        /* after the marker (and a task box) */
    long headEnd;        /* after a heading's "## " (== the text start) */
    short kind, quote, level;
    long number;         /* an ordered item's number */
    char bullet, delim;  /* the marker character: - * + or . ) */
} Parts;

static void GetParts(long ps, Parts *pt)
{
    long q, i;
    short lvl;
    const unsigned char *sty;
    pt->ps = ps;
    pt->pe = MD_ParaEnd(ps);
    sty = MD_Parse(ps, pt->pe, gShowMarks, &lvl);
    pt->kind = MD_KIND(MD_Block());
    pt->quote = MD_QUOTE(MD_Block());
    pt->level = lvl;
    pt->headEnd = ps;
    if (sty)
        while (pt->headEnd < pt->pe && (sty[pt->headEnd - ps] & MD_PREFIX))
            pt->headEnd++;
    q = ps;                              /* the parser's quote rule */
    for (i = 0; i < pt->quote; i++) {
        while (TB_Char(q) == ' ') q++;
        q++;                             /* the '>' */
        if (q < pt->pe && TB_Char(q) == ' ') q++;
    }
    pt->quoteEnd = q;
    pt->markStart = pt->listEnd = q;
    pt->number = 0;
    pt->bullet = '-';
    pt->delim = '.';
    if (pt->kind >= MD_BULLET && pt->kind <= MD_TASKDONE) {
        while (TB_Char(q) == ' ') q++;
        pt->markStart = q;
        if (pt->kind == MD_ORDERED) {
            while (TB_Char(q) >= '0' && TB_Char(q) <= '9')
                pt->number = pt->number * 10 + (TB_Char(q++) - '0');
            pt->delim = TB_Char(q);
        } else
            pt->bullet = TB_Char(q);
        q += 2;                          /* marker and its space */
        if (pt->kind == MD_TASK || pt->kind == MD_TASKDONE)
            q += (q + 3 < pt->pe) ? 4 : 3;
        if (q > pt->pe) q = pt->pe;
        pt->listEnd = q;
    }
    if (pt->kind == MD_CODELINE || pt->kind == MD_FENCE || pt->kind == MD_RULE)
        pt->headEnd = pt->listEnd = pt->markStart = pt->quoteEnd = ps;
}

static Boolean IsItem(short kind) { return kind >= MD_BULLET && kind <= MD_TASKDONE; }

/* Make an item's marker: "- ", "- [ ] ", "3. " */
static short MakeMarker(short kind, char bullet, long number, char delim, char *out)
{
    short n = 0;
    if (kind == MD_ORDERED) {
        char d[10];
        short k = 0;
        if (number < 1) number = 1;
        while (number > 0 && k < 9) { d[k++] = (char)('0' + number % 10); number /= 10; }
        while (k > 0) out[n++] = d[--k];
        out[n++] = delim;
        out[n++] = ' ';
    } else {
        out[n++] = bullet;
        out[n++] = ' ';
        if (kind == MD_TASK) { out[n++] = '['; out[n++] = ' '; out[n++] = ']'; out[n++] = ' '; }
    }
    return n;
}

/* Per-paragraph prefix change: replace [*from, *to) with out[0..n). */
typedef short (*PrefixFn)(Parts *pt, short index, long *from, long *to, char *out);

/* Apply fn to every paragraph touched by the selection, as one edit (one
   undo step). The selection keeps its place in the text. */
static void EditPrefixes(PrefixFn fn)
{
    long a = SelStart(), b = SelEnd(), first = MD_ParaStart(a), last, ps, total, n = 0;
    long newA = a, newB = b, shift = 0, paras = 1, q;
    short idx = 0, k;
    char pre[64], *buf;
    Parts pt;
    last = MD_ParaEnd(b > a && b > first && TB_Char(b - 1) == '\r' ? b - 1 : b);
    total = last - first;
    for (q = first; q < last; q++)
        if (TB_Char(q) == '\r') paras++;
    buf = NewPtr(total + paras * 64);
    if (!buf) { SysBeep(1); return; }
    for (ps = first; ps <= last; idx++) {
        long from = ps, to = ps;
        GetParts(ps, &pt);
        k = fn(&pt, idx, &from, &to, pre);
        for (q = ps; q < from; q++) buf[n++] = TB_Char(q);
        BlockMove(pre, buf + n, k);
        n += k;
        for (q = to; q < pt.pe; q++) buf[n++] = TB_Char(q);
        /* where the selection ends land */
        if (a >= ps && a <= pt.pe)
            newA = a < from ? a + shift : (a < to ? from + shift + k : a + shift + k - (to - from));
        if (b >= ps && b <= pt.pe)
            newB = b < from ? b + shift : (b < to ? from + shift + k : b + shift + k - (to - from));
        shift += k - (to - from);
        if (pt.pe >= last) break;
        buf[n++] = '\r';
        ps = pt.pe + 1;
    }
    if (shift != 0 || n != total) {
        Edit(first, total, buf, n, false);
        SetSel(newA, newB);
    }
    DisposePtr(buf);
}

static short gWant;                      /* the format being applied */
static Boolean gAllHave;                 /* every paragraph has it already */

/* Width of an item's marker and its space: 2 for "- ", 3 for "1. " */
static long MarkerW(Parts *pt)
{
    long q = pt->markStart;
    if (pt->kind != MD_ORDERED) return 2;
    while (TB_Char(q) >= '0' && TB_Char(q) <= '9') q++;
    return q + 2 - pt->markStart;
}

/* Nesting follows standard Markdown: a nested item starts where its
   parent's text starts (2 spaces under "- ", 3 under "1. "). */
static short FnIndent(Parts *pt, short idx, long *from, long *to, char *out)
{
    long sp = pt->markStart - pt->quoteEnd, n = sp, ps;
    short i;
    Parts up;
    if (!IsItem(pt->kind)) return 0;
    for (ps = pt->ps; ps > 0; ) {        /* items above, nearest first */
        ps = MD_ParaStart(ps - 1);
        GetParts(ps, &up);
        if (!IsItem(up.kind) || up.quote != pt->quote) break;
        {
            long usp = up.markStart - up.quoteEnd;
            if (gWant > 0 && usp <= sp) {        /* become its child */
                if (usp + MarkerW(&up) > sp) n = usp + MarkerW(&up);
                break;
            }
            if (gWant < 0 && usp < sp) {         /* line up with it */
                n = usp;
                break;
            }
        }
    }
    if (gWant < 0 && n == sp) n = 0;
    if (gWant > 0 && sp / 2 >= 3) n = sp;        /* three levels deep at most */
    *from = pt->quoteEnd;
    *to = pt->markStart;
    for (i = 0; i < n && i < 60; i++) out[i] = ' ';
    return i;
}

static short gCount;                     /* items numbered so far */

static short FnList(Parts *pt, short idx, long *from, long *to, char *out)
{
    if (pt->pe == pt->ps || pt->kind == MD_CODELINE || pt->kind == MD_FENCE) return 0;
    *from = pt->markStart;
    *to = pt->listEnd;
    if (gAllHave) {                      /* toggle off, indent and all */
        *from = pt->quoteEnd;
        return 0;
    }
    return MakeMarker(gWant, IsItem(pt->kind) && pt->kind != MD_ORDERED ? pt->bullet : '-',
                      ++gCount, '.', out);
}

static short FnQuote(Parts *pt, short idx, long *from, long *to, char *out)
{
    if (pt->kind == MD_CODELINE || pt->kind == MD_FENCE) return 0;
    *from = *to = pt->ps;
    if (gAllHave && pt->quote > 0) {     /* take one level off */
        long q = pt->ps;
        while (TB_Char(q) == ' ') q++;
        q++;
        if (q < pt->pe && TB_Char(q) == ' ') q++;
        *to = q;
        return 0;
    }
    if (gAllHave || pt->pe == pt->ps || pt->quote > 0) return 0;
    out[0] = '>';
    out[1] = ' ';
    return 2;
}

static short FnHeading(Parts *pt, short idx, long *from, long *to, char *out)
{
    short i, n = gAllHave ? 0 : gWant;
    if (pt->pe == pt->ps || pt->kind == MD_CODELINE || pt->kind == MD_FENCE || pt->kind == MD_RULE)
        return 0;
    *from = pt->listEnd;
    *to = pt->headEnd;
    for (i = 0; i < n; i++) out[i] = '#';
    if (n > 0) out[i++] = ' ';
    return i;
}

/* Does every non-empty paragraph in the selection already have it? */
static Boolean AllHave(short what)
{
    long a = SelStart(), b = SelEnd(), ps = MD_ParaStart(a);
    Parts pt;
    Boolean any = false;
    for (;;) {
        GetParts(ps, &pt);
        if (pt.pe > pt.ps) {
            Boolean has = what == 'q' ? pt.quote > 0
                        : what >= 1 && what <= 6 ? pt.level == what && pt.headEnd > pt.listEnd
                        : what == 0 ? true
                        : what == 'b' ? pt.kind == MD_BULLET
                        : what == 'n' ? pt.kind == MD_ORDERED
                        : pt.kind == MD_TASK || pt.kind == MD_TASKDONE;
            if (!has) return false;
            any = true;
        }
        if (pt.pe >= b || pt.pe >= TB_Len()) break;
        ps = pt.pe + 1;
    }
    return any;
}

/* Paragraph formats (Cmd-Shift-8/7/9/', Cmd-Opt-0-6): on every paragraph
   the selection touches; applying one that all of them have removes it. */
void Ed_BlockFormat(short what)
{
    gTyping = false;
    if (what == 'q') {
        gAllHave = AllHave('q');
        EditPrefixes(FnQuote);
    } else if (what >= 0 && what <= 6) {
        gWant = what;
        gAllHave = what == 0 || AllHave(what);
        EditPrefixes(FnHeading);
    } else if (what == 'b' || what == 'n' || what == 't') {
        gWant = what == 'b' ? MD_BULLET : what == 'n' ? MD_ORDERED : MD_TASK;
        gAllHave = AllHave(what);
        gCount = 0;
        EditPrefixes(FnList);
    }
}

/* Tab / Shift-Tab in a list: nest the item(s) a level deeper or shallower. */
static Boolean IndentItems(short dir)
{
    Parts pt;
    GetParts(MD_ParaStart(gCaret), &pt);
    if (!IsItem(pt.kind)) return false;
    gWant = dir;
    gTyping = false;
    EditPrefixes(FnIndent);
    return true;
}

/* Return in a list or quote: the next line continues it. On an empty item
   it ends the list instead (a nested one moves out a level). */
static Boolean ContinueBlock(void)
{
    Parts pt;
    char pre[80];
    short n = 0;
    long q;
    if (HasSel()) return false;
    GetParts(MD_ParaStart(gCaret), &pt);
    if (!IsItem(pt.kind) && pt.quote == 0) return false;
    if (gCaret < pt.headEnd) return false;
    if (pt.headEnd == pt.pe && gCaret == pt.pe) {   /* empty: end it */
        gTyping = false;
        if (IsItem(pt.kind) && pt.markStart > pt.quoteEnd) {
            gWant = -1;
            EditPrefixes(FnIndent);
        } else if (IsItem(pt.kind))
            DeleteRange(pt.quoteEnd, pt.pe);
        else {                           /* "> > ": drop the last level */
            q = pt.quoteEnd;
            while (q > pt.ps && TB_Char(q - 1) == ' ') q--;
            if (q > pt.ps) q--;
            DeleteRange(q, pt.pe);
        }
        return true;
    }
    pre[n++] = '\r';
    for (q = pt.ps; q < pt.markStart && n < 60; q++)
        pre[n++] = TB_Char(q);
    if (IsItem(pt.kind))
        n += MakeMarker(pt.kind == MD_TASKDONE ? MD_TASK : pt.kind, pt.bullet,
                        pt.number + 1, pt.delim, pre + n);
    InsertText(pre, n, false);
    return true;
}

/* Backspace at the start of a paragraph's text takes off its innermost
   format only: a heading's #s, else the list marker, else one quote level. */
static void DeleteInnerPrefix(long ps)
{
    Parts pt;
    long q;
    GetParts(ps, &pt);
    if (pt.headEnd > pt.listEnd)
        DeleteRange(pt.listEnd, pt.headEnd);
    else if (pt.listEnd > pt.markStart)
        DeleteRange(pt.markStart, pt.listEnd);
    else if (pt.quote > 0) {
        q = pt.quoteEnd;
        while (q > pt.ps && TB_Char(q - 1) == ' ') q--;
        if (q > pt.ps) q--;
        DeleteRange(q, pt.quoteEnd);
    } else
        DeleteRange(ps, PrefixEnd(ps));
}

/* A click on a task's box ticks or unticks it. */
static Boolean ClickTaskBox(long line, Point pt)
{
    LineInfo li;
    Parts p;
    long q;
    char c;
    if (gShowMarks || line < 0 || line >= LY_Count()) return false;
    GetLine(line, &li);
    if (li.s != li.ps || (MD_KIND(li.blk) != MD_TASK && MD_KIND(li.blk) != MD_TASKDONE))
        return false;
    if (pt.h < li.x0 - 22 || pt.h >= li.x0 - 2) return false;
    GetParts(li.ps, &p);
    for (q = p.markStart; q < p.listEnd && TB_Char(q) != '['; q++)
        ;
    if (q + 1 >= p.listEnd) return false;
    c = TB_Char(q + 1) == ' ' ? 'x' : ' ';
    {
        long a = gAnchor, k = gCaret;
        gTyping = false;
        Edit(q + 1, 1, &c, 1, false);
        SetSel(a, k);
    }
    return true;
}

/* ---- movement ---------------------------------------------------------- */

static Boolean IsWord(long pos)
{
    unsigned char c = TB_Char(pos);
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c >= 0x80 || c == '\'';
}

static long WordLeft(long p)
{
    while (p > 0 && !IsWord(p - 1)) p--;
    while (p > 0 && IsWord(p - 1)) p--;
    return p;
}

static long WordRight(long p)
{
    long len = TB_Len();
    while (p < len && !IsWord(p)) p++;
    while (p < len && IsWord(p)) p++;
    return p;
}

static long VerticalMove(long lines)
{
    LineInfo li;
    long line = LY_LineOf(gCaret), target;
    if (gGoalX < 0) {
        GetLine(line, &li);
        gGoalX = XOf(&li, gCaret);
    }
    target = line + lines;
    if (target < 0) return 0;
    if (target >= LY_Count()) return TB_Len();
    GetLine(target, &li);
    return PrefixEnd(PosAt(&li, gGoalX));
}

void Ed_Key(EventRecord *ev)
{
    unsigned char c = ev->message & charCodeMask;
    Boolean shift = (ev->modifiers & shiftKey) != 0;
    Boolean opt = (ev->modifiers & optionKey) != 0;
    Boolean cmd = (ev->modifiers & cmdKey) != 0;
    long line = LY_LineOf(gCaret), pos;
    Boolean vertical = false;
    Boolean wasBack = gBackRun;          /* the previous key was a Backspace */
    char ch;

    gBackRun = (c == 0x08 && !cmd && !opt);
    switch (c) {
    case 0x1C:                                           /* left */
        if (HasSel() && !shift) pos = SelStart();
        else if (cmd) pos = PrefixEnd(LY_Start(line));
        else if (opt) pos = WordLeft(gCaret);
        else {
            pos = SkipLeft(gCaret);
            if (pos > 0) pos = Enc_EscapeStart(pos - 1);
        }
        break;
    case 0x1D:                                           /* right */
        if (HasSel() && !shift) pos = SelEnd();
        else if (cmd) pos = LY_End(line);
        else if (opt) pos = WordRight(gCaret);
        else {
            pos = SkipRight(gCaret);
            if (pos < TB_Len()) pos = Enc_EscapeEnd(pos + 1);
        }
        break;
    case 0x1E:                                           /* up */
        pos = cmd ? 0 : VerticalMove(-1);
        vertical = true;
        break;
    case 0x1F:                                           /* down */
        pos = cmd ? TB_Len() : VerticalMove(1);
        vertical = true;
        break;
    case 0x01: pos = PrefixEnd(0); break;                /* home */
    case 0x04: pos = TB_Len(); break;                    /* end */
    case 0x0B: pos = VerticalMove(gTop - LastVisible()); vertical = true; break;  /* page up */
    case 0x0C: pos = VerticalMove(LastVisible() - gTop); vertical = true; break;  /* page down */

    case 0x08:                                           /* delete */
        if (HasSel()) DeleteRange(SelStart(), SelEnd());
        else if (cmd) DeleteRange(LY_Start(line), gCaret);
        else if (opt) DeleteRange(WordLeft(gCaret), gCaret);
        else if (SkipLeft(gCaret) < gCaret) {
            pos = SkipLeft(gCaret);
            if (PrefixEnd(pos) > pos)    /* "### " / "- " / "> " behind */
                DeleteInnerPrefix(pos);
            else if (wasBack             /* backspacing along: reopen the style */
                     || (gTyping && gUndoN > 0 && gUndo[gUndoN - 1].kind == 'i'
                      && gUndo[gUndoN - 1].pos <= pos
                      && gUndo[gUndoN - 1].pos + gUndo[gUndoN - 1].len == gCaret)
                     /* or an opener (line start / after a space): nothing of
                        it would be left to see */
                     || pos == MD_ParaStart(pos) || TB_Char(pos - 1) == ' ')
                DeleteRange(pos, SkipRight(gCaret));   /* a ** just typed goes as
                                               one unit: a misplaced close can be
                                               removed and retyped */
            else if (pos > 0)            /* otherwise markers stay put and the */
                DeleteRange(pos - 1, pos);   /* letter before them goes */
        } else if (gCaret > 0) {
            if (gTyping && gUndoN > 0 && gUndo[gUndoN - 1].kind == 'i'
                && gUndo[gUndoN - 1].len > 0
                && gUndo[gUndoN - 1].pos + gUndo[gUndoN - 1].len == gCaret) {
                /* backspacing over just-typed text: shrink the record */
                gUndo[gUndoN - 1].len--;
                Touch(1);
                Replace(gCaret - 1, 1, 0, 0, gCaret - 1);
            } else
                DeleteRange(gCaret - 1, gCaret);
            /* backspacing up to a hidden closing marker: it goes, so the
               style reopens and what's typed next continues it */
            pos = SkipLeft(gCaret);
            if (!gShowMarks && pos < gCaret && PrefixEnd(pos) == pos
                && pos > MD_ParaStart(pos) && TB_Char(pos - 1) != ' ')
                DeleteRange(pos, gCaret);
        }
        CollapseEmptyPair();
        return;
    case 0x7F:                                           /* forward delete */
        if (HasSel()) DeleteRange(SelStart(), SelEnd());
        else {
            pos = SkipRight(gCaret);
            if (pos < TB_Len() && TB_Char(pos) == '\r' && PrefixEnd(pos + 1) > pos + 1)
                DeleteRange(pos, PrefixEnd(pos + 1));   /* join; the heading's "### " goes too */
            else if (pos < TB_Len())         /* past hidden markers: the letter */
                DeleteRange(pos, pos + 1);
        }
        CollapseEmptyPair();
        return;
    case 0x0D: case 0x03:                                /* return, enter */
        DropLoneOpener();
        if (ContinueBlock())
            return;
        ch = '\r';
        InsertText(&ch, 1, true);
        return;
    case 0x09:                                           /* tab: 4 spaces */
        if (IndentItems(shift ? -1 : 1))                 /* in a list: nest */
            return;
        if (!shift)
            InsertText("    ", 4, true);
        return;
    case 0x1B:                                           /* escape */
        return;
    default:
        if (c >= 0x20 && !cmd) {
            char two[2];
            long ps, k;
            DropLoneOpener();
            ps = MD_ParaStart(gCaret);
            ch = (char)c;
            /* "##text": Markdown needs a space after a heading's #s; put it in
               (2-6 #s only, so a "#hashtag" stays as typed) */
            if (!HasSel() && c != '#' && c != ' ' && gCaret - ps >= 2 && gCaret - ps <= 6) {
                for (k = ps; k < gCaret && TB_Char(k) == '#'; k++)
                    ;
                if (k == gCaret) {
                    two[0] = ' ';
                    two[1] = ch;
                    InsertText(two, 2, true);
                    return;
                }
            }
            InsertText(&ch, 1, true);
        }
        return;
    }

    /* movement */
    gTyping = false;
    if (!vertical)
        gGoalX = -1;
    pos = Enc_EscapeStart(pos);          /* never land inside an escape */
    if (shift)
        SetSel(gAnchor, pos);
    else
        SetSel(pos, pos);
}

/* ---- mouse ------------------------------------------------------------- */

static long LineAt(Point pt)
{
    long line = gTop;
    short y = kTopPad;
    if (pt.v < kTopPad) return gTop - 1;
    while (line < LY_Count() - 1 && y + LineH(line) <= pt.v) {
        y += LineH(line);
        line++;
    }
    return line;
}

static long HitTest(Point pt)
{
    LineInfo li;
    long line = gTop;
    short y = kTopPad;
    if (pt.v < kTopPad)
        line = gTop - 1;
    else
        while (line < LY_Count() - 1 && y + LineH(line) <= pt.v) {
            y += LineH(line);
            line++;
        }
    if (line < 0) return 0;
    if (line >= LY_Count()) return TB_Len();
    GetLine(line, &li);
    return PrefixEnd(PosAt(&li, pt.h));
}

#define kHoldTicks 30                    /* half a second */
static Boolean gPressHeld;

Boolean Ed_ClickWasHold(void) { return gPressHeld; }

void Ed_Click(Point pt, EventRecord *ev)
{
    Point start;
    long pos = HitTest(pt), wordA = 0, wordB = 0, p, a, b;
    Boolean word = false;

    gBackRun = false;
    unsigned long lastScroll = 0;
    short bottom = Bottom();

    gTyping = false;
    gGoalX = -1;
    if (!(ev->modifiers & shiftKey) && ClickTaskBox(LineAt(pt), pt)) {
        while (StillDown()) ;
        return;
    }
    if (ev->when - gLastClick < kDoubleTicks && pos == gLastClickPos) {
        word = true;                     /* double-click: select the word */
        wordA = (pos > 0 && IsWord(pos - 1)) ? WordLeft(pos) : pos;
        wordB = WordRight(wordA);
        SetSel(wordA, wordB);
    } else if (ev->modifiers & shiftKey)
        SetSel(gAnchor, pos);
    else
        SetSel(pos, pos);
    gLastClick = ev->when;
    gLastClickPos = pos;
    gPressHeld = false;
    start = pt;

    while (StillDown()) {
        GetMouse(&pt);
        if (!word && !HasSel() && !(ev->modifiers & shiftKey)) {
            short dh = pt.h - start.h, dv = pt.v - start.v;
            if (dh < -3 || dh > 3 || dv < -3 || dv > 3)
                start.h = -1000;         /* it moved: a drag, not a hold */
            else if (start.h != -1000 && TickCount() - ev->when >= kHoldTicks) {
                gPressHeld = true;       /* press and hold: Paste pill */
                gLastClick = 0;          /* the release isn't half a double-click */
                return;
            }
        }
        if ((pt.v < kTopPad || pt.v > bottom) && TickCount() - lastScroll > 4) {
            long old = gTop;             /* drag past an edge: autoscroll */
            if (pt.v < kTopPad && gTop > 0) gTop--;
            if (pt.v > bottom && LastVisible() < LY_Count() - 1) gTop++;
            if (gTop != old) Ed_DrawAll();
            lastScroll = TickCount();
        }
        p = HitTest(pt);
        if (word) {
            a = p < wordA ? WordLeft(p) : wordA;
            b = p > wordB ? WordRight(p) : wordB;
            if (p < wordA) { if (a != SelStart() || gCaret != a) SetSel(wordB, a); }
            else if (b != SelEnd()) SetSel(a, b);
        } else if (p != gCaret)
            SetSel(gAnchor, p);
    }
}

/* ---- setup ------------------------------------------------------------- */

void Ed_Activate(Boolean active)
{
    HideCaret();
    gActive = active;
    ShowCaret();
}

void Ed_Idle(void)
{
    if (!HasSel() && gActive && TickCount() - gCaretTick >= kBlinkTicks) {
        ToggleCaret();
        gCaretTick = TickCount();
    }
}

Boolean Ed_UsingFallbackFont(void) { return gFallbackFont; }

void Ed_SetShowMarks(Boolean on)
{
    if (on == gShowMarks) return;
    gShowMarks = on;
    LY_SetShowMarks(on);
    MD_Invalidate();
    LY_Rebuild();
    if (gAnchor == gCaret) {             /* caret never rests inside a hidden "# " */
        gAnchor = gCaret = PrefixEnd(MD_ParaStart(gCaret)) > gCaret
                           ? PrefixEnd(MD_ParaStart(gCaret)) : gCaret;
        LY_SetCaret(gCaret);
    }
    if (!ScrollToCaret())
        Ed_DrawAll();
}

Boolean Ed_ShowMarks(void) { return gShowMarks; }

Boolean Ed_HasSelection(void) { return HasSel(); }

/* Rects of the selection on its first and last lines (window coords). */
void Ed_SelectionBounds(Rect *first, Rect *last)
{
    LineInfo li;
    long a = SelStart(), b = SelEnd(), la = LY_LineOf(a), lb = LY_LineOf(b);
    short ya, yb;
    if (la < gTop) la = gTop;
    if (lb > LastVisible()) lb = LastVisible();
    if (lb < la) lb = la;
    ya = LineTop(la);
    yb = LineTop(lb);
    GetLine(la, &li);
    SetRect(first, XOf(&li, a < li.s ? li.s : a), ya, li.right, ya + LineH(la));
    GetLine(lb, &li);
    SetRect(last, li.x0, yb, XOf(&li, b > li.e ? li.e : b), yb + LineH(lb));
    if (la == lb) first->right = last->right;
}

/* Headings: whether there are any, and moving to the next / previous. */
static long FindHeading(long from, short dir)
{
    long len = TB_Len(), p = MD_ParaStart(from), pe;
    short lvl;
    for (;;) {
        if (dir > 0) {
            p = MD_ParaEnd(p);
            if (p >= len) return -1;
            p++;
        } else {
            if (p == 0) return -1;
            p = MD_ParaStart(p - 1);
        }
        pe = MD_ParaEnd(p);
        MD_Parse(p, pe, gShowMarks, &lvl);
        if (lvl > 0 && pe > p) return p;
    }
}

Boolean Ed_HasHeadings(void)
{
    long i, n = LY_Count();
    for (i = 0; i < n; i++)              /* the layout knows each line's level */
        if (LY_Level(i) > 0) return true;
    return false;
}

void Ed_JumpSection(short dir)
{
    long p = FindHeading(gCaret, dir);
    if (p < 0) { SysBeep(1); return; }
    gTyping = false;
    Ed_SetCaret(p);
}

/* TK placeholders (ROADMAP, from the Neo review): "TK" is publishing's
   mark for "to come", a fact or name to fill in later. A TK counts when it
   stands as a word of its own, in capitals. */
static Boolean IsLetter(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
        || c >= 0x80;                    /* accented letters in Mac Roman */
}

static Boolean TKAt(long p, long len)
{
    return p + 1 < len && TB_Char(p) == 'T' && TB_Char(p + 1) == 'K'
        && (p == 0 || !IsLetter(TB_Char(p - 1)))
        && (p + 2 >= len || !IsLetter(TB_Char(p + 2)));
}

/* Select the next TK after the selection, wrapping to the top. Returns
   how many there are (0: none) and which one is now picked (1-based). */
short Ed_NextTK(short *which)
{
    long len = TB_Len(), p, from = SelEnd(), hit = -1;
    short total = 0, at = 0;
    for (p = 0; p + 1 < len; p++)
        if (TKAt(p, len)) {
            total++;
            if (hit < 0 && p >= from) { hit = p; at = total; }
            if (total == 32767) break;
        }
    if (total == 0) return 0;
    if (hit < 0) {                       /* none below: wrap to the first */
        for (p = 0; !TKAt(p, len); p++)
            ;
        hit = p;
        at = 1;
    }
    gTyping = false;
    SetSel(hit, hit + 2);                /* typing replaces it */
    if (which) *which = at;
    return total;
}

/* Drop a TK at the caret (or over the selection), spaced as a word. */
void Ed_InsertTK(void)
{
    long a = SelStart(), e = SelEnd(), len = TB_Len();
    char buf[4];
    short n = 0;
    if (a > 0 && TB_Char(a - 1) != ' ' && TB_Char(a - 1) != '\r') buf[n++] = ' ';
    buf[n++] = 'T';
    buf[n++] = 'K';
    if (e < len && TB_Char(e) != ' ' && TB_Char(e) != '\r') buf[n++] = ' ';
    InsertText(buf, n, false);
}

void Ed_SetCaret(long pos)
{
    if (pos < 0) pos = 0;
    if (pos > TB_Len()) pos = TB_Len();
    pos = PrefixEnd(MD_ParaStart(pos)) > pos ? PrefixEnd(MD_ParaStart(pos)) : pos;
    SetSel(pos, pos);
}

Boolean Ed_SetText(const char *text, long len)
{
    if (!TB_Replace(0, TB_Len(), text, len))
        return false;
    Ed_AdoptText();
    return true;
}

/* The text buffer was filled directly (opening a file): start editing it
   as a fresh document, without a copy of it in between. */
void Ed_AdoptText(void)
{
    MD_Invalidate();
    gAnchor = gCaret = 0;
    LY_SetCaret(0);
    LY_Rebuild();
    while (gUndoN > 0) FreeRec(&gUndo[--gUndoN]);
    ClearRedo();
    gWeight = 0;
    gTyping = false;
    gTop = 0;
    gAnchor = gCaret = PrefixEnd(0);     /* start of the visible text, past "# " */
    LY_SetCaret(gCaret);
    Ed_DrawAll();
}

#define kStripMaxH 48                    /* offscreen strip height */

/* A size's metrics, the first time something needs them: a heading size
   no document uses never has its font loaded (each is a resource read from
   disk, slow from a floppy). */
static void Need(short level)
{
    FontInfo fi;
    GrafPtr port;
    if (gHave[level]) return;
    GetPort(&port);
    SetPort(gWin);
    SetFont(level, false, false);
    GetFontInfo(&fi);
    gAsc[level] = fi.ascent;
    gLH[level] = fi.ascent + fi.descent + fi.leading;
    gCW[level] = CharWidth('m');
    gHave[level] = true;
    SetPort(port);
}

static void Measure(void)
{
    short i;
    for (i = 0; i < 7; i++)
        gHave[i] = false;                /* measured again as needed */
    Need(0);                             /* the body, which every page has */
    SetPort(gWin);
    SetFont(0, false, false);
    ForgetWidths();
}

/* The nearest size the font really has (RealFont: a bitmap, or TrueType). */
static short Snap(short font, short want)
{
    static const short kTry[] = { 9, 10, 12, 14, 16, 18, 20, 24 };
    short i, best = want, bestD = 999, d;
    for (i = 0; i < 8; i++)
        if (RealFont(font, kTry[i])) {
            d = kTry[i] > want ? kTry[i] - want : want - kTry[i];
            if (d < bestD) { bestD = d; best = kTry[i]; }
        }
    return best;
}

Boolean Ed_FontAvailable(short fontID)
{
    return fontID == 0 ? !gFallbackFont : RealFont(fontID, 12) || RealFont(fontID, 9)
                                           || RealFont(fontID, 10);
}

short Ed_FontID(void)   { return gFontID; }

/* The next body size this font really has, up (dir > 0) or down; wraps. */
short Ed_StepBodySize(short dir)
{
    static const short kBody[] = { 9, 10, 12, 14, 16, 18 };
    short f = gFontID != 0 ? gFontID : (gFallbackFont ? kFontMonaco : kFontMDW);
    short j, k, idx = 0;
    for (j = 0; j < 6; j++)
        if (kBody[j] <= gSize[0]) idx = j;          /* where we are now */
    for (k = 1; k < 6; k++) {
        j = (short)((idx + (dir > 0 ? k : 6 - k)) % 6);
        if (RealFont(f, kBody[j])) return kBody[j];
    }
    return gSize[0];
}

short Ed_BodySize(void) { return gSize[0]; }

/* One font and body size for everything. Heading ladder: H1 2x, H2 1.6x,
   H3 1.4x, H4/H5 1.2x, H6 1x, each snapped to a real size. */
void Ed_SetFontChoice(short fontID, short body)
{
    static const short kPct[7] = { 100, 200, 160, 140, 120, 120, 100 };
    short i, f;
    if (!Ed_FontAvailable(fontID)) fontID = 0;
    gFontID = fontID;
    f = fontID != 0 ? fontID : (gFallbackFont ? kFontMonaco : kFontMDW);
    for (i = 0; i < 7; i++)
        gSize[i] = Snap(f, (short)((long)body * kPct[i] / 100));
    if (gSize[1] > 24) gSize[1] = Snap(f, 24);
    SetPort(gWin);
    Measure();
    gOffOK = gOffBits.baseAddr != 0;     /* the offscreen strip is fixed-height */
#if defined(DIAG_NO_OFFSCREEN) || defined(DIAG_MINIMAL)
    gOffOK = false;
#endif
    MD_Invalidate();                     /* (a line taller than the strip
                                            draws directly) */
    LY_Init(kColumnW, CW);
    if (!ScrollToCaret())
        Ed_DrawAll();
}

Boolean Ed_Init(WindowPtr w)
{
    short i;

    gWin = w;
    SetPort(w);
    gFallbackFont = !RealFont(kFontMDW, kSize[0]);
#if defined(DIAG_NO_MDWFONT) || defined(DIAG_MINIMAL)
    gFallbackFont = true;
#endif
    Measure();

    {   /* offscreen line strip, tall enough for any font choice's lines */
        short maxH = kStripMaxH, rowBytes = ((w->portRect.right + 15) / 16) * 2;
        Rect b;
        gOffH = maxH;                    /* taller lines draw directly */
        gOffBits.baseAddr = NewPtr((long)rowBytes * maxH);
#if defined(DIAG_NO_OFFSCREEN) || defined(DIAG_MINIMAL)
        gOffBits.baseAddr = 0;
#endif
        if (gOffBits.baseAddr) {
            gOffBits.rowBytes = rowBytes;
            SetRect(&b, 0, 0, w->portRect.right, maxH);
            gOffBits.bounds = b;
            OpenPort(&gOffPort);
            SetPortBits(&gOffBits);
            PortSize(b.right, b.bottom);
            RectRgn(gOffPort.visRgn, &b);
            ClipRect(&b);
            gOffOK = true;
            SetPort(w);
        }
    }

    if (!TB_Init(kBufferSize))
        return false;
    return LY_Init(kColumnW, CW);
}
