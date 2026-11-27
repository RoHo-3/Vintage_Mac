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
       kActNewFolder, kActOther, kActNewDoc, kActCount };

static const char *kActName[kActCount] = {
    "Open", "Duplicate", "Rename", "Move to Trash",
    "New Folder", "Open File\311",   /* \311 = MacRoman ellipsis */
    "New Document"
};

static WindowPtr gWin;
static Boolean   gOpen;

/* shelves */
static DirItem   gTpl[kMaxItems];
static short     gTplN;
static DirItem   gFiles[kMaxItems];      /* index 0: New (a writing) at the top,
                                            the up tile inside a writing */
static short     gFilesN;
static Prefs     gPrefs;                 /* Recent comes from here */

/* where My Writings is browsing */
#define kMaxDepth 8
static short     gVol;
static long      gDir[kMaxDepth];
static Str31     gDirName[kMaxDepth];
static short     gDepth;                 /* 0 = My Writings itself */

/* selection */
static short     gSelRow = -1, gSelIdx;
static short     gFirst[kRows];

/* actions shown now, with their rects for clicks */
static short     gActs[kActCount];
static short     gActN;
static Rect      gActRect[kActCount];
static Rect      gBackRect;
static Rect      gImportRect;            /* the Import... pill at the right */

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

/* The tiles that lead the My Writings shelf, before its files:
   at the top          New (a writing: a folder with a +)
   inside a writing    up, New Document (a page with a +), New Folder
   one level deeper    up, New Document  (folders go one level deep) */
enum { kSpNone, kSpUp, kSpNewWriting, kSpNewDoc, kSpNewFolder };
static short gSpecialKind[4];
static short gSpecialN;

static short SpecialKind(short row, short idx)
{
    return (row == kRowFiles && idx >= 0 && idx < gSpecialN) ? gSpecialKind[idx] : kSpNone;
}

static Boolean IsUp(short row, short idx) { return SpecialKind(row, idx) == kSpUp; }

/* A + tile: New, New Document or New Folder. */
static Boolean IsNewTile(short row, short idx)
{
    short k = SpecialKind(row, idx);
    return k == kSpNewWriting || k == kSpNewDoc || k == kSpNewFolder;
}

/* Not a file or folder: the up tile or a + tile. */
static Boolean IsSpecial(short row, short idx) { return SpecialKind(row, idx) != kSpNone; }


/* The folder being browsed (My Writings at the top). */
static long HereDir(Boolean unused)
{
    (void)unused;
    return gDir[gDepth];
}

static short WDOfDir(long dir)
{
    short wd = 0;
    Files_WDFor(gVol, dir, &wd);
    return wd;
}

static short ItemWD(short idx) { return WDOfDir(gFiles[idx].parent); }

static void LoadFiles(void);
static void EndRename(Boolean commit);
static void DoNew(Boolean project);

/* The picked file or folder, on My Writings or Recent, with its folder. */
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
    if (gSelRow == kRowFiles && !IsSpecial(gSelRow, gSelIdx)) {
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

static void AddSpecial(short kind, const unsigned char *name, long dirID)
{
    short n = gSpecialN;
    CopyP(name, gFiles[n].name);
    gFiles[n].isFolder = kind == kSpUp;
    gFiles[n].dirID = dirID;
    gFiles[n].parent = 0;
    gSpecialKind[n] = kind;
    gSpecialN++;
}

static void LoadFiles(void)
{
    gSpecialN = 0;
    if (gDepth > 0)                      /* the up tile */
        AddSpecial(kSpUp, gDirName[gDepth - 1], gDir[gDepth - 1]);
    if (gDepth == 0)
        AddSpecial(kSpNewWriting, (const unsigned char *)"\pNew", 0);
    else
        AddSpecial(kSpNewDoc, (const unsigned char *)"\pNew Document", 0);
    if (gDepth == 1)                     /* one level of folders inside */
        AddSpecial(kSpNewFolder, (const unsigned char *)"\pNew Folder", 0);
    gFilesN = gSpecialN + Files_List(gVol, gDir[gDepth], gFiles + gSpecialN,
                                     kMaxItems - gSpecialN);
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

/* Select the file called name in My Writings (after create/rename/duplicate). */
static void SelectFileNamed(const unsigned char *name)
{
    short i, j;
    for (i = 0; i < gFilesN; i++) {
        if (gFiles[i].name[0] != name[0]) continue;
        for (j = 1; j <= name[0] && gFiles[i].name[j] == name[j]; j++)
            ;
        if (j > name[0] && !IsSpecial(kRowFiles, i)) {
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

/* New things come from the shelf's + tiles, so the bar holds only what
   acts on a pick. Opening is a double-click (or Return); Import... sits
   apart at the right. */
static void BuildActions(void)
{
    Boolean file = gSelRow == kRowFiles || gSelRow == kRowRecent;
    Boolean special = IsSpecial(gSelRow, gSelIdx);
    gActN = 0;
    gActSplit = 0;                       /* new things come from the + tiles */
    gActOn[gActN] = file && !special;       gActs[gActN++] = kActDuplicate;
    gActOn[gActN] = file && !special;       gActs[gActN++] = kActRename;
    gActOn[gActN] = file && !special;       gActs[gActN++] = kActTrash;
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

#define kLabelW      (kTileStep - 4)     /* a label line: its own column */
#define kLabelLines  3
#define kLabelLineH  11

/* Wrap a name into at most max lines of width w, breaking after spaces
   (inside a word only when one word is wider than a line); the last line
   ends in "..." if the name goes on. Returns the number of lines. */
static short WrapName(const unsigned char *name, short w, short max, unsigned char out[][40])
{
    short n = 0, i = 1, k;
    while (i <= name[0] && n < max) {
        short start = i, end = i, brk = 0;
        while (end <= name[0] && TextWidth((Ptr)name, start, end - start + 1) <= w) {
            if (name[end] == ' ') brk = end;
            end++;
        }
        if (end > name[0]) brk = name[0];             /* the rest fits */
        else if (brk == 0) brk = end - 1;             /* one long word: cut it */
        if (brk < start) brk = start;
        out[n][0] = 0;
        for (k = start; k <= brk; k++) out[n][++out[n][0]] = name[k];
        while (out[n][0] > 0 && out[n][out[n][0]] == ' ') out[n][0]--;
        i = brk + 1;
        while (i <= name[0] && name[i] == ' ') i++;
        n++;
        if (n == max && i <= name[0]) {               /* more left: "..." */
            short m = out[n - 1][0];
            if (m < 38) out[n - 1][++m] = ' ';       /* the space it ended at */
            for (k = i; k <= name[0] && m < 38; k++) out[n - 1][++m] = name[k];
            out[n - 1][0] = (unsigned char)m;
            Fit(out[n - 1], w);
        }
    }
    return n;
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
    if (up) {                            /* a plain 1-pixel arrow, like the + */
        MoveTo(x + 16, y + 15); LineTo(x + 16, y + 23);
        MoveTo(x + 13, y + 18); LineTo(x + 16, y + 15); LineTo(x + 19, y + 18);
    } else {
        MoveTo(x + 3, y + 12); LineTo(x + 28, y + 12);
    }
}

/* A + in a tile icon's middle: plain 1-pixel strokes, about twice the
   height of the label text, with a cleared margin so it reads on any icon. */
static void PlusAt(short cx, short cy)
{
    Rect r;
    SetRect(&r, cx - 5, cy - 5, cx + 6, cy + 6);
    EraseRect(&r);
    MoveTo(cx - 4, cy); LineTo(cx + 4, cy);
    MoveTo(cx, cy - 4); LineTo(cx, cy + 4);
}

/* The + tiles: a folder (New, New Folder) or a page (New
   Document) with a + in it. */
static void IconNew(short kind, short x, short y)
{
    if (kind == kSpNewDoc) {
        IconDoc(x, y, false);
        {   /* no text lines under the + */
            Rect r;
            SetRect(&r, x + 8, y + 10, x + 25, y + 27);
            EraseRect(&r);
        }
        PlusAt(x + 16, y + 18);
    } else {
        IconFolder(x, y, false);
        PlusAt(x + 16, y + 20);
    }
}

static void TileName(short row, short idx, unsigned char *out)
{
    if (row == kRowTpl) ShownName(gTpl[idx].name, out);
    else if (row == kRowFiles) ShownName(gFiles[idx].name, out);
    else ShownName(gPrefs.recent[idx].name, out);
}

/* The rename field: as wide as the name needs (centered on its tile, kept
   on the screen, over its neighbours as in the Finder); a name wider than
   the screen scrolls to keep the insertion point in view. */
static void DrawEditField(short x, short top)
{
    Rect r, clip;
    RgnHandle old = NewRgn();
    unsigned char s[40];
    short w, fw, left, tx, cx, maxW = gWin->portRect.right - 8;
    CopyP(gEdit, s);
    w = StringWidth(s);
    fw = w + 8;
    if (fw < kTileW + 2) fw = kTileW + 2;
    if (fw > maxW) fw = maxW;
    left = x + kTileW / 2 - fw / 2;
    if (left < 4) left = 4;
    if (left + fw > gWin->portRect.right - 4) left = gWin->portRect.right - 4 - fw;
    SetRect(&r, left, top + kLabelBase - 10, left + fw, top + kLabelBase + 3);
    EraseRect(&r);
    FrameRect(&r);
    tx = left + (fw - w) / 2;
    cx = TextWidth((Ptr)s, 1, gEditCaret);
    if (w > fw - 8) {                    /* too long: scroll to the caret */
        tx = left + 4;
        if (cx > fw - 10) tx -= cx - (fw - 10);
    }
    GetClip(old);
    clip = r;
    InsetRect(&clip, 1, 1);
    ClipRect(&clip);
    MoveTo(tx, top + kLabelBase);
    DrawString(s);
    if (gEditAll && s[0] > 0) {
        Rect sel;
        SetRect(&sel, tx - 1, top + kLabelBase - 9, tx + w + 1, top + kLabelBase + 2);
        InvertRect(&sel);
    } else if (gCaretOn) {
        MoveTo(tx + cx, top + kLabelBase - 9);
        LineTo(tx + cx, top + kLabelBase + 1);
    }
    SetClip(old);
    DisposeRgn(old);
}

static void DrawTile(short row, short idx, short x, short top)
{
    unsigned char name[40];
    short w;
    Rect r;
    SetRect(&r, x - 3, top + kIconTop - 1, x + kTileW + 3,
            top + kLabelBase + (kLabelLines - 1) * kLabelLineH + 4);
    EraseRect(&r);
    if (row == kRowTpl && gTpl[idx].isFolder)
        IconFolder(x + 16, top + kIconTop, false);   /* a writing of several documents */
    else if (row == kRowTpl)
        IconDoc(x + 16, top + kIconTop, true);
    else if (IsNewTile(row, idx))
        IconNew(SpecialKind(row, idx), x + 16, top + kIconTop);
    else if (row == kRowFiles && gFiles[idx].isFolder)
        IconFolder(x + 16, top + kIconTop, IsUp(row, idx));
    else
        IconDoc(x + 16, top + kIconTop, false);

    if (gRenaming && row == gSelRow && idx == gSelIdx)
        return;                          /* DrawRow draws the field over all */
    TileName(row, idx, name);
    {   /* the name wraps under its icon, in its own column: two lines, or
           three for the picked tile (its whole name), "..." if still long */
        Boolean picked = row == gSelRow && idx == gSelIdx;
        unsigned char line[kLabelLines][40];
        short n = WrapName(name, kLabelW, picked ? kLabelLines : 2, line), k;
        for (k = 0; k < n; k++) {
            short y = top + kLabelBase + k * kLabelLineH;
            w = StringWidth(line[k]);
            MoveTo(x + (kTileW - w) / 2, y);
            DrawString(line[k]);
            if (picked) {
                SetRect(&r, x + (kTileW - w) / 2 - 2, y - 9, x + (kTileW + w) / 2 + 2, y + 2);
                InvertRect(&r);
            }
        }
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
        DrawString(PSTR("\pMY WRITINGS"));
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
        else if (row == kRowFiles) DrawString(PSTR("\pEmpty"));   /* (never at the top: + leads) */
    }
    for (i = gFirst[row]; i < n && i < gFirst[row] + kVisible; i++)
        DrawTile(row, i, kLeft + (i - gFirst[row]) * kTileStep, top);
    if (gRenaming && row == gSelRow && gSelIdx >= gFirst[row]
        && gSelIdx < gFirst[row] + kVisible)
        DrawEditField(kLeft + (gSelIdx - gFirst[row]) * kTileStep, top);
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
        if (i == gActSplit && i > 0) {  /* divider, only between two groups */
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
    {   /* Import...: a pill at the right, apart from the pick's actions */
        short w = StringWidth(PSTR("\pImport\311"));
        SetRect(&gImportRect, gWin->portRect.right - kLeft - w - 14, kHeaderH + 1,
                gWin->portRect.right - kLeft, kRowsTop - 2);
        FrameRoundRect(&gImportRect, 12, 12);
        MoveTo(gImportRect.left + 7, kHeaderH + 11);
        DrawString(PSTR("\pImport\311"));
    }
    MoveTo(0, kRowsTop - 1);
    LineTo(gWin->portRect.right, kRowsTop - 1);
}

static void DoImport(void)
{
    EndRename(true);
    if (Doc_Import())
        Lib_Close();
    else
        Lib_Draw();                      /* the dialog drew over us */
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
static void DoNewDocument(void);

/* Where a new page goes from here: a new writing at the top, else the
   writing (or folder) being browsed. */
static short NewWD(void)
{
    return gDepth == 0 ? 0 : WDOfDir(gDir[gDepth]);
}

/* A folder template (Book Project) becomes a new writing in My Writings,
   under its own name or the next free one ("Book Project 2"); the Library
   then shows it open, so its documents are one click away. */
static void NewFromFolderTemplate(short idx)
{
    short tvol, dvol;
    long tdir, ddir, nd;
    unsigned char name[40];
    Files_DocsDir(&dvol, &ddir);
    if (!Files_DirOfWD(Files_TemplatesWD(), &tvol, &tdir)
        || !Files_FreeName(dvol, ddir, gTpl[idx].name, name)
        || !Files_CopyFolderTo(tvol, tdir, gTpl[idx].name, dvol, ddir, name, &nd)) {
        SysBeep(1);
        return;
    }
    gDepth = 1;                          /* inside the new writing */
    gDir[1] = nd;
    CopyP(name, gDirName[1]);
    LoadFiles();
    gFirst[kRowFiles] = 0;
    Select(-1, 0);
    Redraw();
}

static void DoOpen(void)
{
    short wd;
    if (gSelRow < 0) {                   /* nothing picked: any file on disk */
        DoOther();
        return;
    }
    if (gSelRow == kRowTpl && gTpl[gSelIdx].isFolder) {
        NewFromFolderTemplate(gSelIdx);  /* a new writing, shown open */
    } else if (gSelRow == kRowTpl) {
        /* its text on a new page, named when first saved */
        if (Doc_NewFromTemplate(gTpl[gSelIdx].name, Files_TemplatesWD(), NewWD()))
            Lib_Close();
        else
            SysBeep(1);
    } else if (gSelRow == kRowFiles) {
        if (SpecialKind(gSelRow, gSelIdx) == kSpNewFolder)
            DoNew(false);                /* asks the folder's name */
        else if (IsNewTile(gSelRow, gSelIdx))
            DoNewDocument();
        else if (gFiles[gSelIdx].isFolder)
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
    short i, max = kNameMax;            /* the same limit the app names with */
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
    Boolean ok = AskName(false, name);
    (void)project;
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
    if (!GetTarget(&t)) return;
    if (t.isFolder) {                    /* a writing or folder: copied beside it */
        CopyP(t.name, base);
        if (base[0] > 22) base[0] = 22;
        {
            const char *c = " copy";
            while (*c) base[++base[0]] = (unsigned char)*c++;
        }
        if (!Files_FreeName(t.vol, t.dir, base, name)
            || !Files_CopyFolder(t.vol, t.dir, t.name, name))
            SysBeep(1);
        else {
            Reload();
            SelectFileNamed(name);
        }
        Redraw();
        return;
    }
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
        if (Doc_Location(&v, &d) && v == t.vol && Files_IsInside(v, d, t.selfDir))
            Doc_FileTrashed(Doc_WD(), Doc_Name());   /* writing goes on, untitled */
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

/* At the top (or the + tile): a new writing, named when first saved.
   Inside a writing: a new document there. */
static void DoNewDocument(void)
{
    Doc_SetNewFolder(NewWD());
    Doc_New();
    Lib_Close();
}

static void Run(short act)
{
    switch (act) {
    case kActOpen:       DoOpen(); break;
    case kActDuplicate:  DoDuplicate(); break;
    case kActRename:     StartRename(); break;
    case kActTrash:      DoTrash(); break;
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
    Files_DocsDir(&gVol, &gDir[0]);      /* depth 0 is My Writings */
    CopyP((const unsigned char *)"\pMy Writings", gDirName[0]);
    gDepth = 0;
}

void Lib_Open(void)
{
    Doc_Suspend();                       /* the .md current before browsing:
                                            Duplicate copies what you see */
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

/* The drawer's New Folder... asks with the same dialog. */
Boolean Lib_AskFolderName(unsigned char *name)
{
    return AskName(false, name);
}

/* The library moved: browse from its new top. */
void Lib_Reset(void)
{
    gDepth = 0;
    Files_DocsDir(&gVol, &gDir[0]);
}

/* Cmd-N: the Library at the top with New picked; Return starts
   it (Cmd-N again too). */
void Lib_OpenNew(void)
{
    if (gOpen) {                         /* Cmd-N inside the Library: make it */
        DoNewDocument();
        return;
    }
    gDepth = 0;
    Lib_Open();
    gFirst[kRowFiles] = 0;
    Select(kRowFiles, 0);
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
        if (gEdit[0] >= kNameMax) { SysBeep(1); return; }   /* names stop at kNameMax */
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
    if (row < 0) {                       /* nothing picked: start in My Writings */
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
        if (IsSpecial(row, i)) continue;
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
        else DoNewDocument();            /* nothing picked: a new writing here */
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
        if (PtInRect(pt, &gImportRect)) {
            InvertRoundRect(&gImportRect, 12, 12);
            while (StillDown()) ;
            DoImport();
            return;
        }
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
    if (IsSpecial(row, idx)) {
        /* the + tiles and the up tile are buttons, as in the classic Finder
           and Standard File: one click does it (on release over the tile) */
        Point up;
        short x0 = kLeft + (idx - gFirst[row]) * kTileStep;
        Select(row, idx);
        Redraw();
        while (StillDown()) ;
        GetMouse(&up);
        gLastRow = -1;
        if (up.h >= x0 && up.h < x0 + kTileW
            && up.v >= kRowsTop + row * kRowH && up.v < kRowsTop + (row + 1) * kRowH)
            DoOpen();
        return;
    }
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
