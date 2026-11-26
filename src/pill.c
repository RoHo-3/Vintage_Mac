#include "pill.h"
#include "editor.h"
#include "chrome.h"
#include "commands.h"
#include "overlay.h"

#include <Quickdraw.h>
#include <Fonts.h>
#include <Events.h>

#define kGeneva  3
#define kRowH    18
#define kPad     14                      /* item padding, both sides together */
#define kRadius  12
#define kGap     4                       /* between the selection and the pill */
#define kMaxItems 12

/* What an item does: run a command, or go to another level. */
enum { kTop, kFormat, kPara, kCaret };

typedef struct {
    const char *label;
    short cmd;           /* 0: none */
    short go;            /* level to open, or -1 */
    char  look;          /* 'b' bold, 'i' italic, 's' struck, 'x' box, 0 plain */
} PillItem;

static const PillItem kTopItems[] = {
    { "Cut", cCut, -1, 0 }, { "Copy", cCopy, -1, 0 }, { "Paste", cPaste, -1, 0 },
    { "Format \310", 0, kFormat, 0 },
};
static const PillItem kCaretItems[] = {      /* press and hold, no selection */
    { "Paste", cPaste, -1, 0 }, { "Select All", cSelectAll, -1, 0 },
};
static const PillItem kFormatItems[] = {
    { "\307", 0, kTop, 0 }, { "B", cBold, -1, 'b' }, { "I", cItalic, -1, 'i' },
    { "S", cStrike, -1, 's' }, { "Code", cCode, -1, 0 },
    { "Paragraph \310", 0, kPara, 0 },
};
static const PillItem kParaRow1[] = {
    { "H1", cH1, -1, 0 }, { "H2", cH2, -1, 0 }, { "H3", cH3, -1, 0 },
    { "H4", cH4, -1, 0 }, { "H5", cH5, -1, 0 }, { "H6", cH6, -1, 0 },
};
static const PillItem kParaRow2[] = {
    { "\245 List", cBullets, -1, 0 }, { "1. List", cNumbers, -1, 0 },
    { "Task", cTasks, -1, 'x' }, { "\322 Quote", cQuote, -1, 0 },
};
static const PillItem kBack = { "\307", 0, kFormat, 0 };

typedef struct {
    const PillItem *item;
    Rect r;
} Placed;

static WindowPtr gWin;
static Boolean   gShown;
static short     gLevel;
static Rect      gPill;                  /* the pill's outline */
static Placed    gPlaced[kMaxItems];
static short     gN;

#define N(a) ((short)(sizeof(a) / sizeof(a[0])))

static void UIFont(void)
{
    TextFont(kGeneva);
    TextSize(10);
    TextFace(0);
}

static short CW(const char *s)
{
    short n = 0;
    while (s[n]) n++;
    return TextWidth((Ptr)s, 0, n);
}

static short ItemW(const PillItem *it)
{
    short w;
    if (it->look == 'b') TextFace(bold);
    if (it->look == 'i') TextFace(italic);
    w = CW(it->label) + kPad + (it->look == 'x' ? 12 : 0);
    TextFace(0);
    return w;
}

/* Lay a row of items out from x, stretched to width w (0: natural). */
static short PlaceRow(const PillItem *items, short n, short x, short y, short w)
{
    short i, total = 0, extra, at = x;
    for (i = 0; i < n; i++) total += ItemW(&items[i]);
    extra = w > total ? w - total : 0;
    for (i = 0; i < n && gN < kMaxItems; i++) {
        short iw = ItemW(&items[i]) + (extra / n) + (i < extra % n ? 1 : 0);
        gPlaced[gN].item = &items[i];
        SetRect(&gPlaced[gN].r, at, y, at + iw, y + kRowH);
        gN++;
        at += iw;
    }
    return at - x;
}

/* Build the level's items at the origin; returns the pill's size. */
static void Layout(short *w, short *h)
{
    short i, w1 = 0, w2 = 0, bw;
    gN = 0;
    UIFont();
    if (gLevel != kPara) {
        const PillItem *items = gLevel == kTop ? kTopItems
                              : gLevel == kCaret ? kCaretItems : kFormatItems;
        short n = gLevel == kTop ? N(kTopItems)
                : gLevel == kCaret ? N(kCaretItems) : N(kFormatItems);
        *w = PlaceRow(items, n, 0, 0, 0);
        *h = kRowH;
        return;
    }
    bw = ItemW(&kBack);                  /* « spans both rows */
    for (i = 0; i < N(kParaRow1); i++) w1 += ItemW(&kParaRow1[i]);
    for (i = 0; i < N(kParaRow2); i++) w2 += ItemW(&kParaRow2[i]);
    if (w2 > w1) w1 = w2;
    gPlaced[gN].item = &kBack;
    SetRect(&gPlaced[gN].r, 0, 0, bw, 2 * kRowH);
    gN++;
    PlaceRow(kParaRow1, N(kParaRow1), bw, 0, w1);
    PlaceRow(kParaRow2, N(kParaRow2), bw, kRowH, w1);
    *w = bw + w1;
    *h = 2 * kRowH;
}

static void DrawItem(Placed *p)
{
    const PillItem *it = p->item;
    short tx = p->r.left + kPad / 2, base = p->r.top + 13, n = 0;
    while (it->label[n]) n++;
    if (it->look == 'x') {               /* the task box */
        Rect b;
        SetRect(&b, tx, base - 8, tx + 8, base);
        FrameRect(&b);
        tx += 12;
    }
    if (it->look == 'b') TextFace(bold);
    if (it->look == 'i') TextFace(italic);
    MoveTo(tx, base);
    DrawText((Ptr)it->label, 0, n);
    if (it->look == 's') {
        Point e;
        GetPen(&e);
        MoveTo(tx, base - 3);
        LineTo(e.h - 1, base - 3);
    }
    TextFace(0);
}

static void DrawPill(void)
{
    Rect s = gPill;
    short i;
    SetPort(gWin);
    UIFont();
    OffsetRect(&s, 1, 1);                /* 1 px drop shadow */
    FrameRoundRect(&s, kRadius, kRadius);
    EraseRoundRect(&gPill, kRadius, kRadius);
    FrameRoundRect(&gPill, kRadius, kRadius);
    for (i = 0; i < gN; i++) {
        Placed *p = &gPlaced[i];
        DrawItem(p);
        if (p->r.right < gPill.right - 2) {          /* divider */
            MoveTo(p->r.right, p->r.top + 4);
            LineTo(p->r.right, p->r.bottom - 5);
        }
    }
    if (gLevel == kPara) {               /* between the two rows */
        MoveTo(gPlaced[1].r.left + 4, gPill.top + kRowH);
        LineTo(gPill.right - 5, gPill.top + kRowH);
    }
}

static void HitRect(Placed *p, Rect *r)
{
    *r = p->r;
    InsetRect(r, 1, 2);
    if (r->left <= gPill.left + 2) r->left += 4;     /* stay inside the curve */
    if (r->right >= gPill.right - 2) r->right -= 4;
}

/* Place the level over the selection and draw it. */
static void OpenLevel(short level)
{
    Rect first, last, save;
    short w, h, x, y, i, cx;
    gLevel = level;
    SetPort(gWin);
    Layout(&w, &h);
    Ed_SelectionBounds(&first, &last);
    cx = first.top == last.top ? (first.left + last.right) / 2
                               : (gWin->portRect.right) / 2;
    x = cx - w / 2;
    y = first.top - h - kGap;
    if (y < kStripH + 2)                 /* near the top: below instead */
        y = last.bottom + kGap;
    if (y + h > gWin->portRect.bottom - kFooterH - 3)
        y = gWin->portRect.bottom - kFooterH - 3 - h;
    if (x < 4) x = 4;
    if (x + w > gWin->portRect.right - 6) x = gWin->portRect.right - 6 - w;
    SetRect(&gPill, x, y, x + w, y + h);
    for (i = 0; i < gN; i++)
        OffsetRect(&gPlaced[i].r, x, y);
    save = gPill;
    save.right += 2;
    save.bottom += 2;
    Under_Save(gWin, &save);
    gShown = true;
    DrawPill();
}

void Pill_Init(WindowPtr w) { gWin = w; }

void Pill_Show(void)
{
    if (gShown) Pill_Hide();
    if (!Ed_HasSelection()) return;
    OpenLevel(kTop);
}

void Pill_Hide(void)
{
    if (!gShown) return;
    gShown = false;
    Under_Restore();
}

void Pill_ShowAtCaret(void)
{
    if (gShown) Pill_Hide();
    OpenLevel(kCaret);
}

Boolean Pill_IsShown(void) { return gShown; }

void Pill_Redraw(void)
{
    if (!gShown) return;
    Under_Forget();
    gShown = false;
    OpenLevel(gLevel);
}

Boolean Pill_Click(Point pt)
{
    short i, hit = -1;
    Rect r;
    Boolean in = false, was;
    if (!gShown || !PtInRect(pt, &gPill)) return false;
    for (i = 0; i < gN; i++)
        if (PtInRect(pt, &gPlaced[i].r)) hit = i;
    if (hit < 0) return true;
    HitRect(&gPlaced[hit], &r);
    SetPort(gWin);
    InvertRect(&r);
    in = true;
    while (StillDown()) {                /* classic button tracking */
        GetMouse(&pt);
        was = in;
        in = PtInRect(pt, &gPlaced[hit].r);
        if (in != was) InvertRect(&r);
    }
    if (!in) return true;
    InvertRect(&r);
    {
        const PillItem *it = gPlaced[hit].item;
        if (it->go >= 0) {               /* another level, in the same place */
            Pill_Hide();
            OpenLevel(it->go);
        } else if (it->cmd) {
            Pill_Hide();                 /* the page shows again, then changes */
            Cmd_Run(it->cmd);
            if (it->cmd == cSelectAll)   /* as on the iPhone: now act on it */
                Pill_Show();
        }
    }
    return true;
}
