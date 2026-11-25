#include "editor.h"
#include "textbuf.h"
#include "layout.h"

#include <Fonts.h>
#include <Memory.h>
#include <ToolUtils.h>
#include <OSUtils.h>

/* ---- geometry (see docs/DESIGN.md) ------------------------------------ */

#define kFontMDW      200        /* MDW Mono (bitmap Plex Mono) */
#define kFontMonaco   4          /* fallback if our font is missing */
#define kBodySize     10
#define kLeft         46         /* left margin */
#define kColumnW      420        /* text column width */
#define kTopPad       24
#define kBottomPad    14
#define kBlinkTicks   30
#define kDoubleTicks  30
#define kBufferSize   (384L * 1024)
#define kMaxUndo      64

static WindowPtr gWin;
static short gCW, gLH, gAsc, gCols, gVis;
static long  gTop;                       /* first visible line */
static long  gAnchor, gCaret;            /* selection = [min, max) */
static long  gGoalCol = -1;              /* column kept on up/down */
static Boolean gCaretOn, gActive = true;
static unsigned long gCaretTick, gLastClick;
static long  gLastClickPos = -1;
static Boolean gTyping;                  /* coalesce typing into one undo */

#define SelStart() (gAnchor < gCaret ? gAnchor : gCaret)
#define SelEnd()   (gAnchor < gCaret ? gCaret : gAnchor)
#define HasSel()   (gAnchor != gCaret)

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

/* ---- drawing ----------------------------------------------------------- */

static short LineTop(long line) { return kTopPad + (short)(line - gTop) * gLH; }

static void CaretRect(Rect *r)
{
    long line = LY_LineOf(gCaret);
    long col = gCaret - LY_Start(line);
    short x;
    if (col > gCols) col = gCols;
    x = kLeft + (short)col * gCW;
    SetRect(r, x - 1, LineTop(line), x, LineTop(line) + gLH);
}

static Boolean CaretVisible(void)
{
    long line = LY_LineOf(gCaret);
    return line >= gTop && line < gTop + gVis;
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

static void DrawLine(long line)
{
    Rect r;
    short y = LineTop(line);
    long s, e, n, next, a, b;
    const char *p1, *p2;
    long n1, n2;

    SetRect(&r, 0, y, gWin->portRect.right - 2, y + gLH);
    EraseRect(&r);
    if (line >= LY_Count())
        return;

    s = LY_Start(line);
    e = LY_End(line);
    n = e - s;
    if (n > gCols) n = gCols;
    if (n > 0) {
        TB_Runs(s, n, &p1, &n1, &p2, &n2);
        MoveTo(kLeft, y + gAsc);
        if (n1) DrawText(p1, 0, (short)n1);
        if (n2) DrawText(p2, 0, (short)n2);
    }

    if (HasSel()) {                      /* reverse the selected part */
        next = (line + 1 < LY_Count()) ? LY_Start(line + 1) : TB_Len() + 1;
        a = SelStart() > s ? SelStart() : s;
        b = SelEnd() < next ? SelEnd() : next;
        if (a < b) {
            short x1 = kLeft + (short)((a - s > gCols) ? gCols : a - s) * gCW;
            short x2 = (b >= next && line + 1 < LY_Count())
                       ? kLeft + gCols * gCW                 /* runs past line end */
                       : kLeft + (short)((b - s > gCols) ? gCols : b - s) * gCW;
            if (x2 > x1) {
                SetRect(&r, x1, y, x2, y + gLH);
                InvertRect(&r);
            }
        }
    }
}

static void DrawLines(long from, long to)   /* [from, to), visible part */
{
    long i;
    if (from < gTop) from = gTop;
    if (to > gTop + gVis) to = gTop + gVis;
    for (i = from; i < to; i++)
        DrawLine(i);
}

void Ed_DrawAll(void)
{
    gCaretOn = false;
    SetPort(gWin);
    EraseRect(&gWin->portRect);
    DrawLines(gTop, gTop + gVis);
    ShowCaret();
}

/* Scroll so the caret line is on screen; returns true if it redrew. */
static Boolean ScrollToCaret(void)
{
    long line = LY_LineOf(gCaret), old = gTop;
    if (line < gTop)
        gTop = line;
    else if (line >= gTop + gVis)
        gTop = line - gVis + 1;
    if (gTop != old) {
        Ed_DrawAll();
        return true;
    }
    return false;
}

/* ---- selection --------------------------------------------------------- */

static void SetSel(long anchor, long caret)
{
    long oldS = SelStart(), oldE = SelEnd(), lo, hi;
    long len = TB_Len();
    if (anchor < 0) anchor = 0;
    if (caret < 0) caret = 0;
    if (anchor > len) anchor = len;
    if (caret > len) caret = len;

    HideCaret();
    gAnchor = anchor;
    gCaret = caret;
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
    long first, oldCount, newCount;

    HideCaret();
    if (!TB_Replace(from, oldLen, src, newLen)) {
        SysBeep(1);                      /* document full */
        return false;
    }
    if (!LY_Update(from, oldLen, newLen, &first, &oldCount, &newCount)) {
        SysBeep(1);
        LY_Rebuild();
        first = 0;
        oldCount = 0;
        newCount = 1;
    }
    gAnchor = gCaret = newCaret;
    if (!ScrollToCaret()) {
        if (oldCount == newCount)
            DrawLines(first, first + newCount);
        else
            DrawLines(first, gTop + gVis);   /* lines below moved */
    }
    ShowCaret();
    return true;
}

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
    gGoalCol = -1;
    Replace(from, oldLen, src, newLen, from + newLen);
}

static void DeleteRange(long a, long b)
{
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
    long line = LY_LineOf(gCaret), target, s, e;
    if (gGoalCol < 0)
        gGoalCol = gCaret - LY_Start(line);
    target = line + lines;
    if (target < 0) return 0;
    if (target >= LY_Count()) return TB_Len();
    s = LY_Start(target);
    e = LY_End(target);
    return (s + gGoalCol < e) ? s + gGoalCol : e;
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
        else if (cmd) pos = LY_Start(line);
        else if (opt) pos = WordLeft(gCaret);
        else pos = gCaret - 1;
        break;
    case 0x1D:                                           /* right */
        if (HasSel() && !shift) pos = SelEnd();
        else if (cmd) pos = LY_End(line);
        else if (opt) pos = WordRight(gCaret);
        else pos = gCaret + 1;
        break;
    case 0x1E:                                           /* up */
        pos = cmd ? 0 : VerticalMove(-1);
        vertical = true;
        break;
    case 0x1F:                                           /* down */
        pos = cmd ? TB_Len() : VerticalMove(1);
        vertical = true;
        break;
    case 0x01: pos = 0; break;                           /* home */
    case 0x04: pos = TB_Len(); break;                    /* end */
    case 0x0B: pos = VerticalMove(-gVis); vertical = true; break;  /* page up */
    case 0x0C: pos = VerticalMove(gVis); vertical = true; break;   /* page down */

    case 0x08:                                           /* delete */
        if (HasSel()) DeleteRange(SelStart(), SelEnd());
        else if (cmd) DeleteRange(LY_Start(line), gCaret);
        else if (opt) DeleteRange(WordLeft(gCaret), gCaret);
        else if (gCaret > 0) {
            if (gTyping && gUndoN > 0 && gUndo[gUndoN - 1].kind == 'i'
                && gUndo[gUndoN - 1].len > 0
                && gUndo[gUndoN - 1].pos + gUndo[gUndoN - 1].len == gCaret) {
                /* backspacing over just-typed text: shrink the record */
                gUndo[gUndoN - 1].len--;
                Replace(gCaret - 1, 1, 0, 0, gCaret - 1);
            } else
                DeleteRange(gCaret - 1, gCaret);
        }
        return;
    case 0x7F:                                           /* forward delete */
        if (HasSel()) DeleteRange(SelStart(), SelEnd());
        else DeleteRange(gCaret, gCaret + 1 <= TB_Len() ? gCaret + 1 : gCaret);
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
        gGoalCol = -1;
    if (shift)
        SetSel(gAnchor, pos);
    else
        SetSel(pos, pos);
}

/* ---- mouse ------------------------------------------------------------- */

static long HitTest(Point pt)
{
    long line, s, e, col;
    if (pt.v < kTopPad)
        line = gTop - 1;
    else
        line = gTop + (pt.v - kTopPad) / gLH;
    if (line < 0) return 0;
    if (line >= LY_Count()) return TB_Len();
    col = (pt.h - kLeft + gCW / 2) / gCW;
    if (col < 0) col = 0;
    s = LY_Start(line);
    e = LY_End(line);
    return (s + col < e) ? s + col : e;
}

void Ed_Click(Point pt, EventRecord *ev)
{
    long pos = HitTest(pt), wordA = 0, wordB = 0, p, a, b;
    Boolean word = false;
    unsigned long lastScroll = 0;
    long bottom = gWin->portRect.bottom - kBottomPad;

    gTyping = false;
    gGoalCol = -1;
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
            if (pt.v > bottom && gTop + gVis < LY_Count()) gTop++;
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

Boolean Ed_SetText(const char *text, long len)
{
    if (!TB_Replace(0, TB_Len(), text, len))
        return false;
    LY_Rebuild();
    while (gUndoN > 0) FreeRec(&gUndo[--gUndoN]);
    ClearRedo();
    gTop = 0;
    gAnchor = gCaret = 0;
    Ed_DrawAll();
    return true;
}

Boolean Ed_Init(WindowPtr w)
{
    FontInfo fi;

    gWin = w;
    SetPort(w);
    TextFont(kFontMDW);
    TextSize(kBodySize);
    if (!RealFont(kFontMDW, kBodySize)) {
        TextFont(kFontMonaco);           /* our font didn't load */
        TextSize(9);
    }
    GetFontInfo(&fi);
    gAsc = fi.ascent;
    gLH = fi.ascent + fi.descent + fi.leading;
    gCW = CharWidth('m');
    gCols = kColumnW / gCW;
    gVis = (w->portRect.bottom - kTopPad - kBottomPad) / gLH;

    if (!TB_Init(kBufferSize))
        return false;
    return LY_Init(gCols);
}
