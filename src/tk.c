/*
 * TK, "to come": a placeholder for a fact or name to fill in later
 * (Cmd-K > Insert TK, Next TK). The drawer marks documents that still
 * hold one with a dot. The rule is the editor's: capital T, capital K,
 * no letter (Mac Roman accented ones included) on either side.
 */
#include "tk.h"

static Boolean Letter(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
        || c >= 0x80;
}

/* Does b[0..n) hold a TK? b may be a piece of a longer file: before
   says what came just before it (0: the start), and last says whether
   the file ends with it (if not, a TK touching the end isn't judged:
   the caller keeps the last 2 bytes and goes on). */
Boolean TK_In(const unsigned char *b, long n, unsigned char before, Boolean last)
{
    long p;
    for (p = 0; p + 1 < n; p++) {
        unsigned char prev = p ? b[p - 1] : before;
        if (b[p] != 'T' || b[p + 1] != 'K' || Letter(prev)) continue;
        if (p + 2 < n) { if (!Letter(b[p + 2])) return true; }
        else if (last) return true;      /* TK at the very end */
    }
    return false;
}

#ifndef TK_HOST_TEST
#include <Files.h>

/* Read the file in small pieces (never the whole file: it may be large
   and the Mac small), 2 bytes of overlap between them. */
Boolean TK_InFile(short vol, long dir, ConstStr255Param name)
{
    unsigned char buf[2048 + 2];
    short ref;
    long n, keep = 0;
    unsigned char before = 0;
    Boolean found = false, eof = false;
    if (HOpen(vol, dir, name, fsRdPerm, &ref) != noErr) return false;
    while (!found && !eof) {
        OSErr err;
        n = 2048;
        err = FSRead(ref, &n, (Ptr)(buf + keep));
        eof = err != noErr || n < 2048;
        n += keep;
        found = TK_In(buf, n, before, eof);
        if (!eof && n >= 2) {            /* the last 2 bytes go round again */
            before = n >= 3 ? buf[n - 3] : before;
            buf[0] = buf[n - 2];
            buf[1] = buf[n - 1];
            keep = 2;
        }
    }
    FSClose(ref);
    return found;
}
#endif
