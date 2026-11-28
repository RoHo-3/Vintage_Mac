/*
 * The command table (DESIGN.md "Command palette"): every command in the
 * app, with its name, search words (hand-coded synonyms) and shortcut.
 * The Cmd-K palette lists it and the selection pill runs from it, so a
 * command reads and behaves the same everywhere.
 */
#ifndef COMMANDS_H
#define COMMANDS_H

#include <Memory.h>

enum {
    cBold = 1, cItalic, cStrike, cCode,
    cNormal, cH1, cH2, cH3, cH4, cH5, cH6,
    cBullets, cNumbers, cTasks, cQuote,
    cUndo, cRedo, cCut, cCopy, cPaste, cSelectAll,
    cMarks, cNextSection, cPrevSection, cSections,
    cLibrary, cNew, cSave,
    cBigger, cSmaller, cNextFont, cWords, cClock, cDrawer, cSettings,
    cTiming, cQuit,
    cNextTK, cInsertTK,
    cFocusMinutes, cFocusWords, cFocusClear, cHistory,
    cFootnote, cJumpFootnote, cCuts
};

typedef struct {
    short       id;
    const char *group;      /* Navigate, Write, Document, View, App */
    const char *name;
    const char *keys;       /* extra search words */
    const char *shortcut;   /* shown at the right, or "" */
} Command;

const Command *Cmd_Table(short *count);
const char    *Cmd_Label(const Command *c);   /* name, or "Hide ..." for a toggle that's on */
Boolean        Cmd_Available(short id);
void           Cmd_Run(short id);

#endif
