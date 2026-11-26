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
#include <StandardFile.h>

#include "editor.h"
#include "files.h"
#include "library.h"
#include "drawer.h"
#include "app.h"
#include "document.h"
#include "chrome.h"
#include "pill.h"
#include "palette.h"
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
    "Command-O opens the Library: templates, My Writings and recent files. "
    "Every writing is a folder: the + tile (or Command-N, then Return) "
    "starts one on a blank page. Name it with Command-S, or just write: "
    "when you pause, the writing and its first document take their name "
    "from your first line. Double-click a writing to see inside, a "
    "document to write in it. Import copies a Markdown file from anywhere "
    "into a writing of its own. Quit and relaunch: your last "
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
    inText = !Lib_IsOpen() && !Drawer_IsOpen() && pt.h >= kLeftEdge - 4 && pt.h <= kTextRight + 4
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

/* ---- fonts (whole app; the strip's pill until the Settings blade) ---- */

/* MDW Mono, then the Mac's own families (IDs fixed since the 128K Mac;
   missing ones are skipped). */
static const short kFontIDs[] = { 0, 4, 22, 2, 3, 20, 21, 16 };
static const char *kFontNames[] = { "MDW Mono", "Monaco", "Courier", "New York",
                                    "Geneva", "Times", "Helvetica", "Palatino" };
#define kFontCount 8

void App_FontName(unsigned char *name)
{
    short i, k = 0, id = Ed_FontID();
    const char *c;
    for (i = 0; i < kFontCount; i++)
        if (kFontIDs[i] == id) k = i;
    if (id == 0 && Ed_UsingFallbackFont()) k = 1;   /* MDW missing: Monaco */
    for (c = kFontNames[k], i = 0; *c; c++) name[++i] = (unsigned char)*c;
    name[0] = (unsigned char)i;
}


static void SetFontChoice(short id, short body)
{
    Prefs p;
    Ed_SetFontChoice(id, body);
    Prefs_Load(&p);                      /* remembered for the next launch */
    p.fontID = Ed_FontID();
    p.bodySize = Ed_BodySize();
    Prefs_Save(&p);
}

void App_SetStatusParts(Boolean words, Boolean clock)
{
    Prefs p;
    Chrome_SetStatusParts(words, clock);
    Prefs_Load(&p);
    p.hide = (short)((words ? 0 : 1) | (clock ? 0 : 2));
    Prefs_Save(&p);
}

/* Settings > Move Library...: the classic Save dialog picks the folder
   (and the name) the whole library moves into; the open document is saved
   first and reopened from its new place. */
void App_MoveLibrary(void)
{
    SFReply reply;
    Point where;
    short wd;
    unsigned char name[64];
    if (!Doc_CanQuit()) return;          /* saved, and remembered */
    if (!Files_LibraryName(name)) return;
    if (Files_LibraryParentWD(&wd)) Files_SetDialogFolder(wd);
    SetPt(&where, 80, 60);
    SFPutFile(where, PSTR("\pMove the library into this folder as:"), name, NULL, &reply);
    SetPort(gWindow);
    InvalRect(&gWindow->portRect);       /* the dialog drew over the page */
    if (!reply.good) return;
    if (!Files_MoveLibrary(reply.vRefNum, reply.fName)) {
        SysBeep(1);                      /* inside itself, a name taken, disk full */
        return;
    }
    Files_Init();                        /* the library's new home */
    Lib_Reset();
    if (!Doc_Reopen()) {
        Doc_SetNewFolder(0);
        Doc_New();
    }
}

void App_Quit(void)
{
    if (Doc_CanQuit()) gQuit = true;
}

void App_CycleFont(void)
{
    short i, k = 0, id = Ed_FontID();
    for (i = 0; i < kFontCount; i++)
        if (kFontIDs[i] == id) k = i;
    for (i = 1; i <= kFontCount; i++) {  /* the next one this Mac has */
        short next = kFontIDs[(k + i) % kFontCount];
        if (Ed_FontAvailable(next)) {
            SetFontChoice(next, Ed_BodySize());
            return;
        }
    }
}

void App_StepSize(short dir)
{
    SetFontChoice(Ed_FontID(), Ed_StepBodySize(dir));
}

static void DoCommandKey(EventRecord *ev)
{
    char c = ev->message & charCodeMask;
    Boolean shift = (ev->modifiers & shiftKey) != 0;

    if (c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
    if (Palette_IsOpen()) {              /* palette: Cmd-K closes, Q quits */
        if (c == 'k') Palette_Close();
        else if (c == 'q') { Palette_Close(); App_Quit(); }
        return;
    }
    Pill_Hide();                         /* any command puts the pill away */
    if (c == 'k' && !Lib_IsOpen()) {     /* Cmd-K: the command palette */
        if (Drawer_IsOpen()) Drawer_Close();   /* its update redraws the page,
                                                  then the card over it */
        Palette_Open();
        return;
    }
    if (Drawer_IsOpen()) {               /* drawer: Cmd-\ closes; Q, O, N work */
        if (c == 'q') App_Quit();
        else if (c == 'o') { Drawer_Close(); Lib_Open(); }
        else if (c == 'n') { Drawer_Close(); Lib_OpenNew(); }
        else if (c == '\\') Drawer_Close();
        return;
    }
    if (Lib_IsOpen()) {                  /* Library: its own keys, plus Q and N */
        if (c == 'q') { if (Doc_CanQuit()) gQuit = true; }
        else if (c == 'n') Lib_OpenNew();
        else Lib_Key(ev);
        return;
    }
    if ((ev->modifiers & optionKey) && ((ev->message & keyCodeMask) >> 8) == 0x03) {
        App_CycleFont();                     /* Cmd-Option-F (temporary) */
        return;
    }
    {   /* paragraph formats by key position (Shift and Option change the
           character): Cmd-Opt-0..6 normal/headings, Cmd-Shift-8 bullets,
           -7 numbers, -9 tasks, -' quote */
        short code = (short)((ev->message & keyCodeMask) >> 8);
        static const unsigned char kDigit[7] = { 0x1D, 0x12, 0x13, 0x14, 0x15, 0x17, 0x16 };
        short i;
        if (ev->modifiers & optionKey)
            for (i = 0; i < 7; i++)
                if (code == kDigit[i]) { Ed_BlockFormat(i); return; }
        if (shift) {
            if (code == 0x1C) { Ed_BlockFormat('b'); return; }
            if (code == 0x1A) { Ed_BlockFormat('n'); return; }
            if (code == 0x19) { Ed_BlockFormat('t'); return; }
            if (code == 0x27) { Ed_BlockFormat('q'); return; }
        }
    }
    switch (c) {
    case 'q': if (Doc_CanQuit()) gQuit = true; break;
    case 'n': Lib_OpenNew(); break;      /* pick Blank or a template */
    case 'o': Lib_Open(); break;
    case 's': Doc_SaveCommand(); break;
    case 'm': Doc_ToggleMarks(); break;
    case 'b': Ed_Format('*', 2); break;           /* bold */
    case 'i': Ed_Format('*', 1); break;           /* italic */
    case 'e': Ed_Format('`', 1); break;           /* inline code */
    case '=': case '+': App_StepSize(1); break;     /* text size */
    case '-': App_StepSize(-1); break;
    case '\\': Drawer_Open(); break;                 /* Cmd-\ : the drawer */
    case 'z': if (shift) Ed_Redo(); else Ed_Undo(); break;
    case 'x': if (shift) Ed_Format('~', 2); else Ed_Cut(); break;   /* Cmd-Shift-X: strike */
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
            else if (Drawer_IsOpen()) Drawer_Click(pt, ev);
            else if (Palette_IsOpen()) Palette_Click(pt);
            else if (Pill_Click(pt)) ;   /* the pill's own button */
            else {
                Pill_Hide();
                if (Chrome_HamburgerClick(pt)) Drawer_Open();
                else {
                    Ed_Click(pt, ev);
                    if (Ed_ClickWasHold()) Pill_ShowAtCaret();
                    else if (Ed_HasSelection()) Pill_Show();   /* mouse selections only */
                }
            }
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
        else if (Drawer_IsOpen())
            Drawer_Key(ev);
        else if (Palette_IsOpen())
            Palette_Key(ev);
        else {
            unsigned char ch = (unsigned char)(ev->message & charCodeMask);
            if (Pill_IsShown()) {        /* typing puts it away; Esc only that */
                Pill_Hide();
                if (ch == 0x1B) break;
            }
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
            else if (Drawer_IsOpen())
                Drawer_Draw();
            else {
                Ed_DrawAll();
                Chrome_Draw();
                Pill_Redraw();           /* a card floats over the page again */
                Palette_Redraw();
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
                Pill_Hide();
                Palette_Close();
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
    Files_Init();                /* Writings beside the app (or where it moved) */
    Files_WrapLoose();           /* every writing is a folder */
    Files_TidyJournals();        /* journals whose document left via the Finder */
    {
        Prefs p;                 /* the font and size chosen last time */
        Prefs_Load(&p);
        if (p.bodySize > 0)
            Ed_SetFontChoice(p.fontID, p.bodySize);
        Chrome_SetStatusParts(!(p.hide & 1), !(p.hide & 2));
    }
    Doc_Init();
    Lib_Init(gWindow);
    Drawer_Init(gWindow);
    Pill_Init(gWindow);
    Palette_Init(gWindow);
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
        else if (Drawer_IsOpen() || Palette_IsOpen())
            ;                            /* nothing may draw under a card */
        else {
            Ed_Idle();
            Chrome_Idle();
        }
        Doc_Idle();
    }
    Chrome_ShowMenuBar();
    return 0;
}
