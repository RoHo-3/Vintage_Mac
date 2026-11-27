#include "journal.h"
#include "textbuf.h"

#include <Files.h>
#include <OSUtils.h>

#define kJournalType  'MDWJ'
#define kCreator      'MMDW'
#define kVersion      1
#define kMaxJournal   (1024L * 1024)      /* bigger journals aren't replayed */
#define kSnapshotEvery 64                 /* checkpoints between snapshots */

static Boolean gAttached;
static Str63   gName;                     /* "<base>.hist" */
static short   gVRef;
static Ptr     gOps;                      /* pending edit ops */
static long    gOpsLen, gOpsCap;
static Boolean gNeedSnapshot = true;
static short   gSinceSnapshot;
static unsigned char gPrefs;              /* per-document settings */
static Boolean gPrefsDirty;

/* ---- helpers ------------------------------------------------------------ */

static void Put32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

static unsigned long Get32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16)
         | ((unsigned long)p[2] << 8) | p[3];
}

static unsigned short Sum(const unsigned char *p, long n)
{
    unsigned short s = 0;
    while (n-- > 0) s += *p++;
    return s;
}

unsigned long JR_Hash(void)
{
    long len = TB_Len(), i;
    unsigned long h = 5381;
    const char *p1, *p2;
    long n1, n2;
    TB_Runs(0, len, &p1, &n1, &p2, &n2);
    for (i = 0; i < n1; i++) h = h * 33 + (unsigned char)p1[i];
    for (i = 0; i < n2; i++) h = h * 33 + (unsigned char)p2[i];
    return (h ^ (unsigned long)len) & 0xFFFFFFFFUL;   /* 32-bit everywhere */
}

static unsigned long Now(void)
{
    unsigned long t;
    GetDateTime(&t);
    return t;
}

static Boolean Reserve(long extra)
{
    long cap;
    Ptr p;
    if (gOpsLen + extra <= gOpsCap) return true;
    cap = gOpsCap ? gOpsCap * 2 : 1024;
    while (cap < gOpsLen + extra) cap *= 2;
    p = NewPtr(cap);
    if (!p) return false;
    if (gOps) {
        BlockMove(gOps, p, gOpsLen);
        DisposePtr(gOps);
    }
    gOps = p;
    gOpsCap = cap;
    return true;
}

/* ---- recording ----------------------------------------------------------- */

static void Record(unsigned char *buf, long *n, char type,
                   const unsigned char *head, long headLen,
                   const char *body, long bodyLen, Boolean bodyFromText);

/* Journals are bookkeeping: the Finder's invisible flag keeps them out of
   sight beside their documents (as the system's own files are). */
static void Hide(const unsigned char *name, short vRef)
{
    FInfo fi;
    if (GetFInfo(name, vRef, &fi) == noErr && !(fi.fdFlags & 0x4000)) {
        fi.fdFlags |= 0x4000;            /* fInvisible */
        SetFInfo(name, vRef, &fi);
    }
}

void JR_HistoryName(const unsigned char *md, unsigned char *out)
{
    short n = md[0], i, k = 0;
    if (n > 3 && md[n - 2] == '.' && md[n - 1] == 'm' && md[n] == 'd')
        n -= 3;
    if (n > 26) n = 26;                   /* + ".hist" fits HFS\'s 31 */
    for (i = 1; i <= n; i++) out[++k] = md[i];
    {
        const char *ext = ".hist";
        while (*ext) out[++k] = (unsigned char)*ext++;
    }
    out[0] = (unsigned char)k;
}

/* The .md was renamed: the journal follows it. False if its file couldn't
   be renamed (then nothing changes, so no orphan journal is left). */
Boolean JR_Rename(const unsigned char *newMd)
{
    unsigned char nn[40];
    OSErr err;
    short i;
    if (!gAttached) return true;
    JR_HistoryName(newMd, nn);
    err = Rename(gName, gVRef, nn);
    if (err != noErr && err != fnfErr)   /* fnfErr: no journal written yet */
        return false;
    for (i = 0; i <= nn[0]; i++) gName[i] = nn[i];
    Hide(gName, gVRef);
    return true;
}

void JR_Attach(const unsigned char *md, short vRef)
{
    short n = md[0], i, k = 0;
    if (n > 3 && md[n - 2] == '.' && md[n - 1] == 'm' && md[n] == 'd')
        n -= 3;
    if (n > 26) n = 26;                   /* + ".hist" fits HFS\'s 31 */
    for (i = 1; i <= n; i++) gName[++k] = md[i];
    {
        const char *ext = ".hist";
        while (*ext) gName[++k] = (unsigned char)*ext++;
    }
    gName[0] = (unsigned char)k;
    gVRef = vRef;
    gAttached = true;
    Hide(gName, gVRef);                   /* journals from older builds too */
    gPrefs = 0;                           /* until the journal says otherwise */
    gPrefsDirty = false;
}

void JR_Detach(void) { gAttached = false; JR_Reset(); gPrefs = 0; gPrefsDirty = false; }

/* The writer renamed this document by hand: its name stops following the
   first line. Appends an 'F' record to its journal (if it has one), so it
   works for documents that aren't open. */
void JR_MarkRenamed(const unsigned char *md, short vRef)
{
    unsigned char hist[40], rec[8];
    long n = 0, count;
    short ref;
    JR_HistoryName(md, hist);
    if (gAttached && vRef == gVRef && hist[0] == gName[0]) {
        short i;
        for (i = 1; i <= hist[0] && hist[i] == gName[i]; i++)
            ;
        if (i > hist[0]) {               /* the open document */
            gPrefs &= ~kPrefNameFollows;
            gPrefsDirty = true;
            return;
        }
    }
    if (FSOpen(hist, vRef, &ref) != noErr)
        return;
    Record(rec, &n, 'F', NULL, 0, NULL, 0, false);
    count = n;
    if (SetFPos(ref, fsFromLEOF, 0) == noErr)
        FSWrite(ref, &count, rec);
    FSClose(ref);
}

void JR_SetPrefs(unsigned char p)
{
    if (p == gPrefs) return;
    gPrefs = p;
    gPrefsDirty = true;                   /* written with the next checkpoint */
}

unsigned char JR_Prefs(void) { return gPrefs; }

void JR_Reset(void)
{
    gOpsLen = 0;
    gNeedSnapshot = true;
}

void JR_Op(long pos, long oldLen, const char *ins, long newLen)
{
    unsigned char *p;
    if (gNeedSnapshot)
        return;                           /* the next snapshot covers it */
    if (!Reserve(12 + newLen)) {
        gNeedSnapshot = true;             /* out of memory: snapshot instead */
        gOpsLen = 0;
        return;
    }
    p = (unsigned char *)gOps + gOpsLen;
    Put32(p, pos); Put32(p + 4, oldLen); Put32(p + 8, newLen);
    if (newLen) BlockMove(ins, p + 12, newLen);
    gOpsLen += 12 + newLen;
}

/* Append one record to buf at *n (buf has room). */
static void Record(unsigned char *buf, long *n, char type,
                   const unsigned char *head, long headLen,
                   const char *body, long bodyLen, Boolean bodyFromText)
{
    unsigned char *p = buf + *n, *payload = p + 5;
    long len = headLen + bodyLen;
    p[0] = (unsigned char)type;
    Put32(p + 1, len);
    if (headLen) BlockMove(head, payload, headLen);
    if (bodyLen) {
        if (bodyFromText) TB_Copy(0, bodyLen, (char *)payload + headLen);
        else BlockMove(body, payload + headLen, bodyLen);
    }
    {
        unsigned short s = Sum(payload, len);
        payload[len] = (unsigned char)(s >> 8);
        payload[len + 1] = (unsigned char)s;
    }
    *n += 5 + len + 2;
}

Boolean JR_Checkpoint(long caret, const char *label)
{
    long size, n = 0, count, labelLen = 0;
    unsigned char head[17], *buf;
    short ref;
    OSErr err;
    Boolean snapshot;

    if (!gAttached) return false;
    if (label) while (label[labelLen] && labelLen < 63) labelLen++;
    snapshot = gNeedSnapshot || gSinceSnapshot >= kSnapshotEvery;

    /* Open the journal first: a new (or empty) one must start with a
       snapshot, and the buffer has to be sized for that. (Sizing it for an
       edit and then writing a whole snapshot into it overran the block: the
       "out of memory" bomb on Import, whose history always starts new.) */
    err = FSOpen(gName, gVRef, &ref);
    if (err == fnfErr) {                  /* first checkpoint: new journal */
        Create(gName, gVRef, kCreator, kJournalType);
        Hide(gName, gVRef);
        err = FSOpen(gName, gVRef, &ref);
        snapshot = true;
    }
    if (err != noErr) return false;
    GetEOF(ref, &count);
    if (count == 0) snapshot = true;

    size = 16 + (snapshot ? 7 + 4 + TB_Len() : 7 + 4 + gOpsLen) + 7 + 17 + labelLen + 8;
    buf = (unsigned char *)NewPtr(size);
    if (!buf) { FSClose(ref); return false; }
    if (count == 0) {
        unsigned char h[6] = { 'M', 'D', 'W', 'J', 0, kVersion };
        Record(buf, &n, 'H', h, 6, 0, 0, false);
    }

    Put32(head, Now());
    if (snapshot)
        Record(buf, &n, 'S', head, 4, 0, TB_Len(), true);
    else if (gOpsLen > 0)
        Record(buf, &n, 'E', head, 4, gOps, gOpsLen, false);
    if (snapshot || gPrefsDirty)          /* every segment carries the settings */
        Record(buf, &n, 'P', &gPrefs, 1, 0, 0, false);
    Put32(head + 4, TB_Len());
    Put32(head + 8, JR_Hash());
    Put32(head + 12, caret);
    head[16] = (unsigned char)labelLen;
    Record(buf, &n, 'C', head, 17, label, labelLen, false);

    count = n;                            /* one append */
    err = SetFPos(ref, fsFromLEOF, 0);
    if (err == noErr) err = FSWrite(ref, &count, buf);
    FSClose(ref);
    FlushVol(NULL, gVRef);
    DisposePtr((Ptr)buf);
    if (err != noErr) return false;

    gOpsLen = 0;
    gNeedSnapshot = false;
    gPrefsDirty = false;
    gSinceSnapshot = snapshot ? 0 : gSinceSnapshot + 1;
    return true;
}

/* ---- recovery ------------------------------------------------------------ */

short JR_Recover(void)
{
    short ref, result = kRecoverNone;
    long size, n, i, len, k;
    unsigned char *buf, *payload;
    unsigned long fileHash, hashes[64];
    short nHashes = 0, h;
    Boolean replayed = false, known = false;
    Ptr fileText;
    long fileLen = TB_Len();

    if (!gAttached) return kRecoverNone;
    if (FSOpen(gName, gVRef, &ref) != noErr) return kRecoverNone;
    GetEOF(ref, &size);
    if (size < 13 || size > kMaxJournal) { FSClose(ref); return kRecoverNone; }
    buf = (unsigned char *)NewPtr(size);
    fileText = NewPtr(fileLen + 1);       /* keep the file's text to restore */
    if (!buf || !fileText) {
        if (buf) DisposePtr((Ptr)buf);
        if (fileText) DisposePtr(fileText);
        FSClose(ref);
        return kRecoverNone;
    }
    n = size;
    FSRead(ref, &n, buf);
    FSClose(ref);
    fileHash = JR_Hash();
    TB_Copy(0, fileLen, fileText);

    /* Pass 1: walk the records' headers to find the last snapshot; the
       small records (prefs, renames, checkpoint hashes) are read here, as
       they matter wherever they are. Pass 2 replays from that snapshot
       only, so opening a long-lived document doesn't redo its whole past. */
    {
        long lastS = -1, end = 0;
        for (i = 0; i + 7 <= n; i += 5 + len + 2) {
            len = (long)Get32(buf + i + 1);
            if (len < 0 || i + 5 + len + 2 > n) break;      /* torn tail */
            payload = buf + i + 5;
            if (buf[i] == 'S') { lastS = i; end = i + 5 + len + 2; continue; }
            if (buf[i] == 'E') { end = i + 5 + len + 2; continue; }
            if (Sum(payload, len) != (unsigned short)((payload[len] << 8) | payload[len + 1]))
                break;                                       /* damaged */
            end = i + 5 + len + 2;
            switch (buf[i]) {
            case 'P':
                if (len >= 1) gPrefs = payload[0];
                break;
            case 'F':                    /* renamed by hand */
                gPrefs &= ~kPrefNameFollows;
                break;
            case 'C':
                if (len >= 12 && nHashes < 64)
                    hashes[nHashes++] = Get32(payload + 8);
                else if (len >= 12) {
                    BlockMove(&hashes[1], &hashes[0], 63 * sizeof(unsigned long));
                    hashes[63] = Get32(payload + 8);
                }
                break;
            }
        }
        for (i = lastS; i >= 0 && i + 7 <= end; i += 5 + len + 2) {
            len = (long)Get32(buf + i + 1);
            payload = buf + i + 5;
            if (buf[i] != 'S' && buf[i] != 'E') continue;
            if (Sum(payload, len) != (unsigned short)((payload[len] << 8) | payload[len + 1]))
                break;                                       /* damaged */
            if (buf[i] == 'S') {
                if (len >= 4 && TB_Replace(0, TB_Len(), (char *)payload + 4, len - 4))
                    replayed = true;
                continue;
            }
            for (k = 4; replayed && k + 12 <= len; ) {
                long pos = (long)Get32(payload + k), del = (long)Get32(payload + k + 4);
                long ins = (long)Get32(payload + k + 8);
                if (pos < 0 || del < 0 || ins < 0 || pos + del > TB_Len()
                    || k + 12 + ins > len
                    || !TB_Replace(pos, del, (char *)payload + k + 12, ins)) {
                    replayed = false;            /* inconsistent: give up */
                    break;
                }
                k += 12 + ins;
            }
            if (!replayed) break;
        }
    }

    if (replayed && JR_Hash() != fileHash) {
        for (h = 0; h < nHashes; h++)
            if (hashes[h] == fileHash) known = true;
        if (known)
            result = kRecoverJournalNewer;   /* crash before the .md write */
        else
            result = kRecoverEditedElsewhere;
    }
    if (result != kRecoverJournalNewer)      /* keep the file's own text */
        TB_Replace(0, TB_Len(), fileText, fileLen);
    if (result == kRecoverEditedElsewhere)
        gNeedSnapshot = true;                /* new text starts a new segment */
    else
        gNeedSnapshot = !replayed;
    gOpsLen = 0;
    gSinceSnapshot = 0;
    DisposePtr((Ptr)buf);
    DisposePtr(fileText);
    return result;
}

/* ---- history (the drawer's History blade) -------------------------------- */

/* Read the whole journal into a new block (NULL: none, or too big). */
static unsigned char *ReadAll(long *n)
{
    short ref;
    long size;
    unsigned char *buf;
    *n = 0;
    if (!gAttached || FSOpen(gName, gVRef, &ref) != noErr) return NULL;
    GetEOF(ref, &size);
    if (size < 13 || size > kMaxJournal) { FSClose(ref); return NULL; }
    buf = (unsigned char *)NewPtr(size);
    if (buf) {
        *n = size;
        FSRead(ref, n, buf);
    }
    FSClose(ref);
    return buf;
}

static Boolean RecordOK(const unsigned char *buf, long i, long n, long *len)
{
    const unsigned char *payload = buf + i + 5;
    if (i + 7 > n) return false;
    *len = (long)Get32(buf + i + 1);
    if (*len < 0 || i + 5 + *len + 2 > n) return false;      /* torn tail */
    return Sum(payload, *len) == (unsigned short)((payload[*len] << 8) | payload[*len + 1]);
}

/* The versions worth showing, oldest first: every named checkpoint, the
   last one before a pause of ten minutes or more (the end of a sitting),
   and the newest. At most max, the newest kept. */
short JR_Versions(JRVersion *out, short max)
{
    long n, i, len;
    unsigned char *buf = ReadAll(&n);
    short count = 0, k;
    JRVersion pend;
    Boolean have = false;
    if (!buf) return 0;
    for (i = 0; RecordOK(buf, i, n, &len); i += 5 + len + 2) {
        const unsigned char *p = buf + i + 5;
        if (buf[i] != 'C' || len < 17) continue;
        if (have && (pend.label[0] > 0 || Get32(p) >= pend.time + 600)) {
            if (count == max) {          /* full: drop the oldest */
                for (k = 1; k < max; k++) out[k - 1] = out[k];
                count--;
            }
            out[count++] = pend;
        }
        pend.time = Get32(p);
        pend.length = (long)Get32(p + 4);
        pend.end = i + 5 + len + 2;
        k = p[16];
        if (k > 31 || 17 + k > len) k = 0;
        pend.label[0] = (unsigned char)k;
        BlockMove(p + 17, pend.label + 1, k);
        have = true;
    }
    if (have && max > 0) {               /* the newest always shows */
        if (count == max) {
            for (k = 1; k < max; k++) out[k - 1] = out[k];
            count--;
        }
        out[count++] = pend;
    }
    DisposePtr((Ptr)buf);
    return count;
}

/* A flat text being rebuilt, grown as it needs. */
static Boolean Apply(Ptr *t, long *len, long *cap, long pos, long del,
                     const unsigned char *ins, long nIns)
{
    long need = *len - del + nIns;
    if (pos < 0 || del < 0 || nIns < 0 || pos + del > *len) return false;
    if (need > *cap) {
        long c = need + need / 4 + 1024;
        Ptr p = NewPtr(c);
        if (!p) return false;
        BlockMove(*t, p, *len);
        DisposePtr(*t);
        *t = p;
        *cap = c;
    }
    BlockMove(*t + pos + del, *t + pos + nIns, *len - pos - del);
    if (nIns) BlockMove(ins, *t + pos, nIns);
    *len = need;
    return true;
}

/* The document's text as it stood at a version (its end, from
   JR_Versions): the last snapshot before it, then the edits up to it.
   A new block in *text (the caller disposes it); false if it can't be
   rebuilt (damaged, or not enough memory). */
Boolean JR_BuildVersion(long end, Ptr *text, long *textLen)
{
    long n, i, len, lastS = -1, k, cap = 0, tlen = 0;
    unsigned char *buf = ReadAll(&n);
    Ptr t = NULL;
    Boolean ok = true;
    *text = NULL;
    *textLen = 0;
    if (!buf) return false;
    if (end > n) end = n;
    for (i = 0; i < end && RecordOK(buf, i, n, &len); i += 5 + len + 2)
        if (buf[i] == 'S') lastS = i;
    if (lastS < 0) { DisposePtr((Ptr)buf); return false; }
    for (i = lastS; ok && i < end && RecordOK(buf, i, n, &len); i += 5 + len + 2) {
        const unsigned char *p = buf + i + 5;
        if (buf[i] == 'S' && len >= 4) {
            if (t) DisposePtr(t);
            cap = len - 4 + 1024;
            t = NewPtr(cap);
            if (!t) { ok = false; break; }
            BlockMove(p + 4, t, len - 4);
            tlen = len - 4;
        } else if (buf[i] == 'E' && t) {
            for (k = 4; k + 12 <= len; ) {
                long pos = (long)Get32(p + k), del = (long)Get32(p + k + 4);
                long ins = (long)Get32(p + k + 8);
                if (k + 12 + ins > len || !Apply(&t, &tlen, &cap, pos, del, p + k + 12, ins)) {
                    ok = false;
                    break;
                }
                k += 12 + ins;
            }
        }
    }
    DisposePtr((Ptr)buf);
    if (!ok || !t) { if (t) DisposePtr(t); return false; }
    *text = t;
    *textLen = tlen;
    return true;
}
