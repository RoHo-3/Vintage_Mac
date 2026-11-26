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
void    Doc_Open(void);
Boolean Doc_Save(void);          /* false if cancelled or failed */
void    Doc_Idle(void);          /* autosave on pause */
void    Doc_Suspend(void);       /* switching to another app */
Boolean Doc_CanQuit(void);       /* saves first; false = stay open */

void    Doc_ToggleMarks(void);
void    Doc_Remember(void);     /* last document + caret, for the next launch */
Boolean Doc_Reopen(void);       /* launch: reopen it; false = none */  /* Markdown markers shown/hidden */

#endif
