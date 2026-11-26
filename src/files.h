/*
 * Where documents live, and the small preferences file.
 *
 * My Files is a "Mac MD Write" folder next to the application, created on
 * first launch, with a "Templates" folder inside it (hidden from My Files).
 * Folders are reached through working-directory refnums, so the rest of the
 * app keeps using the classic (name, vRefNum) file calls on System 6 and 7.
 *
 * The preferences file ("Mac MD Write Prefs", in My Files) remembers the
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

typedef struct {
    short     version;
    FolderLoc lastDir;
    Str63     lastName;              /* empty: no last document */
    long      lastCaret;
} Prefs;

void    Prefs_Load(Prefs *p);        /* defaults when missing or unreadable */
void    Prefs_Save(const Prefs *p);

#endif
