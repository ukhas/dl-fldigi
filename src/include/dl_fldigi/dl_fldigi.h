#ifndef DL_FLDIGI_GLOBAL_H
#define DL_FLDIGI_GLOBAL_H

#include <vector>
#include <string>
#include <stdexcept>
#include <json/json.h>
#include "habitat/Extractor.h"
#include "habitat/UploaderThread.h"

using namespace std;

namespace dl_fldigi {

enum changed_groups
{
    CH_NONE = 0x00,
    CH_UTHR_SETTINGS = 0x01,
    CH_INFO = 0x02,
    CH_LOCATION_MODE = 0x04,
    CH_STATIONARY_LOCATION = 0x08,
    CH_GPS_SETTINGS = 0x10
};

enum location_mode
{
    LOC_STATIONARY,
    LOC_GPS
};

class DExtractorManager : public habitat::ExtractorManager
{
public:
    DExtractorManager(habitat::UploaderThread &u)
        : habitat::ExtractorManager(u) {};

    void status(const string &msg);
    void data(const Json::Value &d);
};

class DUploaderThread : public habitat::UploaderThread
{
public:
    /* These functions call super() functions, but with data grabbed from
     * progdefaults and other globals. */
    void settings();
    void listener_telemetry();
    void listener_info();

    /* Forward data to fldigi debug/log macros */
    void log(const string &message);
    void warning(const string &message);

    /* Update UI */
    void got_flights(const vector<Json::Value> &flights);
};

extern DExtractorManager *extrmgr;
extern DUploaderThread *uthr;
extern vector<Json::Value> flights;
extern enum location_mode new_location_mode;

void init();                 /* Create globals and stuff; before UI init */
void ready(bool hab_mode);   /* After UI init, start stuff */
void cleanup();
void online(bool value);
bool online();
void changed(enum changed_groups thing);
void commit();

} /* namespace dl_fldigi */

#endif /* DL_FLDIGI_GLOBAL_H */
