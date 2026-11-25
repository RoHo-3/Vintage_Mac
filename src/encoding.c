#include "encoding.h"
#include "textbuf.h"
#include "macroman.inc"

#define kMaxEscLen 8                     /* 0x1B + up to 6 hex + 0x1C */

static const char kHex[] = "0123456789ABCDEF";

static short MacRomanFor(unsigned long u)
{
    short i;
    if (u < 0x80)
        return (short)u;
    for (i = 0; i < 128; i++)
        if (kMacRoman[i] == u)
            return (short)(0x80 + i);
    return -1;
}

static long PutEscape(char *dst, long n, long cap, unsigned long u)
{
    char digits[8];
    short k = 0;
    if (n + kMaxEscLen > cap)
        return -1;
    do {
        digits[k++] = kHex[u & 0xF];
        u >>= 4;
    } while (u);
    dst[n++] = kEscStart;
    while (k > 0)
        dst[n++] = digits[--k];
    dst[n++] = kEscEnd;
    return n;
}

long Enc_Decode(const unsigned char *src, long n, char *dst, long cap,
                FileFormat *fmt)
{
    long i = 0, o = 0, k, need;
    unsigned long u;
    unsigned char b;
    short m;
    Boolean sawEnding = false;

    fmt->lineEnding = kEndLF;
    fmt->bom = false;
    if (n >= 3 && src[0] == 0xEF && src[1] == 0xBB && src[2] == 0xBF) {
        fmt->bom = true;
        i = 3;
    }
    while (i < n) {
        b = src[i];
        if (b == '\r' || b == '\n') {    /* any line ending → '\r' */
            short kind = (b == '\n') ? kEndLF
                       : (i + 1 < n && src[i + 1] == '\n') ? kEndCRLF : kEndCR;
            if (!sawEnding) {
                fmt->lineEnding = kind;
                sawEnding = true;
            }
            i += (kind == kEndCRLF) ? 2 : 1;
            if (o >= cap) return -1;
            dst[o++] = '\r';
            continue;
        }
        if (b < 0x80) {
            if (b < 0x20 && b != '\t') { /* keep stray control chars */
                o = PutEscape(dst, o, cap, b);
                if (o < 0) return -1;
            } else {
                if (o >= cap) return -1;
                dst[o++] = (char)b;
            }
            i++;
            continue;
        }
        /* multi-byte UTF-8 */
        need = (b >= 0xF0 && b < 0xF8) ? 3 : (b >= 0xE0) ? 2 : (b >= 0xC2) ? 1 : -1;
        u = (need == 3) ? (b & 0x07) : (need == 2) ? (b & 0x0F) : (b & 0x1F);
        for (k = 1; need > 0 && k <= need; k++) {
            if (i + k >= n || (src[i + k] & 0xC0) != 0x80) {
                need = -1;
                break;
            }
            u = (u << 6) | (src[i + k] & 0x3F);
        }
        if (need > 0 && ((need == 1 && u < 0x80) || (need == 2 && u < 0x800)
                         || (need == 3 && u < 0x10000)
                         || (u >= 0xD800 && u <= 0xDFFF)))
            need = -1;                   /* overlong or surrogate: invalid */
        if (need < 0) {                  /* invalid byte: keep it raw */
            o = PutEscape(dst, o, cap, 0xDC00UL + b);   /* b >= 0x80 */
            if (o < 0) return -1;
            i++;
            continue;
        }
        i += need + 1;
        m = MacRomanFor(u);
        if (m >= 0x80) {
            if (o >= cap) return -1;
            dst[o++] = (char)m;
        } else {
            o = PutEscape(dst, o, cap, u);
            if (o < 0) return -1;
        }
    }
    return o;
}

static long PutUTF8(char *dst, long n, long cap, unsigned long u)
{
    if (u >= 0xDC80 && u <= 0xDCFF) {    /* a raw byte we kept */
        if (n + 1 > cap) return -1;
        dst[n++] = (char)(u - 0xDC00);
    } else if (u < 0x80) {
        if (n + 1 > cap) return -1;
        dst[n++] = (char)u;
    } else if (u < 0x800) {
        if (n + 2 > cap) return -1;
        dst[n++] = (char)(0xC0 | (u >> 6));
        dst[n++] = (char)(0x80 | (u & 0x3F));
    } else if (u < 0x10000) {
        if (n + 3 > cap) return -1;
        dst[n++] = (char)(0xE0 | (u >> 12));
        dst[n++] = (char)(0x80 | ((u >> 6) & 0x3F));
        dst[n++] = (char)(0x80 | (u & 0x3F));
    } else {
        if (n + 4 > cap) return -1;
        dst[n++] = (char)(0xF0 | (u >> 18));
        dst[n++] = (char)(0x80 | ((u >> 12) & 0x3F));
        dst[n++] = (char)(0x80 | ((u >> 6) & 0x3F));
        dst[n++] = (char)(0x80 | (u & 0x3F));
    }
    return n;
}

long Enc_MaxEncoded(void)
{
    return TB_Len() * 3 + 4;
}

long Enc_Encode(char *dst, long cap, const FileFormat *fmt)
{
    long len = TB_Len(), p, o = 0;
    unsigned char c;
    unsigned long u;

    if (fmt->bom) {
        if (cap < 3) return -1;
        dst[o++] = (char)0xEF;
        dst[o++] = (char)0xBB;
        dst[o++] = (char)0xBF;
    }
    for (p = 0; p < len; p++) {
        c = TB_Char(p);
        if (c == '\r') {
            if (o + 2 > cap) return -1;
            if (fmt->lineEnding == kEndCR || fmt->lineEnding == kEndCRLF)
                dst[o++] = '\r';
            if (fmt->lineEnding == kEndLF || fmt->lineEnding == kEndCRLF)
                dst[o++] = '\n';
            continue;
        }
        if (c == kEscStart) {            /* an escape → its code point */
            u = 0;
            while (p + 1 < len && TB_Char(p + 1) != kEscEnd) {
                char h = (char)TB_Char(++p);
                u = (u << 4) | (unsigned long)((h >= 'A') ? h - 'A' + 10 : h - '0');
            }
            p++;                         /* the kEscEnd */
            o = PutUTF8(dst, o, cap, u);
        } else if (c >= 0x80)
            o = PutUTF8(dst, o, cap, kMacRoman[c - 0x80]);
        else
            o = PutUTF8(dst, o, cap, c);
        if (o < 0) return -1;
    }
    return o;
}

long Enc_EscapeStart(long pos)
{
    long p, limit = pos - kMaxEscLen;
    if (limit < 0) limit = 0;
    for (p = pos - 1; p >= limit; p--) {
        unsigned char c = TB_Char(p);
        if (c == kEscEnd) return pos;    /* pos is after a whole escape */
        if (c == kEscStart) return p;
    }
    return pos;
}

long Enc_EscapeEnd(long pos)
{
    long p, len = TB_Len(), start = Enc_EscapeStart(pos);
    if (start == pos)
        return pos;                      /* not inside an escape */
    for (p = start + 1; p < len && p < start + kMaxEscLen; p++)
        if (TB_Char(p) == kEscEnd)
            return p + 1;
    return pos;
}
