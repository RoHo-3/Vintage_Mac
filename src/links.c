/*
 * Following links. Cmd-click on a [text](url) link, or Follow Link in
 * Cmd-K with the cursor in one, opens a link to another document: its url
 * is a path from this document's folder, as Markdown writes it anywhere
 * ("Outline.md", "Notes/Places.md", "../Book.md", spaces as %20). Web
 * links can't open on this Mac; the strip says so.
 */
#include "links.h"

#ifndef LINKS_HOST_TEST
#include "textbuf.h"
#include "document.h"
#include "files.h"
#include "chrome.h"
#include <Files.h>
#endif

/* The url of the link around pos, if any, into url (C string); its length,
   or 0. Only within pos's own paragraph; [^footnotes] aren't links. */
short Link_At(long pos, char *url, short cap)
{
    long len = TB_Len(), a = pos, b = pos, i, j, k;
    short n;
    while (a > 0 && TB_Char(a - 1) != '\r') a--;
    while (b < len && TB_Char(b) != '\r') b++;
    for (i = a; i < b; i++) {
        if (TB_Char(i) != '[' || (i + 1 < b && TB_Char(i + 1) == '^')) continue;
        for (j = i + 1; j < b && TB_Char(j) != ']' && TB_Char(j) != '['; j++) ;
        if (j + 1 >= b || TB_Char(j) != ']' || TB_Char(j + 1) != '(') continue;
        for (k = j + 2; k < b && TB_Char(k) != ')' && TB_Char(k) != ' '; k++) ;
        if (k >= b || TB_Char(k) != ')' || k == j + 2) continue;
        if (pos < i || pos > k) { i = k; continue; }
        n = 0;
        for (i = j + 2; i < k && n < cap - 1; i++) url[n++] = (char)TB_Char(i);
        url[n] = 0;
        return n;
    }
    return 0;
}

Boolean Link_IsWeb(const char *u)
{
    short i;
    for (i = 0; u[i] && u[i] != '/' && u[i] != '#'; i++)
        if (u[i] == ':') return true;    /* http:, https:, mailto:, ... */
    return false;
}

static short Hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* The next path part of u from *at into seg (Pascal string, decoded):
   false at the end. A part that can't be an HFS name (a colon, too long)
   comes back empty. */
Boolean Link_NextPart(const char *u, short *at, unsigned char *seg)
{
    short i = *at, n = 0;
    Boolean bad = false;
    if (!u[i] || u[i] == '#') return false;
    while (u[i] && u[i] != '/' && u[i] != '#') {
        char c = u[i++];
        if (c == '%' && Hex(u[i]) >= 0 && Hex(u[i + 1]) >= 0) {
            c = (char)(Hex(u[i]) * 16 + Hex(u[i + 1]));
            i += 2;
        }
        if (c == ':' || n >= 31) bad = true;
        else seg[++n] = (unsigned char)c;
    }
    if (u[i] == '/') i++;
    *at = i;
    seg[0] = bad ? 0 : (unsigned char)n;
    return true;
}

#ifndef LINKS_HOST_TEST
static Boolean ParentOf(short vol, long dir, long *parent)
{
    CInfoPBRec ci;
    Str63 nm;
    ci.dirInfo.ioCompletion = NULL;
    ci.dirInfo.ioNamePtr = nm;
    ci.dirInfo.ioVRefNum = vol;
    ci.dirInfo.ioFDirIndex = -1;         /* the folder itself */
    ci.dirInfo.ioDrDirID = dir;
    if (PBGetCatInfoSync(&ci) != noErr || dir == 2) return false;
    *parent = ci.dirInfo.ioDrParID;
    return true;
}

static void Say(const char *a, const unsigned char *name)
{
    unsigned char m[100];
    short i;
    m[0] = 0;
    for (i = 0; a[i] && m[0] < 60; i++) m[++m[0]] = (unsigned char)a[i];
    if (name)
        for (i = 1; i <= name[0] && m[0] < 98; i++) m[++m[0]] = name[i];
    Chrome_ShowMessage(m, 3);
}

Boolean Link_Follow(const char *url)
{
    short vol, wd, at = 0;
    long dir;
    unsigned char seg[32], last[32];
    Boolean have = false;
    if (Link_IsWeb(url)) {
        Say("Open web links on a modern computer", NULL);
        return false;
    }
    if (!Doc_Location(&vol, &dir)) { Say("Save first to follow links", NULL); return false; }
    last[0] = 0;
    while (Link_NextPart(url, &at, seg)) {
        if (have) {                      /* the part before was a folder */
            if (!Files_FolderNamed(vol, dir, last, &dir)) { Say("No folder named ", last); return false; }
            have = false;
        }
        if (seg[0] == 1 && seg[1] == '.') continue;
        if (seg[0] == 2 && seg[1] == '.' && seg[2] == '.') {
            if (!ParentOf(vol, dir, &dir)) { Say("Link not found", NULL); return false; }
            continue;
        }
        if (!seg[0]) { Say("Link not found", NULL); return false; }
        BlockMoveData(seg, last, seg[0] + 1);
        have = true;
    }
    if (!have) { Say("Link not found", NULL); return false; }   /* "#part" alone */
    if (!Files_Exists(vol, dir, last)) { Say("No document named ", last); return false; }
    if (!Files_WDFor(vol, dir, &wd)) return false;
    return Doc_OpenAt(last, wd);
}
#endif
