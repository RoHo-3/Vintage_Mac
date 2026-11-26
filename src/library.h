/*
 * The Library (DESIGN.md): a full-screen browser that replaces the classic
 * Open dialog. Three shelves of 32x32 tiles: Templates, My Files (folders,
 * projects, breadcrumb), Recent. An action bar above them offers what fits
 * the picked tile. Always shown: New Document, New Project / New Folder |
 * Open, Duplicate, Rename (in place), Move to Trash; what doesn't fit the
 * pick is greyed. Open with nothing picked: the classic dialog (any .md).
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
void    Lib_OpenNew(void);      /* Cmd-N: nothing picked, New Document leads;
                                   Return (or Cmd-N again) makes one */
void    Lib_Close(void);         /* back to writing */
Boolean Lib_IsOpen(void);
void    Lib_Draw(void);
void    Lib_Key(EventRecord *ev);
void    Lib_Click(Point local, EventRecord *ev);
void    Lib_Idle(void);

#endif
