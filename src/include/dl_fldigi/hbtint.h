#ifndef DL_FLDIGI_HBTINT_H
#define DL_FLDIGI_HBTINT_H

#include <string>
#include <vector>
#include <json/json.h>
#include "habitat/Extractor.h"
#include "habitat/UploaderThread.h"

namespace dl_fldigi {
namespace hbtint {

class DUploaderThread : public habitat::UploaderThread
{
public:
    /* These functions call super() functions, but with data grabbed from
     * progdefaults and other globals. */
    void settings();
    void listener_telemetry();
    void listener_telemetry(const Json::Value &data);
    void listener_information();

    /* Forward data to fldigi debug/log macros */
    void log(const std::string &message);
    void warning(const std::string &message);
    void saved_id(const std::string &type, const std::string &id);

    /* Update UI */
    void got_flights(const std::vector<Json::Value> &flights);
    void got_payloads(const std::vector<Json::Value> &payloads);

    /* Modify run() to help us shutdown the thread */
    void *run();
};

class DExtractorManager : public habitat::ExtractorManager
{
public:
    DExtractorManager(habitat::UploaderThread &u)
        : habitat::ExtractorManager(u) {};

    void status(const std::string &msg);
    void data(const Json::Value &d);
};

extern DExtractorManager *extrmgr;
extern DUploaderThread *uthr;

void init();
void start();
void cleanup();

} /* namespace hbtint */
} /* namespace dl_fldigi */

#endif /* DL_FLDIGI_HBTINT_H */
