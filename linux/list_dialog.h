/*
 * The Download Station List dialog (File menu): options, then progress,
 * then a summary with "Replace My Station List". The work is done by
 * list_download.c.
 */
#ifndef DXG_LIST_DIALOG_H
#define DXG_LIST_DIALOG_H

#include "ui.h"

extern ui_window *listdlg_window;

/* reload: called after the new list is put in place. status: a message for
 * the status bar. */
void listdlg_init(void (*reload)(void), void (*status)(const char *msg));
void listdlg_open(void);

#endif
