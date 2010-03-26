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

typedef size_t dl_fldigi_data_callback(void *ptr, size_t size, size_t nmemb, void *stream);
void dl_fldigi_init();
void dl_fldigi_post(const char *data, const char *identity);
void dl_fldigi_nonblocking_download(const char *url, dl_fldigi_nonblocking_download_callback callback);

#endif
