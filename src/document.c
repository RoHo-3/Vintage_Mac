#include "document.h"
#include "editor.h"
#include "encoding.h"
#include "textbuf.h"
#include "chrome.h"

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
    Chrome_SetSaveState(kSaveSaved);
    return true;
}

Boolean Doc_Save(void)
{
    SFReply reply;
    unsigned char suggested[40];
    OSErr err;

    if (!gHasFile) {                     /* stand-in until the Library */
        SuggestName(suggested);
        SFPutFile(DialogSpot(), (ConstStringPtr)"\pSave as:", suggested,
                  NULL, &reply);
        if (!reply.good)
            return false;
        err = Create(reply.fName, reply.vRefNum, kCreator, kFileType);
        if (err != noErr && err != dupFNErr) {
            SysBeep(1);
            return false;
        }
        CopyPString(reply.fName, gName);
        gVRef = reply.vRefNum;
        gHasFile = true;
        Chrome_SetTitle(gName);
    }
    return WriteFile();
}

static Boolean SaveIfNeeded(void)
{
    if (Ed_ChangeWeight() == 0)
        return true;
    return Doc_Save();
}

void Doc_New(void)
{
    if (!SaveIfNeeded())
        return;
    gHasFile = false;
    Doc_Init();
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
        if (len >= 0 && Ed_SetText(text, len)) {
            CopyPString(reply.fName, gName);
            gVRef = reply.vRefNum;
            gFmt = fmt;
            gHasFile = true;
            Chrome_SetTitle(gName);
            Chrome_TextChanged();
            Chrome_SetSaveState(kSaveSaved);
            Chrome_ResetClock();
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
        || now - Ed_FirstChangeTick() >= kMaxAgeTicks)
        WriteFile();
}

void Doc_Suspend(void)
{
    if (gHasFile && Ed_ChangeWeight() > 0)
        WriteFile();
}

Boolean Doc_CanQuit(void)
{
    return SaveIfNeeded();
}
