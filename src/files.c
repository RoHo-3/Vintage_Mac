#include "files.h"

#include <Memory.h>

#define kCreator    'MMDW'
#define kPrefsType  'pref'
#define kPrefsVers  1

static short gDocsWD, gTemplatesWD;

static Boolean OpenWD(short vol, long dirID, short *wd)
{
    WDPBRec pb;
    pb.ioCompletion = NULL;
    pb.ioNamePtr = NULL;
    pb.ioVRefNum = vol;
    pb.ioWDDirID = dirID;
    pb.ioWDProcID = kCreator;
    if (PBOpenWDSync(&pb) != noErr)
        return false;
    *wd = pb.ioVRefNum;
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

Boolean Files_Init(void)
{
    short appWD, vol;
    long appDir, docsDir, tplDir;

    GetVol(NULL, &appWD);                /* launch sets it to the app's folder */
    gDocsWD = gTemplatesWD = appWD;      /* fallback: beside the app */
    if (!WDInfo(appWD, &vol, &appDir))
        return false;
    if (!SubFolder(vol, appDir, (ConstStr255Param)"\pMac MD Write", &docsDir)
        || !OpenWD(vol, docsDir, &gDocsWD))
        return false;
    gTemplatesWD = gDocsWD;
    if (SubFolder(vol, docsDir, (ConstStr255Param)"\pTemplates", &tplDir))
        OpenWD(vol, tplDir, &gTemplatesWD);
    return true;
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
    if (FSOpen(kPrefsName, gDocsWD, &ref) != noErr)
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
    OSErr err = Create(kPrefsName, gDocsWD, kCreator, kPrefsType);
    if (err != noErr && err != dupFNErr)
        return;
    if (FSOpen(kPrefsName, gDocsWD, &ref) != noErr)
        return;
    if (FSWrite(ref, &n, (Ptr)p) == noErr)
        SetEOF(ref, sizeof(Prefs));
    FSClose(ref);
}
