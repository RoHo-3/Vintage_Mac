#include "document.h"
#include "editor.h"
#include "encoding.h"
#include "textbuf.h"
#include "chrome.h"
#include "journal.h"
#include "files.h"

#include <Files.h>
#include <StandardFile.h>
#include <Events.h>
#include <OSUtils.h>

#define kSaveTicks     180               /* a 3 s pause saves: the footer shows
                                            the open ring at 2 s, then this */
#define kMaxAgeTicks   (3L * 60 * 60)    /* nonstop typing: every 3 minutes */
#define kCreator       'MMDW'
#define kFileType      'TEXT'

static Boolean    gHasFile;
static Str63      gName;
static short      gVRef;
static FileFormat gFmt;
static Boolean    gMdBehind;            /* journal has text the .md lacks */
static short      gNewWD;               /* the writing a new document joins; 0:
                                            it starts a new writing (a folder in
                                            My Writings, made on its first save) */
#define gDefaultVRef (gNewWD ? gNewWD : Files_DocsWD())

/* Is this working directory My Writings itself (where writings start)? */
static Boolean IsWritingsRoot(short wd)
{
    short v1, v2;
    long d1, d2;
    if (wd == Files_DocsWD()) return true;
    Files_DocsDir(&v2, &d2);
    return Files_DirOfWD(wd, &v1, &d1) && v1 == v2 && d1 == d2;
}

/* "Name.md" -> "Name" */
static void BaseOf(const unsigned char *md, unsigned char *base)
{
    short i;
    for (i = 0; i <= md[0]; i++) base[i] = md[i];
    if (base[0] > 3 && base[base[0] - 2] == '.' && (base[base[0] - 1] | 0x20) == 'm'
        && (base[base[0]] | 0x20) == 'd')
        base[0] -= 3;
}

static void CopyPString(const unsigned char *src, unsigned char *dst)
{
    short i;
    for (i = 0; i <= src[0]; i++)
        dst[i] = src[i];
}

static Point DialogSpot(void)
{
    Point p;
    p.h = 80;
    p.v = 60;
    return p;
}

void Doc_Init(void)
{
    gHasFile = false;
    gFmt.lineEnding = kEndLF;            /* new files: the modern default */
    gFmt.bom = false;
}

/* Suggested name: the first line without Markdown markers, at most 28
   characters at a word boundary, ':' (illegal in HFS) → '-', plus ".md". */
static void SuggestName(unsigned char *name)
{
    long len = TB_Len(), p = 0;
    short n = 0, lastSpace = 0;
    unsigned char c;
    Boolean sentence = false;            /* stopped at the first sentence's end */

    /* skip line markers: headings "## ", quotes "> ", bullets "- " "* " "+ ",
       numbers "1. ", tasks "[ ] " */
    for (;;) {
        long q = p;
        while (p < len && (TB_Char(p) == '#' || TB_Char(p) == '>' || TB_Char(p) == ' '))
            p++;
        if (p + 1 < len && (TB_Char(p) == '-' || TB_Char(p) == '*' || TB_Char(p) == '+')
            && TB_Char(p + 1) == ' ')
            p += 2;
        {
            long d = p;                  /* "12. " or "3) ": a numbered item */
            while (d < len && TB_Char(d) >= '0' && TB_Char(d) <= '9' && d - p < 4)
                d++;
            if (d > p && d + 1 < len && (TB_Char(d) == '.' || TB_Char(d) == ')')
                && TB_Char(d + 1) == ' ')
                p = d + 2;               /* otherwise the digits are words */
        }
        if (p + 3 < len && TB_Char(p) == '[' && TB_Char(p + 2) == ']' && TB_Char(p + 3) == ' ')
            p += 4;
        if (p == q) break;
    }
    for (; p < len && n < 28; p++) {
        c = TB_Char(p);
        if (c == '\r') break;
        if (c == ']' && p + 1 < len && TB_Char(p + 1) == '(') {   /* link target */
            while (p < len && TB_Char(p) != ')' && TB_Char(p) != '\r') p++;
            if (p < len && TB_Char(p) == '\r') break;
            continue;
        }
        if (c == '!' && p + 1 < len && TB_Char(p + 1) == '[') continue;   /* image */
        if ((c == '.' || c == '?' || c == '!')                    /* first sentence */
            && (p + 1 >= len || TB_Char(p + 1) == ' ' || TB_Char(p + 1) == '\r')
            && !(c == '.' && n >= 2 && name[n - 1] == '.')) {   /* not "e.g." */
            sentence = true;
            break;
        }
        /* Markdown markers, and what Windows and FAT drives refuse in names */
        if (c == '*' || c == '_' || c == '`' || c == '~' || c == '[' || c == ']'
            || c == '<' || c == '>' || c == '|' || c == '\\' || c == '"' || c == '?'
            || c < 0x20)
            continue;
        if (c == ':' || c == '/') c = '-';
        if (c == ' ' && (n == 0 || name[n] == ' ')) continue;   /* no doubled spaces */
        if (c == ' ') lastSpace = n;
        name[++n] = c;
    }
    if (!sentence && p < len && TB_Char(p) != '\r' && lastSpace > 0)
        n = lastSpace;                   /* too long: cut at a word boundary */
    {   /* a heading's closing #s: a run of 2+, or after a space ("C#" stays) */
        short k = n;
        while (k > 0 && name[k] == '#') k--;
        if (n - k >= 2 || (n > k && k > 0 && name[k] == ' '))
            n = k;
    }
    while (n > 0 && (name[n] == ' ' || name[n] == '.' || name[n] == ','
                     || name[n] == ';' || name[n] == '-'))
        n--;                             /* "Woke up laughing." -> no "..md" */
    {   /* Title Case: each word's first letter up, but leave words with a
           period or digit ("e.g.", "v1.2") or a capital already ("iPhone") */
        short w = 1, e, k;
        while (w <= n) {
            Boolean keep = false;
            for (e = w; e <= n && name[e] != ' ' && name[e] != '-'; e++)
                if (name[e] == '.' || (name[e] >= '0' && name[e] <= '9')
                    || (name[e] >= 'A' && name[e] <= 'Z'))
                    keep = true;
            if (!keep && name[w] >= 'a' && name[w] <= 'z')
                name[w] -= 32;
            for (k = e; k <= n && (name[k] == ' ' || name[k] == '-'); k++)
                ;
            w = k;
        }
    }
    name[0] = (unsigned char)n;
    Files_FitName(name);                 /* the one limit, at a whole word */
    n = name[0];
    if (n == 0) {
        const char *u = "Untitled";
        while (*u) name[++n] = (unsigned char)*u++;
    }
    name[++n] = '.'; name[++n] = 'm'; name[++n] = 'd';
    name[0] = (unsigned char)n;
}

static Boolean OpenFile(const unsigned char *name, short vRef);

static Boolean SameName(const unsigned char *a, const unsigned char *b)
{
    short i;
    if (a[0] != b[0]) return false;
    for (i = 1; i <= a[0]; i++) {
        unsigned char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x -= 32;   /* HFS names ignore case */
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return false;
    }
    return true;
}

/* Free in vRef, or the open document's own name there. */
static Boolean NameFree(const unsigned char *name, short vRef)
{
    FInfo fi;
    if (gHasFile && vRef == gVRef && SameName(name, gName))
        return true;
    return GetFInfo(name, vRef, &fi) == fnfErr;
}

/* SuggestName, made unique in vRef: "Day Two 2.md", ... */
static Boolean UniqueName(unsigned char *name, short vRef)
{
    unsigned char base[40];
    short n, k, i;

    SuggestName(name);
    if (IsWritingsRoot(vRef)) {          /* a new writing: its folder's name */
        short v;
        long d;
        unsigned char free[40];
        BaseOf(name, base);
        Files_DocsDir(&v, &d);
        if (!Files_FreeName(v, d, base, free)) return false;
        for (i = 0; i <= free[0]; i++) name[i] = free[i];
        name[++name[0]] = '.'; name[++name[0]] = 'm'; name[++name[0]] = 'd';
        return true;
    }
    if (NameFree(name, vRef))
        return true;
    CopyPString(name, base);
    base[0] -= 3;                        /* without ".md" */
    if (base[0] > kNameMax - 3) base[0] = kNameMax - 3;   /* room for " 99" */
    while (base[0] > 0 && base[base[0]] == ' ') base[0]--;
    for (k = 2; k < 100; k++) {
        n = 0;
        for (i = 1; i <= base[0]; i++) name[++n] = base[i];
        name[++n] = ' ';
        if (k >= 10) name[++n] = (unsigned char)('0' + k / 10);
        name[++n] = (unsigned char)('0' + k % 10);
        name[++n] = '.'; name[++n] = 'm'; name[++n] = 'd';
        name[0] = (unsigned char)n;
        if (NameFree(name, vRef))
            return true;
    }
    return false;
}

/* The writing (top folder in My Writings) the open document is the
   namesake of: the folder has the document's name. */
static Boolean NamesakeFolder(short *vol, long *dir, unsigned char *folder)
{
    long proj;
    unsigned char base[40];
    if (!gHasFile || !Files_DirOfWD(gVRef, vol, dir)) return false;
    if (!Files_ProjectOf(*vol, *dir, &proj) || proj != *dir) return false;
    if (!Files_FolderName(*vol, *dir, folder)) return false;
    BaseOf(gName, base);
    return SameName(folder, base);
}

/* An app-named document's name follows its first line (DESIGN.md, Naming
   documents) until the writer renames it; the journal is renamed with it.
   A writing's namesake document takes its folder along: the two names
   come together from what's free in My Writings (its own folder counts as
   free), so they never drift apart. */
static void FollowFirstLine(void)
{
    unsigned char want[40], folder[40], base[40], cand[40];
    short vol, wv, k, i, n;
    long dir, wdir;
    Boolean namesake;
    if (!gHasFile || !(JR_Prefs() & kPrefNameFollows))
        return;
    namesake = NamesakeFolder(&vol, &dir, folder);
    if (namesake) {
        SuggestName(want);
        BaseOf(want, base);
        Files_FitName(base);
        Files_DocsDir(&wv, &wdir);
        for (k = 1; k < 100; k++) {      /* base, "base 2", ... */
            n = 0;
            for (i = 1; i <= base[0]; i++) cand[++n] = base[i];
            if (k > 1) {
                cand[++n] = ' ';
                if (k >= 10) cand[++n] = (unsigned char)('0' + k / 10);
                cand[++n] = (unsigned char)('0' + k % 10);
            }
            cand[0] = (unsigned char)n;
            if (SameName(cand, folder) || !Files_Exists(wv, wdir, cand))
                break;
        }
        if (k >= 100) return;
        for (i = 0; i <= cand[0]; i++) want[i] = cand[i];
        want[++want[0]] = '.'; want[++want[0]] = 'm'; want[++want[0]] = 'd';
    } else if (!UniqueName(want, gVRef))
        return;
    if (SameName(want, gName))
        return;
    if (Rename(gName, gVRef, want) != noErr)
        return;
    if (!JR_Rename(want)) {              /* the journal couldn't follow: undo, */
        Rename(want, gVRef, gName);      /* so the pair stays together */
        return;
    }
    if (namesake && !SameName(folder, cand))
        Files_Rename(wv, wdir, folder, cand);
    CopyPString(want, gName);
    Chrome_SetTitle(gName);
}

static Boolean WriteFile(void)
{
    long n, max = Enc_MaxEncoded();
    Ptr buf = NewPtr(max);
    short ref;
    OSErr err;
    FInfo fi;

    if (!buf) { SysBeep(1); return false; }
    FollowFirstLine();
    Chrome_SetSaveState(kSaveSaving);    /* dithered dot until the write ends */
    n = Enc_Encode(buf, max, &gFmt);
    err = (n < 0) ? -1 : FSOpen(gName, gVRef, &ref);
    if (err == noErr) {
        long count = n;
        err = SetFPos(ref, fsFromStart, 0);
        if (err == noErr) err = FSWrite(ref, &count, buf);
        if (err == noErr) err = SetEOF(ref, n);
        FSClose(ref);
    }
    DisposePtr(buf);
    if (err != noErr) {
        Chrome_SetSaveState(kSaveUnsaved);
        SysBeep(1);
        return false;
    }
    /* files from modern machines arrive untyped: claim them */
    if (GetFInfo(gName, gVRef, &fi) == noErr
        && (fi.fdType != kFileType || fi.fdCreator != kCreator)) {
        fi.fdType = kFileType;
        fi.fdCreator = kCreator;
        SetFInfo(gName, gVRef, &fi);
    }
    FlushVol(NULL, gVRef);
    Ed_ClearChanges();
    gMdBehind = false;
    Chrome_SetSaveState(kSaveSaved);
    return true;
}

/* Give the untitled document its file. follows: the app picked the name,
   so it keeps following the first line. */
static Boolean BecomeFile(const unsigned char *name, short vRef, Boolean follows)
{
    unsigned char prefs = JR_Prefs();
    OSErr err;
    if (IsWritingsRoot(vRef)) {          /* a new writing: its folder, then the */
        short vol;                       /* document inside, both one name */
        long dir, folder;
        unsigned char base[40], free[40];
        BaseOf(name, base);
        Files_FitName(base);
        if (!Files_DirOfWD(vRef, &vol, &dir) || !Files_FreeName(vol, dir, base, free)
            || !Files_NewFolder(vol, dir, free, &folder) || !Files_WDFor(vol, folder, &vRef)) {
            SysBeep(1);
            return false;
        }
        gNewWD = vRef;                   /* more new documents join it */
    }
    err = Create(name, vRef, kCreator, kFileType);
    if (err != noErr && !(err == dupFNErr && !follows)) {   /* dialog: Replace */
        SysBeep(1);
        return false;
    }
    CopyPString(name, gName);
    gVRef = vRef;
    gHasFile = true;
    Chrome_SetTitle(gName);
    JR_Attach(gName, gVRef);
    JR_SetPrefs(follows ? (prefs | kPrefNameFollows)    /* keep what was chosen */
                        : (prefs & ~kPrefNameFollows)); /* while untitled */
    JR_Reset();                          /* the journal starts with a snapshot */
    return true;
}

/* Cmd-S: an untitled document gets the classic Save dialog, opened in My
   Documents (or the current project) with the first-line name filled in;
   a document with a file just saves. */
Boolean Doc_SaveCommand(void)
{
    SFReply reply;
    unsigned char suggested[40];
    Chrome_ShowSaveDot();                /* the dot shows the save happen */
    if (gHasFile) {
        Chrome_ShowTitleBriefly();       /* and the name it's saved under */
        if (Ed_ChangeWeight() == 0 && !gMdBehind) {
            Chrome_SetSaveState(kSaveSaved);   /* nothing new: already solid */
            return true;
        }
        return Doc_Save();
    }
    if (!UniqueName(suggested, gDefaultVRef))   /* a free name, as quiet naming */
        SuggestName(suggested);
    Files_SetDialogFolder(gDefaultVRef);
    SFPutFile(DialogSpot(), IsWritingsRoot(gDefaultVRef)
              ? (ConstStringPtr)"\pName this writing:" : (ConstStringPtr)"\pSave as:",
              suggested, NULL, &reply);
    if (!reply.good)
        return false;
    if (!BecomeFile(reply.fName, reply.vRefNum, false))
        return false;
    Chrome_ShowTitleBriefly();           /* the name just chosen */
    return Doc_Save();
}

Boolean Doc_Save(void)
{
    unsigned char suggested[40];
    Boolean wasUntitled = !gHasFile;

    if (!gHasFile) {
        /* Autosave, quit, switching: named in place from the first line,
           in a new writing (or the writing it joins). No dialog. */
        if (!UniqueName(suggested, gDefaultVRef) || !BecomeFile(suggested, gDefaultVRef, true)) {
            SysBeep(1);
            return false;
        }
    }
    JR_Checkpoint(Ed_Caret(), NULL);     /* journal first, then the .md */
    if (!WriteFile())
        return false;
    if (wasUntitled)
        Doc_Remember();                  /* reopen it on the next launch */
    return true;
}

/* Before quitting or switching: the .md must hold the latest text, even
   if autosave only reached the journal. */
static Boolean SaveIfNeeded(void)
{
    if (Ed_ChangeWeight() == 0 && !gMdBehind)
        return true;
    return Doc_Save();
}

/* Where the next new document goes: 0 starts a new writing; a writing's
   folder (or one inside it) adds the document there. */
void Doc_SetNewFolder(short wd) { gNewWD = wd; }

/* The text in the editor becomes a new writing called name ("Product
   Guide.md"): its folder and document, saved now, with its history. The
   name is the writer's, not the app's: it doesn't follow the first line. */
Boolean Doc_SaveNewWriting(const unsigned char *name)
{
    gNewWD = 0;
    if (!BecomeFile(name, Files_DocsWD(), false))
        return false;
    if (!Doc_Save())
        return false;
    Doc_Remember();
    return true;
}

/* A blank page, named on its first save (see Doc_SetNewFolder). */
void Doc_New(void)
{
    if (!SaveIfNeeded())
        return;
    gHasFile = false;
    JR_Detach();
    Doc_Init();
    Ed_SetShowMarks(false);
    Ed_SetText("", 0);
    Chrome_SetTitle((const unsigned char *)"\pUntitled");
    Chrome_TextChanged();
    Chrome_ResetClock();
    Doc_Remember();                      /* nothing to reopen until it's saved */
}

static Boolean SameLoc(const FolderLoc *a, const FolderLoc *b)
{
    return a->dirID == b->dirID && SameName(a->volName, b->volName);
}

/* Remember the open document and its caret for the next launch, and put
   it first in Recent. */
void Doc_Remember(void)
{
    Prefs p;
    short i, k;
    Prefs_Load(&p);
    p.lastName[0] = 0;
    if (gHasFile && Files_LocOf(gVRef, &p.lastDir)) {
        CopyPString(gName, p.lastName);
        p.lastCaret = Ed_Caret();
        for (i = k = 0; i < p.recentCount; i++)      /* drop it, then put it first */
            if (!(SameLoc(&p.recent[i].dir, &p.lastDir) && SameName(p.recent[i].name, gName)))
                p.recent[k++] = p.recent[i];
        if (k == kMaxRecent) k--;
        for (i = k; i > 0; i--) p.recent[i] = p.recent[i - 1];
        p.recent[0].dir = p.lastDir;
        CopyPString(gName, p.recent[0].name);
        p.recentCount = k + 1;
    }
    Prefs_Save(&p);
}

/* Recent: a file that is gone, or was renamed or trashed from the Library. */
void Doc_ForgetRecent(short wd, const unsigned char *name, const unsigned char *newName)
{
    Prefs p;
    FolderLoc loc;
    short i, k;
    if (!Files_LocOf(wd, &loc)) return;
    Prefs_Load(&p);
    for (i = k = 0; i < p.recentCount; i++) {
        if (SameLoc(&p.recent[i].dir, &loc) && SameName(p.recent[i].name, name)) {
            if (!newName) continue;                  /* trashed: drop it */
            CopyPString(newName, p.recent[i].name);  /* renamed: follow it */
        }
        p.recent[k++] = p.recent[i];
    }
    p.recentCount = k;
    if (SameLoc(&p.lastDir, &loc) && SameName(p.lastName, name)) {
        if (newName) CopyPString(newName, p.lastName);
        else p.lastName[0] = 0;
    }
    Prefs_Save(&p);
}

Boolean Doc_HasFile(void) { return gHasFile; }
short Doc_WD(void) { return gVRef; }
const unsigned char *Doc_Name(void) { return gName; }

Boolean Doc_IsOpen(short wd, const unsigned char *name)
{
    short v1, v2;
    long d1, d2;
    if (!gHasFile || !SameName(name, gName)) return false;
    if (wd == gVRef) return true;
    return Files_DirOfWD(wd, &v1, &d1) && Files_DirOfWD(gVRef, &v2, &d2)
        && v1 == v2 && d1 == d2;
}

/* The Library renamed a file: the open document (and its journal) follow,
   and a hand-picked name stops following the first line. */
Boolean Doc_RenameFile(short wd, const unsigned char *from, const unsigned char *to)
{
    unsigned char h1[40], h2[40];
    Boolean open = Doc_IsOpen(wd, from);
    if (Rename(from, wd, to) != noErr)
        return false;
    JR_HistoryName(from, h1);
    JR_HistoryName(to, h2);
    if (open) {
        if (!JR_Rename(to)) {            /* renames the .history too */
            Rename(to, wd, from);
            return false;
        }
        CopyPString(to, gName);
        Chrome_SetTitle(gName);
    } else
        Rename(h1, wd, h2);              /* fnfErr: no journal, fine */
    JR_MarkRenamed(to, wd);
    Doc_ForgetRecent(wd, from, to);
    return true;
}

/* The Library is trashing a file: if it's the open document, writing goes
   on in a new untitled one. Its journal goes to the Trash with it. */
void Doc_FileTrashed(short wd, const unsigned char *name)
{
    if (Doc_IsOpen(wd, name)) {
        gHasFile = false;
        gNewWD = 0;                      /* writing goes on in a new writing */
        JR_Detach();
        Doc_Init();
        Ed_SetText("", 0);
        Chrome_SetTitle((const unsigned char *)"\pUntitled");
        Chrome_TextChanged();
    }
    Doc_ForgetRecent(wd, name, NULL);
}

/* Open a file picked in the Library. */
Boolean Doc_OpenAt(const unsigned char *name, short wd)
{
    if (Doc_IsOpen(wd, name))
        return true;                     /* already here */
    if (!SaveIfNeeded())
        return false;
    if (!OpenFile(name, wd))
        return false;
    Doc_Remember();
    return true;
}

/* A new writing (destWD 0) or a new document in a writing (destWD) from a
   template (Blank is one): its text fills an untitled page, named on the
   first save like any new page. The template itself is never changed. */
Boolean Doc_NewFromTemplate(const unsigned char *name, short wd, short destWD)
{
    short ref;
    long size, n, len = -1;
    Ptr raw = NULL, text = NULL;
    FileFormat fmt;
    if (!SaveIfNeeded())
        return false;
    if (FSOpen(name, wd, &ref) != noErr)
        return false;
    GetEOF(ref, &size);
    raw = NewPtr(size + 1);
    text = NewPtr(TB_Capacity());
    if (raw && text) {
        n = size;
        FSRead(ref, &n, raw);
        len = Enc_Decode((unsigned char *)raw, n, text, TB_Capacity(), &fmt);
    }
    FSClose(ref);
    if (len >= 0) {
        gHasFile = false;
        gNewWD = destWD;
        JR_Detach();
        Doc_Init();
        gFmt = fmt;
        Ed_SetShowMarks(false);
        Ed_SetText(text, len);
        Chrome_SetTitle((const unsigned char *)"\pUntitled");
        Chrome_TextChanged();
        Chrome_ResetClock();
        Doc_Remember();                  /* nothing to reopen until it's saved */
    }
    if (raw) DisposePtr(raw);
    if (text) DisposePtr(text);
    return len >= 0;
}

/* Launch: reopen the last document at its caret. False: none (first run,
   or the file is gone). */
Boolean Doc_Reopen(void)
{
    Prefs p;
    short wd;
    Prefs_Load(&p);
    if (p.lastName[0] == 0 || !Files_WDOf(&p.lastDir, &wd))
        return false;
    if (!OpenFile(p.lastName, wd)) {
        Doc_ForgetRecent(wd, p.lastName, NULL);   /* gone: don't try again */
        return false;
    }
    Ed_SetCaret(p.lastCaret);
    return true;
}

Boolean Doc_Open(void)
{
    SFReply reply;
    GrafPtr port;
    if (!SaveIfNeeded())
        return false;
    GetPort(&port);
    SFGetFile(DialogSpot(), (ConstStringPtr)"\p", NULL, -1, NULL, NULL, &reply);
    SetPort(port);                       /* our window, not the dialog's */
    if (!reply.good || !OpenFile(reply.fName, reply.vRefNum))
        return false;
    Doc_Remember();
    return true;
}

static Boolean FileSize(const unsigned char *name, short wd, long *size)
{
    short ref;
    if (FSOpen(name, wd, &ref) != noErr) return false;
    GetEOF(ref, size);
    FSClose(ref);
    return true;
}

/* Library > Import...: a Markdown file from anywhere is copied (never moved)
   into a new writing of its own name in My Writings, gets its history, and
   opens. The original is left as it was. */
Boolean Doc_Import(void)
{
    SFReply reply;
    unsigned char base[40], free[40], md[40];
    short v, wd, i, n;
    long d, nd, srcSize, dstSize;
    Boolean reused = false;
    GrafPtr port;
    if (!SaveIfNeeded())
        return false;
    GetPort(&port);
    SFGetFile(DialogSpot(), (ConstStringPtr)"\p", NULL, -1, NULL, NULL, &reply);
    SetPort(port);                       /* our window, not the dialog's */
    if (!reply.good)
        return false;
    for (i = 0; i <= reply.fName[0]; i++) base[i] = reply.fName[i];
    n = base[0];                         /* "Notes.md", ".txt", ".markdown" off */
    for (i = n; i > 1 && base[i] != '.'; i--)
        ;
    if (i > 1 && n - i <= 8) base[0] = (unsigned char)(i - 1);
    Files_FitName(base);                 /* the one limit, at a whole word */
    Files_DocsDir(&v, &d);
    {
        DirItem items[2];
        unsigned char hist[40];
        if (Files_FolderNamed(v, d, base, &nd)
            && Files_List(v, nd, items, 2) == 0 && Files_WDFor(v, nd, &wd)) {
            /* a writing of that name with no documents left (its file was
               taken out): the file goes back into it, a fresh history too */
            for (i = 0; i <= base[0]; i++) free[i] = base[i];
            for (i = 0; i <= base[0]; i++) md[i] = base[i];
            md[++md[0]] = '.'; md[++md[0]] = 'm'; md[++md[0]] = 'd';
            JR_HistoryName(md, hist);
            Files_Trash(v, nd, hist);    /* fnfErr: there was none */
            reused = true;
        } else if (!Files_FreeName(v, d, base, free) || !Files_NewFolder(v, d, free, &nd)
                   || !Files_WDFor(v, nd, &wd)) {
            SysBeep(1);
            return false;
        }
    }
    if (!reused) {
        for (i = 0; i <= free[0]; i++) md[i] = free[i];
        md[++md[0]] = '.'; md[++md[0]] = 'm'; md[++md[0]] = 'd';
    }
    /* the copy must be whole (same size as the original) before it opens;
       anything less is refused, nothing half-made left behind */
    if (!Files_CopyTo(reply.vRefNum, reply.fName, wd, md)
        || !FileSize(reply.fName, reply.vRefNum, &srcSize)
        || !FileSize(md, wd, &dstSize) || srcSize != dstSize
        || !OpenFile(md, wd)) {
        if (reused) Files_Trash(v, nd, md);   /* the folder was there before */
        else Files_Trash(v, d, free);
        SysBeep(1);
        return false;
    }
    gNewWD = wd;
    JR_Checkpoint(Ed_Caret(), "Imported");   /* its history starts here */
    Doc_Remember();
    return true;
}

/* The open document's folder (volume, dirID). */
Boolean Doc_Location(short *vol, long *dirID)
{
    return gHasFile && Files_DirOfWD(gVRef, vol, dirID);
}

static Boolean OpenFile(const unsigned char *name, short vRef)
{
    short ref;
    long size, n;
    Ptr raw, text;
    OSErr err;
    Boolean ok = false;

    err = FSOpen(name, vRef, &ref);
    if (err != noErr) return false;
    GetEOF(ref, &size);
    raw = NewPtr(size + 1);
    text = NewPtr(TB_Capacity());
    if (!raw || !text) {
        FSClose(ref);
        if (raw) DisposePtr(raw);
        if (text) DisposePtr(text);
        SysBeep(1);                      /* too big for memory */
        return false;
    }
    n = size;
    err = FSRead(ref, &n, raw);
    FSClose(ref);
    if (err == noErr || err == eofErr) {
        FileFormat fmt;
        long len = Enc_Decode((unsigned char *)raw, n, text, TB_Capacity(), &fmt);
        short recovered = kRecoverNone;
        if (len >= 0 && TB_Replace(0, TB_Len(), text, len)) {
            JR_Attach(name, vRef);
            recovered = JR_Recover();    /* may leave newer text in the buffer */
            len = TB_Len();
            TB_Copy(0, len, text);
        } else
            len = -1;
        if (len >= 0)
            Ed_SetShowMarks((JR_Prefs() & kPrefShowMarks) != 0);
        if (len >= 0 && Ed_SetText(text, len)) {
            CopyPString(name, gName);
            gVRef = vRef;
            gFmt = fmt;
            gHasFile = true;
            Chrome_SetTitle(gName);
            Chrome_TextChanged();
            Chrome_SetSaveState(kSaveSaved);
            Chrome_ResetClock();
            if (recovered == kRecoverJournalNewer) {
                /* work lost in a crash: quietly bring the .md up to date */
                JR_Checkpoint(Ed_Caret(), "Recovered after a crash");
                WriteFile();
            } else if (recovered == kRecoverEditedElsewhere)
                JR_Checkpoint(Ed_Caret(), "Edited on another computer");
            ok = true;
        } else
            SysBeep(1);                  /* too big for the text buffer */
    } else
        SysBeep(1);
    DisposePtr(raw);
    DisposePtr(text);
    return ok;
}

void Doc_Idle(void)
{
    unsigned long now;
    if (Ed_ChangeWeight() == 0)
        return;
    now = TickCount();
    if (now - Ed_LastEditTick() < kSaveTicks
        && now - Ed_FirstChangeTick() < kMaxAgeTicks)
        return;                          /* still typing */
    {
        if (!gHasFile) {                 /* first save names it in place */
            static unsigned long failedAt;
            if (failedAt == Ed_FirstChangeTick())
                return;                  /* failed for these edits: no beep loop */
            if (!Doc_Save())
                failedAt = Ed_FirstChangeTick();
            return;
        }
        /* one small append to the journal; the .md waits for Cmd-S,
           quit, suspend or switching documents */
        Chrome_SetSaveState(kSaveSaving);
        if (JR_Checkpoint(Ed_Caret(), NULL)) {
            Ed_ClearChanges();
            gMdBehind = true;
            Chrome_SetSaveState(kSaveSaved);
        } else
            WriteFile();                 /* no journal: write the file */
    }
}

void Doc_Suspend(void)
{
    if (gHasFile && (Ed_ChangeWeight() > 0 || gMdBehind)) {
        JR_Checkpoint(Ed_Caret(), NULL);
        WriteFile();
    }
    Doc_Remember();
}

Boolean Doc_CanQuit(void)
{
    if (!SaveIfNeeded())
        return false;
    Doc_Remember();
    return true;
}

/* Markdown markers on or off for this document (Cmd-M for now; it moves
   to the document drawer). Saved in the journal with the next checkpoint. */
void Doc_ToggleMarks(void)
{
    unsigned char p = JR_Prefs() ^ kPrefShowMarks;
    JR_SetPrefs(p);
    Ed_SetShowMarks((p & kPrefShowMarks) != 0);
    if (gHasFile && Ed_ChangeWeight() == 0)
        JR_Checkpoint(Ed_Caret(), NULL); /* nothing else pending: write it now */
}
