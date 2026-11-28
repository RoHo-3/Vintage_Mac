#include "files.h"
#include "templates.h"
#include "journal.h"

#include <Memory.h>
#include <OSUtils.h>
#include <Multiverse.h>

#define kCreator    'MMDW'
#define kPrefsType  'pref'
#define kPrefsVers  2

static void FixFakeTrash(short vol);
static Boolean TrashDir(short vol, long *dir);
static Boolean HaveFindFolder(void);
static Boolean IsDoc(const unsigned char *name, OSType type);
static Boolean MoveItem(short vol, long from, ConstStr255Param name, long to);

static short gDocsWD, gTemplatesWD, gRootWD;   /* My Writings, Templates, Writings */
static short gDocsVol;
static long  gDocsDir;

#define kMaxWD 24                        /* working directories we keep open */
static struct { short vol; long dir; short wd; } gWD[kMaxWD];
static short gWDCount;

static Boolean OpenWD(short vol, long dirID, short *wd)
{
    WDPBRec pb;
    short i;
    for (i = 0; i < gWDCount; i++)
        if (gWD[i].vol == vol && gWD[i].dir == dirID) {
            *wd = gWD[i].wd;
            return true;
        }
    pb.ioCompletion = NULL;
    pb.ioNamePtr = NULL;
    pb.ioVRefNum = vol;
    pb.ioWDDirID = dirID;
    pb.ioWDProcID = kCreator;
    if (PBOpenWDSync(&pb) != noErr)
        return false;
    *wd = pb.ioVRefNum;
    if (gWDCount < kMaxWD) {
        gWD[gWDCount].vol = vol;
        gWD[gWDCount].dir = dirID;
        gWD[gWDCount].wd = *wd;
        gWDCount++;
    }
    return true;
}

/* The real volume and directory behind a working directory. */
static Boolean WDInfo(short wd, short *vol, long *dirID)
{
    WDPBRec pb;
    pb.ioCompletion = NULL;
    pb.ioNamePtr = NULL;
    pb.ioVRefNum = wd;
    pb.ioWDIndex = 0;
    pb.ioWDProcID = 0;
    if (PBGetWDInfoSync(&pb) != noErr)
        return false;
    *vol = pb.ioWDVRefNum;
    *dirID = pb.ioWDDirID;
    return true;
}

/* The folder `name` inside (vol, parent), created if missing. */
static Boolean SubFolder(short vol, long parent, ConstStr255Param name, long *dirID)
{
    CInfoPBRec ci;
    HParamBlockRec pb;
    OSErr err;
    pb.fileParam.ioCompletion = NULL;
    pb.fileParam.ioNamePtr = (StringPtr)name;
    pb.fileParam.ioVRefNum = vol;
    pb.fileParam.ioDirID = parent;
    err = PBDirCreateSync(&pb);
    if (err == noErr) {
        *dirID = pb.fileParam.ioDirID;   /* the new folder's ID comes back here */
        return true;
    }
    if (err != dupFNErr)
        return false;
    ci.dirInfo.ioCompletion = NULL;
    ci.dirInfo.ioNamePtr = (StringPtr)name;
    ci.dirInfo.ioVRefNum = vol;
    ci.dirInfo.ioFDirIndex = 0;
    ci.dirInfo.ioDrDirID = parent;
    if (PBGetCatInfoSync(&ci) != noErr || !(ci.dirInfo.ioFlAttrib & 0x10))
        return false;                    /* a file has the folder's name */
    *dirID = ci.dirInfo.ioDrDirID;
    return true;
}

static Boolean SameP(const unsigned char *a, const unsigned char *b)
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

static short gProjVol;
static long  gProjDir;
static short gRootVol;
static long  gRootDir;                   /* "Writings" itself */

/* ---- where the library is: a pointer file in the Preferences folder ----
   The library can be moved (Settings > Move Library...); the prefs live
   inside it, so where it went is kept outside it: "Mac MD Write Library"
   in the System Folder's Preferences (System 7) or the System Folder. */

#define kPointerName ((ConstStr255Param)"\pMac MD Write Library")

static Boolean PrefsFolder(short *vol, long *dir)
{
    SysEnvRec env;
    if (HaveFindFolder())
        return FindFolder(-32768, 'pref', true, vol, dir) == noErr;   /* kOnSystemDisk */
    if (SysEnvirons(1, &env) != noErr) return false;
    return WDInfo(env.sysVRefNum, vol, dir);
}

static Boolean LoadPointer(FolderLoc *loc)
{
    short vol, wd, ref;
    long dir, n = sizeof(FolderLoc);
    Boolean ok;
    if (!PrefsFolder(&vol, &dir) || !OpenWD(vol, dir, &wd)) return false;
    if (FSOpen(kPointerName, wd, &ref) != noErr) return false;
    ok = FSRead(ref, &n, (Ptr)loc) == noErr && n == sizeof(FolderLoc);
    FSClose(ref);
    return ok;
}

static void SavePointer(const FolderLoc *loc)
{
    short vol, wd, ref;
    long dir, n = sizeof(FolderLoc);
    OSErr err;
    if (!PrefsFolder(&vol, &dir) || !OpenWD(vol, dir, &wd)) return;
    if (!loc) {                          /* back to the default: forget it */
        FSDelete(kPointerName, wd);
        return;
    }
    err = Create(kPointerName, wd, kCreator, kPrefsType);
    if (err != noErr && err != dupFNErr) return;
    if (FSOpen(kPointerName, wd, &ref) != noErr) return;
    FSWrite(ref, &n, (Ptr)loc);
    SetEOF(ref, n);
    FSClose(ref);
}

/* The folder the library lives in: beside the app ("Writings"), unless it
   was moved and the pointer still finds it. */
static Boolean FindRoot(short appVol, long appDir, short *vol, long *dir)
{
    FolderLoc loc;
    short wd;
    CInfoPBRec ci;
    if (LoadPointer(&loc) && Files_WDOf(&loc, &wd) && WDInfo(wd, vol, dir)) {
        ci.dirInfo.ioCompletion = NULL;  /* still a folder (not in the Trash)? */
        ci.dirInfo.ioNamePtr = NULL;
        ci.dirInfo.ioVRefNum = *vol;
        ci.dirInfo.ioFDirIndex = -1;
        ci.dirInfo.ioDrDirID = *dir;
        if (PBGetCatInfoSync(&ci) == noErr) {
            long trash;
            if (!(TrashDir(*vol, &trash) && Files_IsInside(*vol, *dir, trash)))
                return true;
        }
    }
    *vol = appVol;
    return SubFolder(appVol, appDir, (ConstStr255Param)"\pWritings", dir);
}

Boolean Files_Init(void)
{
    short appWD, vol, rvol;
    long appDir, rootDir, docsDir, tplDir;

    GetVol(NULL, &appWD);                /* launch sets it to the app's folder */
    gDocsWD = gTemplatesWD = gRootWD = appWD;   /* fallback: beside the app */
    if (!WDInfo(appWD, &vol, &appDir))
        return false;
    FixFakeTrash(vol);
    if (!FindRoot(vol, appDir, &rvol, &rootDir) || !OpenWD(rvol, rootDir, &gRootWD))
        return false;
    gRootVol = rvol;
    gRootDir = rootDir;
    gDocsWD = gTemplatesWD = gRootWD;
    gDocsVol = gProjVol = rvol;
    gDocsDir = gProjDir = rootDir;
    /* Writings holds two peers: My Writings (every writing is a folder)
       and Templates, plus the invisible prefs */
    if (SubFolder(rvol, rootDir, (ConstStr255Param)"\pMy Writings", &docsDir)
        && OpenWD(rvol, docsDir, &gDocsWD))
        gDocsDir = gProjDir = docsDir;
    if (SubFolder(rvol, rootDir, (ConstStr255Param)"\pTemplates", &tplDir)
        && OpenWD(rvol, tplDir, &gTemplatesWD))
        Templates_Install(rvol, tplDir);  /* the default set, when it has none */
    return true;
}

/* Cut s to kNameMax at its last whole word (at a letter only when one word
   is longer than that), then drop trailing spaces and punctuation. */
void Files_FitName(unsigned char *s) { Files_FitNameTo(s, kNameMax); }

void Files_FitNameTo(unsigned char *s, short max)
{
    short n = s[0], k;
    if (n > max) {
        for (k = max + 1; k > 1 && s[k] != ' '; k--)
            ;
        n = k > 1 ? k - 1 : max;
    }
    while (n > 0 && (s[n] == ' ' || s[n] == '.' || s[n] == ',' || s[n] == ';' || s[n] == '-'))
        n--;
    s[0] = (unsigned char)n;
}

/* A name free in (vol, dir): base, "base 2", ... (at most kNameMax). */
static Boolean FreeIn(short vol, long dir, const unsigned char *base, unsigned char *out)
{
    short k, i, n, room;
    for (k = 1; k < 100; k++) {
        room = k == 1 ? kNameMax : kNameMax - (k >= 10 ? 3 : 2);
        for (i = 0; i <= base[0]; i++) out[i] = base[i];
        Files_FitNameTo(out, room);      /* " 2" needs room: cut at a word */
        n = out[0];
        if (k > 1) {
            out[++n] = ' ';
            if (k >= 10) out[++n] = (unsigned char)('0' + k / 10);
            out[++n] = (unsigned char)('0' + k % 10);
        }
        out[0] = (unsigned char)n;
        if (!Files_Exists(vol, dir, out))
            return true;
    }
    return false;
}

Boolean Files_FreeName(short vol, long dir, const unsigned char *base, unsigned char *out)
{
    return FreeIn(vol, dir, base, out);
}

static Boolean MoveItem(short vol, long from, ConstStr255Param name, long to)
{
    CMovePBRec mv;
    mv.ioCompletion = NULL;
    mv.ioNamePtr = (StringPtr)name;
    mv.ioVRefNum = vol;
    mv.ioNewName = NULL;
    mv.ioNewDirID = to;
    mv.ioDirID = from;
    return PBCatMoveSync(&mv) == noErr;
}

/* Every writing is a folder. A document dropped loose into My Writings (from
   the Finder, say) is wrapped in a folder of its own name at launch, its
   journal moving with it; Recent and the last document follow it. */
void Files_WrapLoose(void)
{
    CInfoPBRec ci;
    Str63 name;
    unsigned char base[40], folder[40], hist[40];
    short idx = 1, i, tries = 0;
    long nd;
    Prefs p;
    Boolean changed = false;
    Prefs_Load(&p);
    while (tries++ < 200) {
        ci.hFileInfo.ioCompletion = NULL;
        ci.hFileInfo.ioNamePtr = name;
        ci.hFileInfo.ioVRefNum = gDocsVol;
        ci.hFileInfo.ioFDirIndex = idx;
        ci.hFileInfo.ioDirID = gDocsDir;
        if (PBGetCatInfoSync(&ci) != noErr)
            break;
        if ((ci.hFileInfo.ioFlAttrib & 0x10) || (ci.hFileInfo.ioFlFndrInfo.fdFlags & 0x4000)
            || !IsDoc(name, ci.hFileInfo.ioFlFndrInfo.fdType)) {
            idx++;                       /* folders, journals, other files */
            continue;
        }
        for (i = 0; i <= name[0]; i++) base[i] = name[i];
        if (base[0] > 3 && base[base[0] - 2] == '.' && (base[base[0] - 1] | 0x20) == 'm'
            && (base[base[0]] | 0x20) == 'd')
            base[0] -= 3;
        if (!FreeIn(gDocsVol, gDocsDir, base, folder)
            || !Files_NewFolder(gDocsVol, gDocsDir, folder, &nd)
            || !MoveItem(gDocsVol, gDocsDir, name, nd)) {
            idx++;                       /* leave it where it is */
            continue;
        }
        JR_HistoryName(name, hist);
        MoveItem(gDocsVol, gDocsDir, hist, nd);       /* fnfErr: no journal */
        if (p.lastDir.dirID == gDocsDir && SameP(p.lastName, name)) {
            p.lastDir.dirID = nd;
            changed = true;
        }
        for (i = 0; i < p.recentCount; i++)
            if (p.recent[i].dir.dirID == gDocsDir && SameP(p.recent[i].name, name)) {
                p.recent[i].dir.dirID = nd;
                changed = true;
            }
        /* the listing changed under us: start over (the new folder is
           skipped, being a folder) */
        idx = 1;
    }
    if (changed) Prefs_Save(&p);
}

/* Where the next Standard File dialog opens (low-memory globals). */
void Files_SetDialogFolder(short wd)
{
    short vol;
    long dir;
    if (Files_DirOfWD(wd, &vol, &dir)) {
        *(short *)0x0214 = -vol;         /* SFSaveDisk */
        *(long *)0x0398 = dir;           /* CurDirStore */
    }
}

static Boolean IsDoc(const unsigned char *name, OSType type);

/* A document was moved or deleted in the Finder: its invisible journal is
   left behind. Move such orphans to the Trash (recoverable), in this folder
   and up to `depth` levels below. */
static void TrashOrphans(short vol, long dir, short depth)
{
    static unsigned char hist[96][32];   /* journal names the documents here use */
    CInfoPBRec ci;
    Str63 name;
    long subs[16];
    short nh = 0, ns = 0, idx, i, j;
    Boolean found;

    for (idx = 1; ; idx++) {             /* pass 1: documents and subfolders */
        ci.hFileInfo.ioCompletion = NULL;
        ci.hFileInfo.ioNamePtr = name;
        ci.hFileInfo.ioVRefNum = vol;
        ci.hFileInfo.ioFDirIndex = idx;
        ci.hFileInfo.ioDirID = dir;
        if (PBGetCatInfoSync(&ci) != noErr) break;
        if (ci.hFileInfo.ioFlAttrib & 0x10) {
            if (ns < 16) subs[ns++] = ci.dirInfo.ioDrDirID;
        } else if (IsDoc(name, ci.hFileInfo.ioFlFndrInfo.fdType) && nh < 96)
            JR_HistoryName(name, hist[nh++]);
    }
    for (idx = 1; ; ) {                  /* pass 2: journals nobody uses */
        ci.hFileInfo.ioCompletion = NULL;
        ci.hFileInfo.ioNamePtr = name;
        ci.hFileInfo.ioVRefNum = vol;
        ci.hFileInfo.ioFDirIndex = idx;
        ci.hFileInfo.ioDirID = dir;
        if (PBGetCatInfoSync(&ci) != noErr) break;
        idx++;
        if ((ci.hFileInfo.ioFlAttrib & 0x10) || ci.hFileInfo.ioFlFndrInfo.fdType != 'MDWJ')
            continue;
        found = false;
        for (i = 0; i < nh && !found; i++) {
            found = hist[i][0] == name[0];
            for (j = 1; found && j <= name[0]; j++) found = hist[i][j] == name[j];
        }
        if (!found && Files_Trash(vol, dir, name))
            idx--;                       /* the folder's indexes shifted */
    }
    if (depth > 0)
        for (i = 0; i < ns; i++)
            TrashOrphans(vol, subs[i], depth - 1);
}

void Files_TidyJournals(void)
{
    TrashOrphans(gDocsVol, gDocsDir, 3);  /* writings, their folders */
}

void Files_ProjectsDir(short *vol, long *dirID) { *vol = gProjVol; *dirID = gProjDir; }

/* A folder's name. */
Boolean Files_FolderName(short vol, long dir, unsigned char *name)
{
    CInfoPBRec ci;
    ci.dirInfo.ioCompletion = NULL;
    ci.dirInfo.ioNamePtr = name;
    ci.dirInfo.ioVRefNum = vol;
    ci.dirInfo.ioFDirIndex = -1;         /* the folder itself */
    ci.dirInfo.ioDrDirID = dir;
    return PBGetCatInfoSync(&ci) == noErr;
}

/* The writing (top folder in My Writings) that holds dir, if any. */
Boolean Files_ProjectOf(short vol, long dir, long *project)
{
    CInfoPBRec ci;
    short n;
    if (vol != gProjVol) return false;
    for (n = 0; n < 16 && dir > 1; n++) {
        ci.dirInfo.ioCompletion = NULL;
        ci.dirInfo.ioNamePtr = NULL;
        ci.dirInfo.ioVRefNum = vol;
        ci.dirInfo.ioFDirIndex = -1;
        ci.dirInfo.ioDrDirID = dir;
        if (PBGetCatInfoSync(&ci) != noErr) return false;
        if (ci.dirInfo.ioDrParID == gProjDir) {
            *project = dir;
            return true;
        }
        dir = ci.dirInfo.ioDrParID;
    }
    return false;
}

/* Is dir the folder anc, or inside it? (walks up the parents) */
Boolean Files_IsInside(short vol, long dir, long anc)
{
    CInfoPBRec ci;
    short n;
    for (n = 0; n < 16 && dir > 1; n++) {
        if (dir == anc) return true;
        ci.dirInfo.ioCompletion = NULL;
        ci.dirInfo.ioNamePtr = NULL;
        ci.dirInfo.ioVRefNum = vol;
        ci.dirInfo.ioFDirIndex = -1;     /* info about the folder itself */
        ci.dirInfo.ioDrDirID = dir;
        if (PBGetCatInfoSync(&ci) != noErr) return false;
        dir = ci.dirInfo.ioDrParID;
    }
    return false;
}

void Files_DocsDir(short *vol, long *dirID) { *vol = gDocsVol; *dirID = gDocsDir; }
Boolean Files_WDFor(short vol, long dirID, short *wd) { return OpenWD(vol, dirID, wd); }
Boolean Files_DirOfWD(short wd, short *vol, long *dirID) { return WDInfo(wd, vol, dirID); }

static Boolean IsDoc(const unsigned char *name, OSType type)
{
    short n = name[0];
    if (n > 3 && name[n - 2] == '.' && (name[n - 1] | 0x20) == 'm' && (name[n] | 0x20) == 'd')
        return true;
    return type == 'TEXT';
}

static Boolean LessName(const unsigned char *a, const unsigned char *b)
{
    short i, n = a[0] < b[0] ? a[0] : b[0];
    for (i = 1; i <= n; i++) {
        unsigned char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return x < y;
    }
    return a[0] < b[0];
}

static Boolean NameEq(const unsigned char *a, const char *c)
{
    short i = 0;
    for (; c[i]; i++) {
        unsigned char x = a[i + 1], y = (unsigned char)c[i];
        if (i >= a[0]) return false;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
    }
    return a[0] == i;
}

static Boolean IsOutlineDoc(const DirItem *d)
{
    return !d->isFolder && (NameEq(d->name, "Outline") || NameEq(d->name, "Outline.md"));
}

/* "Untitled Book.md" in the folder "Untitled Book": the writing's own page. */
static Boolean IsNamesake(const DirItem *d, const unsigned char *folder)
{
    short i, n = d->name[0];
    if (d->isFolder || folder[0] == 0) return false;
    if (n > 3 && d->name[n - 2] == '.' && d->name[n - 1] == 'm' && d->name[n] == 'd') n -= 3;
    if (n != folder[0]) return false;
    for (i = 1; i <= n; i++) {
        unsigned char x = d->name[i], y = folder[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
    }
    return true;
}

short Files_List(short vol, long dirID, DirItem *items, short max)
{
    CInfoPBRec ci;
    Str63 name;
    short idx, n = 0, i, j;
    for (idx = 1; n < max; idx++) {
        ci.hFileInfo.ioCompletion = NULL;
        ci.hFileInfo.ioNamePtr = name;
        ci.hFileInfo.ioVRefNum = vol;
        ci.hFileInfo.ioFDirIndex = idx;
        ci.hFileInfo.ioDirID = dirID;    /* reset: the call overwrites it */
        if (PBGetCatInfoSync(&ci) != noErr)
            break;
        if (ci.hFileInfo.ioFlFndrInfo.fdFlags & 0x4000)
            continue;                    /* invisible */
        if (ci.hFileInfo.ioFlAttrib & 0x10) {
            items[n].isFolder = true;
            items[n].dirID = ci.dirInfo.ioDrDirID;
            items[n].created = ci.dirInfo.ioDrCrDat;
        } else {
            if (!IsDoc(name, ci.hFileInfo.ioFlFndrInfo.fdType))
                continue;                /* prefs, journals, other files */
            items[n].isFolder = false;
            items[n].dirID = 0;
            items[n].created = ci.hFileInfo.ioFlCrDat;
        }
        if (name[0] > 31) name[0] = 31;
        for (i = 0; i <= name[0]; i++) items[n].name[i] = name[i];
        items[n].parent = dirID;
        n++;
    }
    /* the writing's own page (named like its folder: a Book's title page)
       pinned first, then "Outline", then folders and documents by name */
    {
        Str63 folder;
        ci.dirInfo.ioCompletion = NULL;
        ci.dirInfo.ioNamePtr = folder;
        ci.dirInfo.ioVRefNum = vol;
        ci.dirInfo.ioFDirIndex = -1;     /* this folder itself */
        ci.dirInfo.ioDrDirID = dirID;
        if (PBGetCatInfoSync(&ci) != noErr) folder[0] = 0;
        for (i = 1; i < n; i++)
            for (j = i; j > 0; j--) {
                DirItem t;
                DirItem *a = &items[j - 1], *b = &items[j];
                short ra = IsNamesake(a, folder) ? 0 : IsOutlineDoc(a) ? 1 : 2;
                short rb = IsNamesake(b, folder) ? 0 : IsOutlineDoc(b) ? 1 : 2;
                if (ra < rb || (ra == rb && !LessName(b->name, a->name)))
                    break;
                t = *a; *a = *b; *b = t;
            }
    }
    return n;
}

/* My Writings' order: newest made first, so renaming one doesn't move it
   (a writing stays where it was put). Equal dates keep the name order. */
void Files_SortNewest(DirItem *items, short n)
{
    short i, j;
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && items[j].created > items[j - 1].created; j--) {
            DirItem t = items[j - 1];
            items[j - 1] = items[j];
            items[j] = t;
        }
}

Boolean Files_NewFolder(short vol, long parent, ConstStr255Param name, long *dirID)
{
    HParamBlockRec pb;
    pb.fileParam.ioCompletion = NULL;
    pb.fileParam.ioNamePtr = (StringPtr)name;
    pb.fileParam.ioVRefNum = vol;
    pb.fileParam.ioDirID = parent;
    if (PBDirCreateSync(&pb) != noErr)
        return false;
    *dirID = pb.fileParam.ioDirID;
    return true;
}

Boolean Files_Exists(short vol, long dirID, ConstStr255Param name)
{
    CInfoPBRec ci;
    ci.hFileInfo.ioCompletion = NULL;
    ci.hFileInfo.ioNamePtr = (StringPtr)name;
    ci.hFileInfo.ioVRefNum = vol;
    ci.hFileInfo.ioFDirIndex = 0;
    ci.hFileInfo.ioDirID = dirID;
    return PBGetCatInfoSync(&ci) == noErr;
}

Boolean Files_Rename(short vol, long dirID, ConstStr255Param from, ConstStr255Param to)
{
    HParamBlockRec pb;
    pb.ioParam.ioCompletion = NULL;
    pb.ioParam.ioNamePtr = (StringPtr)from;
    pb.ioParam.ioVRefNum = vol;
    pb.ioParam.ioMisc = (Ptr)to;
    pb.fileParam.ioDirID = dirID;
    return PBHRenameSync(&pb) == noErr;
}

/* kind: 0 = OS trap, 1 = Toolbox trap (Retro68 has no names for these) */
static Boolean TrapAvailable(unsigned short trap, short kind)
{
    return NGetTrapAddress(trap, kind) != NGetTrapAddress(0xA89F, 1);
}

static Boolean HaveFindFolder(void)
{
    long resp;
    return TrapAvailable(0xA1AD, 0)                 /* Gestalt */
        && Gestalt('fold', &resp) == noErr && (resp & 1);
}

static void MakeInvisible(short vol, long parent, ConstStr255Param name)
{
    CInfoPBRec ci;
    ci.dirInfo.ioCompletion = NULL;
    ci.dirInfo.ioNamePtr = (StringPtr)name;
    ci.dirInfo.ioVRefNum = vol;
    ci.dirInfo.ioFDirIndex = 0;
    ci.dirInfo.ioDrDirID = parent;
    if (PBGetCatInfoSync(&ci) != noErr) return;
    if (ci.dirInfo.ioDrUsrWds.frFlags & 0x4000) return;
    ci.dirInfo.ioDrUsrWds.frFlags |= 0x4000;
    ci.dirInfo.ioDrDirID = parent;
    PBSetCatInfoSync(&ci);
}

/* The disk's real Trash: FindFolder on System 7 (creating it properly if
   the Finder hasn't yet); on System 6, the Finder's invisible root "Trash". */
static Boolean TrashDir(short vol, long *dir)
{
    short tvol;
    if (HaveFindFolder())
        return FindFolder(vol, 'trsh', true, &tvol, dir) == noErr;
    if (!SubFolder(vol, 2, (ConstStr255Param)"\pTrash", dir))
        return false;
    MakeInvisible(vol, 2, (ConstStr255Param)"\pTrash");
    return true;
}

/* Builds 34-57 could create an ordinary, visible "Trash" folder at a
   disk's root on System 7. Empty it into the real Trash and remove it. */
static void FixFakeTrash(short vol)
{
    CInfoPBRec ci;
    CMovePBRec mv;
    HParamBlockRec del;
    Str63 name;
    long fake, real;
    short tvol;
    if (!HaveFindFolder()) return;
    ci.dirInfo.ioCompletion = NULL;
    ci.dirInfo.ioNamePtr = (StringPtr)"\pTrash";
    ci.dirInfo.ioVRefNum = vol;
    ci.dirInfo.ioFDirIndex = 0;
    ci.dirInfo.ioDrDirID = 2;
    if (PBGetCatInfoSync(&ci) != noErr || !(ci.dirInfo.ioFlAttrib & 0x10))
        return;
    fake = ci.dirInfo.ioDrDirID;
    if (FindFolder(vol, 'trsh', true, &tvol, &real) != noErr || real == fake)
        return;                          /* it is the real one */
    while (true) {
        ci.hFileInfo.ioCompletion = NULL;
        ci.hFileInfo.ioNamePtr = name;
        ci.hFileInfo.ioVRefNum = vol;
        ci.hFileInfo.ioFDirIndex = 1;
        ci.hFileInfo.ioDirID = fake;
        if (PBGetCatInfoSync(&ci) != noErr) break;
        mv.ioCompletion = NULL;
        mv.ioNamePtr = name;
        mv.ioVRefNum = vol;
        mv.ioNewName = NULL;
        mv.ioNewDirID = real;
        mv.ioDirID = fake;
        if (PBCatMoveSync(&mv) != noErr) return;   /* a clash: leave it be */
    }
    del.fileParam.ioCompletion = NULL;
    del.fileParam.ioNamePtr = (StringPtr)"\pTrash";
    del.fileParam.ioVRefNum = vol;
    del.fileParam.ioDirID = 2;
    PBHDeleteSync(&del);
}

Boolean Files_Trash(short vol, long dirID, ConstStr255Param name)
{
    CMovePBRec pb;
    long trash;
    if (!TrashDir(vol, &trash))
        return false;
    pb.ioCompletion = NULL;
    pb.ioNamePtr = (StringPtr)name;
    pb.ioVRefNum = vol;
    pb.ioNewName = NULL;
    pb.ioNewDirID = trash;
    pb.ioDirID = dirID;
    return PBCatMoveSync(&pb) == noErr;
}

Boolean Files_Copy(short wd, ConstStr255Param from, ConstStr255Param to)
{
    return Files_CopyTo(wd, from, wd, to);
}

/* Copy a document's data into (dstWD, to); an existing file there is
   replaced (the Save dialog already asked). */
Boolean Files_CopyTo(short wd, ConstStr255Param from, short dstWD, ConstStr255Param to)
{
    short in, out;
    long n;
    char buf[1024];
    OSErr err;
    FInfo fi;
    if (FSOpen(from, wd, &in) != noErr)
        return false;
    if (GetFInfo(from, wd, &fi) != noErr) { fi.fdCreator = kCreator; fi.fdType = 'TEXT'; }
    err = Create(to, dstWD, fi.fdCreator, fi.fdType);
    if ((err != noErr && err != dupFNErr) || FSOpen(to, dstWD, &out) != noErr) {
        FSClose(in);
        return false;
    }
    SetEOF(out, 0);
    do {
        n = sizeof(buf);
        err = FSRead(in, &n, buf);
        if (n > 0 && FSWrite(out, &n, buf) != noErr) { err = -1; break; }
    } while (err == noErr);
    FSClose(in);
    FSClose(out);
    return err == eofErr;
}

short Files_DocsWD(void)      { return gDocsWD; }
short Files_TemplatesWD(void) { return gTemplatesWD; }

Boolean Files_LocOf(short wdRef, FolderLoc *loc)
{
    HParamBlockRec pb;
    short vol;
    if (!WDInfo(wdRef, &vol, &loc->dirID))
        return false;
    pb.volumeParam.ioCompletion = NULL;
    pb.volumeParam.ioNamePtr = loc->volName;
    pb.volumeParam.ioVRefNum = vol;
    pb.volumeParam.ioVolIndex = 0;
    return PBHGetVInfoSync(&pb) == noErr;
}

Boolean Files_WDOf(const FolderLoc *loc, short *wdRef)
{
    HParamBlockRec pb;
    unsigned char name[32];
    short i, n = loc->volName[0];
    if (n == 0 || n > 27)
        return false;
    for (i = 1; i <= n; i++) name[i] = loc->volName[i];
    name[++n] = ':';                     /* "Disk:" names the volume */
    name[0] = (unsigned char)n;
    pb.volumeParam.ioCompletion = NULL;
    pb.volumeParam.ioNamePtr = name;
    pb.volumeParam.ioVRefNum = 0;
    pb.volumeParam.ioVolIndex = -1;
    if (PBHGetVInfoSync(&pb) != noErr)
        return false;                    /* that disk isn't mounted */
    return OpenWD(pb.volumeParam.ioVRefNum, loc->dirID, wdRef);
}

/* ---- preferences ---- */

#define kPrefsName ((ConstStr255Param)"\pMac MD Write Prefs")

Boolean Prefs_Exist(void)
{
    FInfo fi;
    return GetFInfo(kPrefsName, gRootWD, &fi) == noErr;
}

void Prefs_Load(Prefs *p)
{
    short ref;
    long n = sizeof(Prefs);
    Prefs tmp;
    p->version = kPrefsVers;
    p->lastDir.volName[0] = 0;
    p->lastDir.dirID = 0;
    p->lastName[0] = 0;
    p->lastCaret = 0;
    p->recentCount = 0;
    p->fontID = 0;
    p->bodySize = 0;
    p->hide = 0;
    if (FSOpen(kPrefsName, gRootWD, &ref) != noErr)
        return;
    FSRead(ref, &n, (Ptr)&tmp);          /* eofErr: an older, shorter file */
    if (tmp.version == kPrefsVers && n >= (long)((char *)&tmp.fontID - (char *)&tmp)) {
        long k;
        for (k = 0; k < n; k++)          /* what the file has; defaults after */
            ((char *)p)[k] = ((char *)&tmp)[k];
    }
    FSClose(ref);
}

void Prefs_Save(const Prefs *p)
{
    short ref;
    long n = sizeof(Prefs);
    OSErr err = Create(kPrefsName, gRootWD, kCreator, kPrefsType);
    if (err != noErr && err != dupFNErr)
        return;
    if (FSOpen(kPrefsName, gRootWD, &ref) != noErr)
        return;
    if (FSWrite(ref, &n, (Ptr)p) == noErr)
        SetEOF(ref, sizeof(Prefs));
    FSClose(ref);
    {
        FInfo fi;                        /* bookkeeping: invisible in the Finder */
        if (GetFInfo(kPrefsName, gRootWD, &fi) == noErr && !(fi.fdFlags & 0x4000)) {
            fi.fdFlags |= 0x4000;
            SetFInfo(kPrefsName, gRootWD, &fi);
        }
    }
}

/* ---- copying folders: Duplicate, and moving the library to another disk */

#define kMaxMap 128
static long  gMapFrom[kMaxMap], gMapTo[kMaxMap];
static short gMapN;

static Boolean CopyFork(short svol, long sdir, ConstStr255Param sname,
                        short dvol, long ddir, ConstStr255Param dname, Boolean rsrc)
{
    short in, out;
    long n;
    char buf[1024];
    OSErr err;
    err = rsrc ? HOpenRF(svol, sdir, sname, fsRdPerm, &in)
               : HOpen(svol, sdir, sname, fsRdPerm, &in);
    if (err != noErr) return true;       /* no such fork: nothing to copy */
    err = rsrc ? HOpenRF(dvol, ddir, dname, fsWrPerm, &out)
               : HOpen(dvol, ddir, dname, fsWrPerm, &out);
    if (err != noErr) { FSClose(in); return false; }
    do {
        n = sizeof(buf);
        err = FSRead(in, &n, buf);
        if (n > 0 && FSWrite(out, &n, buf) != noErr) { err = -1; break; }
    } while (err == noErr);
    FSClose(in);
    FSClose(out);
    return err == eofErr;
}

static Boolean CopyFile(short svol, long sdir, ConstStr255Param name,
                        short dvol, long ddir)
{
    FInfo fi;
    if (HGetFInfo(svol, sdir, name, &fi) != noErr) return false;
    if (HCreate(dvol, ddir, name, fi.fdCreator, fi.fdType) != noErr) return false;
    if (!CopyFork(svol, sdir, name, dvol, ddir, name, false)
        || !CopyFork(svol, sdir, name, dvol, ddir, name, true))
        return false;
    fi.fdLocation.h = fi.fdLocation.v = 0;   /* the Finder places it */
    fi.fdFlags &= ~0x0100;                   /* not "inited" on the new disk */
    {
        HParamBlockRec pb;                   /* type, creator, invisible bit */
        pb.fileParam.ioCompletion = NULL;
        pb.fileParam.ioNamePtr = (StringPtr)name;
        pb.fileParam.ioVRefNum = dvol;
        pb.fileParam.ioFDirIndex = 0;
        pb.fileParam.ioDirID = ddir;
        if (PBHGetFInfoSync(&pb) == noErr) {
            pb.fileParam.ioFlFndrInfo = fi;
            pb.fileParam.ioDirID = ddir;
            PBHSetFInfoSync(&pb);
        }
    }
    return true;
}

/* Copy folder sdir (and everything in it) into dparent as name. */
static Boolean CopyTree(short svol, long sdir, short dvol, long dparent,
                        ConstStr255Param name, long *newDir, short depth)
{
    CInfoPBRec ci;
    Str63 item;
    short idx;
    long nd;
    if (depth > 8 || !Files_NewFolder(dvol, dparent, name, &nd)) return false;
    if (gMapN < kMaxMap) { gMapFrom[gMapN] = sdir; gMapTo[gMapN] = nd; gMapN++; }
    for (idx = 1; ; idx++) {
        ci.hFileInfo.ioCompletion = NULL;
        ci.hFileInfo.ioNamePtr = item;
        ci.hFileInfo.ioVRefNum = svol;
        ci.hFileInfo.ioFDirIndex = idx;
        ci.hFileInfo.ioDirID = sdir;
        if (PBGetCatInfoSync(&ci) != noErr) break;
        if (ci.hFileInfo.ioFlAttrib & 0x10) {
            long sub;
            if (!CopyTree(svol, ci.dirInfo.ioDrDirID, dvol, nd, item, &sub, depth + 1))
                return false;
        } else if (!CopyFile(svol, sdir, item, dvol, nd))
            return false;
    }
    if (newDir) *newDir = nd;
    return true;
}

/* Copy folder from (in sparent) into dparent as to: a folder template
   becoming a new writing. */
Boolean Files_CopyFolderTo(short svol, long sparent, ConstStr255Param from,
                           short dvol, long dparent, ConstStr255Param to, long *newDir)
{
    CInfoPBRec ci;
    ci.dirInfo.ioCompletion = NULL;
    ci.dirInfo.ioNamePtr = (StringPtr)from;
    ci.dirInfo.ioVRefNum = svol;
    ci.dirInfo.ioFDirIndex = 0;
    ci.dirInfo.ioDrDirID = sparent;
    if (PBGetCatInfoSync(&ci) != noErr || !(ci.dirInfo.ioFlAttrib & 0x10)) return false;
    gMapN = 0;
    return CopyTree(svol, ci.dirInfo.ioDrDirID, dvol, dparent, to, newDir, 0);
}

/* Duplicate a folder beside itself as name (Library: Duplicate). */
Boolean Files_CopyFolder(short vol, long parent, ConstStr255Param from, ConstStr255Param to)
{
    CInfoPBRec ci;
    ci.dirInfo.ioCompletion = NULL;
    ci.dirInfo.ioNamePtr = (StringPtr)from;
    ci.dirInfo.ioVRefNum = vol;
    ci.dirInfo.ioFDirIndex = 0;
    ci.dirInfo.ioDrDirID = parent;
    if (PBGetCatInfoSync(&ci) != noErr || !(ci.dirInfo.ioFlAttrib & 0x10)) return false;
    gMapN = 0;
    return CopyTree(vol, ci.dirInfo.ioDrDirID, vol, parent, to, NULL, 0);
}

static long Mapped(long dir)
{
    short i;
    for (i = 0; i < gMapN; i++)
        if (gMapFrom[i] == dir) return gMapTo[i];
    return 0;
}

static void VolName(short vol, unsigned char *name)
{
    HParamBlockRec pb;
    pb.volumeParam.ioCompletion = NULL;
    pb.volumeParam.ioNamePtr = name;
    pb.volumeParam.ioVRefNum = vol;
    pb.volumeParam.ioVolIndex = 0;
    if (PBHGetVInfoSync(&pb) != noErr) name[0] = 0;
}

/* Settings > Move Library...: the whole Writings folder (documents, their
   invisible journals, templates, prefs) moves into (destWD) as name, so
   journals always stay beside their documents. On the same disk it is one
   catalog move (folder IDs, so Recent, stay valid); onto another disk it is
   copied, Recent is pointed at the copies, and the original goes to the
   Trash. The caller has saved the open document; call Files_Init after. */
Boolean Files_MoveLibrary(short destWD, ConstStr255Param name)
{
    short dvol, i;
    long ddir, nd;
    unsigned char oldName[64], vname[32];
    FolderLoc loc;
    Prefs p;
    if (!WDInfo(destWD, &dvol, &ddir)) return false;
    if (dvol == gRootVol && Files_IsInside(dvol, ddir, gRootDir)) return false;   /* into itself */
    if (Files_Exists(dvol, ddir, name)) return false;
    if (!Files_FolderName(gRootVol, gRootDir, oldName)) return false;
    if (dvol == gRootVol) {
        CInfoPBRec ci;                   /* the folder Writings is in now */
        ci.dirInfo.ioCompletion = NULL;
        ci.dirInfo.ioNamePtr = NULL;
        ci.dirInfo.ioVRefNum = gRootVol;
        ci.dirInfo.ioFDirIndex = -1;
        ci.dirInfo.ioDrDirID = gRootDir;
        if (PBGetCatInfoSync(&ci) != noErr) return false;
        if (ci.dirInfo.ioDrParID != ddir) {
            if (Files_Exists(dvol, ddir, oldName)) return false;   /* clash */
            if (!MoveItem(gRootVol, ci.dirInfo.ioDrParID, oldName, ddir)) return false;
        }
        for (i = 0; i <= name[0] && oldName[i] == name[i]; i++)
            ;
        if (i <= name[0])                /* a new name too */
            Files_Rename(dvol, ddir, oldName, name);
        nd = gRootDir;
    } else {
        long parent;
        CInfoPBRec ci;
        gMapN = 0;
        Prefs_Load(&p);
        ci.dirInfo.ioCompletion = NULL;
        ci.dirInfo.ioNamePtr = NULL;
        ci.dirInfo.ioVRefNum = gRootVol;
        ci.dirInfo.ioFDirIndex = -1;
        ci.dirInfo.ioDrDirID = gRootDir;
        if (PBGetCatInfoSync(&ci) != noErr) return false;
        parent = ci.dirInfo.ioDrParID;
        if (!CopyTree(gRootVol, gRootDir, dvol, ddir, name, &nd, 0)) {
            Files_Trash(dvol, ddir, name);   /* half a copy: the original stays */
            return false;
        }
        VolName(dvol, vname);            /* Recent and the last document */
        if (Mapped(p.lastDir.dirID)) {
            p.lastDir.dirID = Mapped(p.lastDir.dirID);
            for (i = 0; i <= vname[0]; i++) p.lastDir.volName[i] = vname[i];
        }
        for (i = 0; i < p.recentCount; i++) {
            long m = Mapped(p.recent[i].dir.dirID);
            short k;
            if (!m) continue;
            p.recent[i].dir.dirID = m;
            for (k = 0; k <= vname[0]; k++) p.recent[i].dir.volName[k] = vname[k];
        }
        {
            short wd;                    /* the prefs in the copy */
            if (OpenWD(dvol, nd, &wd)) {
                short save = gRootWD;
                gRootWD = wd;
                Prefs_Save(&p);
                gRootWD = save;
            }
        }
        Files_Trash(gRootVol, parent, oldName);   /* the original: recoverable */
    }
    {
        short wd;
        if (OpenWD(dvol, nd, &wd) && Files_LocOf(wd, &loc))
            SavePointer(&loc);
    }
    return true;
}

/* Where the library is, for Settings: its parent folder's name. */
Boolean Files_LibraryPlace(unsigned char *name)
{
    CInfoPBRec ci;
    ci.dirInfo.ioCompletion = NULL;
    ci.dirInfo.ioNamePtr = NULL;
    ci.dirInfo.ioVRefNum = gRootVol;
    ci.dirInfo.ioFDirIndex = -1;
    ci.dirInfo.ioDrDirID = gRootDir;
    if (PBGetCatInfoSync(&ci) != noErr) return false;
    return Files_FolderName(gRootVol, ci.dirInfo.ioDrParID, name);
}

/* The library's own folder, for the Move dialog to start beside it. */
Boolean Files_LibraryParentWD(short *wd)
{
    CInfoPBRec ci;
    ci.dirInfo.ioCompletion = NULL;
    ci.dirInfo.ioNamePtr = NULL;
    ci.dirInfo.ioVRefNum = gRootVol;
    ci.dirInfo.ioFDirIndex = -1;
    ci.dirInfo.ioDrDirID = gRootDir;
    if (PBGetCatInfoSync(&ci) != noErr) return false;
    return OpenWD(gRootVol, ci.dirInfo.ioDrParID, wd);
}

Boolean Files_LibraryName(unsigned char *name)
{
    return Files_FolderName(gRootVol, gRootDir, name);
}

/* The folder called name in parent, if there is one (no creating). */
Boolean Files_FolderNamed(short vol, long parent, ConstStr255Param name, long *dir)
{
    CInfoPBRec ci;
    ci.dirInfo.ioCompletion = NULL;
    ci.dirInfo.ioNamePtr = (StringPtr)name;
    ci.dirInfo.ioVRefNum = vol;
    ci.dirInfo.ioFDirIndex = 0;
    ci.dirInfo.ioDrDirID = parent;
    if (PBGetCatInfoSync(&ci) != noErr || !(ci.dirInfo.ioFlAttrib & 0x10)) return false;
    *dir = ci.dirInfo.ioDrDirID;
    return true;
}

/* Settings > Back Up Library...: the whole library (documents, journals,
   templates, prefs) copied, never moved, into (destWD) as name, on any disk.
   The library goes on where it is; a failed copy is removed again. */
Boolean Files_BackupLibrary(short destWD, ConstStr255Param name)
{
    short dvol;
    long ddir, nd;
    if (!WDInfo(destWD, &dvol, &ddir)) return false;
    if (dvol == gRootVol && Files_IsInside(dvol, ddir, gRootDir)) return false;   /* into itself */
    if (Files_Exists(dvol, ddir, name)) return false;
    gMapN = 0;
    if (!CopyTree(gRootVol, gRootDir, dvol, ddir, name, &nd, 0)) {
        Files_Trash(dvol, ddir, name);   /* half a backup is no backup */
        return false;
    }
    return true;
}

/* "Writings 2026-11-26": a backup's default name. */
void Files_BackupName(unsigned char *name)
{
    DateTimeRec d;
    unsigned long secs;
    short n = 0;
    const char *w = "Writings ";
    GetDateTime(&secs);
    SecondsToDate(secs, &d);
    while (*w) name[++n] = (unsigned char)*w++;
    name[++n] = (unsigned char)('0' + d.year / 1000);
    name[++n] = (unsigned char)('0' + d.year / 100 % 10);
    name[++n] = (unsigned char)('0' + d.year / 10 % 10);
    name[++n] = (unsigned char)('0' + d.year % 10);
    name[++n] = '-';
    name[++n] = (unsigned char)('0' + d.month / 10);
    name[++n] = (unsigned char)('0' + d.month % 10);
    name[++n] = '-';
    name[++n] = (unsigned char)('0' + d.day / 10);
    name[++n] = (unsigned char)('0' + d.day % 10);
    name[0] = (unsigned char)n;
}

/* Close every working directory this app opened (tagged with our creator),
   cached or not. On System 6 under the Finder they outlive the app, and a
   folder with one open can't leave the Trash ("a file was busy"). Called
   at quit, and when the library moves away from its old folders. */
void Files_CloseAll(void)
{
    WDPBRec pb;
    short guard = 0;
    for (;;) {
        pb.ioCompletion = NULL;
        pb.ioNamePtr = NULL;
        pb.ioVRefNum = 0;
        pb.ioWDIndex = 1;                /* always the first of ours left */
        pb.ioWDProcID = kCreator;
        if (PBGetWDInfoSync(&pb) != noErr || guard++ > 200)
            break;
        pb.ioCompletion = NULL;          /* ioVRefNum: the WD just found */
        if (PBCloseWDSync(&pb) != noErr)
            break;
    }
    gWDCount = 0;
}

/* An empty writing called name in parent (no documents or folders left in
   it, journals aside): new work of that name moves back in. */
Boolean Files_EmptyWriting(short vol, long parent, ConstStr255Param name, long *dir)
{
    DirItem items[2];
    return Files_FolderNamed(vol, parent, name, dir) && Files_List(vol, *dir, items, 2) == 0;
}
