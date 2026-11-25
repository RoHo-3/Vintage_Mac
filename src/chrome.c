#include "chrome.h"
#include "textbuf.h"

#include <Fonts.h>
#include <Menus.h>
#include <Events.h>
#include <OSUtils.h>

#define kPauseTicks   120                /* 2 s without typing */
#define kGeneva       3
#define kLeft         46
#define kColumnRight  (46 + 420)

/* low-memory globals for hiding the menu bar */
#define LM_MBarHeight (*(volatile short *)0x0BAA)
#define LM_GrayRgn    (*(RgnHandle *)0x09EE)

static WindowPtr gWin;
static Boolean   gShown;
static Point     gLastMouse;
static unsigned long gClockStart, gLastKey;
static long      gShownSeconds = -1;
static long      gWords = -1;             /* -1: needs counting */
static short     gSaveState = kSaveUnsaved;
static Str63     gTitle;

static short     gSavedMBarHeight;
static RgnHandle gMBarRgn;
static Boolean   gMenuHidden;

/* ---- menu bar ---------------------------------------------------------- */

void Chrome_HideMenuBar(void)
{
    Rect r;
#if defined(DIAG_NO_MENUHIDE) || defined(DIAG_MINIMAL)
    return;
#endif
    if (gMenuHidden) return;
    gSavedMBarHeight = LM_MBarHeight;
    if (!gMBarRgn) gMBarRgn = NewRgn();
    r = qd.screenBits.bounds;
    r.bottom = r.top + gSavedMBarHeight;
    RectRgn(gMBarRgn, &r);
    UnionRgn(LM_GrayRgn, gMBarRgn, LM_GrayRgn);   /* windows may draw there */
    LM_MBarHeight = 0;
    gMenuHidden = true;
}

void Chrome_ShowMenuBar(void)
{
    if (!gMenuHidden) return;
    LM_MBarHeight = gSavedMBarHeight;
    DiffRgn(LM_GrayRgn, gMBarRgn, LM_GrayRgn);
    gMenuHidden = false;
    DrawMenuBar();
}

/* ---- pieces ------------------------------------------------------------ */

static long CountWords(void)
{
    long len = TB_Len(), p, n = 0;
    Boolean in = false;
    unsigned char c;
    for (p = 0; p < len; p++) {
        c = TB_Char(p);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c >= 0x80 || c == '\'') {
            if (!in) { n++; in = true; }
        } else
            in = false;
    }
    return n;
}

/* Append decimal n to a Pascal string, with thousands separators. */
static void AppendNum(unsigned char *s, long n, Boolean commas)
{
    char d[12];
    short k = 0, i;
    do { d[k++] = (char)('0' + n % 10); n /= 10; } while (n);
    for (i = k - 1; i >= 0; i--) {
        s[++s[0]] = (unsigned char)d[i];
        if (commas && i > 0 && i % 3 == 0)
            s[++s[0]] = ',';
    }
}

static void AppendStr(unsigned char *s, const char *t)
{
    while (*t) s[++s[0]] = (unsigned char)*t++;
}

static void UIFont(short font, short size, Style face)
{
    TextFont(font);
    TextSize(size);
    TextFace(face);
}

static void DrawStrip(void)
{
    Rect r;
    short i, w;
    SetRect(&r, 0, 0, gWin->portRect.right, kStripH);
    EraseRect(&r);
    MoveTo(0, kStripH - 1);
    LineTo(gWin->portRect.right, kStripH - 1);
    for (i = 0; i < 3; i++) {            /* hamburger: three 12x2 bars */
        SetRect(&r, 8, 5 + i * 4, 20, 7 + i * 4);
        PaintRect(&r);
    }
    UIFont(systemFont, 12, bold);
    w = StringWidth(gTitle);
    MoveTo((gWin->portRect.right - w) / 2, 14);
    DrawString(gTitle);
}

static void DrawDot(void)
{
    Rect r;
    short x = kColumnRight - 6, y = gWin->portRect.bottom - kFooterH + 3;
    SetRect(&r, x - 1, y - 1, x + 7, y + 7);
    EraseRect(&r);
    SetRect(&r, x, y, x + 6, y + 6);
    if (gSaveState == kSaveSaved)
        PaintOval(&r);
    else if (gSaveState == kSaveSaving) {
        FillOval(&r, &qd.gray);          /* dithered: saving */
        FrameOval(&r);
    } else
        FrameOval(&r);                   /* empty ring: unsaved changes */
}

static void DrawStatus(void)
{
    Rect r;
    Str63 s;
    long secs = (TickCount() - gClockStart) / 60;
    short top = gWin->portRect.bottom - kFooterH;

    if (gWords < 0)
        gWords = CountWords();
    s[0] = 0;
    AppendNum(s, gWords, true);
    AppendStr(s, gWords == 1 ? " word / " : " words / ");
    if (secs >= 3600) {
        AppendNum(s, secs / 3600, false);
        AppendStr(s, ":");
        if ((secs / 60) % 60 < 10) AppendStr(s, "0");
    }
    AppendNum(s, (secs / 60) % 60, false);
    AppendStr(s, ":");
    if (secs % 60 < 10) AppendStr(s, "0");
    AppendNum(s, secs % 60, false);

    SetRect(&r, 0, top, kColumnRight - 10, gWin->portRect.bottom);
    EraseRect(&r);
    UIFont(kGeneva, 9, 0);
    MoveTo(kLeft, gWin->portRect.bottom - 3);
    DrawString(s);
    gShownSeconds = secs;
}

/* ---- public ------------------------------------------------------------ */

void Chrome_Init(WindowPtr w)
{
    gWin = w;
    gClockStart = gLastKey = TickCount();
    GetMouse(&gLastMouse);
    gTitle[0] = 0;
    AppendStr(gTitle, "Untitled");
}

void Chrome_Draw(void)
{
    if (!gShown || !gWin) return;
    SetPort(gWin);
    DrawStrip();
    DrawStatus();
    DrawDot();
}

static void Show(void)
{
#if defined(DIAG_NO_CHROME) || defined(DIAG_MINIMAL)
    return;
#endif
    if (gShown || !gWin) return;
    gShown = true;
    Chrome_Draw();
}

void Chrome_KeyPressed(void)
{
    Rect r;
    gLastKey = TickCount();
    if (!gShown || !gWin) return;
    gShown = false;
    SetPort(gWin);
    SetRect(&r, 0, 0, gWin->portRect.right, kStripH);
    EraseRect(&r);
    SetRect(&r, 0, gWin->portRect.bottom - kFooterH,
            gWin->portRect.right, gWin->portRect.bottom);
    EraseRect(&r);
}

void Chrome_Idle(void)
{
    Point m;
    if (!gWin) return;
    GetMouse(&m);
    if (m.h != gLastMouse.h || m.v != gLastMouse.v) {
        gLastMouse = m;
        Show();                          /* the mouse moved */
    } else if (TickCount() - gLastKey >= kPauseTicks)
        Show();                          /* the writer paused */
    if (gShown && (long)((TickCount() - gClockStart) / 60) != gShownSeconds) {
        SetPort(gWin);                   /* tick the clock: one small redraw */
        DrawStatus();
    }
}

void Chrome_SetTitle(const unsigned char *t)
{
    short i, n = t[0];
    if (n > 3 && t[n - 2] == '.' && t[n - 1] == 'm' && t[n] == 'd')
        n -= 3;                          /* "Day Two.md" → "Day Two" */
    if (n > 63) n = 63;                  /* gTitle is a Str63 */
    for (i = 1; i <= n; i++) gTitle[i] = t[i];
    gTitle[0] = (unsigned char)n;
    if (gShown) { SetPort(gWin); DrawStrip(); }
}

void Chrome_SetSaveState(short state)
{
    if (state == gSaveState) return;
    gSaveState = state;
    if (gShown) { SetPort(gWin); DrawDot(); }
}

void Chrome_TextChanged(void)
{
    gWords = -1;
    if (gSaveState != kSaveUnsaved)
        Chrome_SetSaveState(kSaveUnsaved);
}

void Chrome_ResetClock(void)
{
    gClockStart = TickCount();
}
