#include "document.h"
#include "editor.h"
#include "encoding.h"
#include "textbuf.h"
#include "chrome.h"
#include "journal.h"

#include <Files.h>
#include <StandardFile.h>
#include <Events.h>
#include <OSUtils.h>

#define kPauseTicks    120               /* 2 s without typing */
#define kThreshold     250               /* weighted characters */
#define kMaxAgeTicks   (3L * 60 * 60)    /* 3 minutes */
#define kCreator       'MMDW'
#define kFileType      'TEXT'

static Boolean    gHasFile;
static Str63      gName;
static short      gVRef;
static FileFormat gFmt;
static Boolean    gMdBehind;            /* journal has text the .md lacks */
static short      gDefaultVRef;         /* where new documents go: the app's folder */
static Boolean    gHaveDefault;

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
    if (!gHaveDefault) {
        GetVol(NULL, &gDefaultVRef);     /* the folder the app was launched from */
        gHaveDefault = true;
    }
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

/* SuggestName, made unique in the default folder: "Day Two 2.md", ... */
static Boolean UniqueName(unsigned char *name)
{
    FInfo fi;
    unsigned char base[40];
    short n, k, i;

    SuggestName(name);
    if (GetFInfo(name, gDefaultVRef, &fi) == fnfErr)
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
        if (GetFInfo(name, gDefaultVRef, &fi) == fnfErr)
            return true;
    }
    return false;
}

static Boolean WriteFile(void)
{
    long n, max = Enc_MaxEncoded();
    Ptr buf = NewPtr(max);
    short ref;
    OSErr err;
    FInfo fi;

    if (!buf) { SysBeep(1); return false; }
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

    if (!gHasFile) {
        /* Naming happens in place (DESIGN.md): the first line names the
           file, in the app's folder until the Library exists. No dialog. */
        unsigned char prefs = JR_Prefs();
        if (!UniqueName(suggested)) {
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
        JR_SetPrefs(prefs);              /* keep what was chosen while untitled */
        JR_Reset();                      /* the journal starts with a snapshot */
    }
    JR_Checkpoint(Ed_Caret(), NULL);     /* journal first, then the .md */
    return WriteFile();
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
}

void Doc_Open(void)
{
    SFReply reply;
    short ref;
    long size, n;
    Ptr raw, text;
    OSErr err;

    if (!SaveIfNeeded())
        return;
    SFGetFile(DialogSpot(), (ConstStringPtr)"\p", NULL, -1, NULL, NULL, &reply);
    if (!reply.good)
        return;

    err = FSOpen(reply.fName, reply.vRefNum, &ref);
    if (err != noErr) { SysBeep(1); return; }
    GetEOF(ref, &size);
    raw = NewPtr(size + 1);
    text = NewPtr(TB_Capacity());
    if (!raw || !text) {
        FSClose(ref);
        if (raw) DisposePtr(raw);
        if (text) DisposePtr(text);
        SysBeep(1);                      /* too big for memory */
        return;
    }
    n = size;
    err = FSRead(ref, &n, raw);
    FSClose(ref);
    if (err == noErr || err == eofErr) {
        FileFormat fmt;
        long len = Enc_Decode((unsigned char *)raw, n, text, TB_Capacity(), &fmt);
        short recovered = kRecoverNone;
        if (len >= 0 && TB_Replace(0, TB_Len(), text, len)) {
            JR_Attach(reply.fName, reply.vRefNum);
            recovered = JR_Recover();    /* may leave newer text in the buffer */
            len = TB_Len();
            TB_Copy(0, len, text);
        } else
            len = -1;
        if (len >= 0)
            Ed_SetShowMarks((JR_Prefs() & kPrefShowMarks) != 0);
        if (len >= 0 && Ed_SetText(text, len)) {
            CopyPString(reply.fName, gName);
            gVRef = reply.vRefNum;
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
        } else
            SysBeep(1);                  /* too big for the text buffer */
    } else
        SysBeep(1);
    DisposePtr(raw);
    DisposePtr(text);
}

void Doc_Idle(void)
{
    unsigned long now;
    if (!gHasFile || Ed_ChangeWeight() == 0)
        return;
    now = TickCount();
    if (now - Ed_LastEditTick() < kPauseTicks)
        return;                          /* still typing */
    if (Ed_ChangeWeight() >= kThreshold
        || now - Ed_FirstChangeTick() >= kMaxAgeTicks) {
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
}

Boolean Doc_CanQuit(void)
{
    return SaveIfNeeded();
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
