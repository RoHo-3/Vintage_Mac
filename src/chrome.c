#include "chrome.h"
#include "textbuf.h"

#include <Fonts.h>
#include <TextUtils.h>
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
static Boolean   gShown;                 /* footer */
static Boolean   gStripShown;            /* top strip */
static Boolean   gHold;
static Boolean   gShowWords = true, gShowClock = true;   /* Settings */
                  /* the Library is up: draw nothing */
static unsigned long gStripUntil;       /* shown briefly (a document opened): hide then */
static Point     gLastMouse;
static unsigned long gClockStart, gLastKey;   /* gClockStart 0: not started */
#define Elapsed() (gClockStart ? (long)((TickCount() - gClockStart) / 60) : 0L)
static long      gShownSeconds = -1;
static long      gWords = -1;             /* -1: needs counting */
/* Focus goal (ROADMAP, with the Focus Timer): N minutes or N new words
   this session. Shown in the footer beside the count and the clock; met
   at the next pause (nothing interrupts mid-sentence), with one small
   celebration, then cleared. Words carry across documents opened since
   the goal was set: gGoalPrior holds what earlier documents added. */
static char      gGoalKind;               /* 0 none, 'm' minutes, 'w' words */
static long      gGoalTarget, gGoalBase, gGoalPrior, gLastCount;
static unsigned long gGoalStart;
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

static unsigned char gMessage[100];      /* shown in place of the title */

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
    if (gMessage[0] && TickCount() < gStripUntil) {   /* a passing note */
        UIFont(3, 9, 0);                  /* Geneva 9: long lines fit */
        w = StringWidth(gMessage);
        MoveTo((gWin->portRect.right - w) / 2, 13);
        DrawString(gMessage);
        return;
    }
    UIFont(systemFont, 12, bold);
    w = StringWidth(gTitle);
    MoveTo((gWin->portRect.right - w) / 2, 14);
    DrawString(gTitle);
}

static void DrawStatus(void);

const unsigned char *Chrome_Title(void) { return gTitle; }

void Chrome_SetStatusParts(Boolean words, Boolean clock)
{
    gShowWords = words;
    gShowClock = clock;
    if (gShown && !gHold) { SetPort(gWin); DrawStatus(); }
}

/* A click in the shown strip: true if on the hamburger (opens the drawer). */
Boolean Chrome_HamburgerClick(Point pt)
{
    return gStripShown && !gHold && pt.v < kStripH && pt.h < 26;
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

static long GoalDone(void)
{
    long d;
    if (gGoalKind == 'm')
        return (long)((TickCount() - gGoalStart) / 3600);
    if (gWords < 0) gWords = CountWords();
    d = gGoalPrior + gWords - gGoalBase;
    return d < 0 ? 0 : d;
}

/* The goal is met: say so in the strip, flash the footer once, clear it. */
static void Celebrate(void)
{
    unsigned char msg[48];
    Rect r;
    long until;
    msg[0] = 0;
    AppendStr(msg, "Goal reached: ");
    AppendNum(msg, gGoalTarget, true);
    AppendStr(msg, gGoalKind == 'w' ? " new words" : (gGoalTarget == 1 ? " minute" : " minutes"));
    gGoalKind = 0;
    SetRect(&r, 0, gWin->portRect.bottom - kFooterH,
            gWin->portRect.right, gWin->portRect.bottom);
    InvertRect(&r);
    Delay(10, &until);
    InvertRect(&r);
    DrawStatus();                        /* the footer without the goal */
    Chrome_ShowMessage(msg, 4);
}

static void DrawStatus(void)
{
    Rect r;
    Str63 s;
    long secs = Elapsed();               /* starts with the first keystroke */
    short top = gWin->portRect.bottom - kFooterH;

    if (gWords < 0)
        gWords = CountWords();
    s[0] = 0;
    if (gShowWords) {
        AppendNum(s, gWords, true);
        AppendStr(s, gWords == 1 ? " word" : " words");
        if (gShowClock) AppendStr(s, " / ");
    }
    if (gShowClock) {
        if (secs >= 3600) {
            AppendNum(s, secs / 3600, false);
            AppendStr(s, ":");
            if ((secs / 60) % 60 < 10) AppendStr(s, "0");
        }
        AppendNum(s, (secs / 60) % 60, false);
        AppendStr(s, ":");
        if (secs % 60 < 10) AppendStr(s, "0");
        AppendNum(s, secs % 60, false);
    }

    gLastCount = gWords;
    if (gGoalKind) {
        if (s[0] > 0) AppendStr(s, " / ");
        AppendNum(s, GoalDone(), true);
        AppendStr(s, " of ");
        AppendNum(s, gGoalTarget, true);
        AppendStr(s, gGoalKind == 'w' ? " new words" : " min");
    }

    SetRect(&r, 0, top, kColumnRight - 10, gWin->portRect.bottom);
    EraseRect(&r);
    UIFont(kGeneva, 9, 0);
    MoveTo(kLeft, gWin->portRect.bottom - 3);
    DrawString(s);
    gShownSeconds = secs;
    if (gGoalKind && GoalDone() >= gGoalTarget)
        Celebrate();                     /* drawn only at a pause */
}

/* ---- public ------------------------------------------------------------ */

void Chrome_Init(WindowPtr w)
{
    gWin = w;
    gClockStart = 0;
    gLastKey = TickCount();
    GetMouse(&gLastMouse);
    gTitle[0] = 0;
    AppendStr(gTitle, "Untitled");
}

void Chrome_Hold(Boolean hold)
{
    gHold = hold;
    if (hold) gShown = gStripShown = false;   /* the Library covers them */
}

void Chrome_Draw(void)
{
    if (!gWin || gHold) return;
    SetPort(gWin);
    if (gStripShown) DrawStrip();
    if (gShown) { DrawStatus(); DrawDot(); }
}

/* The footer (words / clock / save dot) appears when the writer pauses or
   moves the mouse. The top strip (hamburger, title) appears only while the
   mouse is in it: a pause alone never brings it up. */
static void ShowFooter(void)
{
#if defined(DIAG_NO_CHROME) || defined(DIAG_MINIMAL)
    return;
#endif
    if (gShown || !gWin || gHold) return;
    gShown = true;
    SetPort(gWin);
    DrawStatus();
    DrawDot();
}

static void EraseStrip(void)
{
    Rect r;
    SetPort(gWin);
    SetRect(&r, 0, 0, gWin->portRect.right, kStripH);
    EraseRect(&r);
}

static void SetStrip(Boolean on)
{
#if defined(DIAG_NO_CHROME) || defined(DIAG_MINIMAL)
    return;
#endif
    if (on == gStripShown || !gWin) return;
    gStripShown = on;
    if (gHold) return;                   /* the Library owns the screen */
    if (on) { SetPort(gWin); DrawStrip(); }
    else EraseStrip();
}

/* Cmd-S: show the footer so the save dot is seen doing its work. */
void Chrome_ShowSaveDot(void)
{
    ShowFooter();
}

/* A document just opened: show its name for a moment (~2 s). */
/* A note in the title strip for a few seconds (the Option-launch timing). */
void Chrome_ShowMessage(const unsigned char *msg, short seconds)
{
    short i;
    if (!gWin) return;
    for (i = 0; i <= msg[0] && i < 99; i++) gMessage[i] = msg[i];
    gStripUntil = TickCount() + 60L * seconds;   /* before drawing: the strip
                                                    checks it */
    gStripShown = false;
    SetStrip(true);
    GetMouse(&gLastMouse);
}

void Chrome_ShowTitleBriefly(void)
{
    if (!gWin) return;
    gStripShown = false;
    SetStrip(true);
    GetMouse(&gLastMouse);               /* don't count this as a mouse move */
    gStripUntil = TickCount() + 120;
}

/* A character was typed: the first one starts the session clock (arrows,
   Delete and Cmd-keys don't). */
void Chrome_CharTyped(void)
{
    if (gClockStart == 0)
        gClockStart = TickCount();
}

void Chrome_KeyPressed(void)
{
    Rect r;
    gLastKey = TickCount();
    if (!gWin || gHold) return;
    gStripUntil = 0;
    SetStrip(false);
    if (!gShown) return;
    gShown = false;
    SetPort(gWin);
    SetRect(&r, 0, gWin->portRect.bottom - kFooterH,
            gWin->portRect.right, gWin->portRect.bottom);
    EraseRect(&r);
}

/* Hover to open the drawer (an experiment; one commit to revert): the
   pointer resting on the hamburger for a fifth of a second opens it. Passing
   through, or flinging the mouse into the corner, doesn't. */
#define kHoverTicks 12
static unsigned long gHoverSince;         /* 0: not over the hamburger */
static Boolean gHoverFired, gHoverWant;

/* Resting on the hamburger, the timer is running: the event loop should
   look again soon rather than sleep a quarter second. */
Boolean Chrome_Hovering(void)
{
    return gHoverSince != 0 && !gHoverFired;
}

Boolean Chrome_TakeHoverOpen(void)
{
    Boolean w = gHoverWant;
    gHoverWant = false;
    return w;
}

void Chrome_Idle(void)
{
    Point m;
    Boolean moved;
    if (!gWin) return;
    SetPort(gWin);
    GetMouse(&m);
    moved = m.h != gLastMouse.h || m.v != gLastMouse.v;
    if (gStripShown && !gHold && m.v >= 0 && m.v < kStripH && m.h >= 0 && m.h < 26) {
        if (!gHoverSince) gHoverSince = TickCount();
        else if (!gHoverFired && TickCount() - gHoverSince >= kHoverTicks
                 && !Button()) {         /* (a press is the click's to handle) */
            gHoverFired = true;
            gHoverWant = true;
        }
    } else {
        gHoverSince = 0;
        gHoverFired = false;
    }
    if (gStripUntil && TickCount() >= gStripUntil) {
        gStripUntil = 0;                 /* the title has been seen */
        gMessage[0] = 0;                 /* and any passing note */
        if (!(m.v >= 0 && m.v < kStripH)) SetStrip(false);
    }
    if (moved) {
        gLastMouse = m;
        ShowFooter();
        if (!gStripUntil) SetStrip(m.v >= 0 && m.v < kStripH);
    } else if (TickCount() - gLastKey >= kPauseTicks)
        ShowFooter();                    /* the writer paused */
    if (gShown && Elapsed() != gShownSeconds) {
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
    if (gStripShown && !gHold) { SetPort(gWin); DrawStrip(); }
}

void Chrome_SetSaveState(short state)
{
    if (state == gSaveState) return;
    gSaveState = state;
    if (gShown && !gHold) { SetPort(gWin); DrawDot(); }
}

void Chrome_TextChanged(void)
{
    gWords = -1;
    if (gSaveState != kSaveUnsaved)
        Chrome_SetSaveState(kSaveUnsaved);
}

unsigned long Chrome_ClockStart(void) { return gClockStart; }
void Chrome_SetClockStart(unsigned long t) { gClockStart = t; }

void Chrome_ResetClock(void)
{
    gClockStart = 0;                     /* the next keystroke starts it */
    if (gGoalKind == 'w') {              /* another document: keep what the */
        gGoalPrior += gLastCount - gGoalBase;   /* last one added, start anew */
        gWords = CountWords();
        gGoalBase = gLastCount = gWords;
    }
}

void Chrome_SetGoal(char kind, long target)
{
    gGoalKind = target > 0 ? kind : 0;
    gGoalTarget = target;
    gGoalStart = TickCount();
    gGoalPrior = 0;
    if (gWords < 0) gWords = CountWords();
    gGoalBase = gLastCount = gWords;
    if (gShown && !gHold && gWin) { SetPort(gWin); DrawStatus(); }
}

char Chrome_GoalKind(void) { return gGoalKind; }
long Chrome_GoalTarget(void) { return gGoalTarget; }

/* A save: the text is final for now, so count it (cheap beside the
   write), keeping a word goal right if another document opens next. */
void Chrome_NoteWords(void)
{
    if (gWords < 0) gWords = CountWords();
    gLastCount = gWords;
}
