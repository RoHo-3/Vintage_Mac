/*
 * Where documents live, and the small preferences file.
 *
 * A "Mac MD Write" folder next to the application holds two peers: "My
 * Files" (your documents and projects) and "Templates", plus the prefs.
 * Folders are reached through working-directory refnums, so the rest of the
 * app keeps using the classic (name, vRefNum) file calls on System 6 and 7.
 *
 * The preferences file ("Mac MD Write Prefs", in "Mac MD Write") remembers the
 * last document and its caret, so launch reopens it where you left off.
 */
#ifndef FILES_H
#define FILES_H

#include <Files.h>

Boolean Files_Init(void);            /* false: no folder (locked disk?) */
short   Files_DocsWD(void);          /* My Files */
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
void    Files_DocsDir(short *vol, long *dirID);
Boolean Files_WDFor(short vol, long dirID, short *wdRef);
Boolean Files_DirOfWD(short wdRef, short *vol, long *dirID);
Boolean Files_IsInside(short vol, long dir, long ancestor);

/* One folder listing: documents (.md / TEXT) and folders, Finder order. */
#define kMaxItems 64
typedef struct {
    Str31   name;
    Boolean isFolder;
    long    dirID;                   /* folders: their own ID */
} DirItem;
short   Files_List(short vol, long dirID, DirItem *items, short max);

Boolean Files_NewFolder(short vol, long parent, ConstStr255Param name, long *dirID);
Boolean Files_Rename(short vol, long dirID, ConstStr255Param from, ConstStr255Param to);
Boolean Files_Trash(short vol, long dirID, ConstStr255Param name);
Boolean Files_Copy(short wdRef, ConstStr255Param from, ConstStr255Param to);
Boolean Files_Exists(short vol, long dirID, ConstStr255Param name);

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
} Prefs;

Boolean Prefs_Exist(void);           /* false on the very first launch */

void    Prefs_Load(Prefs *p);        /* defaults when missing or unreadable */
void    Prefs_Save(const Prefs *p);

#endif
