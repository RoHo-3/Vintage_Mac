/*
 * Save-under for small floating cards (the selection pill, the Cmd-K
 * palette): the screen bits under the card are kept offscreen and put
 * back when it closes, so the page never redraws or flickers. One card at
 * a time.
 */
#ifndef OVERLAY_H
#define OVERLAY_H

#include <Quickdraw.h>

Boolean Under_Save(WindowPtr w, const Rect *r);   /* false: no memory (then
                                                     Under_Restore redraws) */
void    Under_Restore(void);
void    Under_Forget(void);                       /* the page was redrawn */

#endif
