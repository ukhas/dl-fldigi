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
    void log(const string &message);
    void got_flights(const vector<Json::Value> &flights);
};

extern DExtractorManager *extrmgr;
extern DUploaderThread *uthr;
extern vector<Json::Value> flights;

void init();
void set_online(bool online);
bool is_online();
void flights_init();

} /* namespace dl_fldigi */

#endif /* DL_FLDIGI_GLOBAL_H */
