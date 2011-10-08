#include "dl_fldigi/dl_fldigi.h"

#include <vector>
#include <string>
#include <stdexcept>
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <time.h>
#include "habitat/UKHASExtractor.h"
#include "habitat/EZ.h"
#include "configuration.h"
#include "debug.h"
#include "fl_digi.h"
#include "confdialog.h"
#include "main.h"

using namespace std;

namespace dl_fldigi {

/*
 * A short note on thread safety.
 * The embedded habitat submodule, cpp_uploader, is thread safe.
 * fldigi in general, however, is not at all. Most of the dl_fldigi functions
 * are intended to be called by the main gui thread. The functions that are
 * not called by the main thread that we need to worry about are:
 *  - extrmgr->status and extrmgr->data
 *  - uthr->payload_telemetry (called by extrmgr)
 *  - uthr->log and uthr->warning
 *  - uthr->got_flights
 */

/* How does online/offline work? if online() is false, uthr->settings() will
 * reset the UploaderThread, leaving it unintialised */

/* TODO: maybe upload the git commit when compiled as the 'version' */

DExtractorManager *extrmgr;
DUploaderThread *uthr;
enum location_mode new_location_mode;

static EZ::cURLGlobal *cgl;

static EZ::Mutex flight_docs_lock;
static vector<Json::Value> flight_docs;
static bool dl_online, downloaded_once, hab_ui_exists;
static int dirty;
static enum location_mode current_location_mode;
static habitat::UKHASExtractor *ukhas;
static string fldocs_cache_file;

static void flight_docs_init();
static void reset_gps_settings();
static void periodically();

void init()
{
    cgl = new EZ::cURLGlobal();

    uthr = new DUploaderThread();
    extrmgr = new DExtractorManager(*uthr);

    ukhas = new habitat::UKHASExtractor();
    extrmgr->add(*ukhas);

    fldocs_cache_file = HomeDir + "flight_docs.json";
}

void ready(bool hab_mode)
{
    /* if --hab was specified, default online to true, and update ui */
    hab_ui_exists = hab_mode;

    if (progdefaults.gps_start_enabled)
        current_location_mode = LOC_GPS;
    else
        current_location_mode = LOC_STATIONARY;

    flight_docs_init();
    uthr->start();
    reset_gps_settings();
    online(hab_mode);

    /* online will call uthr->settings() if hab_mode since it online will
     * "change" from false to true) */
}

void cleanup()
{
    delete extrmgr;
    extrmgr = 0;

    if (uthr)
        uthr->shutdown();
    delete uthr;
    uthr = 0;

    delete cgl;
    cgl = 0;
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

    confdialog_dl_online->value(val);
    set_menu_dl_online(val);
}

bool online()
{
    return dl_online;
}

static void flight_docs_init()
{
    EZ::MutexLock lock(flight_docs_lock);

    ifstream cf(fldocs_cache_file.c_str());

    if (cf.fail())
    {
        LOG_DEBUG("Failed to open cache file");
        return;
    }

    flight_docs.clear();

    while (cf.good())
    {
        string line;
        getline(cf, line, '\n');

        Json::Reader reader;
        Json::Value root;
        if (!reader.parse(line, root, false))
            break;

        flight_docs.push_back(root);
    }

    bool failed = cf.fail() || !cf.eof();

    cf.close();

    if (failed)
    {
        flight_docs.clear();
        LOG_WARN("Failed to load flight doc cache from file");
    }
    else
    {
        LOG_DEBUG("Loaded %li flight docs from file", flight_docs.size());
    }
}

static void reset_gps_settings()
{
    /* if (gps_thread != NULL)  gps_thread->join(); delete gps_thread; */

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
    LOG_DEBUG("hbtE %s", msg.c_str());
    /* TODO: Log message from extractor */
    /* TODO: put_status safely */
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
        return;
    }

    UploaderThread::settings(progdefaults.myCall, progdefaults.habitat_uri,
                             progdefaults.habitat_db);
}

/* This function is used for stationary listener telemetry */
void DUploaderThread::listener_telemetry()
{
    if (current_location_mode != LOC_STATIONARY)
    {
        warning("attempted to upload stationary listener "
                "telemetry while in GPS telemetry mode");
        return;
    }

    if (!progdefaults.myLat.size() || !progdefaults.myLon.size())
    {
        warning("unable to upload stationary listener telemetry: "
                "latitude or longitude missing");
        return;
    }

    double latitude, longitude;
    istringstream lat_strm(progdefaults.myLat), lon_strm(progdefaults.myLon);
    lat_strm >> latitude;
    lon_strm >> longitude;

    if (lat_strm.fail())
    {
        warning("unable to parse stationary latitude");
        return;
    }

    if (lon_strm.fail())
    {
        warning("unable to parse stationary longitude");
        return;
    }

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

    data["latitude"] = latitude;
    data["longitude"] = longitude;

    UploaderThread::listener_telemetry(data);
}

static void info_add(Json::Value &data, const string &key, const string &value)
{
    if (value.size())
        data[key] = value;
}

void DUploaderThread::listener_info()
{
    Json::Value data(Json::objectValue);
    info_add(data, "name", progdefaults.myName);
    info_add(data, "location", progdefaults.myQth);
    info_add(data, "radio", progdefaults.myRadio);
    info_add(data, "antenna", progdefaults.myAntenna);

    if (!data.size())
    {
        warning("not uploading empty listener info");
        return;
    }

    UploaderThread::listener_info(data);
}

/* These functions must try to be thread safe. */
void DUploaderThread::log(const string &message)
{
    LOG_DEBUG("hbtUT %s", message.c_str());
    /* TODO: put_status safely */
}

void DUploaderThread::warning(const string &message)
{
    LOG_WARN("hbtUT %s", message.c_str());
    /* TODO: put_status safely & kick up a fuss */
}

void DUploaderThread::got_flights(const vector<Json::Value> &new_flights)
{
    EZ::MutexLock lock(flight_docs_lock);

    ostringstream ltmp;
    ltmp << "Downloaded " << new_flights.size() << " flight docs";
    log(ltmp.str());

    flight_docs = new_flights;
    downloaded_once = true;

    ofstream cf(fldocs_cache_file.c_str(), ios_base::out | ios_base::trunc);

    for (vector<Json::Value>::const_iterator it = flight_docs.begin();
         it != flight_docs.end() && cf.good();
         it++)
    {
        Json::FastWriter writer;
        cf << writer.write(*it);
    }

    bool success = cf.good();

    cf.close();

    if (!success)
    {
        warning("unable to save flights data");
        unlink(fldocs_cache_file.c_str());
    }

    /* TODO: REQ(update flights list). */
}

} /* namespace dl_fldigi */
