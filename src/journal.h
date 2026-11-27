/*
 * The append-only journal (docs/STORAGE.md): every change since the last
 * checkpoint is appended to "<name>.hist" beside the .md file in one
 * small write. Autosave writes only the journal; the .md is rewritten on
 * Cmd-S, quit, suspend and switching documents. On open, the journal is
 * replayed to recover work lost in a crash.
 *
 * Record: type (1) · payload length (4) · payload · checksum (2)
 *   'H' header      "MDWJ" + version (2)
 *   'S' snapshot    time (4) + full text
 *   'E' edit batch  time (4) + ops { pos (4), deleted (4), inserted (4), bytes }
 *   'C' checkpoint  time (4) + text length (4) + text hash (4) + caret (4)
 *                   + label length (1) + label
 *   'P' settings    flags (1): kPrefShowMarks, kPrefNameFollows
 *   'F' renamed     (none): the name no longer follows the first line
 * Text is the in-memory form (MacRoman + escapes, '\r' paragraphs).
 */
#ifndef JOURNAL_H
#define JOURNAL_H

#include <Memory.h>

enum { kRecoverNone, kRecoverJournalNewer, kRecoverEditedElsewhere };

/* Attach to a document's file (name + volume of its .md). */
void    JR_Attach(const unsigned char *mdName, short vRefNum);
void    JR_Detach(void);

/* Record a change (called for every replace, including undo/redo). */
void    JR_Op(long pos, long oldLen, const char *ins, long newLen);

/* Forget pending ops; the next checkpoint starts with a snapshot. */
void    JR_Reset(void);

/* Append pending ops + a checkpoint (label may be NULL). */
Boolean JR_Checkpoint(long caret, const char *label);

/* On open: replay the journal and compare with the file's text, which is
   in the text buffer. If the journal is newer it is left in the text
   buffer (returns kRecoverJournalNewer). */
short   JR_Recover(void);

/* Per-document settings, kept in the journal ('P' record: one flag byte).
   JR_Attach resets them; JR_Recover reads the latest. */
enum { kPrefShowMarks = 1,
       kPrefNameFollows = 2 };            /* app-named: name tracks line 1 */
void          JR_HistoryName(const unsigned char *md, unsigned char *out);
Boolean       JR_Rename(const unsigned char *newMd);
void          JR_MarkRenamed(const unsigned char *md, short vRef);
void          JR_SetPrefs(unsigned char prefs);
unsigned char JR_Prefs(void);

unsigned long JR_Hash(void);     /* hash of the whole text buffer */

/* History: past versions (drawer > History). end is where the version's
   checkpoint record ends in the journal; length is its text length. */
typedef struct {
    unsigned long time;               /* seconds since 1904 */
    long          length, end;
    unsigned char label[32];          /* "Imported", or empty */
} JRVersion;
short   JR_Versions(JRVersion *out, short max);     /* oldest first */
Boolean JR_BuildVersion(long end, Ptr *text, long *textLen);

#endif
