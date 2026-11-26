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

/* True if MDW Mono didn't load and Monaco is being used instead. */
Boolean Ed_UsingFallbackFont(void);

/* Change tracking for autosave: weighted characters changed since the
   last Ed_ClearChanges (paste, cut, undo count at least 50). */
long          Ed_ChangeWeight(void);
unsigned long Ed_FirstChangeTick(void);
unsigned long Ed_LastEditTick(void);
void          Ed_ClearChanges(void);
long          Ed_Caret(void);

/* Replace the whole document (no undo). */
Boolean Ed_SetText(const char *text, long len);

/* Markdown markers shown or hidden (a per-document setting). */
void    Ed_SetShowMarks(Boolean on);
Boolean Ed_ShowMarks(void);
void    Ed_Hold(Boolean hold);
/* Paragraph formats for the paragraphs the selection touches: 0 normal,
   1-6 heading, 'b' bulleted / 'n' numbered / 't' task list, 'q' quote.
   One that all of them already have is taken off. */
void    Ed_BlockFormat(short what);
void    Ed_Format(char marker, short count);   /* Cmd-B ('*',2), Cmd-I ('*',1), Cmd-E ('`',1) */

/* Fonts: 0 = MDW Mono, else a system font ID (Monaco 4, Courier 22, New
   York 2, Geneva 3, Times 20, Helvetica 21, Palatino 16), fixed-width or
   proportional: lines are measured per character. One
   choice for the whole app; headings scale from the body size. */
void    Ed_SetFontChoice(short fontID, short bodySize);
Boolean Ed_FontAvailable(short fontID);
short   Ed_FontID(void);
short   Ed_BodySize(void);
short   Ed_StepBodySize(short dir); /* next real body size up/down (wraps) */      /* true while the Library covers the text */

void    Ed_SetCaret(long pos);      /* places the cursor and scrolls to it */
Boolean Ed_HasSelection(void);
Boolean Ed_ClickWasHold(void);      /* the last click was a press and hold */
void    Ed_SelectionBounds(Rect *firstLine, Rect *lastLine);   /* on screen */
Boolean Ed_HasHeadings(void);
void    Ed_JumpSection(short dir);   /* next (1) / previous (-1) heading */

#endif
