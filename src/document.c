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
static short      gNewWD;               /* where the next new document goes */
#define gDefaultVRef (gNewWD ? gNewWD : Files_DocsWD())

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

    while (p < len && (TB_Char(p) == '#' || TB_Char(p) == ' '))
        p++;
    for (; p < len && n < 28; p++) {
        c = TB_Char(p);
        if (c == '\r') break;
        if (c == '*' || c == '_' || c == '`' || c < 0x20) continue;
        if (c == ':') c = '-';
        if (c == ' ') lastSpace = n;
        name[++n] = c;
    }
    if (p < len && TB_Char(p) != '\r' && lastSpace > 0)
        n = lastSpace;                   /* cut at a word boundary */
    while (n > 0 && name[n] == ' ')
        n--;
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
    if (NameFree(name, vRef))
        return true;
    CopyPString(name, base);
    base[0] -= 3;                        /* without ".md" */
    if (base[0] > 24) base[0] = 24;      /* room for " 99" + journal's limit */
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

/* An app-named document's name follows its first line (DESIGN.md, Naming
   documents) until the writer renames it; the journal is renamed with it. */
static void FollowFirstLine(void)
{
    unsigned char want[40];
    if (!gHasFile || !(JR_Prefs() & kPrefNameFollows))
        return;
    if (!UniqueName(want, gVRef) || SameName(want, gName))
        return;
    if (Rename(gName, gVRef, want) != noErr)
        return;
    JR_Rename(want);
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
    OSErr err = Create(name, vRef, kCreator, kFileType);
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
    if (gHasFile)
        return Doc_Save();
    SuggestName(suggested);
    Files_SetDialogFolder(gDefaultVRef);
    SFPutFile(DialogSpot(), (ConstStringPtr)"\pSave as:", suggested, NULL, &reply);
    if (!reply.good)
        return false;
    if (!BecomeFile(reply.fName, reply.vRefNum, false))
        return false;
    return Doc_Save();
}

Boolean Doc_Save(void)
{
    unsigned char suggested[40];
    Boolean wasUntitled = !gHasFile;

    if (!gHasFile) {
        /* Autosave, quit, switching: named in place from the first line,
           in My Documents or the current project. No dialog. */
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

/* Cmd-N inside a project creates the document in that project; otherwise
   new documents go to My Documents. */
static void NewGoesBesideCurrent(void)
{
    short vol, mvol;
    long dir, mdir;
    gNewWD = 0;
    if (gHasFile && Files_DirOfWD(gVRef, &vol, &dir)) {
        Files_ProjectsDir(&mvol, &mdir);           /* inside a project */
        if (vol == mvol && dir != mdir && Files_IsInside(vol, dir, mdir))
            gNewWD = gVRef;
    }
}

void Doc_SetNewFolder(short wd) { gNewWD = wd; }

void Doc_New(void)
{
    if (!SaveIfNeeded())
        return;
    NewGoesBesideCurrent();
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
        JR_Rename(to);                   /* renames the .history too */
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

/* A new document from a template (Blank is one): the Save dialog comes
   first, in destWD with the template's name filled in ("Untitled" for
   Blank); then the template's text fills the new file and writing begins.
   The template itself is never changed. Cancel: nothing changes. */
Boolean Doc_NewFromTemplate(const unsigned char *name, short wd, short destWD)
{
    SFReply reply;
    unsigned char suggest[40];
    short ref;
    long size, n, len = -1;
    Ptr raw = NULL, text = NULL;
    FileFormat fmt;
    if (!SaveIfNeeded())
        return false;
    if (name[0] == 8 && (name[1] | 0x20) == 'b' && (name[2] | 0x20) == 'l'
        && (name[3] | 0x20) == 'a' && (name[4] | 0x20) == 'n' && (name[5] | 0x20) == 'k')
        CopyPString((const unsigned char *)"\pUntitled.md", suggest);
    else
        CopyPString(name, suggest);
    Files_SetDialogFolder(destWD);
    SFPutFile(DialogSpot(), (ConstStringPtr)"\pSave new document as:", suggest, NULL, &reply);
    if (!reply.good)
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
        JR_Detach();
        Doc_Init();
        gFmt = fmt;
        Ed_SetShowMarks(false);
        Ed_SetText(text, len);
        Chrome_SetTitle((const unsigned char *)"\pUntitled");
        Chrome_TextChanged();
        Chrome_ResetClock();
        if (BecomeFile(reply.fName, reply.vRefNum, false))
            Doc_Save();                  /* the file exists from the start */
        Doc_Remember();
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
    if (!SaveIfNeeded())
        return false;
    SFGetFile(DialogSpot(), (ConstStringPtr)"\p", NULL, -1, NULL, NULL, &reply);
    if (!reply.good || !OpenFile(reply.fName, reply.vRefNum))
        return false;
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
