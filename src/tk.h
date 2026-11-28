#ifndef TK_H
#define TK_H
#ifndef TK_HOST_TEST
#include <Types.h>
#endif

Boolean TK_In(const unsigned char *b, long n, unsigned char before, Boolean last);
#ifndef TK_HOST_TEST
Boolean TK_InFile(short vol, long dir, ConstStr255Param name);   /* a document on disk */
#endif

#endif
