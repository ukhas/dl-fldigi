// ----------------------------------------------------------------------------
//
//	dl_fldigi.h
//
// ----------------------------------------------------------------------------

#ifndef DL_FLDIGI_H
#define DL_FLDIGI_H

#include <string>
#include <unistd.h>
#include <sys/types.h>

#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>

#include <FL/Fl_Choice.H>
#include <FL/Fl_Button.H>

#include "confdialog.h"
#include "fl_digi.h"
#include "main.h"

extern bool dl_fldigi_downloaded_once;
extern Fl_Choice *habFlightXML;
extern Fl_Button *habConfigureButton;

void dl_fldigi_init();
void cb_dl_fldigi_toggle_dl_online();
void dl_fldigi_post(const char *data, const char *identity);
void dl_fldigi_download();
void dl_fldigi_update_payloads();
void cb_dl_fldigi_select_payload(Fl_Widget *o, void *a);
void cb_dl_fldigi_configure_payload(Fl_Widget *o, void *a);
void dl_fldigi_select_payload(const char *name);
void dl_fldigi_reset_rxtimer();
void dl_fldigi_update_rxtimer();

#endif
