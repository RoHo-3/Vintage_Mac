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
void Chrome_CharTyped(void);            /* first typed character starts the clock */
void Chrome_Hold(Boolean hold);
void    Chrome_SetStatusParts(Boolean words, Boolean clock);   /* Settings */
const unsigned char *Chrome_Title(void);   /* the document's name, without .md */
Boolean Chrome_HamburgerClick(Point local);   /* opens the drawer */         /* true while the Library is up */
void Chrome_ShowTitleBriefly(void);     /* a document opened: its name, ~2 s */
void Chrome_ShowSaveDot(void);          /* Cmd-S: footer up, so the dot shows the save */
void Chrome_SetTitle(const unsigned char *pascalTitle);
void Chrome_SetSaveState(short state);
void Chrome_TextChanged(void);          /* word count is stale */
void Chrome_ResetClock(void);
void Chrome_SetGoal(char kind, long target);   /* 'm' minutes, 'w' new words; 0 clears */
char Chrome_GoalKind(void);
void Chrome_NoteWords(void);            /* a save: count the words now */
void Chrome_ShowMessage(const unsigned char *msg, short seconds);   /* in the strip */
unsigned long Chrome_ClockStart(void);      /* the session clock, kept through */
void Chrome_SetClockStart(unsigned long t); /* a Move Library reopen */

/* Menu bar hiding (restored on suspend and quit). */
void Chrome_HideMenuBar(void);
void Chrome_ShowMenuBar(void);

#endif
