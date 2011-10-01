#include "dl_fldigi/dl_fldigi.h"

#include <vector>
#include <string>
#include <stdexcept>
#include <json/json.h>
#include "habitat/UKHASExtractor.h"

using namespace std;

namespace dl_fldigi {

/* A short note on thread safety.
 * The embedded habitat submodule, cpp_uploader, is thread safe.
 * fldigi in general, however, is not at all. All of the dl_fldigi functions
 * are intended to be called by the main gui thread. The only ones that arn't
 * are the extractor manager methods (extrmgr.push etc). These call
 * uthr->payload_telemetry, but that is not overridden by DUploaderThread and
 * is safe. */

/* TODO override methods of UploaderThread (in DUp..) to pick data from
 * progdefaults and abort if data is empty. */

DExtractorManager *extrmgr;
DUploaderThread *uthr;
vector<Json::Value> flights;
enum location_mode new_location_mode;

static bool dl_online, downloaded_once, hab_ui_exists;
static enum changed_groups dirty;
static enum location_mode current_location_mode;
static habitat::UKHASExtractor *ukhas;

static void flights_init();
static void reset_gps_settings();
static void periodically();

void init()
{
    uthr = new DUploaderThread();
    extrmgr = new DExtractorManager(*uthr);

    ukhas = new habitat::UKHASExtractor();
    extrmgr->add(*ukhas);
}

void ready(bool hab_mode)
{
    uthr->settings();
    flights_init();

    location_mode(progdefaults.gps_start_enabled);

    /* if --hab was specified, default online to true, and update ui */
    online(hab_mode);
    hab_ui_exists = hab_mode;
}

static void periodically()
{
    /* TODO: arrange for this to be called periodically by fltk */

    if (online())
    {
        uthr->listener_info();
        uthr->listener_telemetry();
    }
}

void online(bool val)
{
    dl_online = val;

    if (dl_online)
    {
        if (!downloaded_once)
            uthr->flights();

        uthr->listener_info();
        uthr->listener_telemetry();
    }

    /* TODO: update UI checkboxes if necessary */
}

bool online()
{
    return dl_online;
}

static void flights_init()
{
    /* TODO: Load flights from file */
}

static void reset_gps_settings()
{
    /* if (gps_thread != NULL)  gps_thread.join(); delete gps_thread; */

    if (current_location_mode == LOC_GPS)
    {
        /* TODO gps_thread = new stuff */
    }
}

void changed(enum changed_type thing)
{
    dirty |= thing;
}

void commit()
{
    /* Update something if its settings change; fairly simple: */
    if (dirty & CH_UTHR_SETTINGS)
    {
        downloaded_once = false;
        uthr->settings();
        uthr->flights();
    }
    
    if (dirty & CH_INFO)
    {
        uthr->listener_info();
    }

    if (dirty & CH_LOCATION_MODE)
    {
        current_location_mode = new_location_mode;
    }

    if ((dirty & CH_LOCATION_MODE) || (dirty & CH_GPS_SETTINGS))
    {
        reset_gps_settings();
    }

    /* what is this mess?:
     * if stationary and settings changed, or if we just switched to
     * stationary mode from gps mode, send a telemetry. */
    if (current_location_mode == LOC_STATIONARY &&
        (dirty & (CH_STATIONARY_LOCATION || CH_LOCATION_MODE)))
    {
        uthr->listener_telemetry();
    }

    dirty = CH_NONE;
}

void DExtractorManager::status(const string &msg)
{
    LOG_DEBUG("habitat Extractor: %s", msg.c_str());
    /* TODO: Log message from extractor */
    /* TODO: put_status */
}

void DExtractorManager::data(const Json::Value &d)
{
    if (!hab_ui_exists)
        return;

    /* TODO: Data to fill out HAB UI */
}

void DUploaderThread::log(const string &message)
{
    LOG_DEBUG("habitat UploaderThread: %s", msg.c_str());
    /* TODO: Log message from UploaderThread */
    /* TODO: put_status */
}

void DUploaderThread::got_flights(const vector<Json::Value> &flights)
{
    /* TODO: Save stuff */
    downloaded_once = true;
}

} /* namespace dl_fldigi */
