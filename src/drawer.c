#include "drawer.h"
#include "journal.h"
#include "editor.h"
#include "chrome.h"
#include "document.h"
#include "files.h"
#include "library.h"
#include "markdown.h"
#include "textbuf.h"
#include "app.h"
#include "commands.h"

#include <Quickdraw.h>
#include <Fonts.h>
#include <Memory.h>
#include <TextUtils.h>

#define PSTR(s) ((ConstStr255Param)(s))

/* geometry (DESIGN.md / drawer mockups) */
#define kPanelW   180
#define kStep     14                     /* each blade sits 14 px further right */
#define kHeadH    20
#define kRowsTop  24
#define kRowH     16
#define kFootH    14
#define kGeneva   3
#define kInset    6                      /* a blade floats this far from top and bottom */

static short gY;                         /* top of the panel being drawn: 0, or kInset */
#define PanelBottom() (gWin->portRect.bottom - gY)

enum { kRoot, kProject, kFolder, kSections, kSettings, kHistory, kFocus };
enum { aNone, aPush, aOpenDoc, aJump, aLibrary, aQuit,
       aFont, aSize, aMarks, aWords, aClock, aNewDoc, aMoveLib, aBackup, aNewFolder,
       aVersion, aGoal, aGoalOther, aGoalOff };

typedef struct {
    short kind;
    long  dir;                           /* project / folder blades */
    short sel, top;                      /* keyboard row, first row shown */
} Blade;

typedef struct {
    Str31   text;
    Str31   value;                       /* right side: "Cmd-O", "MDW Mono", ... */
    Str31   file;                        /* documents: the file's real name */
    short   act;
    long    arg;                         /* folder dirID / text position */
    char    arrow, check, checked, current, indent, sep;
    char    icon;                        /* 'f' folder, 'd' document, 0 none */
} Row;

#define kMaxDepth 3                      /* drawer -> project -> one folder */
#define kMaxRows  64

static WindowPtr gWin;
static Boolean   gOpen;
static Boolean   gByHover, gTouched;     /* opened by hovering; used since */
static Blade     gStack[kMaxDepth];
static short     gDepth = 1;             /* blades in the stack; [0] is the drawer */
static Row       gRows[kMaxRows];
static short     gRowN;
static Str63     gBladeTitle;
static Boolean   gWords = true, gClock = true;   /* mirrors the Settings */

/* ---- helpers ------------------------------------------------------------ */

static void CStr(const char *c, unsigned char *p)
{
    short n = 0;
    while (c[n] && n < 31) { p[n + 1] = (unsigned char)c[n]; n++; }
    p[0] = (unsigned char)n;
}

static void CStrCat(unsigned char *p, const char *c)
{
    while (*c && p[0] < 31) p[++p[0]] = (unsigned char)*c++;
}

static void CopyP(const unsigned char *s, unsigned char *d, short max)
{
    short i, n = s[0] > max ? max : s[0];
    for (i = 1; i <= n; i++) d[i] = s[i];
    d[0] = (unsigned char)n;
}

static void NoMd(unsigned char *p)       /* "Day Two.md" -> "Day Two" */
{
    short n = p[0];
    if (n > 3 && p[n - 2] == '.' && (p[n - 1] | 0x20) == 'm' && (p[n] | 0x20) == 'd')
        p[0] -= 3;
}

static Boolean SameName(const unsigned char *a, const unsigned char *b)
{
    short i;
    if (a[0] != b[0]) return false;
    for (i = 1; i <= a[0]; i++) {
        unsigned char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
    }
    return true;
}

static Row *AddRow(const char *text, short act, long arg)
{
    Row *r;
    if (gRowN >= kMaxRows) return NULL;
    r = &gRows[gRowN++];
    CStr(text, r->text);
    r->value[0] = 0;
    r->file[0] = 0;
    r->act = act;
    r->arg = arg;
    r->arrow = r->check = r->checked = r->current = r->indent = r->sep = 0;
    r->icon = 0;
    return r;
}

static Boolean ProjectOfDoc(short *vol, long *dir, long *project)
{
    return Doc_HasFile() && Doc_Location(vol, dir) && Files_ProjectOf(*vol, *dir, project);
}

/* ---- rows of each blade -------------------------------------------------- */

/* The folder holding the open document, shown open under its row, so
   the writing's list always shows where you are (Chapters > Chapter 3). */
static void BuildOpenFolder(long dir)
{
    static DirItem sub[kMaxItems];
    short vol, n, i;
    long proj;
    Row *r;
    unsigned char name[40];
    Files_ProjectsDir(&vol, &proj);
    n = Files_List(vol, dir, sub, kMaxItems);
    for (i = 0; i < n; i++) {
        if (sub[i].isFolder) continue;   /* folders go one level deep */
        r = AddRow("", aOpenDoc, dir);
        if (!r) return;
        CopyP(sub[i].name, name, 31);
        NoMd(name);
        CopyP(name, r->text, 31);
        CopyP(sub[i].name, r->file, 31);
        r->icon = 'd';
        r->indent = 1;
        if (SameName(sub[i].name, Doc_Name())) r->current = 1;
    }
}

static void BuildFolderRows(long dir, Boolean allowFolders)
{
    static DirItem items[kMaxItems];
    short vol, dvol, n, i;
    long ddir, proj;
    Row *r;
    Files_ProjectsDir(&vol, &proj);
    n = Files_List(vol, dir, items, kMaxItems);
    Doc_Location(&dvol, &ddir);
    for (i = 0; i < n; i++) {
        unsigned char name[40];
        CopyP(items[i].name, name, 31);
        NoMd(name);
        r = AddRow("", items[i].isFolder ? aPush : aOpenDoc, items[i].isFolder ? items[i].dirID : dir);
        if (!r) break;
        CopyP(name, r->text, 31);
        CopyP(items[i].name, r->file, 31);
        r->icon = items[i].isFolder ? 'f' : 'd';
        if (items[i].isFolder) {
            r->arrow = 1;
            if (!allowFolders) r->act = aNone;       /* three levels at most */
            if (Doc_HasFile() && dvol == vol && ddir == items[i].dirID)
                BuildOpenFolder(items[i].dirID);     /* the open document is in it */
        } else if (Doc_HasFile() && dvol == vol && ddir == dir
                   && SameName(items[i].name, Doc_Name()))
            r->current = 1;                          /* the open document */
    }
    if (n == 0) AddRow("(empty)", aNone, 0);
}

static void BuildSections(void)
{
    long len = TB_Len(), p = 0, pe, q;
    short level, k;
    const unsigned char *sty;
    Row *r;
    while (p < len || (p == 0 && len == 0)) {
        if (len == 0) break;
        pe = MD_ParaEnd(p);
        sty = MD_Parse(p, pe, false, &level);
        if (level > 0) {
            r = AddRow("", aJump, p);
            if (!r) break;
            for (q = p, k = 0; q < pe && k < 31; q++)
                if (!sty || !(sty[q - p] & MD_HIDDEN))
                    r->text[++k] = TB_Char(q);
            r->text[0] = (unsigned char)k;
            r->indent = (char)(level - 1);
        }
        p = pe + 1;
    }
    if (gRowN == 0) AddRow("No headings yet", aNone, 0);
}

/* History: this document's past versions, newest first (one per sitting,
   and every named moment). Picking one opens it as a new document beside
   this one; this one is never changed. */
#define kMaxVersions 60
static JRVersion gVers[kMaxVersions];

static void BuildHistory(void)
{
    short n = JR_Versions(gVers, kMaxVersions), i, k;
    Row *r;
    if (n == 0) { AddRow("No history yet", aNone, 0); return; }
    AddRow("Opens a copy beside this one", aNone, 0);
    for (i = n - 1; i >= 0; i--) {
        r = AddRow("", aVersion, i);
        if (!r) break;
        Doc_VersionStamp(gVers[i].time, r->text);
        for (k = 1; k <= r->text[0]; k++)
            if (r->text[k] == '.') r->text[k] = ':';   /* "Nov 27 16:24" */
        if (i == n - 1) CStr("Latest", r->value);
        else CopyP(gVers[i].label, r->value, 31);
    }
}

/* The goal now set, for the right side of its row: "25 min", "500 words". */
static void GoalValue(unsigned char *v)
{
    long t = Chrome_GoalTarget();
    v[0] = 0;
    if (!Chrome_GoalKind()) return;
    NumToString(t, v);
    if (Chrome_GoalKind() == 'm') CStrCat(v, " min");
    else CStrCat(v, " words");
}

static void BuildRows(void)
{
    Blade *b = &gStack[gDepth - 1];
    short vol;
    long dir, proj;
    Row *r;
    unsigned char name[40];
    gRowN = 0;
    switch (b->kind) {
    case kRoot:
        CopyP(Chrome_Title(), gBladeTitle, 63);
        if (ProjectOfDoc(&vol, &dir, &proj) && Files_FolderName(vol, proj, name)) {
            r = AddRow("", aPush, proj);
            CopyP(name, r->text, 31);
            r->arrow = 1;
            r->icon = 'f';                   /* the writing this document is in */
        }
        r = AddRow("Sections", Ed_HasHeadings() ? aPush : aNone, -kSections);
        r->arrow = 1;                        /* greyed until there are headings */
        r = AddRow("Focus", aPush, -kFocus); r->arrow = 1;
        GoalValue(r->value);                 /* "25 min" while one is set */
        r = AddRow("History", Doc_HasFile() ? aPush : aNone, -kHistory);
        r->arrow = 1;                        /* greyed until it has a file */
        r = AddRow("Settings", aPush, -kSettings); r->arrow = 1;
        r = AddRow("", aNone, 0); r->sep = 1;
        r = AddRow("Library\311", aLibrary, 0); CStr("Cmd-O", r->value);
        r = AddRow("Quit", aQuit, 0); CStr("Cmd-Q", r->value);
        break;
    case kProject:
    case kFolder:
        Files_ProjectsDir(&vol, &proj);
        if (!Files_FolderName(vol, b->dir, gBladeTitle)) gBladeTitle[0] = 0;
        BuildFolderRows(b->dir, b->kind == kProject && gDepth < kMaxDepth);
        r = AddRow("", aNone, 0); if (r) r->sep = 1;
        r = AddRow("New Document", aNewDoc, b->dir);     /* the writing grows */
        if (b->kind == kProject && gDepth < kMaxDepth)   /* one level of folders */
            AddRow("New Folder\311", aNewFolder, b->dir);
        break;
    case kSections:
        CStr("Sections", gBladeTitle);
        BuildSections();
        break;
    case kFocus:
        CStr("Focus", gBladeTitle);
        AddRow("Minutes", aNone, 0);
        AddRow("15 minutes", aGoal, 15);
        AddRow("25 minutes", aGoal, 25);
        AddRow("45 minutes", aGoal, 45);
        AddRow("60 minutes", aGoal, 60);
        AddRow("Other\311", aGoalOther, 'm');
        r = AddRow("", aNone, 0); r->sep = 1;
        AddRow("New words", aNone, 0);
        AddRow("250 words", aGoal, -250);
        AddRow("500 words", aGoal, -500);
        AddRow("1,000 words", aGoal, -1000);
        AddRow("Other\311", aGoalOther, 'w');
        if (Chrome_GoalKind()) {
            r = AddRow("", aNone, 0); r->sep = 1;
            r = AddRow("Stop This Goal", aGoalOff, 0);
            GoalValue(r->value);
        }
        break;
    case kHistory:
        CStr("History", gBladeTitle);
        BuildHistory();
        break;
    case kSettings:
        CStr("Settings", gBladeTitle);
        /* three groups under grey headings: the app, the library, this
           document (shortcuts at the right, as for Library and Quit) */
        AddRow("App", aNone, 0);
        r = AddRow("Font", aFont, 0);  App_FontName(r->value);
        r = AddRow("Size", aSize, 0);  NumToString(Ed_BodySize(), r->value);
        r = AddRow("Word count", aWords, 0);    r->check = 1; r->checked = gWords;
        r = AddRow("Session clock", aClock, 0); r->check = 1; r->checked = gClock;
        r = AddRow("", aNone, 0); r->sep = 1;
        AddRow("Library", aNone, 0);
        r = AddRow("Move Library\311", aMoveLib, 0);
        if (!Files_LibraryPlace(r->value)) r->value[0] = 0;   /* the folder it's in */
        AddRow("Back Up Library\311", aBackup, 0);
        r = AddRow("", aNone, 0); r->sep = 1;
        AddRow("This document", aNone, 0);
        r = AddRow("Show Markdown", aMarks, 0); r->check = 1; r->checked = Ed_ShowMarks();
        CStr("Cmd-M", r->value);
        break;
    }
    if (b->sel >= gRowN) b->sel = gRowN - 1;
    if (b->sel < 0) b->sel = 0;
    while (b->sel < gRowN - 1 && (gRows[b->sel].sep || gRows[b->sel].act == aNone)
           && gRows[b->sel].text[0] == 0)
        b->sel++;
    if (b->kind == kHistory && b->sel == 0 && gRowN > 1)
        b->sel = 1;                      /* past the note: the latest version */
    if (b->kind == kProject || b->kind == kFolder) {   /* start on the open doc */
        short i;
        for (i = 0; i < gRowN; i++)
            if (gRows[i].current && b->top == 0 && b->sel == 0) b->sel = i;
    }
}

/* ---- drawing ------------------------------------------------------------- */

static short VisibleRows(void)
{
    return (gWin->portRect.bottom - 2 * kInset - kRowsTop - kFootH) / kRowH;
}

static void UIFont(short font, short size, short face)
{
    TextFont(font);
    TextSize(size);
    TextFace(face);
}

static void FitTo(unsigned char *s, short w)
{
    if (StringWidth(s) <= w) return;
    while (s[0] > 1 && StringWidth(s) + CharWidth('\311') > w) s[0]--;
    s[++s[0]] = '\311';
}

static void DrawFrame(short x)
{
    Rect r;
    SetRect(&r, x, 0, x + kPanelW, gWin->portRect.bottom);
    EraseRect(&r);
    MoveTo(x + kPanelW - 1, 0);          /* edge + 2 px shadow */
    LineTo(x + kPanelW - 1, gWin->portRect.bottom);
    SetRect(&r, x + kPanelW, 0, x + kPanelW + 2, gWin->portRect.bottom);
    PaintRect(&r);
    MoveTo(x, kHeadH - 1);
    LineTo(x + kPanelW - 1, kHeadH - 1);
}

/* A panel lower in the stack: a clean white strip with its edge (14 px of
   a panel's text can't be read anyway); the top blade's edge and shadow
   show it lies over. A click on the strip goes back to it. */
static void DrawUnder(short level)
{
    short x = level * kStep;
    Rect r;
    SetRect(&r, x, 0, x + kPanelW + 2, gWin->portRect.bottom);
    FillRect(&r, &qd.gray);              /* the panel underneath is dimmed away:
                                            the blade is the only thing open */
}

static void DrawHeader(short x, Boolean root)
{
    Rect r;
    unsigned char t[64];
    short i;
    SetRect(&r, x + 6, gY + 2, x + 22, gY + 18);
    if (root) {                          /* the hamburger, as in the strip */
        for (i = 0; i < 3; i++) {
            Rect bar;
            SetRect(&bar, x + 8, gY + 5 + i * 4, x + 20, gY + 7 + i * 4);
            PaintRect(&bar);
        }
    } else {                             /* back: a plain chevron, as in the Library */
        UIFont(systemFont, 12, 0);
        MoveTo(x + 8, gY + 15);
        DrawString(PSTR("\p\307"));      /* << (MacRoman) */
    }
    UIFont(systemFont, 12, 0);
    CopyP(gBladeTitle, t, 63);
    FitTo(t, kPanelW - 34);
    MoveTo(x + 28, gY + 15);
    DrawString(t);
}

static void DrawRow(short i, short x, short y, Boolean sel)
{
    Row *r = &gRows[i];
    Rect box;
    unsigned char t[40];
    short tx = x + 10 + r->indent * 8, vw;
    SetRect(&box, x + 1, y, x + kPanelW - 2, y + kRowH);
    EraseRect(&box);
    if (r->sep) {
        MoveTo(x + 10, y + kRowH / 2);
        LineTo(x + kPanelW - 12, y + kRowH / 2);
        return;
    }
    UIFont(kGeneva, 10, 0);
    if (r->icon) {                       /* small folder or page, as in the
                                            Finder's list views */
        if (r->icon == 'f') {
            MoveTo(tx, y + 5); LineTo(tx + 3, y + 5); LineTo(tx + 4, y + 4);
            LineTo(tx + 6, y + 4); LineTo(tx + 7, y + 5); LineTo(tx + 10, y + 5);
            LineTo(tx + 10, y + 12); LineTo(tx, y + 12); LineTo(tx, y + 5);
        } else {
            MoveTo(tx + 1, y + 3); LineTo(tx + 6, y + 3); LineTo(tx + 9, y + 6);
            LineTo(tx + 9, y + 13); LineTo(tx + 1, y + 13); LineTo(tx + 1, y + 3);
            MoveTo(tx + 6, y + 3); LineTo(tx + 6, y + 6); LineTo(tx + 9, y + 6);
        }
        tx += 15;
    }
    if (r->check) {
        Rect c;
        SetRect(&c, tx, y + 4, tx + 9, y + 13);
        FrameRect(&c);
        if (r->checked) { InsetRect(&c, 2, 2); PaintRect(&c); }
        tx += 15;
    }
    vw = r->value[0] ? StringWidth(r->value) + 8 : 0;
    CopyP(r->text, t, 39);
    if (r->current) TextFace(bold);      /* the open document, wherever the
                                            selection is (a Window menu's ✓) */
    FitTo(t, x + kPanelW - 14 - vw - (r->arrow ? 10 : 0) - tx);
    MoveTo(tx, y + 12);
    DrawString(t);
    TextFace(0);
    if (r->value[0]) {
        UIFont(kGeneva, 9, 0);
        MoveTo(x + kPanelW - 12 - StringWidth(r->value) - (r->arrow ? 10 : 0), y + 12);
        DrawString(r->value);
        UIFont(kGeneva, 10, 0);
    }
    if (r->arrow) {
        MoveTo(x + kPanelW - 16, y + 12);
        DrawString(PSTR("\p\310"));      /* >> (MacRoman) stands in for › */
    }
    if (r->act == aNone && !r->check && r->text[0]) {   /* inactive: grey it */
        PenPat(&qd.gray);
        PenMode(patBic);
        PaintRect(&box);
        PenNormal();
    }
    if (sel)                             /* one highlight: the selection */
        InvertRect(&box);
}

static void DrawTop(void)
{
    Blade *b = &gStack[gDepth - 1];
    short x = (gDepth - 1) * kStep, i, y, vis = VisibleRows();
    if (b->sel < b->top) b->top = b->sel;
    if (b->sel >= b->top + vis) b->top = b->sel - vis + 1;
    if (gDepth > 1) {                    /* a blade floats: inset card, 1 px
                                            frame, classic 1 px drop shadow */
        Rect c;
        gY = kInset;
        SetRect(&c, x, gY, x + kPanelW, PanelBottom());
        EraseRect(&c);
        FrameRect(&c);
        MoveTo(c.left + 1, c.bottom);    /* shadow: bottom and right */
        LineTo(c.right, c.bottom);
        LineTo(c.right, c.top + 1);
        MoveTo(x, gY + kHeadH - 1);
        LineTo(x + kPanelW - 1, gY + kHeadH - 1);
    } else {
        gY = 0;
        DrawFrame(x);
    }
    DrawHeader(x, gDepth == 1);
    for (i = b->top, y = gY + kRowsTop; i < gRowN && i < b->top + vis; i++, y += kRowH)
        DrawRow(i, x, y, i == b->sel);
    UIFont(kGeneva, 9, 0);
    MoveTo(x + 10, PanelBottom() - 5);
    if (gDepth == 1) DrawString(PSTR("\pCmd-\\ closes"));
    else DrawString(PSTR("\pArrows move \245 Esc back"));
}

static void DimPage(void)
{
    PenPat(&qd.gray);                    /* 50% dither over the page */
    PenMode(patOr);
    PaintRect(&gWin->portRect);
    PenNormal();
}

/* The drawer over the page. repaint: draw the page first (an update);
   opening from the writing view, the page is already there, so the dither
   goes straight over it, with no blank-and-redraw flash. */
static void DrawOver(Boolean repaint)
{
    short i;
    if (!gOpen) return;
    SetPort(gWin);
    if (repaint) {
        Ed_Hold(false);                  /* the page under the dither */
        Chrome_Hold(false);
        Ed_DrawAll();
        Chrome_Draw();
    }
    Ed_Hold(true);
    Chrome_Hold(true);
    DimPage();
    for (i = 0; i < gDepth - 1; i++)
        DrawUnder(i);
    BuildRows();
    DrawTop();
}

void Drawer_Draw(void) { DrawOver(true); }

/* ---- navigation ---------------------------------------------------------- */

static void Push(short kind, long dir)
{
    if (gDepth >= kMaxDepth) return;
    gStack[gDepth].kind = kind;
    gStack[gDepth].dir = dir;
    gStack[gDepth].sel = gStack[gDepth].top = 0;
    gDepth++;
    DrawUnder(gDepth - 2);               /* the panel it covers dims */
    BuildRows();
    DrawTop();
}

static void Pop(void)
{
    if (gDepth <= 1) { Drawer_Close(); return; }
    gDepth--;
    Drawer_Draw();
}

static void Activate(short i)
{
    Row *r;
    Blade *b = &gStack[gDepth - 1];
    if (i < 0 || i >= gRowN) return;
    r = &gRows[i];
    b->sel = i;
    switch (r->act) {
    case aPush:
        if (r->arg == -kSections) Push(kSections, 0);
        else if (r->arg == -kSettings) Push(kSettings, 0);
        else if (r->arg == -kFocus) Push(kFocus, 0);
        else if (r->arg == -kHistory) {
            Doc_Suspend();               /* what's on the page is the latest */
            Push(kHistory, 0);
        }
        else Push(gDepth == 1 ? kProject : kFolder, r->arg);
        break;
    case aOpenDoc: {
        short wd, vol;
        long proj;
        if (r->current) {                /* the page you're on: back to it */
            Drawer_Close();
            break;
        }
        Files_ProjectsDir(&vol, &proj);
        if (Files_WDFor(vol, r->arg, &wd) && Doc_OpenAt(r->file, wd))
            Drawer_Close();              /* the document shows; drawer goes */
        else
            SysBeep(1);
        break;
    }
    case aJump:
        Drawer_Close();
        Ed_SetCaret(r->arg);
        break;
    case aLibrary:
        Drawer_Close();
        Lib_Open();
        break;
    case aQuit:
        Drawer_Close();
        App_Quit();
        break;
    case aFont:
        App_CycleFont();
        BuildRows();
        DrawTop();
        break;
    case aSize:
        App_StepSize(1);
        BuildRows();
        DrawTop();
        break;
    case aWords:
    case aClock:
        if (r->act == aWords) gWords = !gWords; else gClock = !gClock;
        App_SetStatusParts(gWords, gClock);
        BuildRows();
        DrawTop();
        break;
    case aMarks:
        Doc_ToggleMarks();
        BuildRows();
        DrawTop();
        break;
    case aNewDoc: {                      /* a new page in this writing */
        short wd, vol;
        long proj;
        Files_ProjectsDir(&vol, &proj);
        if (!Files_WDFor(vol, r->arg, &wd)) { SysBeep(1); break; }
        Drawer_Close();
        Doc_SetNewFolder(wd);
        Doc_New();
        break;
    }
    case aNewFolder: {                   /* inside a writing: chapters, say */
        unsigned char name[40];
        short vol;
        long proj, nd;
        Boolean ok = Lib_AskFolderName(name);
        Files_ProjectsDir(&vol, &proj);
        if (ok && !Files_NewFolder(vol, r->arg, name, &nd))
            SysBeep(1);                  /* the name is taken */
        Drawer_Draw();                   /* the dialog drew over the drawer */
        break;
    }
    case aGoal:                          /* minutes > 0, words < 0 */
        Drawer_Close();
        Chrome_SetGoal(r->arg > 0 ? 'm' : 'w', r->arg > 0 ? r->arg : -r->arg);
        break;
    case aGoalOther:
        Drawer_Close();
        Cmd_Run(r->arg == 'm' ? cFocusMinutes : cFocusWords);
        break;
    case aGoalOff:
        Drawer_Close();
        Chrome_SetGoal(0, 0);
        break;
    case aVersion: {
        Ptr t;
        long len;
        JRVersion v = gVers[r->arg];
        if (!JR_BuildVersion(v.end, &t, &len)) { SysBeep(1); break; }
        Drawer_Close();
        if (!Doc_OpenVersion(t, len, v.time)) SysBeep(1);
        DisposePtr(t);
        break;
    }
    case aMoveLib:
        Drawer_Close();
        App_MoveLibrary();
        break;
    case aBackup:
        Drawer_Close();
        App_BackupLibrary();
        break;
    }
}

/* ---- open / close / input ------------------------------------------------- */

void Drawer_Init(WindowPtr w)
{
    Prefs p;
    gWin = w;
    gStack[0].kind = kRoot;
    gStack[0].sel = gStack[0].top = 0;
    Prefs_Load(&p);
    gWords = !(p.hide & 1);
    gClock = !(p.hide & 2);
}

void Drawer_Open(void)
{
    short vol;
    long dir, proj;
    if (gOpen) { Drawer_Close(); return; }   /* Cmd-\ toggles */
    /* reopen to the project/folder blade last used (switching files stays
       one click), if it's still this document's project; otherwise the
       main panel (Settings and Sections open fresh) */
    if (gDepth > 1 && !(gStack[1].kind == kProject
                        && ProjectOfDoc(&vol, &dir, &proj) && proj == gStack[1].dir))
        gDepth = 1;
    {
        Prefs p;                         /* the palette may have changed them */
        Prefs_Load(&p);
        gWords = !(p.hide & 1);
        gClock = !(p.hide & 2);
    }
    gOpen = true;
    Ed_HideCaret();                      /* no cursor left under the dither */
    DrawOver(false);
}

/* Straight to a blade: 's' Sections, 't' Settings (the palette). */
void Drawer_OpenBlade(char which)
{
    if (gOpen) Drawer_Close();
    gDepth = 1;
    Drawer_Open();
    if (which == 'h') {
        if (!Doc_HasFile()) { SysBeep(1); return; }
        Doc_Suspend();
        Push(kHistory, 0);
    } else
        Push(which == 's' ? kSections : kSettings, 0);
}

void Drawer_Close(void)
{
    gByHover = false;
    if (!gOpen) return;
    gOpen = false;
    Ed_Hold(false);
    Chrome_Hold(false);
    SetPort(gWin);
    {   /* the strip and footer bands: hidden chrome doesn't clear itself */
        Rect r = gWin->portRect;
        r.bottom = kStripH;
        EraseRect(&r);
        r = gWin->portRect;
        r.top = r.bottom - kFooterH;
        EraseRect(&r);
    }
    Ed_DrawAll();                        /* lines replace the dither in place */
    Chrome_Draw();
}

Boolean Drawer_IsOpen(void) { return gOpen; }

static void MoveSel(short d)
{
    Blade *b = &gStack[gDepth - 1];
    short i = b->sel;
    do {
        i += d;
    } while (i >= 0 && i < gRowN && (gRows[i].sep || (gRows[i].act == aNone && !gRows[i].check)));
    if (i < 0 || i >= gRowN) return;
    b->sel = i;
    DrawTop();
}

void Drawer_Key(EventRecord *ev)
{
    unsigned char c = (unsigned char)(ev->message & charCodeMask);
    gTouched = true;
    switch (c) {
    case 0x1E: MoveSel(-1); break;                       /* up */
    case 0x1F: MoveSel(1); break;                        /* down */
    case 0x1D: case '\r': case 0x03:                     /* right, return */
        Activate(gStack[gDepth - 1].sel);
        break;
    case 0x1C: case 0x1B: Pop(); break;                  /* left, esc */
    }
}

/* Opened by hovering: if the pointer leaves the drawer before anything in
   it is touched, it closes again, quietly. */
void Drawer_OpenByHover(void)
{
    if (gOpen) return;
    Drawer_Open();
    gByHover = true;
    gTouched = false;
}

void Drawer_Idle(void)
{
    Point m;
    if (!gOpen || !gByHover || gTouched) return;
    SetPort(gWin);
    GetMouse(&m);
    if (m.h > (gDepth - 1) * kStep + kPanelW + 12 || m.v > gWin->portRect.bottom
        || m.h < -4 || m.v < -4)
        Drawer_Close();
}

void Drawer_Click(Point pt, EventRecord *ev)
{
    short x = (gDepth - 1) * kStep, i;
    (void)ev;
    gTouched = true;
    if (pt.h >= x + kPanelW + 2) { Drawer_Close(); return; }   /* the page */
    if (pt.h < x) {                      /* a strip underneath: back to it */
        short level = pt.h / kStep + 1;
        if (level < 1) level = 1;
        gDepth = level;
        Drawer_Draw();
        return;
    }
    gY = gDepth > 1 ? kInset : 0;
    if (pt.v < gY + kHeadH) {
        if (pt.h >= x + 4 && pt.h < x + 24) Pop();       /* hamburger / back */
        return;
    }
    if (pt.v < gY + kRowsTop) return;
    i = gStack[gDepth - 1].top + (pt.v - gY - kRowsTop) / kRowH;
    if (i < gRowN && !gRows[i].sep && (gRows[i].act != aNone)) {
        if (gRows[i].act == aSize && (ev->modifiers & optionKey)) {
            App_StepSize(-1);            /* Option-click: smaller */
            BuildRows();
            DrawTop();
            return;
        }
        Activate(i);
    }
}
