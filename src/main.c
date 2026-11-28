/*
 * Mac MD Write — 0.01 alpha (text engine, fonts, live Markdown).
 *
 * Toolbox setup, the full-screen writing window and the event loop.
 * Works on System 6 (with or without MultiFinder) and System 7.
 * No C standard library: everything goes through the Toolbox.
 */

#include <Files.h>
#include <Resources.h>
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
#include "links.h"

static char gLinkURL[256];

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
    "# Product Guide\r"
    "\r"
    "Mac MD Write is a quiet Markdown writer for the compact Macs. This "
    "guide is an ordinary document: edit it, keep it, or move it to the "
    "Trash.\r"
    "\r"
    "## This build adds\r"
    "\r"
    "- Links between documents: Command-click one to go there. The Book "
    "title page links to its Outline, chapters and notes.\r"
    "- The Library: inside a writing, Recent steps aside and your documents "
    "get two rows. In a folder like Chapters, New makes the next chapter.\r"
    "- Six templates: Article, Book, Essay, Journal, Script and "
    "Short Story.\r"
    "- History in the drawer (or Command-K, History): pick a past version "
    "and it opens as a copy beside your document.\r"
    "- Command-K, Focus for Minutes or Focus for Words: a goal for this "
    "sitting, shown in the footer.\r"
    "- Command-K, Insert TK marks a spot to come back to; Next TK finds it.\r"
    "- Command-K, Move to Cuts keeps a passage you cut in Cuts, beside your "
    "document.\r"
    "- Command-K, Insert Footnote adds a mark and its note at the end; "
    "Jump to Footnote goes between them.\r"
    "- Every writing is a folder in the Library: the New tile starts one, "
    "and it can grow into a project with more documents and folders.\r"
    "- Import copies a Markdown file from anywhere into a writing of its own.\r"
    "- Settings: Move Library and Back Up Library.\r"
    "- Command-K, then Launch Timing: how long this launch took.\r"
    "\r"
    "## Writing\r"
    "\r"
    "Markdown shows as you type: the markers hide and the text takes its "
    "style. Command-M shows the markers for this document, and hides them "
    "again. The arrow keys and Delete step over hidden markers.\r"
    "\r"
    "Inline: **bold** (Command-B), *italic* (Command-I), `code` "
    "(Command-E), ~~struck~~ (Shift-Command-X). A backslash keeps a "
    "character plain: \\*not italic\\*.\r"
    "\r"
    "## Paragraphs\r"
    "\r"
    "Start a line with # and a space for a heading; ## to ###### for the "
    "smaller ones (Option-Command-1 to 6; Option-Command-0 for plain text).\r"
    "\r"
    "> A quote starts with > (Shift-Command-').\r"
    "\r"
    "- A bullet starts with - (Shift-Command-8)\r"
    "  - Tab nests it, Shift-Tab brings it back\r"
    "1. A number starts with 1. (Shift-Command-7)\r"
    "- [ ] A task starts with - [ ] (Shift-Command-9); click its box\r"
    "\r"
    "Return continues a list or quote; Return on an empty item ends it. "
    "Three dashes on a line make a rule:\r"
    "\r"
    "---\r"
    "\r"
    "Three backticks on a line start a code block, and three more end it.\r"
    "\r"
    "## The pill and Command-K\r"
    "\r"
    "Select text with the mouse and a pill appears: Cut, Copy, Paste and "
    "Format. Press and hold on the page for Paste and Select All. "
    "Command-K opens the command palette: type a few letters of anything "
    "the app can do.\r"
    "\r"
    "## The drawer\r"
    "\r"
    "Command-\\ (or the three bars at the top left) opens the drawer: the "
    "writing this document is in, its Sections (headings), Settings, the "
    "Library and Quit.\r"
    "\r"
    "## The Library\r"
    "\r"
    "Command-O opens the Library: Recent, My Writings and Templates. Click "
    "a + tile once to make something new; double-click a folder to open "
    "it, or a document to write in it. Click a name in the path above "
    "the documents, or press Command-Up, to go back up. Command-N opens "
    "the Library with New picked: press Return.\r"
    "\r"
    "Templates start a piece with a little structure. Each opens with an "
    "Untitled heading: type your title over it and the writing takes that "
    "name. Fill in the short prompts or delete them, then write below the "
    "line. Book is a whole writing: a title page, an Outline, a Chapters folder "
    "and Notes for people, places and research; the title page links to "
    "each (Command-click a link to go there). Script is Fountain, the "
    "plain-text screenplay format; its [[notes]] and = lines never print. "
    "Add your own templates by putting Markdown files in the Templates "
    "folder.\r"
    "\r"
    "## Templates\r"
    "\r"
    "Each starts a new writing with a little structure. Press Command-K and "
    "type its name, or find it on the Library's Templates shelf.\r"
    "\r"
    "- **Article**: Command-K, New Article. A headline, the lede, why it "
    "matters, then the details, most important first. [How To Write Great "
    "Articles](https://owl.purdue.edu/owl/subject_specific_writing/journalism_and_journalistic_writing/the_inverted_pyramid.html)\r"
    "- **Book**: Command-K, New Book. A whole writing: a title page (type "
    "the title there and the book takes its name), an Outline, a Chapters "
    "folder and Notes for people, places and research. [How To Write Great "
    "Books](https://en.wikipedia.org/wiki/Writing_process)\r"
    "- **Essay**: Command-K, New Essay. What the reader should believe, your "
    "points, the other side, why it matters. [How To Write Great "
    "Essays](https://owl.purdue.edu/owl/general_writing/academic_writing/essay_writing/index.html)\r"
    "- **Journal**: Command-K, New Journal. Write without stopping; fix "
    "nothing. [How To Write Great Journals](https://en.wikipedia.org/wiki/Diary)\r"
    "- **Script**: Command-K, New Script. Fountain, the plain-text "
    "screenplay format. [How To Write Great Scripts](https://fountain.io/syntax/)\r"
    "- **Short Story**: Command-K, New Short Story. Who, what they want, "
    "what stands in the way, the turn, the end. [How To Write Great Short "
    "Stories](https://en.wikipedia.org/wiki/Short_story)\r"
    "\r"
    "Inside a Book: [How To Write Great Outlines](https://en.wikipedia.org/wiki/Outline_%28list%29), "
    "[Chapters](https://en.wikipedia.org/wiki/Chapter_%28books%29), "
    "[Characters](https://en.wikipedia.org/wiki/Characterization), "
    "[Places](https://en.wikipedia.org/wiki/Setting_%28narrative%29) and "
    "[Research](https://owl.purdue.edu/owl/research_and_citation/conducting_research/research_overview/index.html).\r"
    "\r"
    "## Saving\r"
    "\r"
    "A new page is named when you first pause: the writing and its first "
    "document take their name from your first line (Command-S names it "
    "yourself). After that it saves itself whenever you pause, and "
    "Command-Q saves and quits. Your last document reopens where you left "
    "off. Files are plain UTF-8 Markdown, so they round-trip with modern "
    "editors; each keeps an invisible history beside it.\r"
    "\r"
    "## Quiet chrome\r"
    "\r"
    "There is no menu bar. Pause or move the mouse and a footer appears: "
    "word count, session clock and the save dot (a ring is unsaved, "
    "dithered is saving, solid is saved). The top edge shows the title. "
    "Type and they vanish.\r"
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
    SetCursor(*GetCursor(watchCursor));  /* another disk: copying takes a while */
    if (!Files_MoveLibrary(reply.vRefNum, reply.fName)) {
        InitCursor();
        SysBeep(1);                      /* inside itself, a name taken, disk full */
        return;
    }
    InitCursor();
    {
        unsigned long clock = Chrome_ClockStart();   /* the session goes on */
        Files_CloseAll();                /* let go of the old folders */
        Files_Init();                    /* the library's new home */
        Lib_Reset();
        Doc_SetNewFolder(0);             /* no stale folder for new pages */
        if (!Doc_Reopen()) {
            Doc_SetNewFolder(0);
            Doc_New();
        }
        Chrome_SetClockStart(clock);
    }
}

/* Settings > Back Up Library...: a dated copy of the whole library in a
   folder you pick, on any disk: a snapshot, and a way back if a disk or
   the Mac fails. Writing goes on where it was. */
void App_BackupLibrary(void)
{
    SFReply reply;
    Point where;
    unsigned char name[40];
    Boolean ok;
    if (!Doc_CanQuit()) return;          /* the open document, saved first */
    Files_BackupName(name);
    SetPt(&where, 80, 60);
    SFPutFile(where, PSTR("\pBack up the library into this folder as:"), name, NULL, &reply);
    SetPort(gWindow);
    InvalRect(&gWindow->portRect);       /* the dialog drew over the page */
    if (!reply.good) return;
    SetCursor(*GetCursor(watchCursor));  /* copying can take a while */
    ok = Files_BackupLibrary(reply.vRefNum, reply.fName);
    InitCursor();
    if (!ok) SysBeep(1);                 /* inside itself, a name taken, disk full */
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
    case 'n': Lib_OpenNew(); break;      /* New, or a template */
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
                else if ((ev->modifiers & cmdKey)
                         && Link_At(Ed_PosAt(pt), gLinkURL, sizeof gLinkURL)) {
                    while (StillDown()) ;    /* Cmd-click a link: follow it */
                    Link_Follow(gLinkURL);
                } else {
                    Ed_Click(pt, ev);
                    if (Ed_ClickWasHold()) Pill_ShowAtCaret();
                    else if (Ed_HasSelection()) Pill_Show();   /* mouse selections only */
                }
            }
        }
        break;
    }
}

/* ---- launch timing: every launch is timed; Cmd-K > Launch Timing shows
   where the time went (the performance passes measure first). Not a
   modifier key at launch: on System 6, Option-opening an app asks the
   Finder to make its disk the startup disk. ---- */

enum { tStart, tToolbox, tEditor, tFiles, tTidy, tInit, tOpen, tDraw, tCount };
static unsigned long gT[tCount];
static Boolean gTiming;

static void AddTenths(unsigned char *s, const char *label, unsigned long ticks)
{
    unsigned long t = (ticks * 10 + 30) / 60;      /* tenths of a second */
    const char *c = label;
    if (s[0]) { s[++s[0]] = ','; s[++s[0]] = ' '; }
    while (*c) s[++s[0]] = (unsigned char)*c++;
    s[++s[0]] = ' ';
    if (t >= 100) s[++s[0]] = (unsigned char)('0' + t / 100 % 10);
    s[++s[0]] = (unsigned char)('0' + t / 10 % 10);
    s[++s[0]] = '.';
    s[++s[0]] = (unsigned char)('0' + t % 10);
}

/* "Launch 4.2: toolbox 0.1, editor 0.3, ..." in the title strip. */
void App_ShowLaunchTiming(void)
{
    unsigned char m[100];
    static const char *kLabel[tCount] = { "", "toolbox", "editor", "files", "journals",
                                          "setup", "open", "draw" };
    short i;
    m[0] = 0;
    AddTenths(m, "Launch", gT[tDraw] - gT[tStart]);
    m[++m[0]] = ':';
    {
        unsigned char rest[100];
        rest[0] = 0;
        for (i = 1; i < tCount; i++)
            AddTenths(rest, kLabel[i], gT[i] - gT[i - 1]);
        for (i = 1; i <= rest[0] && m[0] < 98; i++) m[++m[0]] = rest[i];
    }
    Chrome_ShowMessage(m, 10);
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
            if (gTiming) {               /* the first full draw ends the launch */
                gT[tDraw] = TickCount();
                gTiming = false;
            }
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

/* The Finder shows our icons only if the app's "has bundle" bit is set,
   and the build tools don't set it. So the app sets it on itself (once),
   clearing "inited" so the Finder reads the bundle afresh. */
static void SetOwnBundleBit(void)
{
    FCBPBRec fcb;
    HParamBlockRec pb;
    Str63 name;
    fcb.ioCompletion = NULL;
    fcb.ioNamePtr = name;
    fcb.ioVRefNum = 0;
    fcb.ioRefNum = CurResFile();         /* the app's own resource file */
    fcb.ioFCBIndx = 0;
    if (PBGetFCBInfoSync(&fcb) != noErr) return;
    pb.fileParam.ioCompletion = NULL;
    pb.fileParam.ioNamePtr = name;
    pb.fileParam.ioVRefNum = fcb.ioFCBVRefNum;
    pb.fileParam.ioFDirIndex = 0;
    pb.fileParam.ioDirID = fcb.ioFCBParID;
    if (PBHGetFInfoSync(&pb) != noErr) return;
    if (pb.fileParam.ioFlFndrInfo.fdFlags & 0x2000) return;   /* already set */
    pb.fileParam.ioFlFndrInfo.fdFlags |= 0x2000;              /* hasBundle */
    pb.fileParam.ioFlFndrInfo.fdFlags &= ~0x0100;             /* not inited */
    pb.fileParam.ioDirID = fcb.ioFCBParID;
    PBHSetFInfoSync(&pb);
}

int main(void)
{
    EventRecord ev;
    Boolean gotEvent;

    gT[tStart] = TickCount();
    InitMemory();
    InitToolbox();
    gTiming = true;
    gT[tToolbox] = TickCount();
    gHasWNE = HasWaitNextEvent();
    OpenCanvas();
    Chrome_Init(gWindow);
    if (!Ed_Init(gWindow)) {
        SysBeep(1);              /* not enough memory for the text buffer */
        return 1;
    }
    gT[tEditor] = TickCount();
    SetOwnBundleBit();           /* our icons in the Finder */
    Files_Init();                /* Writings beside the app (or where it moved) */
    Files_WrapLoose();           /* every writing is a folder */
    gT[tFiles] = TickCount();
    Files_TidyJournals();        /* journals whose document left via the Finder */
    gT[tTidy] = TickCount();
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
    gT[tInit] = TickCount();
    Ed_Hold(true);               /* no drawing while it opens: the first
                                    update draws the page once */
    if (Doc_Reopen()) {
        /* back where you left off */
    } else if (Prefs_Exist()) {
        /* no previous document: a blank page, ready to write (Cmd-O for the
           Library) */
    } else {
        /* first run: the Product Guide (+ which font is in use), saved as a
           real writing so it moves, copies and trashes like any other */
        const char *f = Ed_UsingFallbackFont() ? kFontFallback : kFontOK;
        long n1 = sizeof(kWelcome) - 1, n2 = 0, i;
        static char buf[sizeof(kWelcome) + 128];   /* not on the stack */
        while (f[n2]) n2++;
        for (i = 0; i < n1; i++) buf[i] = kWelcome[i];
        for (i = 0; i < n2; i++) buf[n1 + i] = f[i];
        Ed_SetText(buf, n1 + n2);
        Doc_SaveNewWriting(PSTR("\pProduct Guide.md"));
    }
    Ed_Hold(false);
    SetPort(gWindow);
    InvalRect(&gWindow->portRect);
    gT[tOpen] = TickCount();

    while (!gQuit) {
        if (gHasWNE) {
            gotEvent = WaitNextEvent(everyEvent, &ev,
                                     Chrome_Hovering() ? 1 : 15, NULL);
        } else {
            SystemTask();
            gotEvent = GetNextEvent(everyEvent, &ev);
        }
        if (gotEvent)
            HandleEvent(&ev);
        UpdateCursor();
        if (Lib_IsOpen())
            Lib_Idle();
        else if (Drawer_IsOpen())
            Drawer_Idle();               /* nothing may draw under a card */
        else if (Palette_IsOpen())
            ;
        else {
            Ed_Idle();
            Chrome_Idle();
            if (Chrome_TakeHoverOpen())
                Drawer_OpenByHover();
        }
        Doc_Idle();
    }
    Files_CloseAll();            /* no folder left "busy" for the Finder */
    Chrome_ShowMenuBar();
    return 0;
}
