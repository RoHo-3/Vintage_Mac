/*
 * The quiet chrome (docs/DESIGN.md §2): a 20 px top strip (hamburger and
 * title) and a 12 px status footer (word count / session clock, save dot).
 * Both appear when the writer pauses or moves the mouse and vanish on the
 * next keystroke. Nothing here ever takes focus from the text.
 */
#ifndef CHROME_H
#define CHROME_H

#include <Quickdraw.h>

enum { kSaveUnsaved = 0, kSaveSaving = 1, kSaveSaved = 2 };

#define kStripH  20
#define kFooterH 12

void Chrome_Init(WindowPtr w);
void Chrome_Draw(void);                 /* redraw if shown (update events) */
void Chrome_Idle(void);                 /* show on pause / mouse move, tick clock */
void Chrome_KeyPressed(void);           /* hide */
void Chrome_ShowTitleBriefly(void);     /* a document opened: its name, ~2 s */
void Chrome_ShowSaveDot(void);          /* Cmd-S: footer up, so the dot shows the save */
void Chrome_SetTitle(const unsigned char *pascalTitle);
void Chrome_SetSaveState(short state);
void Chrome_TextChanged(void);          /* word count is stale */
void Chrome_ResetClock(void);

/* Menu bar hiding (restored on suspend and quit). */
void Chrome_HideMenuBar(void);
void Chrome_ShowMenuBar(void);

#endif
