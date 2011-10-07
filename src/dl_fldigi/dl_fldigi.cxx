#include "dl_fldigi/dl_fldigi.h"

#include <vector>
#include <string>
#include <stdexcept>
#include <json/json.h>
#include <time.h>
#include "habitat/UKHASExtractor.h"
#include "configuration.h"
#include "debug.h"

using namespace std;

namespace dl_fldigi {

/* A short note on thread safety.
 * The embedded habitat submodule, cpp_uploader, is thread safe.
 * fldigi in general, however, is not at all. All of the dl_fldigi functions
 * are intended to be called by the main gui thread. The only ones that arn't
 * are the extractor manager methods (extrmgr.push etc). These call
 * uthr->payload_telemetry, but that is not overridden by DUploaderThread and
 * is safe. */

/* How does online/offline work? if online() is false, uthr->settings() will
 * reset the UploaderThread, leaving it unintialised */

/* TODO: maybe upload the git commit when compiled as the 'version' */
/* TODO: update the submodule */

DExtractorManager *extrmgr;
DUploaderThread *uthr;
vector<Json::Value> flights;
enum location_mode new_location_mode;

static bool dl_online, downloaded_once, hab_ui_exists;
static int dirty;
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
    uthr->start();
    flights_init();

    /* if --hab was specified, default online to true, and update ui */
    online(hab_mode);
    hab_ui_exists = hab_mode;

    if (progdefaults.gps_start_enabled)
        current_location_mode = LOC_STATIONARY;
    else
        current_location_mode = LOC_GPS;

    reset_gps_settings();
}

void cleanup()
{
    delete extrmgr;
    extrmgr = 0;

    uthr->shutdown();
    delete uthr;
    uthr = 0;
}

static void periodically()
{
    /* TODO: arrange for this to be called periodically by fltk */

    uthr->listener_info();

    if (current_location_mode == LOC_STATIONARY)
        uthr->listener_telemetry();
}

void online(bool val)
{
    bool changed;

    changed = (dl_online != val);
    dl_online = val;

    if (changed)
    {
        uthr->settings();
    }

    if (changed && dl_online)
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

void changed(enum changed_groups thing)
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
    
    if (dirty & CH_LOCATION_MODE)
    {
        current_location_mode = new_location_mode;
    }

    if ((dirty & CH_LOCATION_MODE) || (dirty & CH_GPS_SETTINGS))
    {
        reset_gps_settings();
    }

    /* If the info has been updated, or the upload settings changed... */
    if (dirty & (CH_UTHR_SETTINGS | CH_INFO))
    {
        uthr->listener_info();
    }

    /* if stationary and (settings changed, or if we just switched to
     * stationary mode from gps mode, or if the upload settings changed) */
    if (current_location_mode == LOC_STATIONARY &&
        (dirty & (CH_STATIONARY_LOCATION | CH_LOCATION_MODE |
                  CH_UTHR_SETTINGS)))
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

/* TODO: abort these if critical settings are missing and don't upload
 * empty string values (e.g., "antenna": ""). */

/* All these functions are called via a DUploaderThread pointer so
 * the fact that they are non virtual is OK. Having a different set of
 * arguments prevents the wrong function from being called except in the
 * case of flights() */

void DUploaderThread::settings()
{
    UploaderThread::reset();

    if (!online())
    {
        warning("upload disabled: offline");
        return;
    }

    if (!progdefaults.myCall.size() || !progdefaults.habitat_uri.size() ||
        !progdefaults.habitat_db.size())
    {
        warning("upload disabled: settings missing");
    }

    UploaderThread::settings(progdefaults.myCall, progdefaults.habitat_uri,
                             progdefaults.habitat_db);
}

void DUploaderThread::listener_telemetry()
{
    Json::Value data(Json::objectValue);

    /* TODO: is it really a good idea to upload time like this? */
    struct tm tm;
    time_t now;

    now = time(NULL);
    if (now < 0)
        throw runtime_error("time() failed");

    struct tm *tm_p = gmtime_r(&now, &tm);
    if (tm_p != &tm)
        throw runtime_error("gmtime() failed");

    data["time"] = Json::Value(Json::objectValue);
    Json::Value &time = data["time"];
    time["hour"] = tm.tm_hour;
    time["minute"] = tm.tm_min;
    time["second"] = tm.tm_sec;

    data["latitude"] = progdefaults.myLat;
    data["longitude"] = progdefaults.myLon;

    UploaderThread::listener_telemetry(data);
}

void DUploaderThread::listener_info()
{
    Json::Value data(Json::objectValue);
    data["name"] = progdefaults.myName;
    data["location"] = progdefaults.myQth;
    data["radio"] = progdefaults.myRadio;
    data["antenna"] = progdefaults.myAntenna;
    UploaderThread::listener_info(data);
}

void DUploaderThread::log(const string &message)
{
    LOG_DEBUG("habitat UploaderThread: %s", message.c_str());
    /* TODO: put_status */
}

void DUploaderThread::warning(const string &message)
{
    LOG_WARN("habitat UploaderThread: WARNING %s", message.c_str());
    /* TODO: put_status & kick up a fuss */
}

void DUploaderThread::got_flights(const vector<Json::Value> &flights)
{
    /* TODO: Save stuff */
    downloaded_once = true;
}

} /* namespace dl_fldigi */
