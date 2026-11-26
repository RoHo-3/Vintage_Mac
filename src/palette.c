#include "palette.h"
#include "commands.h"
#include "overlay.h"

#include <Quickdraw.h>
#include <Fonts.h>
#include <Events.h>
#include <OSUtils.h>

#define kGeneva  3
#define kW       260
#define kH       170
#define kFieldH  22
#define kRowH    14
#define kFootH   14
#define kRows    ((kH - kFieldH - kFootH - 4) / kRowH)
#define kMaxQ    40
#define kMaxHits 64

static WindowPtr gWin;
static Boolean   gOpen;
static Rect      gCard;
static char      gQuery[kMaxQ];
static short     gQLen;
static short     gHits[kMaxHits];        /* indexes into the table */
static short     gHitN, gSel, gTop;

static char Lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* Is the word q[0..n) inside s (case ignored)? */
static Boolean Contains(const char *s, const char *q, short n)
{
    short i, j;
    for (i = 0; s[i]; i++) {
        for (j = 0; j < n && s[i + j] && Lower(s[i + j]) == Lower(q[j]); j++)
            ;
        if (j == n) return true;
    }
    return false;
}

/* Every word of the query is in the name or the search words. Returns 2
   when they are all in the name (those come first), 1, or 0. */
static short Match(const Command *c)
{
    short i = 0, a, best = 2;
    while (i < gQLen) {
        while (i < gQLen && gQuery[i] == ' ') i++;
        a = i;
        while (i < gQLen && gQuery[i] != ' ') i++;
        if (i == a) break;
        if (Contains(Cmd_Label(c), gQuery + a, i - a)) continue;
        if (Contains(c->name, gQuery + a, i - a) || Contains(c->keys, gQuery + a, i - a)
            || Contains(c->group, gQuery + a, i - a))
            best = 1;
        else
            return 0;
    }
    return best;
}

static void Filter(void)
{
    short n, i, pass;
    const Command *t = Cmd_Table(&n);
    gHitN = 0;
    for (pass = 2; pass >= 1; pass--)
        for (i = 0; i < n && gHitN < kMaxHits; i++)
            if (Cmd_Available(t[i].id) && Match(&t[i]) == pass)
                gHits[gHitN++] = i;
    gSel = gTop = 0;
}

static void Str(const char *s, short x, short y)
{
    short n = 0;
    while (s[n]) n++;
    MoveTo(x, y);
    DrawText(s, 0, n);
}

static short SW(const char *s)
{
    short n = 0;
    while (s[n]) n++;
    return TextWidth(s, 0, n);
}

static void DrawRows(void)
{
    short i, n, y = gCard.top + kFieldH + 2;
    const Command *t = Cmd_Table(&n);
    Rect r;
    SetRect(&r, gCard.left + 1, y, gCard.right - 1, y + kRows * kRowH);
    EraseRect(&r);
    TextFont(kGeneva);
    for (i = gTop; i < gHitN && i < gTop + kRows; i++, y += kRowH) {
        const Command *c = &t[gHits[i]];
        TextSize(10);
        Str(Cmd_Label(c), gCard.left + 10, y + 11);
        if (c->shortcut[0]) {
            TextSize(9);
            Str(c->shortcut, gCard.right - 10 - SW(c->shortcut), y + 11);
        }
        if (i == gSel) {
            SetRect(&r, gCard.left + 2, y, gCard.right - 2, y + kRowH);
            InvertRect(&r);
        }
    }
    if (gHitN == 0) {
        TextSize(10);
        Str("No command matches.", gCard.left + 10, y + 11);
    }
}

static void DrawField(void)
{
    Rect r;
    SetRect(&r, gCard.left + 1, gCard.top + 1, gCard.right - 1, gCard.top + kFieldH - 1);
    EraseRect(&r);
    TextFont(kGeneva);
    TextSize(10);
    TextFace(0);
    MoveTo(gCard.left + 10, gCard.top + 15);
    if (gQLen == 0) {
        Str("Type a command", gCard.left + 10, gCard.top + 15);
        PenPat(&qd.gray);                /* placeholder: dimmed */
        PenMode(patBic);
        PaintRect(&r);
        PenNormal();
        MoveTo(gCard.left + 9, gCard.top + 5);       /* the insertion point */
        LineTo(gCard.left + 9, gCard.top + 16);
    } else {
        Point e;
        DrawText(gQuery, 0, gQLen);
        GetPen(&e);
        MoveTo(e.h + 1, gCard.top + 5);
        LineTo(e.h + 1, gCard.top + 16);
    }
}

static void DrawAll(void)
{
    Rect s = gCard;
    SetPort(gWin);
    OffsetRect(&s, 2, 2);                /* drop shadow, as the drawer's blades */
    PaintRect(&s);
    EraseRect(&gCard);
    FrameRect(&gCard);
    MoveTo(gCard.left, gCard.top + kFieldH);
    LineTo(gCard.right - 1, gCard.top + kFieldH);
    MoveTo(gCard.left, gCard.bottom - kFootH);
    LineTo(gCard.right - 1, gCard.bottom - kFootH);
    TextFont(kGeneva);
    TextSize(9);
    Str("Return runs \245 Esc closes", gCard.left + 10, gCard.bottom - 4);
    DrawField();
    DrawRows();
}

void Palette_Init(WindowPtr w) { gWin = w; }

void Palette_Open(void)
{
    Rect save;
    short x, y;
    if (gOpen) { Palette_Close(); return; }
    x = (gWin->portRect.right - kW) / 2;
    y = (gWin->portRect.bottom - kH) / 3;
    SetRect(&gCard, x, y, x + kW, y + kH);
    save = gCard;
    save.right += 2;
    save.bottom += 2;
    Under_Save(gWin, &save);
    gQLen = 0;
    Filter();
    gOpen = true;
    DrawAll();
}

void Palette_Close(void)
{
    if (!gOpen) return;
    gOpen = false;
    Under_Restore();
}

Boolean Palette_IsOpen(void) { return gOpen; }

void Palette_Redraw(void)
{
    Rect save;
    if (!gOpen) return;
    save = gCard;
    save.right += 2;
    save.bottom += 2;
    Under_Save(gWin, &save);
    DrawAll();
}

static void Run(short i)
{
    short n;
    const Command *t = Cmd_Table(&n);
    if (i < 0 || i >= gHitN) { SysBeep(1); return; }
    Palette_Close();                     /* the page is back, then the command */
    Cmd_Run(t[gHits[i]].id);
}

static void MoveSel(short d)
{
    short s = gSel + d;
    if (s < 0 || s >= gHitN) return;
    gSel = s;
    if (gSel < gTop) gTop = gSel;
    if (gSel >= gTop + kRows) gTop = gSel - kRows + 1;
    DrawRows();
}

void Palette_Key(EventRecord *ev)
{
    unsigned char c = (unsigned char)(ev->message & charCodeMask);
    switch (c) {
    case 0x1B: Palette_Close(); return;                  /* esc */
    case '\r': case 0x03: Run(gSel); return;             /* return, enter */
    case 0x1E: MoveSel(-1); return;                      /* up */
    case 0x1F: MoveSel(1); return;                       /* down */
    case 0x0B: MoveSel(-kRows); return;                  /* page up */
    case 0x0C: MoveSel(kRows); return;                   /* page down */
    case 0x08:                                           /* delete */
        if (gQLen == 0) return;
        if (ev->modifiers & (cmdKey | optionKey)) gQLen = 0; else gQLen--;
        break;
    default:
        if (c < 0x20 || c == 0x7F || gQLen >= kMaxQ - 1) return;
        gQuery[gQLen++] = (char)c;
        break;
    }
    Filter();
    SetPort(gWin);
    DrawField();
    DrawRows();
}

void Palette_Click(Point pt)
{
    short row;
    if (!PtInRect(pt, &gCard)) { Palette_Close(); return; }
    if (pt.v < gCard.top + kFieldH + 2 || pt.v >= gCard.top + kFieldH + 2 + kRows * kRowH)
        return;
    row = gTop + (pt.v - gCard.top - kFieldH - 2) / kRowH;
    if (row >= gHitN) return;
    gSel = row;
    DrawRows();
    while (StillDown()) ;
    Run(row);
}
