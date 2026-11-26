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
Boolean Doc_SaveCommand(void);  /* Cmd-S: Save dialog when untitled */
Boolean Doc_Open(void);          /* classic dialog (Library's Other...) */
Boolean Doc_Save(void);          /* false if cancelled or failed */
void    Doc_Idle(void);          /* autosave on pause */
void    Doc_Suspend(void);       /* switching to another app */
Boolean Doc_CanQuit(void);       /* saves first; false = stay open */

void    Doc_ToggleMarks(void);
void    Doc_Remember(void);     /* last document + caret, for the next launch */
Boolean Doc_Reopen(void);       /* launch: reopen it; false = none */

/* For the Library. wd: a working directory (files.h). */
Boolean Doc_HasFile(void);
Boolean Doc_Location(short *vol, long *dirID);
Boolean Doc_IsOpen(short wd, const unsigned char *name);
Boolean Doc_OpenAt(const unsigned char *name, short wd);
Boolean Doc_NewFromTemplate(const unsigned char *name, short wd);
void    Doc_SetNewFolder(short wd); /* where the next new document is saved */
Boolean Doc_RenameFile(short wd, const unsigned char *from, const unsigned char *to);
void    Doc_FileTrashed(short wd, const unsigned char *name);
void    Doc_ForgetRecent(short wd, const unsigned char *name, const unsigned char *newName);  /* Markdown markers shown/hidden */

#endif
