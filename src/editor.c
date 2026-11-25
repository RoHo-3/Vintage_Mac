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
static const short kSize[7] = { 10, 20, 16, 14, 12, 12, 10 };
static short gAsc[7], gLH[7], gCW[7], gCols[7];

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
    Boolean active;      /* paragraph holds the cursor: markers shown */
    const unsigned char *sty;   /* styles, indexed from ps */
} LineInfo;

static short LineH(long line)
{
    return gLH[line < LY_Count() ? LY_Level(line) : 0];
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
    short cw = gCW[li->level], n = 0;
    long q;
    if (p < li->prefixEnd)
        return kLeft - (short)(li->prefixEnd - p) * cw;
    for (q = li->s; q < p && q < li->e; q++)
        if (!(StyleAt(li, q) & MD_HIDDEN) && n < gCols[li->level])
            n++;
    return kLeft + n * cw;
}

/* Text position nearest pixel x on this line. */
static long PosAt(LineInfo *li, short x)
{
    short cw = gCW[li->level], col, n = 0;
    long p;
    col = (x - kLeft + (x >= kLeft ? cw / 2 : -cw / 2)) / cw;
    if (col < 0) {
        if (li->active && li->prefixEnd > li->ps && li->s == li->ps) {
            p = li->prefixEnd + col;     /* click into the hanging "##" */
            return p < li->ps ? li->ps : p;
        }
        col = 0;
    }
    for (p = li->s; p < li->e; p++) {
        if (StyleAt(li, p) & MD_HIDDEN)
            continue;
        if (n >= col)
            return p;
        n++;
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

static void SetFont(short level, Boolean isBold, Boolean isItalic)
{
    short f;
    Style face = 0;
    if (level > 0) isBold = true;
    if (gFallbackFont) {                 /* Monaco: let QuickDraw style it */
        f = kFontMonaco;
        if (isBold) face |= bold;
        if (isItalic) face |= italic;
    } else
        f = kFontMDW + (isBold ? 1 : 0) + (isItalic ? 2 : 0);
    if (level == 4)
        face |= underline;
    TextFont(f);
    TextSize(kSize[level]);
    TextFace(face);
}

static void DrawRun(short level, unsigned char style, short x, short base, short n)
{
    if (n <= 0) return;
    SetFont(level, (style & MD_BOLD) != 0, (style & MD_ITALIC) != 0);
    MoveTo(x, base);
    DrawText(gRun, 0, n);
}

static void DrawLine(long line, short y)
{
    Rect r, dst;
    LineInfo li;
    short h = LineH(line), base, x, runX, n = 0, cols, vis = 0;
    short screenY = y;
    long p, a, b, next;
    unsigned char st, runSt = 0;
    char c;

    if (gOffOK) {                        /* compose offscreen at y = 0 */
        SetPort(&gOffPort);
        y = 0;
    }
    SetRect(&r, 0, y, gWin->portRect.right - 2, y + h);
    EraseRect(&r);
    if (line >= LY_Count())
        goto done;

    GetLine(line, &li);
    base = y + gAsc[li.level];
    cols = gCols[li.level];

    /* the heading prefix hangs in the margin while the cursor is here */
    if (li.active && li.s == li.ps && li.prefixEnd > li.ps) {
        n = (short)(li.prefixEnd - li.ps);
        if (n > kRunMax) n = kRunMax;
        TB_Copy(li.ps, n, gRun);
        DrawRun(li.level, 0, kLeft - n * gCW[li.level], base, n);
        n = 0;
    }

    x = runX = kLeft;
    for (p = li.s; p < li.e && vis < cols; p++) {
        st = StyleAt(&li, p);
        if (st & MD_HIDDEN)
            continue;
        st &= (MD_BOLD | MD_ITALIC);
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
        x += gCW[li.level];
        vis++;
    }
    DrawRun(li.level, runSt, runX, base, n);

    if (HasSel()) {                      /* reverse the selected part */
        next = (line + 1 < LY_Count()) ? LY_Start(line + 1) : TB_Len() + 1;
        a = SelStart() > li.s ? SelStart() : li.s;
        b = SelEnd() < next ? SelEnd() : next;
        if (a < b) {
            short x1 = XOf(&li, a);
            short x2 = (b >= next && line + 1 < LY_Count())
                       ? kLeft + cols * gCW[li.level]       /* past line end */
                       : XOf(&li, b);
            if (x2 > x1) {
                SetRect(&r, x1, y, x2, y + h);
                InvertRect(&r);
            }
        }
    }
done:
    if (gOffOK) {
        SetPort(gWin);
        SetRect(&r, 0, 0, gWin->portRect.right - 2, h);
        SetRect(&dst, 0, screenY, gWin->portRect.right - 2, screenY + h);
        CopyBits(&gOffPort.portBits, &gWin->portBits, &r, &dst, srcCopy, NULL);
    }
}

/* Draw lines [from, to) that are on screen. */
static void DrawLines(long from, long to)
{
    short y, bottom = Bottom();
    Rect r;
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
    SetPort(gWin);
    SetRect(&r, 0, kStripH, gWin->portRect.right, gWin->portRect.bottom - kFooterH);
    EraseRect(&r);                       /* the text area; chrome draws itself */
    DrawLines(gTop, kToEnd);
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
    if (!CaretVisible()) { gCaretOn = false; return; }
    CaretRect(&r);
    InvertRect(&r);
    gCaretOn = !gCaretOn;
}

static void HideCaret(void)
{
    if (gCaretOn)
        ToggleCaret();
}

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
    if (MD_ParaStart(oldPos) == MD_ParaStart(gCaret))
        return;
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
        if (oldCount == newCount)
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

void Ed_Copy(void)
{
    long n = SelEnd() - SelStart();
    Ptr p;
    if (n <= 0) return;
    p = NewPtr(n);
    if (!p) { SysBeep(1); return; }
    TB_Copy(SelStart(), n, p);
    ZeroScrap();
    PutScrap(n, 'TEXT', p);
    DisposePtr(p);
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
    char ch;

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
            if (PrefixEnd(pos) > pos)    /* "### " behind: heading -> paragraph */
                DeleteRange(pos, PrefixEnd(pos));
            else                         /* a hidden ** or * just typed or passed: */
                DeleteRange(pos, gCaret);   /* it goes as one unit, so a misplaced
                                               close can be removed and retyped */
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
        }
        return;
    case 0x7F:                                           /* forward delete */
        if (HasSel()) DeleteRange(SelStart(), SelEnd());
        else {
            pos = SkipRight(gCaret);
            if (pos < TB_Len() && TB_Char(pos) == '\r' && PrefixEnd(pos + 1) > pos + 1)
                DeleteRange(pos, PrefixEnd(pos + 1));   /* join; the heading's "### " goes too */
            else if (pos > gCaret)
                DeleteRange(gCaret, pos);       /* hidden marker ahead: one unit */
            else if (pos < TB_Len())
                DeleteRange(pos, pos + 1);
        }
        return;
    case 0x0D: case 0x03:                                /* return, enter */
        ch = '\r';
        InsertText(&ch, 1, true);
        return;
    case 0x09:                                           /* tab: 4 spaces */
        InsertText("    ", 4, true);
        return;
    case 0x1B:                                           /* escape */
        return;
    default:
        if (c >= 0x20 && !cmd) {
            ch = (char)c;
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

void Ed_Click(Point pt, EventRecord *ev)
{
    long pos = HitTest(pt), wordA = 0, wordB = 0, p, a, b;
    Boolean word = false;
    unsigned long lastScroll = 0;
    short bottom = Bottom();

    gTyping = false;
    gGoalX = -1;
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

    while (StillDown()) {
        GetMouse(&pt);
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

Boolean Ed_SetText(const char *text, long len)
{
    if (!TB_Replace(0, TB_Len(), text, len))
        return false;
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
    return true;
}

Boolean Ed_Init(WindowPtr w)
{
    FontInfo fi;
    short i;

    gWin = w;
    SetPort(w);
    gFallbackFont = !RealFont(kFontMDW, kSize[0]);
#if defined(DIAG_NO_MDWFONT) || defined(DIAG_MINIMAL)
    gFallbackFont = true;
#endif
    for (i = 0; i < 7; i++) {            /* measure body and each heading */
        SetFont(i, false, false);
        GetFontInfo(&fi);
        gAsc[i] = fi.ascent;
        gLH[i] = fi.ascent + fi.descent + fi.leading;
        gCW[i] = CharWidth('m');
        gCols[i] = kColumnW / gCW[i];
    }
    SetFont(0, false, false);

    {   /* offscreen line strip, as tall as the tallest line */
        short maxH = 0, rowBytes = ((w->portRect.right + 15) / 16) * 2;
        Rect b;
        for (i = 0; i < 7; i++)
            if (gLH[i] > maxH) maxH = gLH[i];
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
    return LY_Init(gCols);
}
