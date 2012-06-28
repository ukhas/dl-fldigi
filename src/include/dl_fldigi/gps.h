#ifndef DL_FLDIGI_GPS_H
#define DL_FLDIGI_GPS_H

#include <string>
#include <stdio.h>
#include <time.h>
#include "habitat/EZ.h"
#ifdef __MINGW32__
#include <windows.h>
#endif

namespace dl_fldigi {
namespace gps {

class GPSThread : public EZ::SimpleThread
{
    const std::string device;
    const int baud, rate;
    bool term;
    time_t last_upload;

#ifdef __MINGW32__
    HANDLE handle;
#endif
    int fd;
    FILE *f;
    int wait_exp;

    void prepare_signals();
    void send_signal();
    bool check_term();
    void set_term();
    void wait();

    void setup();
    void cleanup();
    void log(const std::string &message);
    void warning(const std::string &message);

    void read();
    void update_ui(const string &time_str, double lat, double lon, double alt);
    void upload(const string &time_str, double lat, double lon, double alt);

public:
    GPSThread(const std::string &d, int b, int r)
        : device(d), baud(b), rate(r), term(false), last_upload(0),
#ifdef __MINGW32__
          handle(INVALID_HANDLE_VALUE),
#endif
          fd(-1), f(NULL), wait_exp(0) {};
    ~GPSThread() {};

    void *run();
    void shutdown();
};

void configure_gps();
void cleanup();

} /* namespace gps */
} /* namespace dl_fldigi */

#endif /* DL_FLDIGI_GPS_H */
