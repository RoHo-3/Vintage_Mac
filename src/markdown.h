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
#define MD_STRIKE  0x40     /* ~~strikethrough~~ (GitHub Markdown) */
#define MD_LINK    0x80     /* [text](url): the text, underlined */

/* Block info for the paragraph last parsed (MD_Block):
   bits 0-2 kind, 3-4 quote depth (0-3), 5-6 list depth (0-3),
   bit 7 set when a ``` code block is still open after this line. */
#define MD_PLAIN     0
#define MD_BULLET    1      /* - * + */
#define MD_ORDERED   2      /* 1. 2) */
#define MD_TASK      3      /* - [ ] */
#define MD_TASKDONE  4      /* - [x] */
#define MD_RULE      5      /* --- *** ___ */
#define MD_FENCE     6      /* a syntax-only line: ``` fence, setext underline */
#define MD_CODELINE  7      /* inside a ``` block */
#define MD_KIND(b)       ((b) & 7)
#define MD_QUOTE(b)      (((b) >> 3) & 3)
#define MD_LISTDEPTH(b)  (((b) >> 5) & 3)
#define MD_FENCEOPEN(b)  (((b) & 0x80) != 0)

#define kQuoteStep   12     /* indent per quote level, pixels */
#define kListStep    18     /* indent per list level */
#define kCodeIndent  12     /* code block indent */

unsigned char MD_Block(void);
Boolean MD_MaybeUnderline(long pos);     /* line at pos starts with - or = */
short MD_Indent(unsigned char blk);      /* text indent for a block, pixels */

/* Whether a ``` block is open before position start (a paragraph start).
   Layout answers this; while it re-wraps it forces the answer instead
   (MD_ForceFence(0/1), MD_ForceFence(-1) to stop). */
typedef Boolean (*MD_FenceQuery)(long start);
void MD_SetFenceQuery(MD_FenceQuery q);
void MD_ForceFence(short inFence);

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
