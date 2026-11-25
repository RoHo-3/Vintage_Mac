/*
 * Mac MD Write — application skeleton.
 *
 * Toolbox init, a borderless full-screen window, and an event loop that
 * works on both System 6 (with or without MultiFinder) and System 7.
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

#define kWaitNextEventTrap 0xA860
#define kUnimplementedTrap 0xA89F
#define kToolTrapType      1    /* TrapType: OSTrap = 0, ToolTrap = 1 */
#define kMonacoFontID      4

/* Retro68 makes "\p..." literals plain char*; Toolbox calls want
   const unsigned char*. */
#define PSTR(s) ((ConstStringPtr)(s))

static WindowPtr gWindow;
static Boolean   gQuit = false;
static Boolean   gHasWNE = false;

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
    r.top += GetMBarHeight();   /* menu-bar hiding comes later */

    gWindow = NewWindow(NULL, &r, PSTR("\p"), true, plainDBox,
                        (WindowPtr)-1, false, 0);
    SetPort(gWindow);
}

static void DrawCanvas(void)
{
    SetPort(gWindow);
    EraseRect(&gWindow->portRect);
    TextFont(kMonacoFontID);
    TextSize(9);
    MoveTo(46, 30);
    DrawString(PSTR("\pMac MD Write \321 skeleton build. Cmd-Q to quit."));
}

static void DoIdle(void)
{
    /* Future: autosave check (pause + change threshold), caret blink,
       chrome fade-in/out. */
}

static void HandleEvent(EventRecord *ev)
{
    switch (ev->what) {
    case keyDown:
    case autoKey: {
        char c = ev->message & charCodeMask;
        if ((ev->modifiers & cmdKey) && (c == 'q' || c == 'Q'))
            gQuit = true;
        break;
    }
    case updateEvt:
        if ((WindowPtr)ev->message == gWindow) {
            BeginUpdate(gWindow);
            DrawCanvas();
            EndUpdate(gWindow);
        }
        break;
    case osEvt:
        /* Suspend/resume: future autosave-on-suspend hook. */
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

    while (!gQuit) {
        if (gHasWNE) {
            gotEvent = WaitNextEvent(everyEvent, &ev, 15, NULL);
        } else {
            SystemTask();
            gotEvent = GetNextEvent(everyEvent, &ev);
        }
        if (gotEvent)
            HandleEvent(&ev);
        else
            DoIdle();
    }
    return 0;
}
