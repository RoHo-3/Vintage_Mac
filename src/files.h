/*
 * Where documents live, and the small preferences file.
 *
 * A "Writings" folder (next to the application, unless moved with Settings >
 * Move Library...) holds two peers: "My Writings", where every writing is a
 * folder holding its documents, and "Templates", plus the invisible prefs.
 * The Library shows My Writings on its middle shelf.
 * Folders are reached through working-directory refnums, so the rest of the
 * app keeps using the classic (name, vRefNum) file calls on System 6 and 7.
 *
 * The preferences file ("Mac MD Write Prefs", in "Writings") remembers the
 * last document and its caret, so launch reopens it where you left off.
 */
#ifndef FILES_H
#define FILES_H

#include <Files.h>

Boolean Files_Init(void);            /* false: no folder (locked disk?) */
short   Files_DocsWD(void);          /* My Writings */
short   Files_TemplatesWD(void);

/* A folder as a lasting location (volume name + dirID) and back. */
typedef struct {
    unsigned char volName[28];       /* Pascal string, HFS max 27 */
    long  dirID;
} FolderLoc;

Boolean Files_LocOf(short wdRef, FolderLoc *loc);
Boolean Files_WDOf(const FolderLoc *loc, short *wdRef);

/* Folders as (volume, dirID); a working directory is opened (and kept)
   for each folder the app touches, so (name, wdRef) calls work on it. */
void    Files_DocsDir(short *vol, long *dirID);       /* My Writings */
void    Files_ProjectsDir(short *vol, long *dirID);   /* My Writings too */
Boolean Files_WDFor(short vol, long dirID, short *wdRef);
Boolean Files_DirOfWD(short wdRef, short *vol, long *dirID);
Boolean Files_IsInside(short vol, long dir, long ancestor);
Boolean Files_FolderName(short vol, long dir, unsigned char *name);
Boolean Files_ProjectOf(short vol, long dir, long *project);   /* its writing folder */
void    Files_SetDialogFolder(short wdRef);
void    Files_TidyJournals(void);  /* launch: orphaned .hist files to the Trash */   /* next SFGetFile/SFPutFile */

/* One folder listing: documents (.md / TEXT) and folders, Finder order. */
#define kMaxItems 64
typedef struct {
    Str31   name;
    Boolean isFolder;
    long    dirID;                   /* folders: their own ID */
    long    parent;                  /* the folder it is in */
} DirItem;
short   Files_List(short vol, long dirID, DirItem *items, short max);

Boolean Files_NewFolder(short vol, long parent, ConstStr255Param name, long *dirID);
Boolean Files_Rename(short vol, long dirID, ConstStr255Param from, ConstStr255Param to);
Boolean Files_Trash(short vol, long dirID, ConstStr255Param name);
Boolean Files_Copy(short wdRef, ConstStr255Param from, ConstStr255Param to);
Boolean Files_CopyTo(short wdRef, ConstStr255Param from, short dstWD, ConstStr255Param to);
Boolean Files_Exists(short vol, long dirID, ConstStr255Param name);
/* One limit for every name the app makes or takes (writings, documents,
   renames): 26 characters, so "<name>.hist" still fits HFS's 31 and a
   writing's folder and document can always share a name. */
#define kNameMax 26
void    Files_FitName(unsigned char *s);     /* cut to kNameMax at a word */
void    Files_FitNameTo(unsigned char *s, short max);
Boolean Files_FolderNamed(short vol, long parent, ConstStr255Param name, long *dir);
Boolean Files_FreeName(short vol, long dir, const unsigned char *base, unsigned char *out);
Boolean Files_CopyFolder(short vol, long parent, ConstStr255Param from, ConstStr255Param to);
void    Files_WrapLoose(void);       /* launch: loose documents get a folder each */

/* The library itself (Settings). Move: the caller saved first and calls
   Files_Init afterwards. */
Boolean Files_MoveLibrary(short destWD, ConstStr255Param name);
Boolean Files_LibraryPlace(unsigned char *name);   /* the folder it's in */
Boolean Files_BackupLibrary(short destWD, ConstStr255Param name);   /* a copy */
void    Files_BackupName(unsigned char *name);         /* "Writings 2026-11-26" */
Boolean Files_LibraryName(unsigned char *name);
Boolean Files_LibraryParentWD(short *wd);

#define kMaxRecent 8
typedef struct {
    FolderLoc dir;
    Str63     name;
} RecentItem;

typedef struct {
    short      version;
    FolderLoc  lastDir;
    Str63      lastName;             /* empty: no last document */
    long       lastCaret;
    short      recentCount;
    RecentItem recent[kMaxRecent];   /* newest first; includes the last doc */
    short      fontID;               /* 0 = MDW Mono, else a system font ID */
    short      bodySize;             /* 0 = default */
    short      hide;                 /* Settings: 1 = no word count, 2 = no clock */
} Prefs;

Boolean Prefs_Exist(void);           /* false on the very first launch */

void    Prefs_Load(Prefs *p);        /* defaults when missing or unreadable */
void    Prefs_Save(const Prefs *p);

#endif
