/*
 * Mac MD Write — 0.01 alpha (text engine, fonts, live Markdown).
 *
 * Toolbox setup, the full-screen writing window and the event loop.
 * Works on System 6 (with or without MultiFinder) and System 7.
 * No C standard library: everything goes through the Toolbox.
 */

#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <OSUtils.h>
#include <ToolUtils.h>

#include "editor.h"

#define kWaitNextEventTrap 0xA860
#define kUnimplementedTrap 0xA89F
#define kToolTrapType      1    /* TrapType: OSTrap = 0, ToolTrap = 1 */
#define kIBeamCursor       1
#define kLeftEdge          46
#define kTextRight         (46 + 420)

/* Retro68 makes "\p..." literals plain char*; Toolbox calls want
   const unsigned char*. */
#define PSTR(s) ((ConstStringPtr)(s))

static WindowPtr gWindow;
static Boolean   gQuit = false;
static Boolean   gHasWNE = false;
static Boolean   gIBeam = false;

static const char kWelcome[] =
    "# Mac MD Write 0.01 alpha\r"
    "\r"
    "This build adds **live Markdown**: headings at their own sizes, "
    "**bold**, *italic* and `code`. Click into a paragraph and its markers "
    "appear; move away and they hide. A heading's # signs hang in the left "
    "margin so the text never shifts.\r"
    "\r"
    "## Things to try\r"
    "\r"
    "Start a new line with # and a space to make a heading. Select with the "
    "mouse or Shift-arrows; double-click selects a word. Command-Z and "
    "Shift-Command-Z undo and redo. Cut, Copy and Paste use the Mac "
    "clipboard.\r"
    "\r"
    "### Heading three\r"
    "#### Heading four\r"
    "##### Heading five\r"
    "###### Heading six\r"
    "\r"
    "Command-Q quits. There is no saving yet: this build forgets your text "
    "when it quits.\r"
    "\r"
    "Font: ";
static const char kFontOK[] = "MDW Mono 10 (bitmap IBM Plex Mono).";
static const char kFontFallback[] = "Monaco 9 - MDW Mono did not load.";

static void InitToolbox(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    FlushEvents(everyEvent, 0);
    InitCursor();
}

static Boolean HasWaitNextEvent(void)
{
    return NGetTrapAddress(kWaitNextEventTrap, kToolTrapType)
        != NGetTrapAddress(kUnimplementedTrap, kToolTrapType);
}

static void OpenCanvas(void)
{
    Rect r = qd.screenBits.bounds;
    r.top += GetMBarHeight();   /* menu-bar hiding comes in a later alpha */

    gWindow = NewWindow(NULL, &r, PSTR("\p"), true, plainDBox,
                        (WindowPtr)-1, false, 0);
    SetPort(gWindow);
}

static void UpdateCursor(void)
{
    Point pt;
    Boolean inText;
    GetMouse(&pt);
    inText = pt.h >= kLeftEdge - 4 && pt.h <= kTextRight + 4
          && pt.v >= 0 && pt.v < gWindow->portRect.bottom;
    if (inText && !gIBeam) {
        CursHandle h = GetCursor(kIBeamCursor);
        if (h) SetCursor(*h);
        gIBeam = true;
    } else if (!inText && gIBeam) {
        InitCursor();
        gIBeam = false;
    }
}

static void DoCommandKey(EventRecord *ev)
{
    char c = ev->message & charCodeMask;
    Boolean shift = (ev->modifiers & shiftKey) != 0;

    if (c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
    switch (c) {
    case 'q': gQuit = true; break;
    case 'z': if (shift) Ed_Redo(); else Ed_Undo(); break;
    case 'x': Ed_Cut(); break;
    case 'c': Ed_Copy(); break;
    case 'v': Ed_Paste(); break;
    case 'a': Ed_SelectAll(); break;
    default:  Ed_Key(ev); break;     /* Command-arrows, Command-Delete */
    }
}

static void DoMouseDown(EventRecord *ev)
{
    WindowPtr w;
    short part = FindWindow(ev->where, &w);
    Point pt;

    switch (part) {
    case inMenuBar:
        MenuSelect(ev->where);
        HiliteMenu(0);
        break;
    case inSysWindow:
        SystemClick(ev, w);
        break;
    case inContent:
        if (w == gWindow) {
            SetPort(gWindow);
            pt = ev->where;
            GlobalToLocal(&pt);
            Ed_Click(pt, ev);
        }
        break;
    }
}

static void HandleEvent(EventRecord *ev)
{
    switch (ev->what) {
    case mouseDown:
        DoMouseDown(ev);
        break;
    case keyDown:
    case autoKey:
        ObscureCursor();
        if (ev->modifiers & cmdKey)
            DoCommandKey(ev);
        else
            Ed_Key(ev);
        break;
    case updateEvt:
        if ((WindowPtr)ev->message == gWindow) {
            BeginUpdate(gWindow);
            Ed_DrawAll();
            EndUpdate(gWindow);
        }
        break;
    case activateEvt:
        if ((WindowPtr)ev->message == gWindow)
            Ed_Activate((ev->modifiers & activeFlag) != 0);
        break;
    case osEvt:
        /* suspend / resume under MultiFinder: bit 0 of message = resume */
        if (((ev->message >> 24) & 0xFF) == 1)
            Ed_Activate((ev->message & 1) != 0);
        break;
    }
}

int main(void)
{
    EventRecord ev;
    Boolean gotEvent;

    InitToolbox();
    gHasWNE = HasWaitNextEvent();
    OpenCanvas();
    if (!Ed_Init(gWindow)) {
        SysBeep(1);              /* not enough memory for the text buffer */
        return 1;
    }
    {
        /* welcome text + which font is in use */
        const char *f = Ed_UsingFallbackFont() ? kFontFallback : kFontOK;
        long n1 = sizeof(kWelcome) - 1, n2 = 0, i;
        char buf[sizeof(kWelcome) + 64];
        while (f[n2]) n2++;
        for (i = 0; i < n1; i++) buf[i] = kWelcome[i];
        for (i = 0; i < n2; i++) buf[n1 + i] = f[i];
        Ed_SetText(buf, n1 + n2);
    }

    while (!gQuit) {
        if (gHasWNE) {
            gotEvent = WaitNextEvent(everyEvent, &ev, 15, NULL);
        } else {
            SystemTask();
            gotEvent = GetNextEvent(everyEvent, &ev);
        }
        if (gotEvent)
            HandleEvent(&ev);
        UpdateCursor();
        Ed_Idle();
    }
    return 0;
}
