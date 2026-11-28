#include "commands.h"
#include "editor.h"
#include "document.h"
#include "library.h"
#include "drawer.h"
#include "files.h"
#include "app.h"
#include "chrome.h"
#include "links.h"
#include <Dialogs.h>

/* Search words are what a writer might type instead of the name:
   "cancel", "turn off", a plain-language word for a Markdown term. */
static const Command kCommands[] = {
    { cHistory,     "Navigate", "History\311",          "versions past earlier undo restore recover backup old revert", "" },
    { cLibrary,     "Navigate", "Library\311",          "open files browse documents projects folder find file", "Cmd-O" },
    { cJumpFootnote,"Navigate", "Jump to Footnote",     "note mark back reference endnote go",        "" },
    { cFollowLink,  "Navigate", "Follow Link",          "open link go url page document cmd-click",   "" },
    { cNextTK,      "Navigate", "Next TK",              "placeholder to come todo fill in later find mark xxx", "" },
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
    { cFootnote,    "Write",    "Insert Footnote",      "note reference citation source endnote",     "" },
    { cInsertTK,    "Write",    "Insert TK",            "placeholder to come mark later fill in xxx",  "" },
    { cUndo,        "Write",    "Undo",                 "revert back mistake",                         "Cmd-Z" },
    { cRedo,        "Write",    "Redo",                 "again forward",                               "Cmd-Shift-Z" },
    { cCuts,        "Write",    "Move to Cuts",         "darlings keep cut remove save for later kill your darlings trim", "" },
    { cCut,         "Write",    "Cut",                  "clipboard remove",                            "Cmd-X" },
    { cCopy,        "Write",    "Copy",                 "clipboard duplicate",                         "Cmd-C" },
    { cPaste,       "Write",    "Paste",                "clipboard insert",                            "Cmd-V" },
    { cSelectAll,   "Write",    "Select All",           "everything whole document",                   "Cmd-A" },
    { cNew,         "Document", "New Document\311",     "create blank start template",                 "Cmd-N" },
    { cTplArticle,  "Document", "New Article",          "template news report piece headline lede",   "" },
    { cTplBook,     "Document", "New Book",             "template novel book chapters project memoir", "" },
    { cTplEssay,    "Document", "New Essay",            "template argument opinion blog post",        "" },
    { cTplJournal,  "Document", "New Journal",          "template diary entry daily pages",           "" },
    { cTplScript,   "Document", "New Script",           "template screenplay fountain film play",     "" },
    { cTplStory,    "Document", "New Short Story",      "template fiction tale story",                "" },
    { cSave,        "Document", "Save",                 "store write keep",                            "Cmd-S" },
    { cMarks,       "View",     "Show Markdown",        "markers syntax source reveal hide markdown symbols", "Cmd-M" },
    { cBigger,      "View",     "Bigger Text",          "zoom in larger increase font size",           "Cmd-=" },
    { cSmaller,     "View",     "Smaller Text",         "zoom out decrease font size",                 "Cmd--" },
    { cNextFont,    "View",     "Next Font",            "typeface change font",                        "Cmd-Opt-F" },
    { cWords,       "View",     "Show Word Count",      "words count statistics hide turn off",        "" },
    { cClock,       "View",     "Show Session Clock",   "timer time elapsed hide turn off",            "" },
    { cDrawer,      "View",     "Drawer",               "menu sidebar hamburger panel",                "Cmd-\\" },
    { cSettings,    "View",     "Settings\311",         "preferences options prefs font size",         "" },
    { cFocusMinutes,"View",     "Focus for Minutes\311", "timer goal pomodoro session time write sprint", "" },
    { cFocusWords,  "View",     "Focus for Words\311",   "goal target count write sprint new words",    "" },
    { cFocusClear,  "View",     "Clear Focus Goal",     "stop cancel timer goal turn off",             "" },
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

/* The default templates' names, as the Library shows them. */
static const unsigned char *TemplateName(short id)
{
    switch (id) {
    case cTplArticle: return (const unsigned char *)"\pArticle";
    case cTplBook:    return (const unsigned char *)"\pBook";
    case cTplEssay:   return (const unsigned char *)"\pEssay";
    case cTplJournal: return (const unsigned char *)"\pJournal";
    case cTplScript:  return (const unsigned char *)"\pScript";
    }
    return (const unsigned char *)"\pShort Story";
}

Boolean Cmd_Available(short id)
{
    switch (id) {
    case cCut: case cCopy:
        return Ed_HasSelection();
    case cCuts:
        return Ed_HasSelection() && Doc_HasFile();
    case cNextSection: case cPrevSection:
        return Ed_HasHeadings();
    case cFocusClear:
        return Chrome_GoalKind() != 0;
    case cHistory:
        return Doc_HasFile();
    case cTplArticle: case cTplBook: case cTplEssay:
    case cTplJournal: case cTplScript: case cTplStory:
        return Lib_HasTemplate(TemplateName(id));
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

static void AppendNumL(unsigned char *s, long v)
{
    char d[12];
    short k = 0;
    do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v > 0);
    while (k > 0) s[++s[0]] = (unsigned char)d[--k];
}

static short AppendNum(unsigned char *s, short n, short v)
{
    char d[6];
    short k = 0;
    do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v > 0);
    while (k > 0) s[++n] = (unsigned char)d[--k];
    return n;
}

/* "TK 2 of 5" in the strip, so the writer knows how many are left. */
static void NextTK(void)
{
    unsigned char msg[40];
    short which = 0, total = Ed_NextTK(&which), n = 0;
    if (total == 0) {
        Chrome_ShowMessage((const unsigned char *)"\pNo TK marks", 2);
        return;
    }
    msg[++n] = 'T'; msg[++n] = 'K'; msg[++n] = ' ';
    n = AppendNum(msg, n, which);
    msg[++n] = ' '; msg[++n] = 'o'; msg[++n] = 'f'; msg[++n] = ' ';
    n = AppendNum(msg, n, total);
    msg[0] = (unsigned char)n;
    Chrome_ShowMessage(msg, 2);
}

/* "Focus for how many minutes?" A small classic dialog; the last goal of
   that kind is offered again (25 minutes, 500 words to start). */
static void AskGoal(char kind)
{
    Prefs pr;
    long lastMin, lastWords;
    unsigned char num[16];
    DialogPtr d;
    short hit, type, i;
    long v = 0;
    Handle h;
    Rect box;
    ParamText(kind == 'm' ? (const unsigned char *)"\pFocus for how many minutes?"
                          : (const unsigned char *)"\pFocus for how many new words?",
              (const unsigned char *)"\p", (const unsigned char *)"\p", (const unsigned char *)"\p");
    Prefs_Load(&pr);                     /* the last Other... kept, across launches */
    lastMin = pr.goalMin > 0 ? pr.goalMin : 25;
    lastWords = pr.goalWords > 0 ? pr.goalWords : 500;
    d = GetNewDialog(201, NULL, (WindowPtr)-1);
    if (!d) return;
    num[0] = 0;
    AppendNumL(num, kind == 'm' ? lastMin : lastWords);
    GetDialogItem(d, 4, &type, &h, &box);
    SetDialogItemText(h, num);
    SelectDialogItemText(d, 4, 0, 32767);         /* typing replaces it */
    ShowWindow(d);
    do {
        ModalDialog(NULL, &hit);
    } while (hit != 1 && hit != 2);
    if (hit == 1) {
        GetDialogItem(d, 4, &type, &h, &box);
        GetDialogItemText(h, num);
        for (i = 1; i <= num[0]; i++)
            if (num[i] >= '0' && num[i] <= '9' && v < 100000L)
                v = v * 10 + (num[i] - '0');
    }
    DisposeDialog(d);
    if (hit != 1 || v <= 0) return;
    if (kind == 'm') { if (v > 600) v = 600; pr.goalMin = v; }
    else pr.goalWords = v;
    Prefs_Save(&pr);
    Chrome_SetGoal(kind, v);
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
    case cHistory:     Drawer_OpenBlade('h'); break;
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
    case cNextTK:      NextTK(); break;
    case cTplArticle: case cTplBook: case cTplEssay:
    case cTplJournal: case cTplScript: case cTplStory:
        if (!Lib_NewFromTemplateNamed(TemplateName(id))) SysBeep(1);
        break;
    case cCuts:
        switch (Doc_MoveToCuts()) {
        case 0: Chrome_ShowMessage((const unsigned char *)"\pMoved to Cuts", 2); break;
        case 2: Chrome_ShowMessage((const unsigned char *)"\pThis is Cuts: use Cut instead", 3); break;
        case 3: SysBeep(1); break;
        }
        break;
    case cFootnote:
        if (!Ed_InsertFootnote())
            Chrome_ShowMessage((const unsigned char *)"\pFootnote marks go in the text, not in a note", 3);
        break;
    case cJumpFootnote:
        if (!Ed_JumpFootnote())
            Chrome_ShowMessage((const unsigned char *)"\pPut the caret on a footnote mark or note", 3);
        break;
    case cFollowLink: {
        static char url[256];
        long a, b;
        Ed_SelRange(&a, &b);
        if (Link_At(a, url, sizeof url)) Link_Follow(url);
        else Chrome_ShowMessage((const unsigned char *)"\pPut the caret in a link", 3);
        break;
    }
    case cFocusMinutes: AskGoal('m'); break;
    case cFocusWords:  AskGoal('w'); break;
    case cFocusClear:  Chrome_SetGoal(0, 0); break;
    case cInsertTK:    Ed_InsertTK(); break;
    case cQuit:        App_Quit(); break;
    }
}
