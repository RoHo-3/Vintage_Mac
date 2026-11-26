/*
 * App-level actions shared by the drawer and the keyboard (main.c).
 */
#ifndef APP_H
#define APP_H

void App_FontName(unsigned char *name);   /* the current font's name */
void App_CycleFont(void);                 /* next font this Mac has */
void App_StepSize(short dir);             /* body size up (1) / down (-1) */
void App_SetStatusParts(Boolean words, Boolean clock);   /* Settings, saved */
void App_Quit(void);

#endif
