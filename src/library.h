/*
 * The Library (DESIGN.md): a full-screen browser that replaces the classic
 * Open dialog. Three shelves of 32x32 tiles: Templates, My Writings (every
 * writing is a folder; a + tile, New, leads; breadcrumb inside),
 * Recent. The action bar: Open, Duplicate, Rename (in place), Move to
 * Trash, greyed when they don't fit the pick; inside a writing, New
 * Document and New Folder join them. Open with nothing picked: the classic
 * dialog (any .md). Double-click a folder to open it, a document to write.
 *
 * Keyboard: arrows move, Return opens, Esc returns to writing, Cmd-R
 * renames, Cmd-D duplicates, Cmd-Delete moves to the Trash, typing a letter
 * jumps to a name on the current shelf.
 */
#ifndef LIBRARY_H
#define LIBRARY_H

#include <Windows.h>
#include <Events.h>

void    Lib_Init(WindowPtr w);
void    Lib_Open(void);
void    Lib_OpenNew(void);      /* Cmd-N: New picked; Return (or
                                   Cmd-N again) starts one */
void    Lib_Close(void);         /* back to writing */
Boolean Lib_IsOpen(void);
void    Lib_Reset(void);         /* after Move Library */
void    Lib_Draw(void);
void    Lib_Key(EventRecord *ev);
void    Lib_Click(Point local, EventRecord *ev);
void    Lib_Idle(void);

#endif
