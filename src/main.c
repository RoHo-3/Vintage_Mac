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
#include "files.h"
#include "library.h"
#include "document.h"
#include "chrome.h"
#include "build_number.h"

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
    "**bold**, *italic* and `code`. The markers are hidden; Command-M "
    "shows them for this document (a heading's # signs hang in the left "
    "margin) and hides them again. The arrow keys and Delete step over "
    "hidden markers.\r"
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
    "## Quiet chrome\r"
    "\r"
    "The menu bar is gone. Pause or move the mouse and a footer appears: "
    "word count, session clock, and the save dot (a ring means unsaved, "
    "dithered means saving, solid means saved). Move the mouse to the top "
    "edge for the title strip. Type and they vanish.\r"
    "\r"
    "## Files\r"
    "\r"
    "Command-O opens the Library: templates, your documents and projects, "
    "and recent files. Command-N opens it with Blank picked: press Return "
    "(or pick a template), choose a folder and a name, and start writing. "
    "Command-S saves; an untitled page asks for a name first. An untitled "
    "page you never save is named from its first line when you pause, in "
    "My Documents (in the \"Writings\" folder next to the app). Quit "
    "and relaunch: your last "
    "document reopens where you left off. Once a document has a file it saves itself when you pause "
    "typing, so Command-Q saves and quits. Files stay UTF-8 with their "
    "original line endings, so they round-trip with modern editors. "
    "Characters the Mac can't show appear as a box and are kept exactly.\r"
    "\r"
    "Font: ";
#define STR2(x) #x
#define STR(x) STR2(x)
#ifdef FONT_FITTED
static const char kFontOK[] = "MDW Mono 10, grid-fitted test strikes (bitmap IBM Plex Mono). Build " STR(BUILD_NUMBER) ".";
#else
static const char kFontOK[] = "MDW Mono 10 (bitmap IBM Plex Mono; body grid-fitted). Build " STR(BUILD_NUMBER) ".";
#endif
static const char kFontFallback[] = "Monaco 9 - MDW Mono did not load. Build " STR(BUILD_NUMBER) ".";

#define kExtraStack (32L * 1024)
#define LM_ApplLimit (*(Ptr *)0x0130)

/* Classic startup: give the stack room to grow (the 68000 default is
   only ~8 KB and Toolbox calls like CopyBits and styled DrawText use a lot
   of it), then grow the heap to its limit and pre-allocate master
   pointers so handles don't fragment it. */
static void InitMemory(void)
{
    short i;
    SetApplLimit(LM_ApplLimit - kExtraStack);
    MaxApplZone();
    for (i = 0; i < 4; i++)
        MoreMasters();
}

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
    Rect r = qd.screenBits.bounds;      /* the whole screen: no menu bar */

    Chrome_HideMenuBar();
#if defined(DIAG_NO_MENUHIDE) || defined(DIAG_MINIMAL)
    r.top += GetMBarHeight();
#endif

    gWindow = NewWindow(NULL, &r, PSTR("\p"), true, plainDBox,
                        (WindowPtr)-1, false, 0);
    SetPort(gWindow);
}

static void UpdateCursor(void)
{
    Point pt;
    Boolean inText;
    GetMouse(&pt);
    inText = !Lib_IsOpen() && pt.h >= kLeftEdge - 4 && pt.h <= kTextRight + 4
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
    if (Lib_IsOpen()) {                  /* Library: its own keys, plus Q and N */
        if (c == 'q') { if (Doc_CanQuit()) gQuit = true; }
        else if (c == 'n') Lib_OpenNew();
        else Lib_Key(ev);
        return;
    }
    switch (c) {
    case 'q': if (Doc_CanQuit()) gQuit = true; break;
    case 'n': Lib_OpenNew(); break;      /* pick Blank or a template */
    case 'o': Lib_Open(); break;
    case 's': Doc_SaveCommand(); break;
    case 'm': Doc_ToggleMarks(); break;
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
    case inSysWindow:
        SystemClick(ev, w);
        break;
    case inContent:
        if (w == gWindow) {
            SetPort(gWindow);
            pt = ev->where;
            GlobalToLocal(&pt);
            if (Lib_IsOpen()) Lib_Click(pt, ev);
            else Ed_Click(pt, ev);
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
        Chrome_KeyPressed();
        if (ev->modifiers & cmdKey)
            DoCommandKey(ev);
        else if (Lib_IsOpen())
            Lib_Key(ev);
        else {
            unsigned char ch = (unsigned char)(ev->message & charCodeMask);
            if ((ch >= 0x20 && ch != 0x7F) || ch == '\r' || ch == 0x03 || ch == '\t')
                Chrome_CharTyped();      /* the session clock starts with writing */
            Ed_Key(ev);
        }
        break;
    case updateEvt:
        if ((WindowPtr)ev->message == gWindow) {
            BeginUpdate(gWindow);
            if (Lib_IsOpen())
                Lib_Draw();
            else {
                Ed_DrawAll();
                Chrome_Draw();
            }
            EndUpdate(gWindow);
        }
        break;
    case activateEvt:
        if ((WindowPtr)ev->message == gWindow)
            Ed_Activate((ev->modifiers & activeFlag) != 0);
        break;
    case osEvt:
        /* suspend / resume under MultiFinder: bit 0 of message = resume */
        if (((ev->message >> 24) & 0xFF) == 1) {
            if (!(ev->message & 1)) {
                Doc_Suspend();           /* save before another app runs */
                Chrome_ShowMenuBar();    /* other apps need the menu bar */
            } else {
                Chrome_HideMenuBar();
                SetPort(gWindow);
                InvalRect(&gWindow->portRect);
            }
            Ed_Activate((ev->message & 1) != 0);
        }
        break;
    }
}

int main(void)
{
    EventRecord ev;
    Boolean gotEvent;

    InitMemory();
    InitToolbox();
    gHasWNE = HasWaitNextEvent();
    OpenCanvas();
    Chrome_Init(gWindow);
    if (!Ed_Init(gWindow)) {
        SysBeep(1);              /* not enough memory for the text buffer */
        return 1;
    }
    Files_Init();                /* Writings beside the app */
    Files_TidyJournals();        /* journals whose document left via the Finder */
    Doc_Init();
    Lib_Init(gWindow);
    if (Doc_Reopen()) {
        /* back where you left off */
    } else if (Prefs_Exist()) {
        /* no previous document: a blank page, ready to write (Cmd-O for the
           Library) */
    } else {
        /* first run: welcome text + which font is in use */
        const char *f = Ed_UsingFallbackFont() ? kFontFallback : kFontOK;
        long n1 = sizeof(kWelcome) - 1, n2 = 0, i;
        static char buf[sizeof(kWelcome) + 128];   /* not on the stack */
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
        if (Lib_IsOpen())
            Lib_Idle();
        else {
            Ed_Idle();
            Chrome_Idle();
        }
        Doc_Idle();
    }
    Chrome_ShowMenuBar();
    return 0;
}
