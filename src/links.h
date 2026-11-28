#ifndef LINKS_H
#define LINKS_H
#ifndef LINKS_HOST_TEST
#include <Types.h>
#endif

short   Link_At(long pos, char *url, short cap);   /* url of the link at pos, or 0 */
Boolean Link_IsWeb(const char *url);
Boolean Link_NextPart(const char *url, short *at, unsigned char *seg);
Boolean Link_Follow(const char *url);              /* open the linked document */

#endif
