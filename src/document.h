/*
 * The open document: reading and writing .md files, and autosave.
 * 0.01 alpha uses the classic Open/Save dialogs as a stand-in for the
 * Library; files themselves follow docs/STORAGE.md (UTF-8, original line
 * endings, exact round trip).
 */
#ifndef DOCUMENT_H
#define DOCUMENT_H

#include <Memory.h>

void    Doc_Init(void);
void    Doc_New(void);
void    Doc_NewWithText(const char *text, long len, long selA, long selB);
Boolean Doc_SaveCommand(void);  /* Cmd-S: Save dialog when untitled */
Boolean Doc_Open(void);          /* classic dialog (Library's Other...) */
Boolean Doc_Import(void);
Boolean Doc_SaveNewWriting(const unsigned char *name);   /* first run: the guide */        /* copy a .md from anywhere into a new writing */
Boolean Doc_Save(void);          /* false if cancelled or failed */
void    Doc_Idle(void);          /* autosave on pause */
void    Doc_Suspend(void);       /* switching to another app */
Boolean Doc_CanQuit(void);       /* saves first; false = stay open */

void    Doc_ToggleMarks(void);
void    Doc_Remember(void);     /* last document + caret, for the next launch */
Boolean Doc_Reopen(void);       /* launch: reopen it; false = none */

/* For the Library. wd: a working directory (files.h). */
Boolean Doc_HasFile(void);
short   Doc_WD(void);                  /* its folder's working directory */
const unsigned char *Doc_Name(void);   /* the file's name (if it has one) */
Boolean Doc_Location(short *vol, long *dirID);
Boolean Doc_IsOpen(short wd, const unsigned char *name);
Boolean Doc_OpenAt(const unsigned char *name, short wd);
Boolean Doc_NewFromTemplate(const unsigned char *name, short wd, short destWD);
void    Doc_SetNewFolder(short wd); /* where the next new document is saved */
Boolean Doc_RenameFile(short wd, const unsigned char *from, const unsigned char *to);
void    Doc_FileTrashed(short wd, const unsigned char *name);
void    Doc_ForgetRecent(short wd, const unsigned char *name, const unsigned char *newName);  /* Markdown markers shown/hidden */

/* History: open a past version (text in the app's form) as a new
   document beside this one, named with its time. */
Boolean Doc_OpenVersion(Ptr text, long len, unsigned long secs);
void    Doc_VersionStamp(unsigned long secs, unsigned char *out);   /* appends "Nov 27 16.24" */

/* Move to Cuts: the selection goes to Cuts.md beside this document.
   0 done; 1 nothing selected; 2 this is Cuts; 3 couldn't write. */
short   Doc_MoveToCuts(void);

#endif
