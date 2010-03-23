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

void dl_fldigi_post(const char *data, const char *identity);
#endif
