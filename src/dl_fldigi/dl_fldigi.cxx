#include "dl_fldigi/dl_fldigi.h"

#include <vector>
#include <string>
#include <stdexcept>
#include <json/json.h>
#include "habitat/EZ.h"
#include "habitat/UKHASExtractor.h"

using namespace std;

namespace dl_fldigi {

DExtractorManager *extrmgr;
DUploaderThread *uthr;
vector<Json::Value> flights;

static EZ::Mutex mutex;
static bool downloaded_once = false;
static habitat::UKHASExtractor *ukhas;

void init()
{
    EZ::MutexLock lock(mutex);

    uthr = new DUploaderThread();
    extrmgr = new DExtractorManager(*uthr);

    ukhas = new habitat::UKHASExtractor();
    extrmgr->add(*ukhas);
}

void set_online(bool online)
{
    EZ::MutexLock lock(mutex);

    if (online && !downloaded_once)
        uthr->flights();

    /* TODO: update UI checkboxes if necessary */
}

bool is_online()
{
    EZ::MutexLock lock(mutex);
    bool v = downloaded_once;
    return v;
}

void flights_init()
{
    /* TODO: Load flights from file */
}

void DExtractorManager::status(const string &msg)
{
    /* TODO: Log message from extractor */
}

void DExtractorManager::data(const Json::Value &d)
{
    /* TODO: Data to fill out HAB UI */
}

void DUploaderThread::log(const string &message)
{
    /* TODO: Log message from UploaderThread */
}

void DUploaderThread::got_flights(const vector<Json::Value> &flights)
{
    /* TODO: Save stuff */
}

} /* namespace dl_fldigi */
