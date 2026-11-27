#include "commands.h"
#include "editor.h"
#include "document.h"
#include "library.h"
#include "drawer.h"
#include "files.h"
#include "app.h"

/* Search words are what a writer might type instead of the name:
   "cancel", "turn off", a plain-language word for a Markdown term. */
static const Command kCommands[] = {
    { cLibrary,     "Navigate", "Library\311",          "open files browse documents projects folder find file", "Cmd-O" },
    { cSections,    "Navigate", "Go to Section\311",    "headings outline jump contents toc chapter",  "" },
    { cNextSection, "Navigate", "Next Section",         "heading down forward jump chapter",           "" },
    { cPrevSection, "Navigate", "Previous Section",     "heading up back jump chapter",                "" },
    { cBold,        "Write",    "Bold",                 "strong heavy emphasis",                       "Cmd-B" },
    { cItalic,      "Write",    "Italic",               "emphasis slant oblique",                      "Cmd-I" },
    { cStrike,      "Write",    "Strikethrough",        "strike cross out line through delete",        "Cmd-Shift-X" },
    { cCode,        "Write",    "Code",                 "monospace inline typewriter verbatim",        "Cmd-E" },
    { cH1,          "Write",    "Heading 1",            "title header h1 section big",                 "Cmd-Opt-1" },
    { cH2,          "Write",    "Heading 2",            "header h2 section subtitle",                  "Cmd-Opt-2" },
    { cH3,          "Write",    "Heading 3",            "header h3 section",                           "Cmd-Opt-3" },
    { cH4,          "Write",    "Heading 4",            "header h4 section",                           "Cmd-Opt-4" },
    { cH5,          "Write",    "Heading 5",            "header h5 section",                           "Cmd-Opt-5" },
    { cH6,          "Write",    "Heading 6",            "header h6 section small caps",                "Cmd-Opt-6" },
    { cNormal,      "Write",    "Normal Paragraph",     "body plain text remove heading clear",        "Cmd-Opt-0" },
    { cBullets,     "Write",    "Bulleted List",        "bullets unordered dots list ul",              "Cmd-Shift-8" },
    { cNumbers,     "Write",    "Numbered List",        "ordered numbers list ol 123",                 "Cmd-Shift-7" },
    { cTasks,       "Write",    "Task List",            "todo to-do checkbox check checklist tick",    "Cmd-Shift-9" },
    { cQuote,       "Write",    "Quote",                "blockquote block quote citation cite",        "Cmd-Shift-'" },
    { cUndo,        "Write",    "Undo",                 "revert back mistake",                         "Cmd-Z" },
    { cRedo,        "Write",    "Redo",                 "again forward",                               "Cmd-Shift-Z" },
    { cCut,         "Write",    "Cut",                  "clipboard remove",                            "Cmd-X" },
    { cCopy,        "Write",    "Copy",                 "clipboard duplicate",                         "Cmd-C" },
    { cPaste,       "Write",    "Paste",                "clipboard insert",                            "Cmd-V" },
    { cSelectAll,   "Write",    "Select All",           "everything whole document",                   "Cmd-A" },
    { cNew,         "Document", "New Document\311",     "create blank start template",                 "Cmd-N" },
    { cSave,        "Document", "Save",                 "store write keep",                            "Cmd-S" },
    { cMarks,       "View",     "Show Markdown",        "markers syntax source reveal hide markdown symbols", "Cmd-M" },
    { cBigger,      "View",     "Bigger Text",          "zoom in larger increase font size",           "Cmd-=" },
    { cSmaller,     "View",     "Smaller Text",         "zoom out decrease font size",                 "Cmd--" },
    { cNextFont,    "View",     "Next Font",            "typeface change font",                        "Cmd-Opt-F" },
    { cWords,       "View",     "Show Word Count",      "words count statistics hide turn off",        "" },
    { cClock,       "View",     "Show Session Clock",   "timer time elapsed hide turn off",            "" },
    { cDrawer,      "View",     "Drawer",               "menu sidebar hamburger panel",                "Cmd-\\" },
    { cSettings,    "View",     "Settings\311",         "preferences options prefs font size",         "" },
    { cTiming,      "App",      "Launch Timing",        "speed performance startup slow time how long", "" },
    { cQuit,        "App",      "Quit",                 "exit close leave stop",                       "Cmd-Q" },
};

#define kCount ((short)(sizeof(kCommands) / sizeof(kCommands[0])))

const Command *Cmd_Table(short *count)
{
    *count = kCount;
    return kCommands;
}

/* Toggles read as what they will do: "Hide Word Count" while it shows. */
const char *Cmd_Label(const Command *c)
{
    Prefs p;
    switch (c->id) {
    case cMarks:
        return Ed_ShowMarks() ? "Hide Markdown" : c->name;
    case cWords: case cClock:
        Prefs_Load(&p);
        if (c->id == cWords) return (p.hide & 1) ? c->name : "Hide Word Count";
        return (p.hide & 2) ? c->name : "Hide Session Clock";
    }
    return c->name;
}

Boolean Cmd_Available(short id)
{
    switch (id) {
    case cCut: case cCopy:
        return Ed_HasSelection();
    case cNextSection: case cPrevSection:
        return Ed_HasHeadings();
    }
    return true;
}

static void ToggleStatus(short bit)
{
    Prefs p;
    Prefs_Load(&p);
    p.hide ^= bit;
    App_SetStatusParts(!(p.hide & 1), !(p.hide & 2));
}

void Cmd_Run(short id)
{
    switch (id) {
    case cBold:        Ed_Format('*', 2); break;
    case cItalic:      Ed_Format('*', 1); break;
    case cStrike:      Ed_Format('~', 2); break;
    case cCode:        Ed_Format('`', 1); break;
    case cNormal:      Ed_BlockFormat(0); break;
    case cH1: case cH2: case cH3: case cH4: case cH5: case cH6:
                       Ed_BlockFormat((short)(id - cH1 + 1)); break;
    case cBullets:     Ed_BlockFormat('b'); break;
    case cNumbers:     Ed_BlockFormat('n'); break;
    case cTasks:       Ed_BlockFormat('t'); break;
    case cQuote:       Ed_BlockFormat('q'); break;
    case cUndo:        Ed_Undo(); break;
    case cRedo:        Ed_Redo(); break;
    case cCut:         Ed_Cut(); break;
    case cCopy:        Ed_Copy(); break;
    case cPaste:       Ed_Paste(); break;
    case cSelectAll:   Ed_SelectAll(); break;
    case cMarks:       Doc_ToggleMarks(); break;
    case cNextSection: Ed_JumpSection(1); break;
    case cPrevSection: Ed_JumpSection(-1); break;
    case cSections:    Drawer_OpenBlade('s'); break;
    case cLibrary:     Lib_Open(); break;
    case cNew:         Lib_OpenNew(); break;
    case cSave:        Doc_SaveCommand(); break;
    case cBigger:      App_StepSize(1); break;
    case cSmaller:     App_StepSize(-1); break;
    case cNextFont:    App_CycleFont(); break;
    case cWords:       ToggleStatus(1); break;
    case cClock:       ToggleStatus(2); break;
    case cDrawer:      Drawer_Open(); break;
    case cSettings:    Drawer_OpenBlade('t'); break;
    case cTiming:      App_ShowLaunchTiming(); break;
    case cQuit:        App_Quit(); break;
    }
}
