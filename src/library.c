#include "library.h"
#include "files.h"
#include "document.h"
#include "journal.h"
#include "chrome.h"
#include "editor.h"

#include <Quickdraw.h>
#include <Fonts.h>
#include <Events.h>
#include <OSUtils.h>
#include <ToolUtils.h>
#include <Memory.h>
#include <StandardFile.h>
#include <Dialogs.h>
#include <TextUtils.h>

#define PSTR(s) ((ConstStr255Param)(s))

/* Low-memory globals (Retro68 has no GetDblTime/GetCaretTime glue). */
#define DblTime()   (*(volatile unsigned long *)0x02F0)
#define CaretTime() (*(volatile unsigned long *)0x02F4)

/* layout (DESIGN.md / Library mockup) */
#define kHeaderH   20
#define kActionsH  16
#define kRowsTop   (kHeaderH + kActionsH)
#define kRowH      102
#define kLeft      14
#define kTileW     64
#define kTileStep  72
#define kVisible   6                     /* whole tiles per shelf */
#define kIconTop   18                    /* icon top within a row */
#define kLabelBase 62                    /* label baseline within a row */
#define kGeneva    3

enum { kRowTpl, kRowFiles, kRowRecent, kRows };
enum { kActOpen, kActDuplicate, kActRename, kActTrash,
       kActNewProject, kActNewFolder, kActOther, kActNewDoc, kActCount };

static const char *kActName[kActCount] = {
    "Open", "Duplicate", "Rename", "Move to Trash",
    "New Project", "New Folder", "Open File\311",   /* \311 = MacRoman ellipsis */
    "New Document"
};

static WindowPtr gWin;
static Boolean   gOpen;

/* shelves */
static DirItem   gTpl[kMaxItems];
static short     gTplN;
static DirItem   gFiles[kMaxItems];      /* index 0 is the up tile when deep */
static short     gFilesN;
static Prefs     gPrefs;                 /* Recent comes from here */

/* where My Files is browsing */
#define kMaxDepth 8
static short     gVol;
static long      gDir[kMaxDepth];
static Str31     gDirName[kMaxDepth];
static short     gDepth;                 /* 0 = My Files itself */

/* selection */
static short     gSelRow = -1, gSelIdx;
static short     gFirst[kRows];

/* actions shown now, with their rects for clicks */
static short     gActs[kActCount];
static short     gActN;
static Rect      gActRect[kActCount];
static Rect      gBackRect;

/* renaming in place */
static Boolean   gRenaming;
static Str31     gEdit;
static short     gEditCaret;
static Boolean   gEditAll;               /* whole name selected: typing replaces */
static Boolean   gEditIsDoc;
static unsigned long gBlinkAt;
static Boolean   gCaretOn;

/* double-click */
static unsigned long gLastClick;
static short     gLastRow = -1, gLastIdx = -1;

/* ---- helpers ------------------------------------------------------------ */

static void CopyP(const unsigned char *s, unsigned char *d)
{
    short i;
    for (i = 0; i <= s[0]; i++) d[i] = s[i];
}

static Boolean HasMd(const unsigned char *n)
{
    short k = n[0];
    return k > 3 && n[k - 2] == '.' && (n[k - 1] | 0x20) == 'm' && (n[k] | 0x20) == 'd';
}

/* The name as shown: without ".md". */
static void ShownName(const unsigned char *n, unsigned char *out)
{
    CopyP(n, out);
    if (HasMd(out)) out[0] -= 3;
}

static short RowCount(short row)
{
    if (row == kRowTpl) return gTplN;
    if (row == kRowFiles) return gFilesN;
    return gPrefs.recentCount;
}

static Boolean IsUp(short row, short idx)
{
    return row == kRowFiles && gDepth > 0 && idx == 0;
}

static DirItem *FileItem(short idx)
{
    return &gFiles[idx];
}

/* The folder new things go in here: My Documents / My Projects at the top
   (MY STUFF), otherwise the folder being browsed. */
static long HereDir(Boolean forProject)
{
    short v;
    long d;
    if (gDepth > 0) return gDir[gDepth];
    if (forProject) Files_ProjectsDir(&v, &d);
    else Files_DocsDir(&v, &d);
    return d;
}

static short WDOfDir(long dir)
{
    short wd = 0;
    Files_WDFor(gVol, dir, &wd);
    return wd;
}

static short ItemWD(short idx) { return WDOfDir(gFiles[idx].parent); }

static void LoadFiles(void);

/* The picked file or folder, on My Stuff or Recent, with its folder. */
typedef struct {
    short   vol;
    long    dir;                         /* the folder it is in */
    short   wd;
    long    selfDir;                     /* folders: their own ID */
    Str31   name;
    Boolean isFolder;
} Target;

static Boolean GetTarget(Target *t)
{
    if (gSelRow == kRowFiles && !IsUp(gSelRow, gSelIdx)) {
        DirItem *it = &gFiles[gSelIdx];
        t->vol = gVol;
        t->dir = it->parent;
        t->wd = ItemWD(gSelIdx);
        t->selfDir = it->dirID;
        CopyP(it->name, t->name);
        t->isFolder = it->isFolder;
        return true;
    }
    if (gSelRow == kRowRecent && gSelIdx < gPrefs.recentCount) {
        RecentItem *r = &gPrefs.recent[gSelIdx];
        if (!Files_WDOf(&r->dir, &t->wd) || !Files_DirOfWD(t->wd, &t->vol, &t->dir))
            return false;                /* its disk isn't here */
        t->selfDir = 0;
        CopyP(r->name, t->name);
        t->isFolder = false;
        return true;
    }
    return false;
}

/* After a change: reload both shelves, keep the pick where it was. */
static void Reload(void)
{
    LoadFiles();
    Prefs_Load(&gPrefs);
    if (gSelRow >= 0 && gSelIdx >= RowCount(gSelRow)) gSelIdx = RowCount(gSelRow) - 1;
    if (gSelRow >= 0 && gSelIdx < 0) gSelRow = -1;
}

static void LoadFiles(void)
{
    short n = 0;
    if (gDepth > 0) {                    /* the up tile */
        CopyP(gDirName[gDepth - 1], gFiles[0].name);
        gFiles[0].isFolder = true;
        gFiles[0].dirID = gDir[gDepth - 1];
        gFiles[0].parent = 0;
        n = 1;
    }
    if (gDepth == 0) {                   /* MY STUFF: projects, then documents */
        n += Files_List(gVol, HereDir(true), gFiles + n, kMaxItems - n);
        n += Files_List(gVol, HereDir(false), gFiles + n, kMaxItems - n);
        gFilesN = n;
    } else
        gFilesN = n + Files_List(gVol, gDir[gDepth], gFiles + n, kMaxItems - n);
}

static void LoadAll(void)
{
    short vol, wd = Files_TemplatesWD();
    long dir;
    gTplN = 0;
    if (Files_DirOfWD(wd, &vol, &dir) && wd != Files_DocsWD())
        gTplN = Files_List(vol, dir, gTpl, kMaxItems);
    LoadFiles();
    Prefs_Load(&gPrefs);
}

static void ScrollIntoView(short row, short idx)
{
    if (idx < gFirst[row]) gFirst[row] = idx;
    if (idx >= gFirst[row] + kVisible) gFirst[row] = idx - kVisible + 1;
    if (gFirst[row] < 0) gFirst[row] = 0;
}

static void Select(short row, short idx)
{
    gSelRow = row;
    gSelIdx = idx;
    if (row >= 0) ScrollIntoView(row, idx);
}

/* Select the file called name in My Files (after create/rename/duplicate). */
static void SelectFileNamed(const unsigned char *name)
{
    short i, j;
    for (i = 0; i < gFilesN; i++) {
        if (gFiles[i].name[0] != name[0]) continue;
        for (j = 1; j <= name[0] && gFiles[i].name[j] == name[j]; j++)
            ;
        if (j > name[0] && !IsUp(kRowFiles, i)) {
            Select(kRowFiles, i);
            return;
        }
    }
}

/* The bar shows every action, always: what you can make, a divider, then
   what you can do with a file. Those that don't fit what's picked are
   greyed. Open with nothing picked is the classic dialog (any .md on disk). */
static short gActSplit;                  /* index of the first file action */
static Boolean gActOn[kActCount];        /* by slot: enabled now */

static void BuildActions(void)
{
    Boolean file = gSelRow == kRowFiles || gSelRow == kRowRecent;
    Boolean up = IsUp(gSelRow, gSelIdx);
    Boolean folder = gSelRow == kRowFiles && !up && FileItem(gSelIdx)->isFolder;
    gActN = 0;
    gActOn[gActN] = true;  gActs[gActN++] = kActNewDoc;
    if (gDepth <= 1) {
        gActOn[gActN] = true;  gActs[gActN++] = gDepth == 0 ? kActNewProject : kActNewFolder;
    } else {
        gActOn[gActN] = false; gActs[gActN++] = kActNewFolder;   /* 3 levels at most */
    }
    gActSplit = gActN;
    gActOn[gActN] = true;                   gActs[gActN++] = kActOpen;
    gActOn[gActN] = file && !up && !folder; gActs[gActN++] = kActDuplicate;
    gActOn[gActN] = file && !up;            gActs[gActN++] = kActRename;
    gActOn[gActN] = file && !up;            gActs[gActN++] = kActTrash;
}

/* ---- drawing ------------------------------------------------------------ */

static void CStr(const char *c, unsigned char *p)
{
    short n = 0;
    while (c[n] && n < 255) { p[n + 1] = (unsigned char)c[n]; n++; }
    p[0] = (unsigned char)n;
}

static void Upper(unsigned char *p)
{
    short i;
    for (i = 1; i <= p[0]; i++)
        if (p[i] >= 'a' && p[i] <= 'z') p[i] -= 32;
}

/* Fit a label into w pixels, ending in an ellipsis if cut. */
static void Fit(unsigned char *p, short w)
{
    if (StringWidth(p) <= w) return;
    while (p[0] > 1 && StringWidth(p) + CharWidth('\311') > w)
        p[0]--;
    p[++p[0]] = '\311';
}

static void IconDoc(short x, short y, Boolean dashed)
{
    Rect r;
    if (dashed) PenPat(&qd.gray);
    MoveTo(x + 6, y + 2);  LineTo(x + 20, y + 2); LineTo(x + 26, y + 8);
    LineTo(x + 26, y + 29); LineTo(x + 6, y + 29); LineTo(x + 6, y + 2);
    PenNormal();
    MoveTo(x + 20, y + 2); LineTo(x + 20, y + 8); LineTo(x + 26, y + 8);
    SetRect(&r, x + 10, y + 13, x + 22, y + 14); PaintRect(&r);
    SetRect(&r, x + 10, y + 17, x + (dashed ? 19 : 22), y + 18); PaintRect(&r);
    SetRect(&r, x + 10, y + 21, x + (dashed ? 21 : 18), y + 22); PaintRect(&r);
}

static void IconFolder(short x, short y, Boolean up)
{
    MoveTo(x + 2, y + 9);   LineTo(x + 12, y + 9); LineTo(x + 14, y + 6);
    LineTo(x + 22, y + 6);  LineTo(x + 22, y + 9); LineTo(x + 29, y + 9);
    LineTo(x + 29, y + 28); LineTo(x + 2, y + 28); LineTo(x + 2, y + 9);
    if (up) {
        PolyHandle p = OpenPoly();
        MoveTo(x + 16, y + 13); LineTo(x + 11, y + 18); LineTo(x + 14, y + 18);
        LineTo(x + 14, y + 24); LineTo(x + 18, y + 24); LineTo(x + 18, y + 18);
        LineTo(x + 21, y + 18); LineTo(x + 16, y + 13);
        ClosePoly();
        PaintPoly(p);
        KillPoly(p);
    } else {
        MoveTo(x + 3, y + 12); LineTo(x + 28, y + 12);
    }
}

static void TileName(short row, short idx, unsigned char *out)
{
    if (row == kRowTpl) ShownName(gTpl[idx].name, out);
    else if (row == kRowFiles) ShownName(gFiles[idx].name, out);
    else ShownName(gPrefs.recent[idx].name, out);
}

static void DrawEditField(short x, short top)
{
    Rect r;
    unsigned char s[40];
    short w, tx;
    SetRect(&r, x - 1, top + kLabelBase - 10, x + kTileW + 1, top + kLabelBase + 3);
    EraseRect(&r);
    FrameRect(&r);
    CopyP(gEdit, s);
    w = StringWidth(s);
    tx = x + (kTileW - w) / 2;
    if (tx < x + 2) tx = x + 2;
    MoveTo(tx, top + kLabelBase);
    DrawString(s);
    if (gEditAll && s[0] > 0) {
        Rect sel;
        SetRect(&sel, tx - 1, top + kLabelBase - 9, tx + w + 1, top + kLabelBase + 2);
        InvertRect(&sel);
    } else if (gCaretOn) {
        short cx = tx + TextWidth((Ptr)s, 1, gEditCaret);
        MoveTo(cx, top + kLabelBase - 9);
        LineTo(cx, top + kLabelBase + 1);
    }
}

static void DrawTile(short row, short idx, short x, short top)
{
    unsigned char name[40];
    short w;
    Rect r;
    SetRect(&r, x - 2, top + kIconTop - 1, x + kTileW + 2, top + kLabelBase + 4);
    EraseRect(&r);
    if (row == kRowTpl)
        IconDoc(x + 16, top + kIconTop, true);
    else if (row == kRowFiles && gFiles[idx].isFolder)
        IconFolder(x + 16, top + kIconTop, IsUp(row, idx));
    else
        IconDoc(x + 16, top + kIconTop, false);

    if (gRenaming && row == gSelRow && idx == gSelIdx) {
        DrawEditField(x, top);
        return;
    }
    TileName(row, idx, name);
    Fit(name, kTileW - 4);
    w = StringWidth(name);
    MoveTo(x + (kTileW - w) / 2, top + kLabelBase);
    DrawString(name);
    if (row == gSelRow && idx == gSelIdx) {
        SetRect(&r, x + (kTileW - w) / 2 - 3, top + kLabelBase - 9,
                x + (kTileW + w) / 2 + 3, top + kLabelBase + 2);
        InvertRect(&r);
    }
}

static void DrawRow(short row)
{
    short top = kRowsTop + row * kRowH, i, n = RowCount(row);
    unsigned char s[100];
    Rect r;
    SetRect(&r, 0, top, gWin->portRect.right, top + kRowH - 1);
    EraseRect(&r);
    TextFont(kGeneva);
    TextSize(9);
    MoveTo(kLeft, top + 13);
    if (row == kRowTpl) DrawString(PSTR("\pTEMPLATES"));
    else if (row == kRowRecent) DrawString(PSTR("\pRECENT"));
    else {
        DrawString(PSTR("\pMY STUFF"));
        for (i = 1; i <= gDepth; i++) {
            DrawString(PSTR("\p \310 "));          /* \310 = MacRoman >> */
            ShownName(gDirName[i], s);
            Upper(s);
            if (i == gDepth) TextFace(bold);
            DrawString(s);
            TextFace(0);
        }
    }
    if (n == 0) {
        MoveTo(kLeft, top + kLabelBase);
        if (row == kRowRecent) DrawString(PSTR("\pNothing opened yet"));
        else if (row == kRowFiles) DrawString(PSTR("\pEmpty"));
    }
    for (i = gFirst[row]; i < n && i < gFirst[row] + kVisible; i++)
        DrawTile(row, i, kLeft + (i - gFirst[row]) * kTileStep, top);
    /* the picked tile shows its whole name, as in the Finder, over its
       neighbours' labels if it must */
    if (row == gSelRow && !gRenaming && gSelIdx >= gFirst[row]
        && gSelIdx < gFirst[row] + kVisible && gSelIdx < n) {
        unsigned char nm[40];
        short x = kLeft + (gSelIdx - gFirst[row]) * kTileStep, w, lx;
        TileName(row, gSelIdx, nm);
        Fit(nm, gWin->portRect.right - 2 * kLeft);
        w = StringWidth(nm);
        lx = x + (kTileW - w) / 2;
        if (lx < 4) lx = 4;
        if (lx + w > gWin->portRect.right - 4) lx = gWin->portRect.right - 4 - w;
        SetRect(&r, lx - 3, top + kLabelBase - 9, lx + w + 3, top + kLabelBase + 2);
        EraseRect(&r);
        MoveTo(lx, top + kLabelBase);
        DrawString(nm);
        InvertRect(&r);
    }
    if (gFirst[row] > 0) {                          /* more to the left */
        MoveTo(3, top + kIconTop + 20);
        DrawString(PSTR("\p\307"));                 /* << */
    }
    if (gFirst[row] + kVisible < n) {               /* more to the right */
        MoveTo(gWin->portRect.right - 12, top + kIconTop + 20);
        DrawString(PSTR("\p\310"));                 /* >> */
    }
    if (row < kRows - 1) {
        MoveTo(0, top + kRowH - 1);
        LineTo(gWin->portRect.right, top + kRowH - 1);
    }
}

static void DrawActions(void)
{
    Rect r;
    short i, x = kLeft;
    unsigned char s[40];
    BuildActions();
    SetRect(&r, 0, kHeaderH, gWin->portRect.right, kRowsTop - 1);
    EraseRect(&r);
    TextFont(kGeneva);
    TextSize(9);
    for (i = 0; i < gActN; i++) {
        CStr(kActName[gActs[i]], s);
        if (i == gActSplit) {           /* divider */
            MoveTo(x - 6, kHeaderH + 3);
            LineTo(x - 6, kRowsTop - 4);
            x += 6;
        }
        MoveTo(x, kHeaderH + 11);
        DrawString(s);
        SetRect(&gActRect[i], x - 4, kHeaderH, x + StringWidth(s) + 4, kRowsTop - 1);
        if (!gActOn[i]) {               /* not for what's picked: grey */
            Rect g = gActRect[i];
            InsetRect(&g, 0, 2);
            PenPat(&qd.gray);
            PenMode(patBic);
            PaintRect(&g);
            PenNormal();
        }
        x += StringWidth(s) + 12;
    }
    MoveTo(0, kRowsTop - 1);
    LineTo(gWin->portRect.right, kRowsTop - 1);
}

static void DrawHeader(void)
{
    Rect r;
    short w;
    SetRect(&r, 0, 0, gWin->portRect.right, kHeaderH);
    EraseRect(&r);
    TextFont(kGeneva);
    TextSize(9);
    TextFont(systemFont);                /* back to writing: just the chevron */
    TextSize(12);
    SetRect(&gBackRect, 2, 0, 26, kHeaderH - 1);
    MoveTo(8, 15);
    DrawString(PSTR("\p\307"));
    TextFont(kGeneva);
    TextSize(9);
    w = StringWidth(PSTR("\pReturn opens \245 Esc closes"));   /* \245 = bullet */
    MoveTo(gWin->portRect.right - 6 - w, 13);
    DrawString(PSTR("\pReturn opens \245 Esc closes"));
    TextFont(systemFont);
    TextSize(12);
    w = StringWidth(PSTR("\pLibrary"));
    MoveTo((gWin->portRect.right - w) / 2, 15);
    DrawString(PSTR("\pLibrary"));
    MoveTo(0, kHeaderH - 1);
    LineTo(gWin->portRect.right, kHeaderH - 1);
}

void Lib_Draw(void)
{
    short r;
    if (!gOpen) return;
    SetPort(gWin);
    PenNormal();
    DrawHeader();
    DrawActions();
    for (r = 0; r < kRows; r++)
        DrawRow(r);
}

static void Redraw(void)
{
    DrawActions();
    DrawRow(kRowTpl);
    DrawRow(kRowFiles);
    DrawRow(kRowRecent);
}

/* ---- actions ------------------------------------------------------------ */

static void Navigate(short idx)
{
    if (IsUp(kRowFiles, idx)) {
        gDepth--;
    } else if (gDepth < kMaxDepth - 1) {
        gDir[gDepth + 1] = gFiles[idx].dirID;
        CopyP(gFiles[idx].name, gDirName[gDepth + 1]);
        gDepth++;
    } else {
        SysBeep(1);
        return;
    }
    LoadFiles();
    gFirst[kRowFiles] = 0;
    Select(-1, 0);
    Redraw();
}

static void DoOther(void);

static void DoOpen(void)
{
    short wd;
    if (gSelRow < 0) {                   /* nothing picked: any file on disk */
        DoOther();
        return;
    }
    if (gSelRow == kRowTpl) {
        /* the Save dialog comes first, in the folder being browsed */
        if (Doc_NewFromTemplate(gTpl[gSelIdx].name, Files_TemplatesWD(),
                                WDOfDir(HereDir(false))))
            Lib_Close();
        else
            Lib_Draw();                  /* cancelled: the dialog drew over us */
    } else if (gSelRow == kRowFiles) {
        if (gFiles[gSelIdx].isFolder)
            Navigate(gSelIdx);
        else if (Doc_OpenAt(gFiles[gSelIdx].name, ItemWD(gSelIdx)))
            Lib_Close();
        else SysBeep(1);
    } else if (gSelRow == kRowRecent) {
        RecentItem *it = &gPrefs.recent[gSelIdx];
        if (Files_WDOf(&it->dir, &wd) && Doc_OpenAt(it->name, wd))
            Lib_Close();
        else {
            SysBeep(1);                  /* moved or deleted elsewhere */
            if (Files_WDOf(&it->dir, &wd)) Doc_ForgetRecent(wd, it->name, NULL);
            else {                       /* its disk is gone: drop by index */
                short i;
                for (i = gSelIdx; i < gPrefs.recentCount - 1; i++)
                    gPrefs.recent[i] = gPrefs.recent[i + 1];
                gPrefs.recentCount--;
                Prefs_Save(&gPrefs);
            }
            Prefs_Load(&gPrefs);
            Select(-1, 0);
            Redraw();
        }
    }
}

/* "Name copy.md", "Name copy 2.md" ... / "Untitled Project", "... 2" */
static Boolean FreeName(const unsigned char *base, Boolean md, short vol, long dir, unsigned char *out)
{
    short k, i, n;
    for (k = 1; k < 100; k++) {
        n = 0;
        for (i = 1; i <= base[0] && n < 24; i++) out[++n] = base[i];
        if (k > 1) {
            out[++n] = ' ';
            if (k >= 10) out[++n] = (unsigned char)('0' + k / 10);
            out[++n] = (unsigned char)('0' + k % 10);
        }
        if (md) { out[++n] = '.'; out[++n] = 'm'; out[++n] = 'd'; }
        out[0] = (unsigned char)n;
        if (!Files_Exists(vol, dir, out))
            return true;
    }
    return false;
}

static void StartRename(void)
{
    unsigned char s[40];
    Target t;
    if (!GetTarget(&t)) return;
    gEditIsDoc = !t.isFolder && HasMd(t.name);
    ShownName(t.name, s);
    CopyP(s, gEdit);
    gEditCaret = gEdit[0];
    gEditAll = true;                     /* typing replaces, like the Finder */
    gRenaming = true;
    gCaretOn = true;
    gBlinkAt = TickCount();
    DrawActions();
    DrawRow(gSelRow);
}

static void EndRename(Boolean commit)
{
    unsigned char to[40], from[40];
    short i, max = gEditIsDoc ? 28 : 31;
    Target t;
    if (!gRenaming) return;
    gRenaming = false;
    if (commit && GetTarget(&t)) {
        CopyP(t.name, from);
        CopyP(gEdit, to);
        while (to[0] > 0 && to[to[0]] == ' ') to[0]--;
        if (to[0] > max) to[0] = (unsigned char)max;
        for (i = 1; i <= to[0]; i++)
            if (to[i] == ':') to[i] = '-';          /* illegal in HFS */
        if (gEditIsDoc) { to[++to[0]] = '.'; to[++to[0]] = 'm'; to[++to[0]] = 'd'; }
        if (to[0] > (gEditIsDoc ? 3 : 0)) {
            Boolean same = to[0] == from[0];
            for (i = 1; same && i <= to[0]; i++) same = to[i] == from[i];
            if (!same) {
                Boolean ok;
                if (t.isFolder)
                    ok = Files_Rename(t.vol, t.dir, from, to);
                else
                    ok = Doc_RenameFile(t.wd, from, to);
                if (ok) {
                    Reload();
                    if (gSelRow == kRowFiles) SelectFileNamed(to);
                } else
                    SysBeep(1);          /* the name is taken */
            }
        }
    }
    Redraw();
}

/* Ask for the new project's (or folder's) name: a small classic dialog.
   False if cancelled. */
static Boolean AskName(Boolean project, unsigned char *name)
{
    DialogPtr d;
    short hit, type, i;
    Handle h;
    Rect box;
    ParamText(project ? PSTR("\pName the new project:") : PSTR("\pName the new folder:"),
              PSTR("\p"), PSTR("\p"), PSTR("\p"));
    d = GetNewDialog(200, NULL, (WindowPtr)-1);
    if (!d) return false;
    GetDialogItem(d, 4, &type, &h, &box);
    SetDialogItemText(h, project ? PSTR("\pUntitled Project") : PSTR("\pUntitled Folder"));
    SelectDialogItemText(d, 4, 0, 32767);         /* typing replaces it */
    ShowWindow(d);
    do {
        ModalDialog(NULL, &hit);
    } while (hit != 1 && hit != 2);
    if (hit == 1) {
        GetDialogItem(d, 4, &type, &h, &box);
        GetDialogItemText(h, name);
        while (name[0] > 0 && name[name[0]] == ' ') name[0]--;
        if (name[0] > 31) name[0] = 31;
        for (i = 1; i <= name[0]; i++)
            if (name[i] == ':') name[i] = '-';     /* illegal in HFS */
    }
    DisposeDialog(d);
    return hit == 1 && name[0] > 0;
}

static void DoNew(Boolean project)
{
    unsigned char name[40];
    long dir;
    Boolean ok = AskName(project, name);
    Lib_Draw();                          /* the dialog drew over us */
    if (!ok) return;
    if (Files_Exists(gVol, HereDir(project), name)
        || !Files_NewFolder(gVol, HereDir(project), name, &dir)) {
        SysBeep(1);                      /* that name is taken */
        return;
    }
    LoadFiles();
    SelectFileNamed(name);
    Redraw();
}

/* Duplicate: the Save dialog, in the original's folder with "<name> copy"
   filled in, so the copy can be renamed or put elsewhere. */
static void DoDuplicate(void)
{
    unsigned char base[40], name[40];
    short n;
    Target t;
    SFReply reply;
    Point where;
    if (!GetTarget(&t) || t.isFolder) return;
    ShownName(t.name, base);
    n = base[0];
    if (n > 20) n = 20;                  /* room for " copy.md" */
    base[0] = (unsigned char)n;
    {
        const char *c = " copy";
        while (*c) base[++base[0]] = (unsigned char)*c++;
    }
    if (!FreeName(base, true, t.vol, t.dir, name))
        CopyP(base, name);
    Files_SetDialogFolder(t.wd);
    SetPt(&where, (gWin->portRect.right - 304) / 2, 60);
    SFPutFile(where, PSTR("\pSave the copy as:"), name, NULL, &reply);
    if (reply.good) {
        short v;
        long d;
        Boolean same = Files_DirOfWD(reply.vRefNum, &v, &d) && v == t.vol && d == t.dir
                       && reply.fName[0] == t.name[0];
        for (n = 1; same && n <= t.name[0]; n++)
            same = (reply.fName[n] | 0x20) == (t.name[n] | 0x20);
        if (same || !Files_CopyTo(t.wd, t.name, reply.vRefNum, reply.fName))
            SysBeep(1);                  /* onto itself, or the copy failed */
        else {
            Reload();
            if (gSelRow == kRowFiles) SelectFileNamed(reply.fName);
        }
    }
    Lib_Draw();                          /* the dialog drew over us */
}

static void DoTrash(void)
{
    unsigned char hist[40];
    short v;
    long d;
    Target t;
    if (!GetTarget(&t)) return;
    if (t.isFolder) {
        /* not the folder the open document lives in (it would keep saving
           into the Trash) */
        if (Doc_Location(&v, &d) && v == t.vol && d == t.selfDir) { SysBeep(1); return; }
        if (!Files_Trash(t.vol, t.dir, t.name)) { SysBeep(1); return; }
    } else {
        Doc_FileTrashed(t.wd, t.name);   /* also drops it from Recent */
        if (!Files_Trash(t.vol, t.dir, t.name)) { SysBeep(1); return; }
        JR_HistoryName(t.name, hist);
        Files_Trash(t.vol, t.dir, hist);             /* its journal, if any */
    }
    Reload();
    Redraw();
}

static void DoOther(void)
{
    Files_SetDialogFolder(WDOfDir(HereDir(false)));   /* start where you browse */
    if (Doc_Open())
        Lib_Close();
    else
        Lib_Draw();                      /* the dialog drew over us */
}

/* New Document: Blank, through the Save dialog, in the folder being browsed
   (My Documents at the top). */
static void DoNewDocument(void)
{
    short i;
    unsigned char n[40];
    for (i = 0; i < gTplN; i++) {
        ShownName(gTpl[i].name, n);
        if (n[0] == 5 && (n[1] | 0x20) == 'b' && (n[2] | 0x20) == 'l'
            && (n[3] | 0x20) == 'a' && (n[4] | 0x20) == 'n' && (n[5] | 0x20) == 'k')
            break;
    }
    if (Doc_NewFromTemplate(i < gTplN ? gTpl[i].name : (const unsigned char *)"\pBlank.md",
                            Files_TemplatesWD(), WDOfDir(HereDir(false))))
        Lib_Close();
    else
        Lib_Draw();                      /* cancelled: the dialog drew over us */
}

static void Run(short act)
{
    switch (act) {
    case kActOpen:       DoOpen(); break;
    case kActDuplicate:  DoDuplicate(); break;
    case kActRename:     StartRename(); break;
    case kActTrash:      DoTrash(); break;
    case kActNewProject: DoNew(true); break;
    case kActNewFolder:  DoNew(false); break;
    case kActOther:      DoOther(); break;
    case kActNewDoc:     DoNewDocument(); break;
    }
}

static Boolean Offered(short act)
{
    short i;
    BuildActions();
    for (i = 0; i < gActN; i++)
        if (gActs[i] == act) return gActOn[i];
    return false;
}

/* ---- open / close ------------------------------------------------------- */

void Lib_Init(WindowPtr w)
{
    gWin = w;
    Files_DocsDir(&gVol, &gDir[0]);      /* depth 0 is MY STUFF (two folders) */
    gDirName[0][0] = 8;
    BlockMove("My Stuff", gDirName[0] + 1, 8);
    gDepth = 0;
}

void Lib_Open(void)
{
    if (gDepth == 0)
        Files_DocsDir(&gVol, &gDir[0]);
    gOpen = true;
    gRenaming = false;
    Ed_Hold(true);                       /* the writing view stays still below */
    Chrome_Hold(true);
    LoadAll();
    Select(-1, 0);
    if (gPrefs.recentCount > 0)
        Select(kRowRecent, 0);           /* the open document, ready to go back */
    Lib_Draw();
}

void Lib_Close(void)
{
    if (!gOpen) return;
    EndRename(true);
    gOpen = false;
    Ed_Hold(false);
    Chrome_Hold(false);
    SetPort(gWin);
    EraseRect(&gWin->portRect);          /* no Library header left behind */
    InvalRect(&gWin->portRect);          /* the writing view redraws */
    Chrome_ShowTitleBriefly();           /* the document's name, for a moment */
}

Boolean Lib_IsOpen(void) { return gOpen; }

/* Cmd-N: the Library with Blank picked; Return starts it. */
void Lib_OpenNew(void)
{
    short i;
    unsigned char n[40];
    (void)i; (void)n;
    if (gOpen) {                         /* Cmd-N inside the Library: make it */
        DoNewDocument();
        return;
    }
    Lib_Open();
    Select(-1, 0);                       /* nothing picked: New Document leads */
    Redraw();
}

/* ---- input -------------------------------------------------------------- */

static void EditKey(unsigned char c)
{
    short i;
    if (c == 0x1B) { EndRename(false); return; }
    if (c == '\r' || c == 0x03) { EndRename(true); return; }
    if (gEditAll && (c >= 0x20 || c == 0x08) && c != 0x7F && c != 0x1C && c != 0x1D) {
        gEdit[0] = 0;
        gEditCaret = 0;
        gEditAll = false;
        if (c == 0x08) goto shown;
    }
    gEditAll = false;
    if (c == 0x1C) { if (gEditCaret > 0) gEditCaret--; }
    else if (c == 0x1D) { if (gEditCaret < gEdit[0]) gEditCaret++; }
    else if (c == 0x08) {
        if (gEditCaret > 0) {
            for (i = gEditCaret; i < gEdit[0]; i++) gEdit[i] = gEdit[i + 1];
            gEdit[0]--;
            gEditCaret--;
        }
    } else if (c >= 0x20 && c != 0x7F) {
        if (gEdit[0] >= (gEditIsDoc ? 28 : 31)) { SysBeep(1); return; }
        for (i = gEdit[0]; i > gEditCaret; i--) gEdit[i + 1] = gEdit[i];
        gEdit[gEditCaret + 1] = c;
        gEdit[0]++;
        gEditCaret++;
    }
shown:
    gCaretOn = true;
    gBlinkAt = TickCount();
    DrawRow(gSelRow);
}

static void MoveSel(short dRow, short dIdx)
{
    short row = gSelRow, idx = gSelIdx, n;
    if (row < 0) {                       /* nothing picked: start in My Files */
        row = gFilesN > 0 ? kRowFiles : (gTplN > 0 ? kRowTpl : -1);
        if (row < 0) return;
        Select(row, 0);
        Redraw();
        return;
    }
    if (dRow) {
        short r = row;
        do { r += dRow; } while (r >= 0 && r < kRows && RowCount(r) == 0);
        if (r < 0 || r >= kRows) return;
        row = r;
        idx = gFirst[row] + (gSelIdx - gFirst[gSelRow]);   /* same column */
    }
    idx += dIdx;
    n = RowCount(row);
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    Select(row, idx);
    Redraw();
}

static void TypeSelect(unsigned char c)
{
    short row = gSelRow < 0 ? kRowFiles : gSelRow, i, n = RowCount(row);
    unsigned char name[40];
    if (c >= 'A' && c <= 'Z') c += 32;
    for (i = 0; i < n; i++) {
        unsigned char f;
        if (IsUp(row, i)) continue;
        TileName(row, i, name);
        f = name[0] ? name[1] : 0;
        if (f >= 'A' && f <= 'Z') f += 32;
        if (f == c) { Select(row, i); Redraw(); return; }
    }
}

void Lib_Key(EventRecord *ev)
{
    unsigned char c = (unsigned char)(ev->message & charCodeMask);
    if (gRenaming && !(ev->modifiers & cmdKey)) {
        EditKey(c);
        return;
    }
    if (ev->modifiers & cmdKey) {
        if (c == 'r' || c == 'R') { if (Offered(kActRename)) StartRename(); }
        else if (c == 'd' || c == 'D') { if (Offered(kActDuplicate)) DoDuplicate(); }
        else if (c == 0x08) { if (Offered(kActTrash)) DoTrash(); }
        else if (c == 'o' || c == 'O') Lib_Close();
        return;
    }
    switch (c) {
    case 0x1B: Lib_Close(); break;
    case '\r': case 0x03:
        if (gSelRow >= 0) DoOpen();
        else DoNewDocument();            /* Cmd-N, Return: a new document */
        break;
    case 0x1C: MoveSel(0, -1); break;
    case 0x1D: MoveSel(0, 1); break;
    case 0x1E: MoveSel(-1, 0); break;
    case 0x1F: MoveSel(1, 0); break;
    default:
        if (c > 0x20 && c < 0x7F) TypeSelect(c);
    }
}

void Lib_Click(Point pt, EventRecord *ev)
{
    short i, row, idx;
    if (PtInRect(pt, &gBackRect)) { Lib_Close(); return; }
    if (pt.v >= kHeaderH && pt.v < kRowsTop) {
        for (i = 0; i < gActN; i++)
            if (PtInRect(pt, &gActRect[i])) {
                if (gActOn[i]) Run(gActs[i]);
                return;
            }
        return;
    }
    if (pt.v < kRowsTop) return;
    row = (pt.v - kRowsTop) / kRowH;
    if (row >= kRows) return;
    if (pt.h < kLeft) {                  /* << */
        if (gFirst[row] > 0) { gFirst[row] -= kVisible; if (gFirst[row] < 0) gFirst[row] = 0; DrawRow(row); }
        return;
    }
    if (pt.h >= kLeft + kVisible * kTileStep - (kTileStep - kTileW)) {   /* >> */
        if (gFirst[row] + kVisible < RowCount(row)) { gFirst[row] += kVisible; DrawRow(row); }
        return;
    }
    idx = gFirst[row] + (pt.h - kLeft) / kTileStep;
    if ((pt.h - kLeft) % kTileStep >= kTileW || idx >= RowCount(row)) {
        EndRename(true);                 /* a click on nothing */
        Select(-1, 0);
        Redraw();
        return;
    }
    if (gRenaming && !(row == gSelRow && idx == gSelIdx))
        EndRename(true);
    if (row == gLastRow && idx == gLastIdx
        && ev->when - gLastClick <= DblTime() && !gRenaming) {
        gLastRow = -1;
        Select(row, idx);
        DoOpen();
        return;
    }
    gLastRow = row;
    gLastIdx = idx;
    gLastClick = ev->when;
    if (!gRenaming) {
        Select(row, idx);
        Redraw();
    }
}

void Lib_Idle(void)
{
    if (!gOpen || !gRenaming || gEditAll) return;
    if (TickCount() - gBlinkAt >= CaretTime()) {
        gBlinkAt = TickCount();
        gCaretOn = !gCaretOn;
        SetPort(gWin);
        DrawRow(gSelRow);
    }
}
