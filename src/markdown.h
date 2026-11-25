/*
 * Live Markdown: classifies each character of a paragraph so layout and
 * drawing can style it and hide syntax. Supports ATX headings (# … ######),
 * **bold** / __bold__, *italic* / _italic_ and `code`.
 *
 * Markers are visible in the "active" paragraph (the one holding the
 * cursor) and hidden everywhere else. A heading's "## " prefix is never
 * laid out inline: when shown it hangs in the left margin, so the text
 * never shifts as the cursor arrives.
 */
#ifndef MARKDOWN_H
#define MARKDOWN_H

#include <Memory.h>

#define MD_HIDDEN  0x01     /* takes no space and isn't drawn */
#define MD_BOLD    0x02
#define MD_ITALIC  0x04
#define MD_CODE    0x08
#define MD_PREFIX  0x10     /* heading prefix: hangs in the margin */
#define MD_MARK    0x20     /* a syntax marker (shown only when active) */

/* Styles for the paragraph [start, end) (end = its '\r' or text end).
   *level is 0 for body text, 1-6 for headings. The returned array is
   valid until the next call. Returns NULL if out of memory. */
/* Where the writer is typing. In that paragraph an unclosed **, *, _ or `
   before the caret styles the rest of the paragraph right away (its marker
   hides like a closed one); elsewhere an unclosed marker is plain text. */
void MD_SetCaret(long pos);

const unsigned char *MD_Parse(long start, long end, Boolean active,
                              short *level);

/* Forget the cached parse (call after every text change). */
void MD_Invalidate(void);

/* Start of the paragraph containing pos, and its end. */
long MD_ParaStart(long pos);
long MD_ParaEnd(long pos);

#endif
