/*
 * The writing canvas: draws the text column, cursor and selection, and
 * handles typing, movement, mouse selection, undo and the clipboard.
 */
#ifndef EDITOR_H
#define EDITOR_H

#include <Quickdraw.h>
#include <Events.h>

Boolean Ed_Init(WindowPtr w);
void    Ed_DrawAll(void);
void    Ed_Idle(void);
void    Ed_Key(EventRecord *ev);           /* non-command keys and arrows */
void    Ed_Click(Point local, EventRecord *ev);
void    Ed_Activate(Boolean active);

void    Ed_Undo(void);
void    Ed_Redo(void);
void    Ed_Cut(void);
void    Ed_Copy(void);
void    Ed_Paste(void);
void    Ed_SelectAll(void);

/* Replace the whole document (no undo). */
Boolean Ed_SetText(const char *text, long len);

#endif
