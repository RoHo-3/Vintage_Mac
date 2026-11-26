/*
 * The Cmd-K command palette (DESIGN.md "Command palette"): a 260 x 170 card
 * centered on the page. Type to filter every command in the app (names and
 * synonyms; each word must match), arrows pick, Return runs, Esc or Cmd-K
 * closes. Shortcuts sit at the right, so it doubles as help.
 */
#ifndef PALETTE_H
#define PALETTE_H

#include <Windows.h>
#include <Events.h>

void    Palette_Init(WindowPtr w);
void    Palette_Open(void);
void    Palette_Close(void);
Boolean Palette_IsOpen(void);
void    Palette_Key(EventRecord *ev);
void    Palette_Click(Point local);
void    Palette_Redraw(void);            /* after an update redrew the page */

#endif
