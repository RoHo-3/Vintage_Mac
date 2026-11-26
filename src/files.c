#include "files.h"

#include <Memory.h>
#include <OSUtils.h>

#define kCreator    'MMDW'
#define kPrefsType  'pref'
#define kPrefsVers  2

static short gDocsWD, gTemplatesWD, gRootWD;   /* My Files, Templates, Mac MD Write */
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

/* Earlier builds kept documents directly in "Mac MD Write" (29-34) or in a
   "My Files" folder (35). Each launch tidies the top: documents move to My
   Documents, folders (projects) to My Projects; the prefs follow. */
static void Tidy(short vol, long from, long root, long docs, long projs, Prefs *p)
{
    CInfoPBRec ci;
    CMovePBRec mv;
    Str63 name;
    short idx = 1, i;
    Boolean isDir;
    long sub;
    while (true) {
        ci.hFileInfo.ioCompletion = NULL;
        ci.hFileInfo.ioNamePtr = name;
        ci.hFileInfo.ioVRefNum = vol;
        ci.hFileInfo.ioFDirIndex = idx;
        ci.hFileInfo.ioDirID = from;
        if (PBGetCatInfoSync(&ci) != noErr)
            break;
        isDir = (ci.hFileInfo.ioFlAttrib & 0x10) != 0;
        sub = ci.dirInfo.ioDrDirID;
        if (from == root && (SameP(name, (ConstStr255Param)"\pMy Documents")
            || SameP(name, (ConstStr255Param)"\pMy Projects")
            || SameP(name, (ConstStr255Param)"\pTemplates")
            || SameP(name, (ConstStr255Param)"\pMac MD Write Prefs"))) {
            idx++;
            continue;
        }
        if (from == root && isDir && SameP(name, (ConstStr255Param)"\pMy Files")) {
            HParamBlockRec del;
            Tidy(vol, sub, root, docs, projs, p);       /* empty it, then remove it */
            del.fileParam.ioCompletion = NULL;
            del.fileParam.ioNamePtr = name;
            del.fileParam.ioVRefNum = vol;
            del.fileParam.ioDirID = root;
            if (PBHDeleteSync(&del) != noErr) idx++;
            continue;
        }
        mv.ioCompletion = NULL;
        mv.ioNamePtr = name;
        mv.ioVRefNum = vol;
        mv.ioNewName = NULL;
        mv.ioNewDirID = isDir ? projs : docs;
        mv.ioDirID = from;
        if (PBCatMoveSync(&mv) != noErr)
            idx++;                       /* a name clash: leave it */
        /* on success the indexes shift: same idx again */
    }
    if (p->lastDir.dirID == from) p->lastDir.dirID = docs;   /* moved documents */
    for (i = 0; i < p->recentCount; i++)
        if (p->recent[i].dir.dirID == from) p->recent[i].dir.dirID = docs;
}

Boolean Files_Init(void)
{
    short appWD, vol;
    long appDir, rootDir, docsDir, projDir, tplDir;
    Prefs p;

    GetVol(NULL, &appWD);                /* launch sets it to the app's folder */
    gDocsWD = gTemplatesWD = gRootWD = appWD;   /* fallback: beside the app */
    if (!WDInfo(appWD, &vol, &appDir))
        return false;
    /* Builds 29-41 called it "Mac MD Write", which looked like the app's
       own name in the Finder: rename it once (contents and IDs stay). */
    if (!Files_Exists(vol, appDir, (ConstStr255Param)"\pLibrary"))
        Files_Rename(vol, appDir, (ConstStr255Param)"\pMac MD Write",
                     (ConstStr255Param)"\pLibrary");
    if (!SubFolder(vol, appDir, (ConstStr255Param)"\pLibrary", &rootDir)
        || !OpenWD(vol, rootDir, &gRootWD))
        return false;
    gDocsWD = gTemplatesWD = gRootWD;
    gDocsVol = gProjVol = vol;
    gDocsDir = gProjDir = rootDir;
    if (SubFolder(vol, rootDir, (ConstStr255Param)"\pMy Documents", &docsDir)
        && OpenWD(vol, docsDir, &gDocsWD))
        gDocsDir = docsDir;
    if (SubFolder(vol, rootDir, (ConstStr255Param)"\pMy Projects", &projDir))
        gProjDir = projDir;
    if (gDocsDir != rootDir && gProjDir != rootDir) {
        Prefs_Load(&p);
        Tidy(vol, rootDir, rootDir, gDocsDir, gProjDir, &p);
        Prefs_Save(&p);
    }
    if (SubFolder(vol, rootDir, (ConstStr255Param)"\pTemplates", &tplDir)
        && OpenWD(vol, tplDir, &gTemplatesWD)) {
        /* a Templates folder always offers at least Blank */
        short ref;
        if (Create((ConstStr255Param)"\pBlank.md", gTemplatesWD, kCreator, 'TEXT') == noErr
            && FSOpen((ConstStr255Param)"\pBlank.md", gTemplatesWD, &ref) == noErr)
            FSClose(ref);
    }
    return true;
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

void Files_ProjectsDir(short *vol, long *dirID) { *vol = gProjVol; *dirID = gProjDir; }

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
            if (dirID == gDocsDir && NameEq(name, "Templates"))
                continue;                /* Templates has its own row */
            items[n].isFolder = true;
            items[n].dirID = ci.dirInfo.ioDrDirID;
        } else {
            if (!IsDoc(name, ci.hFileInfo.ioFlFndrInfo.fdType))
                continue;                /* prefs, journals, other files */
            items[n].isFolder = false;
            items[n].dirID = 0;
        }
        if (name[0] > 31) name[0] = 31;
        for (i = 0; i <= name[0]; i++) items[n].name[i] = name[i];
        items[n].parent = dirID;
        n++;
    }
    /* "Outline" pinned first, then folders and documents by name */
    for (i = 1; i < n; i++)
        for (j = i; j > 0; j--) {
            DirItem t;
            DirItem *a = &items[j - 1], *b = &items[j];
            if (IsOutlineDoc(a) || (!IsOutlineDoc(b) && !LessName(b->name, a->name)))
                break;
            t = *a; *a = *b; *b = t;
        }
    return n;
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

/* The Trash: on System 6 and 7 alike, the Finder keeps it in a folder
   named "Trash" at the root of each disk. */
static Boolean TrashDir(short vol, long *dir)
{
    return SubFolder(vol, 2, (ConstStr255Param)"\pTrash", dir);
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
    short in, out;
    long n;
    char buf[1024];
    OSErr err;
    FInfo fi;
    if (FSOpen(from, wd, &in) != noErr)
        return false;
    if (GetFInfo(from, wd, &fi) != noErr) { fi.fdCreator = kCreator; fi.fdType = 'TEXT'; }
    if (Create(to, wd, fi.fdCreator, fi.fdType) != noErr || FSOpen(to, wd, &out) != noErr) {
        FSClose(in);
        return false;
    }
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
    if (FSOpen(kPrefsName, gRootWD, &ref) != noErr)
        return;
    if (FSRead(ref, &n, (Ptr)&tmp) == noErr && n == sizeof(Prefs)
        && tmp.version == kPrefsVers)
        *p = tmp;
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
}
