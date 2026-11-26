#include "overlay.h"
#include "editor.h"
#include "chrome.h"

#include <Memory.h>

static BitMap    gBits;
static Rect      gRect;
static WindowPtr gWin;
static Boolean   gHave, gLost;

Boolean Under_Save(WindowPtr w, const Rect *r)
{
    short rowBytes = (short)(((r->right - r->left + 15) / 16) * 2);
    long  need = (long)rowBytes * (r->bottom - r->top);
    Under_Forget();
    gWin = w;
    gRect = *r;
    gBits.baseAddr = NewPtr(need);
    if (!gBits.baseAddr) {
        gLost = true;                    /* restore by redrawing instead */
        return false;
    }
    gBits.rowBytes = rowBytes;
    SetRect(&gBits.bounds, 0, 0, r->right - r->left, r->bottom - r->top);
    SetPort(w);
    CopyBits(&w->portBits, &gBits, r, &gBits.bounds, srcCopy, NULL);
    gHave = true;
    return true;
}

void Under_Restore(void)
{
    if (gHave) {
        SetPort(gWin);
        CopyBits(&gBits, &gWin->portBits, &gBits.bounds, &gRect, srcCopy, NULL);
    } else if (gLost) {
        Ed_DrawAll();
        Chrome_Draw();
    }
    Under_Forget();
}

void Under_Forget(void)
{
    if (gBits.baseAddr) DisposePtr(gBits.baseAddr);
    gBits.baseAddr = 0;
    gHave = gLost = false;
}
