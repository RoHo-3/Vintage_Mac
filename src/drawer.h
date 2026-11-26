/*
 * The drawer (DESIGN.md §4, "Hamburger drawer = document explorer"):
 * a 180 px panel at the left, headed with the document's name, over a 50%
 * dither of the page. Rows with › slide a blade over it (14 px of the panel
 * underneath stays visible as the way back).
 *
 * 0.02 rows: <Project> ›, Sections ›, Settings ›, Library..., Quit. Blades
 * for features not built yet (Links, Find, Focus Timer, History, Export)
 * are left out rather than shown dead.
 *
 * Opened by the hamburger or Cmd-\; arrows move, Return / -> opens,
 * <- / Esc goes back (closes at the top), Cmd-\ closes. It reopens to the
 * last blade used.
 */
#ifndef DRAWER_H
#define DRAWER_H

#include <Windows.h>
#include <Events.h>

void    Drawer_Init(WindowPtr w);
void    Drawer_Open(void);
void    Drawer_Close(void);
Boolean Drawer_IsOpen(void);
void    Drawer_Draw(void);               /* update events */
void    Drawer_Key(EventRecord *ev);
void    Drawer_Click(Point local, EventRecord *ev);

#endif
