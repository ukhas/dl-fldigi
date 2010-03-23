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

extern bool offline;

extern int dl_fldigi_pfds[2];
extern pid_t dl_fldigi_cpid;
extern std::string dl_fldigi_rx_string;

// This is the writer call back function used by curl  
void dlServerCommunicator();
#endif
