/*
 * Mac MD Write — 0.01 alpha.
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
    "Mac MD Write 0.01 alpha\r"
    "\r"
    "This build tests the text engine and the MDW Mono font. Type anywhere. "
    "Lines wrap at 70 characters. Try Option-arrows to move by word, "
    "Command-arrows to jump to the start or end of a line, Shift to "
    "select, double-click to select a word, and Command-Z / Shift-Command-Z "
    "to undo and redo.\r"
    "\r"
    "Cut, Copy and Paste use the Mac clipboard. Command-A selects all. "
    "Command-Q quits. There is no saving yet: this build forgets your text "
    "when it quits.";

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
    Ed_SetText(kWelcome, sizeof(kWelcome) - 1);

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
