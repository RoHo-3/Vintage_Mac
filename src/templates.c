/*
 * The default templates (docs/ROADMAP.md, 0.05). Written into the
 * Templates folder when it holds none, so a writer who deletes one keeps
 * it deleted, and an emptied folder gets the set back.
 *
 * Every template opens with a level-1 heading that reads well as a name
 * if kept ("Untitled Story"), then short labelled prompts, then a rule and
 * one "Start here." line. Script is Fountain saved as Markdown: its prompts
 * are Fountain notes and a synopsis, which never print. Book is a folder:
 * copied whole into My Writings when picked, its title page named for it.
 *
 * Plain ASCII and LF line endings, like the files the app writes.
 */

#include <Files.h>
#include "templates.h"
#include "files.h"

#define kCreator 'MMDW'
#define kType    'TEXT'

typedef struct {
    short folder;            /* 0 Templates, 1 Book, 2 its Chapters, 3 its Notes */
    const char *name;        /* C string */
    const char *text;
} Tpl;

static const Tpl kTpls[] = {
    { 0, "Article.md",
      "# Headline\n"
      "\n"
      "- **Lede:** who, what, when, where, in a sentence or two\n"
      "- **Why it matters:** \n"
      "- **Then:** the details, most important first\n"
      "\n"
      "[How To Write Great Articles](https://owl.purdue.edu/owl/subject_specific_writing/journalism_and_journalistic_writing/the_inverted_pyramid.html)\n"
      "\n"
      "---\n"
      "\n"
      "Start here.\n" },
    { 0, "Essay.md",
      "# Untitled Essay\n"
      "\n"
      "- **By the end, the reader believes:** \n"
      "- **First point:** \n"
      "- **Second point:** \n"
      "- **The other side:** the best case against me, put fairly\n"
      "- **Why it matters:** \n"
      "\n"
      "[How To Write Great Essays](https://owl.purdue.edu/owl/general_writing/academic_writing/essay_writing/index.html)\n"
      "\n"
      "---\n"
      "\n"
      "Start here.\n" },
    { 0, "Journal.md",
      "# Untitled Entry\n"
      "\n"
      "- **On my mind:** \n"
      "- **One thing I noticed:** \n"
      "\n"
      "[How To Write Great Journals](https://en.wikipedia.org/wiki/Diary)\n"
      "\n"
      "---\n"
      "\n"
      "Start here. Write without stopping; fix nothing.\n" },
    { 0, "Script.md",
      "# Untitled Script\n"
      "\n"
      "= Logline: who wants what, and what stands in the way.\n"
      "\n"
      "[[Scenes start with INT. or EXT., a place, then DAY or NIGHT.]]\n"
      "[[A name in capitals, then the dialogue on the line below.]]\n"
      "[[How To Write Great Scripts: https://fountain.io/syntax/]]\n"
      "\n"
      "INT. PLACE - DAY\n"
      "\n"
      "Start here.\n" },
    { 0, "Short Story.md",
      "# Untitled Story\n"
      "\n"
      "- **Who:** \n"
      "- **Wants:** \n"
      "- **Against:** \n"
      "- **Turn:** the moment it changes\n"
      "- **End:** what is different now\n"
      "\n"
      "[How To Write Great Short Stories](https://en.wikipedia.org/wiki/Short_story)\n"
      "\n"
      "---\n"
      "\n"
      "Start here.\n" },
    { 1, "Book.md",          /* its title page: named like the folder, so the */
      "# Untitled Book\n"     /* writing's name follows its first line */
      "\n"
      "- **In one line:** \n"
      "- **For readers who:** \n"
      "\n"
      "[How To Write Great Books](https://en.wikipedia.org/wiki/Writing_process)\n"
      "\n"
      "---\n"
      "\n"
      "Plan it in the [Outline](Outline.md), then write\n"
      "[Chapter 1](Chapters/Chapter%201.md). Keep notes on\n"
      "[People & Characters](Notes/People%20&%20Characters.md),\n"
      "[Places](Notes/Places.md) and [Research](Notes/Research.md).\n"
      "Cmd-click a link to go there.\n" },
    { 1, "Outline.md",
      "# Outline\n"
      "\n"
      "[How To Write Great Outlines](https://en.wikipedia.org/wiki/Outline_%28list%29)\n"
      "\n"
      "- **In one line:** \n"
      "\n"
      "## Beginning\n"
      "\n"
      "- \n"
      "\n"
      "## Middle\n"
      "\n"
      "- \n"
      "\n"
      "## End\n"
      "\n"
      "- \n" },
    { 2, "Chapter 1.md",
      "# Chapter 1\n"
      "\n"
      "[How To Write Great Chapters](https://en.wikipedia.org/wiki/Chapter_%28books%29)\n"
      "\n"
      "---\n"
      "\n"
      "Start here.\n" },
    { 3, "People & Characters.md",
      "# People & Characters\n"
      "\n"
      "[How To Write Great Characters](https://en.wikipedia.org/wiki/Characterization)\n"
      "\n"
      "## Name\n"
      "\n"
      "- **Role:** \n"
      "- **Wants:** \n"
      "- **Fears:** \n"
      "- **Secret:** \n"
      "- **Sounds like:** \n" },
    { 3, "Places.md",
      "# Places\n"
      "\n"
      "[How To Write Great Places](https://en.wikipedia.org/wiki/Setting_%28narrative%29)\n"
      "\n"
      "## Name\n"
      "\n"
      "- **Where:** \n"
      "- **Feels like:** \n"
      "- **History:** \n" },
    { 3, "Research.md",
      "# Research\n"
      "\n"
      "[How To Write Great Research](https://owl.purdue.edu/owl/research_and_citation/conducting_research/research_overview/index.html)\n"
      "\n"
      "## Source name\n"
      "\n"
      "**Source:** title, author, page\n"
      "\n"
      "> A line worth keeping, copied exactly.\n"
      "\n"
      "What it means to me, in my own words.\n" }
};
#define kTplCount (short)(sizeof(kTpls) / sizeof(kTpls[0]))

static void PStr(const char *c, unsigned char *p)
{
    short n = 0;
    while (c[n] && n < 63) { p[n + 1] = (unsigned char)c[n]; n++; }
    p[0] = (unsigned char)n;
}

static long CLen(const char *c)
{
    long n = 0;
    while (c[n]) n++;
    return n;
}

static Boolean MakeFolder(short vol, long parent, const char *name, long *dir)
{
    HParamBlockRec pb;
    unsigned char p[64];
    PStr(name, p);
    pb.fileParam.ioCompletion = NULL;
    pb.fileParam.ioNamePtr = p;
    pb.fileParam.ioVRefNum = vol;
    pb.fileParam.ioDirID = parent;
    if (PBDirCreateSync(&pb) != noErr) return false;
    *dir = pb.fileParam.ioDirID;
    return true;
}

static Boolean WriteText(short vol, long dir, const char *name, const char *text)
{
    unsigned char p[64];
    short ref;
    long n = CLen(text);
    OSErr err;
    PStr(name, p);
    if (HCreate(vol, dir, p, kCreator, kType) != noErr) return false;
    if (HOpen(vol, dir, p, fsWrPerm, &ref) != noErr) return false;
    err = FSWrite(ref, &n, (Ptr)text);
    FSClose(ref);
    return err == noErr;
}

/* What's in the folder: how many visible items, and whether the only one
   is an empty Blank.md (the one template before 0.05). */
static short Contents(short vol, long dir, Boolean *onlyEmptyBlank)
{
    CInfoPBRec ci;
    Str63 name;
    short idx, n = 0;
    *onlyEmptyBlank = false;
    for (idx = 1; ; idx++) {
        ci.hFileInfo.ioCompletion = NULL;
        ci.hFileInfo.ioNamePtr = name;
        ci.hFileInfo.ioVRefNum = vol;
        ci.hFileInfo.ioFDirIndex = idx;
        ci.hFileInfo.ioDirID = dir;
        if (PBGetCatInfoSync(&ci) != noErr) break;
        if (ci.hFileInfo.ioFlFndrInfo.fdFlags & 0x4000) continue;   /* invisible */
        n++;
        *onlyEmptyBlank = n == 1 && !(ci.hFileInfo.ioFlAttrib & 0x10)
            && ci.hFileInfo.ioFlLgLen == 0 && ci.hFileInfo.ioFlRLgLen == 0
            && name[0] == 8 && name[1] == 'B' && name[2] == 'l' && name[3] == 'a'
            && name[4] == 'n' && name[5] == 'k' && name[6] == '.' && name[7] == 'm'
            && name[8] == 'd';
    }
    return n;
}

static Boolean Exists(short vol, long dir, const char *name)
{
    unsigned char p[64];
    PStr(name, p);
    return Files_Exists(vol, dir, p);
}

/* The first 0.05 set (Book Project, no links) is replaced once: its six
   go to the Trash (recoverable), the new set comes in. Only when the old
   Book Project is there and Book isn't: a writer's own set is never
   touched. */
static Boolean ReplaceFirstSet(short vol, long dir)
{
    static const char *kOld[] = { "Article.md", "Book Project", "Essay.md",
                                  "Journal.md", "Script.md", "Short Story.md" };
    unsigned char p[64];
    short i;
    if (!Exists(vol, dir, "Book Project") || Exists(vol, dir, "Book"))
        return false;
    for (i = 0; i < 6; i++) {
        PStr(kOld[i], p);
        Files_Trash(vol, dir, p);
    }
    return true;
}

/* Build 114's Book title page said where things were; now it links to
   them. Rewritten once, only while it is still exactly that text. */
static const char kOldBookPage[] =
    "# Untitled Book\n\n- **In one line:** \n- **For readers who:** \n\n"
    "[How To Write Great Books](https://en.wikipedia.org/wiki/Writing_process)\n"
    "\n---\n\nChapters are in Chapters; people, places and research in Notes.\n";

static void RefreshBookPage(short vol, long dir)
{
    long book, n, len = sizeof kOldBookPage - 1, i;
    unsigned char p[64];
    char buf[sizeof kOldBookPage];
    short ref;
    PStr("Book", p);
    if (!Files_FolderNamed(vol, dir, p, &book)) return;
    PStr("Book.md", p);
    if (HOpen(vol, book, p, fsRdWrPerm, &ref) != noErr) return;
    n = len;
    if (GetEOF(ref, &i) == noErr && i == len
        && FSRead(ref, &n, buf) == noErr && n == len) {
        for (i = 0; i < len && buf[i] == kOldBookPage[i]; i++) ;
        if (i == len) {
            for (i = 0; i < kTplCount; i++)
                if (kTpls[i].folder == 1 && kTpls[i].name[0] == 'B') break;
            n = 0;
            while (kTpls[i].text[n]) n++;
            if (SetFPos(ref, fsFromStart, 0) == noErr && SetEOF(ref, n) == noErr)
                FSWrite(ref, &n, (Ptr)kTpls[i].text);
        }
    }
    FSClose(ref);
}

void Templates_Install(short vol, long dir)
{
    long folder[4];
    Boolean blank;
    short n = Contents(vol, dir, &blank), i;
    if (n > 1 || (n == 1 && !blank)) {
        if (!ReplaceFirstSet(vol, dir)) {
            RefreshBookPage(vol, dir);   /* the writer's own set: leave it */
            return;
        }
        blank = false;
    }
    if (blank) {                         /* Blank left the shelf in 0.05: */
        unsigned char b[64];             /* New makes a blank writing */
        PStr("Blank.md", b);
        HDelete(vol, dir, b);
    }
    folder[0] = dir;
    folder[1] = folder[2] = folder[3] = 0;
    if (MakeFolder(vol, dir, "Book", &folder[1])) {
        MakeFolder(vol, folder[1], "Chapters", &folder[2]);
        MakeFolder(vol, folder[1], "Notes", &folder[3]);
    }
    for (i = 0; i < kTplCount; i++)
        if (folder[kTpls[i].folder])
            WriteText(vol, folder[kTpls[i].folder], kTpls[i].name, kTpls[i].text);
}
