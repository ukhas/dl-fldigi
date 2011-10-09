#include "dl_fldigi/dl_fldigi.h"

#include <vector>
#include <map>
#include <string>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <set>
#include <json/json.h>
#include <time.h>
#include <Fl/Fl.h>
#include "habitat/UKHASExtractor.h"
#include "habitat/EZ.h"
#include "configuration.h"
#include "debug.h"
#include "fl_digi.h"
#include "confdialog.h"
#include "main.h"

using namespace std;

namespace dl_fldigi {

/* How does online/offline work? if online() is false, uthr->settings() will
 * reset the UploaderThread, leaving it unintialised */

/* TODO: maybe upload the git commit when compiled as the 'version' */

DExtractorManager *extrmgr;
DUploaderThread *uthr;
enum location_mode new_location_mode;
bool show_testing_flights;

static EZ::cURLGlobal *cgl;

static vector<Json::Value> flight_docs;
static bool dl_online, downloaded_once, hab_ui_exists;
static int dirty;
static enum location_mode current_location_mode;
static habitat::UKHASExtractor *ukhas;
static string fldocs_cache_file;

static void flight_docs_init();
static void reset_gps_settings();
static void periodically();

/* Functions init, ready and cleanup should only be called from main() */
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

/* All other functions should hopefully be thread safe */
void online(bool val)
{
    Fl::lock();

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

    Fl::unlock();
}

bool online()
{
    Fl::lock();
    bool val = dl_online;
    Fl::unlock();

    return val
}

static void flight_docs_init()
{
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

    populate_flights();
}

static string escape_menu_string(const string &s_)
{
    string s(s_);
    size_t pos = 0;

    do
    {
        pos = s.find_first_of("&/\\_", pos);

        if (pos != string::npos)
        {
            s.insert(pos, 1, '\\');
            pos += 2;
        }
    }
    while (pos != string::npos);

    return s;
}

static string escape_browser_string(const string &s_)
{
    string s(s_);
    size_t pos = 0;

    do
    {
        pos = s.find('\t', pos);
        if (pos != string::npos)
            s[pos] = ' ';
    }
    while (pos != string::npos);

    return s;
}

static string get_payload_list(const Json::Value &flight)
{
    const Json::Value &payloads = flight["payloads"];

    if (!payloads.isObject() || !payloads.size())
        return "";

    const vector<string> payloads = payloads.getMemberNames();

    ostringstream payload_list;

    for (vector<string::const_iterator it = payloads.begin();
        it != payloads.end();
        it++)
    {
        if (it != payloads.begin())
            payload_list << ',';
        payload_list << (*it);
    }

    return payload_list.str();
}

static string flight_launch_date(const Json::Value &flight)
{
    const Json::Value &launch = root["launch"];

    if (!launch.isObject() || !launch.size() || !launch.isMember("time"))
        return "";

    time_t date = launch["time"].asInt();
    char buf[20];
    struct tm tm;

    if (gmtime_r(&date, &tm) != &tm)
        return "";

    if (strftime(buf, sizeof(buf), "%a %d %b", &tm) <= 0)
        return "";

    return buf;
}

static bool is_testing_flight(const Json::Value &flight)
{
    /* TODO: is this a testing flight? */
    return false;
}

static string flight_choice_item(const string &name,
                                 const string &payload_list,
                                 int attempt)
{
    /* "<name>: <payload>,<payload>,<payload>" */

    string attempt_suffix;

    if (attempt != 1)
    {
        ostringstream sfx;
        sfx << " (" << attempt << ")";
        attempt_suffix = sfx.str();
    }

    return escape_menu_string(name + ": " + payload_list + attempt_suffix);
}

static string flight_browser_item(const string &name, const string &date,
                                  const string &payload_list)
{
    /* "<name>\t<optional date>\t<payload>,<payload>" */
    return "@." + escape_browser_string(name) + "\t" +
           "@." + date + "\t"
           "@." + escape_browser_string(payload_list.str());
}

static void flight_choice_callback(Fl_Widget *w, void *a)
{
    int index = reinterpret_cast<int>(a);
    Fl_Choice *choice = static_cast<Fl_Choice *>(w);
    flight_browser->value(choice->value());
    select_flight(index);
}

void populate_flights()
{
    Fl::lock();

    set<string> choice_items;

    if (habFlights)
        habFlights->clear();

    flight_browser->clear();

    vector<Json::Value>::const_iterator it;
    int i;
    for (it = flight_docs.begin(), i = 0; it != flight_docs.end(); it++, i++)
    {
        const Json::Value &root = (*it);
        const string name = root["name"].asString();
        const string payload_list = get_payload_list(root);
        const string date = flight_launch_date(root);
        void *userdata = reinterpret_cast<void *>(i);

        if (!name.size() || !payloads.size())
        {
            LOG_WARN("invalid flight doc: missing a key");
            continue;
        }

        if (is_testing_flight(root) && !show_testing_flights)
            continue;

        if (habFlights)
        {
            string item;
            int attempt = 1;

            /* Avoid duplicate menu items: fltk removes them */
            do
            {
                item = flight_choice_item(name, payload_list, attempt);
                attempt++;
            }
            while (choice_items.count(item));
            choice_items.insert(item);

            habFlights->add(item, 0L, flight_choice_callback, userdata);
        }

        flight_browser->add(flight_browser_item(name, date, payload_list),
                            userdata);
    }

    /* TODO: re-select previous item */
    /* TODO avoid duplicate names in choice somehow */

    Fl::unlock();
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
    Fl::lock();
    dirty |= thing;
    Fl::unlock();
}

void commit()
{
    Fl::lock();

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

    Fl::unlock();
}

void DExtractorManager::status(const string &msg)
{
    Fl::lock();

    LOG_DEBUG("hbtE %s", msg.c_str());
    /* TODO: Log message from extractor */
    /* TODO: put_status safely */

    Fl::unlock();
}

void DExtractorManager::data(const Json::Value &d)
{
    Fl::lock();

    if (!hab_ui_exists)
        return;

    /* TODO: Data to fill out HAB UI */

    Fl::unlock();
}

/* TODO: abort these if critical settings are missing and don't upload
 * empty string values (e.g., "antenna": ""). */

/* All these functions are called via a DUploaderThread pointer so
 * the fact that they are non virtual is OK. Having a different set of
 * arguments prevents the wrong function from being called except in the
 * case of flights() */

void DUploaderThread::settings()
{
    Fl::lock();

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

    Fl::unlock();

    UploaderThread::settings(progdefaults.myCall, progdefaults.habitat_uri,
                             progdefaults.habitat_db);
}

/* This function is used for stationary listener telemetry */
void DUploaderThread::listener_telemetry()
{
    Fl::lock();

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

    Fl::unlock();

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
    Fl::lock();

    if (value.size())
        data[key] = value;

    Fl::unlock();
}

void DUploaderThread::listener_info()
{
    Fl::lock();

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

    Fl::unlock();

    UploaderThread::listener_info(data);
}

/* These functions must try to be thread safe. */
void DUploaderThread::log(const string &message)
{
    Fl::lock();
    LOG_DEBUG("hbtUT %s", message.c_str());
    /* TODO: put_status safely */
    Fl::unlock();
}

void DUploaderThread::warning(const string &message)
{
    Fl::lock();
    LOG_WARN("hbtUT %s", message.c_str());
    /* TODO: put_status safely & kick up a fuss */
    Fl::unlock();
}

void DUploaderThread::got_flights(const vector<Json::Value> &new_flight_docs)
{
    Fl::lock();

    flight_docs = new_flight_docs;

    ostringstream ltmp;
    ltmp << "Downloaded " << new_flight_docs.size() << " flight docs";
    log(ltmp.str());

    flight_docs = new_flight_docs;
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

    populate_flights();

    Fl::unlock();
}

} /* namespace dl_fldigi */
