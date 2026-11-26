/*
 * The selection pill (DESIGN.md §4a): the app's context menu, shown above a
 * mouse selection on mouse-up. Three levels that drill down like the
 * drawer's blades:
 *   Cut · Copy · Paste · Format »
 *   « · B · I · S · Code · Paragraph »
 *   « | H1-H6 / • List · 1. List · Task · Quote
 * Press and hold on the page (no selection) shows Paste · Select All.
 * Typing, Esc or a click elsewhere puts it away.
 */
#ifndef PILL_H
#define PILL_H

#include <Windows.h>

void    Pill_Init(WindowPtr w);
void    Pill_Show(void);                 /* after a mouse selection */
void    Pill_ShowAtCaret(void);          /* press and hold: Paste · Select All */
void    Pill_Hide(void);
Boolean Pill_IsShown(void);
Boolean Pill_Click(Point local);         /* true: the click was the pill's */
void    Pill_Redraw(void);               /* after an update redrew the page */

#endif
