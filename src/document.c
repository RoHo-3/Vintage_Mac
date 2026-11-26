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

#define kPauseTicks    120               /* 2 s without typing */
#define kThreshold     250               /* weighted characters */
#define kIdleSaveTicks (10L * 60)  /* a 10 s pause saves any change */
#define kMaxAgeTicks   (3L * 60 * 60)    /* 3 minutes */
#define kCreator       'MMDW'
#define kFileType      'TEXT'

static Boolean    gHasFile;
static Str63      gName;
static short      gVRef;
static FileFormat gFmt;
static Boolean    gMdBehind;            /* journal has text the .md lacks */
#define gDefaultVRef Files_DocsWD()     /* new documents go to My Files */

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

Boolean Doc_Save(void)
{
    unsigned char suggested[40];
    OSErr err;
    Boolean wasUntitled = !gHasFile;

    if (!gHasFile) {
        /* Naming happens in place (DESIGN.md): the first line names the
           file, in the app's folder until the Library exists. No dialog. */
        unsigned char prefs = JR_Prefs();
        if (!UniqueName(suggested, gDefaultVRef)) {
            SysBeep(1);
            return false;
        }
        err = Create(suggested, gDefaultVRef, kCreator, kFileType);
        if (err != noErr) {
            SysBeep(1);
            return false;
        }
        CopyPString(suggested, gName);
        gVRef = gDefaultVRef;
        gHasFile = true;
        Chrome_SetTitle(gName);
        JR_Attach(gName, gVRef);
        JR_SetPrefs(prefs | kPrefNameFollows);   /* keep what was chosen while
                                                    untitled; app-named */
        JR_Reset();                      /* the journal starts with a snapshot */
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

/* Remember the open document and its caret for the next launch. */
void Doc_Remember(void)
{
    Prefs p;
    Prefs_Load(&p);
    p.lastName[0] = 0;
    if (gHasFile && Files_LocOf(gVRef, &p.lastDir)) {
        CopyPString(gName, p.lastName);
        p.lastCaret = Ed_Caret();
    }
    Prefs_Save(&p);
}

static Boolean OpenFile(const unsigned char *name, short vRef);

/* Launch: reopen the last document at its caret. False: none (first run,
   or the file is gone). */
Boolean Doc_Reopen(void)
{
    Prefs p;
    short wd;
    Prefs_Load(&p);
    if (p.lastName[0] == 0 || !Files_WDOf(&p.lastDir, &wd))
        return false;
    if (!OpenFile(p.lastName, wd))
        return false;
    Ed_SetCaret(p.lastCaret);
    return true;
}

void Doc_Open(void)
{
    SFReply reply;
    if (!SaveIfNeeded())
        return;
    SFGetFile(DialogSpot(), (ConstStringPtr)"\p", NULL, -1, NULL, NULL, &reply);
    if (reply.good && OpenFile(reply.fName, reply.vRefNum))
        Doc_Remember();
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
    if (now - Ed_LastEditTick() < kPauseTicks)
        return;                          /* still typing */
    if (Ed_ChangeWeight() >= kThreshold
        || now - Ed_FirstChangeTick() >= kMaxAgeTicks
        || now - Ed_LastEditTick() >= kIdleSaveTicks) {
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
