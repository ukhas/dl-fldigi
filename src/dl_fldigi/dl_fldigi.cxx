/* 
 * Copyright (C) 2011 James Coxon, Daniel Richman, Robert Harrison,
 *                    Philip Heron, Adam Greig, Simrun Basuita
 * License: GNU GPL 3
 */

#include "dl_fldigi/dl_fldigi.h"

#include <vector>
#include <map>
#include <string>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <set>
#include <jsoncpp/json.h>
#include <time.h>
#include <math.h>
#include <ctype.h>
#include <signal.h>
#include <pthread.h>
#ifndef __MINGW32__
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#else
#include <windows.h>
#endif
#include <Fl/Fl.H>
#include "habitat/UKHASExtractor.h"
#include "habitat/EZ.h"
#include "dl_fldigi/version.h"
#include "configuration.h"
#include "debug.h"
#include "fl_digi.h"
#include "confdialog.h"
#include "main.h"
#include "rtty.h"

using namespace std;

namespace dl_fldigi {

/* FLTK doesn't provide something like this, as far as I can tell. */
class Fl_AutoLock
{
public:
    Fl_AutoLock() { Fl::lock(); };
    ~Fl_AutoLock() { Fl::unlock(); };
};

class GPSThread : public EZ::SimpleThread
{
    const string device;
    const int baud;
    bool term;

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
    void log(const string &message);
    void warning(const string &message);

    void read();
    void upload(int h, int m, int s, double lat, double lon, double alt);

public:
    GPSThread(const string &d, int b)
        : device(d), baud(b), term(false),
#ifdef __MINGW32__
          handle(INVALID_HANDLE_VALUE),
#endif
          fd(-1), f(NULL), wait_exp(0) {};
    ~GPSThread() {};
    void *run();
    void shutdown();
};

/* How does online/offline work? if online() is false, uthr->settings() will
 * reset the UploaderThread, leaving it unintialised */

DExtractorManager *extrmgr;
DUploaderThread *uthr;
enum location_mode new_location_mode;
bool show_testing_flights;

static EZ::cURLGlobal *cgl;
static GPSThread *gps_thread;
static habitat::UKHASExtractor *ukhas;

static string fldocs_cache_file;
static vector<Json::Value> flight_docs;
static vector<string> payload_index;
/* These pointers just point at some part of the heap allocated by something
 * in the flight_docs vector; they're invalidated when filght_docs is modified
 * but we make sure to update them when that happens (only happens when
 * new data is downloaded; populate_flights cleans up). */
static const Json::Value *cur_flight, *cur_payload, *cur_mode;
static int cur_mode_index, cur_payload_modecount;
static int flight_search_first = 1;

static bool dl_online, downloaded_once, hab_ui_exists, shutting_down;
static int dirty;
static enum location_mode current_location_mode;
static time_t last_rx, last_warn;

/* Keep the last listener lat/lon that we uploaded. */
/* Call update_distance_bearing whenever it is changed! */
static double listener_latitude, listener_longitude,
              balloon_latitude, balloon_longitude;
static bool listener_valid, balloon_valid;

static const time_t period = 10;

static void flight_docs_init();
static void reset_gps_settings();
static void periodically(void *);
static void select_payload(int index);
static void select_mode(int index);
static void update_distance_bearing();

/*
 * Functions init, ready and cleanup should only be called from main().
 * thread_death and periodically are called by FLTK, which will have the lock.
 */
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

    Fl::add_timeout(period, periodically);

    if (hab_ui_exists)
        habTimeSinceLastRx->value("ages");
}

void cleanup()
{
    LOG_DEBUG("cleaning up");

    shutting_down = true;

    /* Destroy extraction stuff */
    if (extrmgr)
    {
        delete extrmgr;
        extrmgr = 0;
    }

    if (ukhas)
    {
        delete ukhas;
        ukhas = 0;
    }

    /* Ask running threads to shutdown */
    if (uthr)
        uthr->shutdown();

    if (gps_thread)
        gps_thread->shutdown();

    /* Wait for threads to die and be cleaned up */
    while (uthr || gps_thread)
        Fl::wait();

    /* Clean up cURL */
    if (cgl)
    {
        delete cgl;
        cgl = 0;
    }

    /* Clean up flight docs stuff */
    flight_docs.clear();
    payload_index.clear();
    cur_flight = NULL;
    cur_payload = NULL;
    cur_mode = NULL;
    cur_mode_index = 0;
    cur_payload_modecount = 0;
}

static void thread_death(void *what)
{
    if (what == uthr)
    {
        LOG_INFO("uthr");
        uthr->join();
        delete uthr;
        uthr = 0;
    }
    else if (what == gps_thread)
    {
        LOG_INFO("gps_thread");
        gps_thread->join();
        delete gps_thread;
        gps_thread = 0;

        if (!shutting_down)
            reset_gps_settings();
    }
    else
    {
        LOG_ERROR("unknown thread");
        return;
    }
}

static void periodically(void *)
{
    if (shutting_down)
        return;

    Fl::repeat_timeout(period, periodically);

    time_t now = time(NULL);

    if (hab_ui_exists)
    {
        time_t rx_delta = now - last_rx;

        if (rx_delta > 3600 * 24)
        {
            habTimeSinceLastRx->value("ages");
        }
        else if (rx_delta < 3)
        {
            habTimeSinceLastRx->value("just now");
        }
        else
        {
            ostringstream sval;

            if (rx_delta < 60)
                goto seconds;
            if (rx_delta < 3600)
                goto minutes;

            sval << (rx_delta / 3600) << "h ";
            rx_delta %= 3600;
minutes:
            sval << (rx_delta / 60) << "m ";
            rx_delta %= 60;
seconds:
            sval << rx_delta << "s";

            habTimeSinceLastRx->value(sval.str().c_str());
        }
    }
}

/* All other functions should hopefully be thread safe */
void online(bool val)
{
    Fl_AutoLock lock;

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
    set_menu_dl_refresh_active(dl_online);

    if (dl_online)
        flight_docs_refresh->activate();
    else
        flight_docs_refresh->deactivate();
}

bool online()
{
    Fl_AutoLock lock;
    return dl_online;
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

        char discard;
        while (cf.good() && cf.peek() == '\n')
            cf.get(discard);

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

    const vector<string> payload_names = payloads.getMemberNames();
    ostringstream payload_list;
    vector<string>::const_iterator it;

    for (it = payload_names.begin(); it != payload_names.end(); it++)
    {
        if (it != payload_names.begin())
            payload_list << ',';
        payload_list << (*it);
    }

    return payload_list.str();
}

static string flight_launch_date(const Json::Value &flight)
{
    const Json::Value &launch = flight["launch"];

    if (!launch.isObject() || !launch.size())
        return "";

    if (!launch.isMember("time") || !launch["time"].isInt())
        return "";

    time_t date = launch["time"].asInt();
    char buf[20];
    struct tm tm;

    if (gmtime_r(&date, &tm) != &tm)
        return "";

    if (strftime(buf, sizeof(buf), "%a %d %b %y", &tm) <= 0)
        return "";

    return buf;
}

static bool is_testing_flight(const Json::Value &flight)
{
    /* TODO: HABITAT is this a testing flight? */
    /* Crude test: */
    return !(flight["end"].isInt() && flight["end"].asInt());
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
           "@." + date + "\t" +
           "@." + escape_browser_string(payload_list);
}

static void flight_choice_callback(Fl_Widget *w, void *a)
{
    int index = reinterpret_cast<intptr_t>(a);
    Fl_Choice *choice = static_cast<Fl_Choice *>(w);
    flight_browser->value(choice->value() + 1);
    select_flight(index);
}

void populate_flights()
{
    Fl_AutoLock lock;

    set<string> choice_items;

    if (hab_ui_exists)
    {
        habFlight->value(-1);
        habFlight->clear();
    }

    flight_browser->clear();

    select_flight(-1);

    vector<Json::Value>::const_iterator it;
    intptr_t i;
    for (it = flight_docs.begin(), i = 0; it != flight_docs.end(); it++, i++)
    {
        const Json::Value &root = (*it);

        if (!root.isObject() || !root.size() ||
            !root["_id"].isString() || !root["name"].isString())
        {
            LOG_WARN("invalid flight doc");
            continue;
        }

        const string id = root["_id"].asString();
        const string name = root["name"].asString();
        const string payload_list = get_payload_list(root);
        const string date = flight_launch_date(root);
        void *userdata = reinterpret_cast<void *>(i);

        if (!id.size() || !name.size() || !payload_list.size())
        {
            LOG_WARN("invalid flight doc");
            continue;
        }

        if (is_testing_flight(root) && !show_testing_flights)
            continue;

        if (hab_ui_exists)
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

            habFlight->add(item.c_str(), (int) 0, flight_choice_callback,
                           userdata);
        }

        string browser_item = flight_browser_item(name, date, payload_list);
        flight_browser->add(browser_item.c_str(), userdata);

        if (progdefaults.tracking_flight == id)
        {
            if (hab_ui_exists)
                habFlight->value(habFlight->size() - 2);
            flight_browser->value(flight_browser->size());
            select_flight(i);
        }
    }
}

static string squash_string(const char *str)
{
    string result;
    result.reserve(strlen(str));

    while (*str)
    {
        char c = *str;
        if (isalnum(c))
            result.push_back(tolower(c));
        str++;
    }

    return result;
}

void flight_search(bool next)
{
    /* Searching the flight_browser rather than looking through our JSON docs?
     * Well: it's easier in several ways, and quicker. We've already extracted
     * the important strings in populate_flights and filtered for testing
     * flights; that would have to be duplicated. */

    Fl_AutoLock lock;

    const string search(squash_string(flight_search_text->value()));
    if (!search.size())
        return;

    int n = flight_browser->size();

    if (!next || flight_search_first > n)
        flight_search_first = 1;

    int i = flight_search_first;

    do
    {
        const string line(squash_string(flight_browser->text(i)));

        if (line.find(search) != string::npos)
        {
            flight_browser->value(i);
            flight_browser->do_callback();
            flight_search_first = i + 1;
            break;
        }

        i++;
        if (i > n)
            i = 1;
    }
    while (i != flight_search_first);
}

static void payload_choice_callback(Fl_Widget *w, void *a)
{
    Fl_Choice *choice = static_cast<Fl_Choice *>(w);
    Fl_Choice *other = static_cast<Fl_Choice *>(a);

    if (other)
        other->value(choice->value());
    select_payload(choice->value());
}

void select_flight(int index)
{
    Fl_AutoLock lock;

    LOG_DEBUG("Selecting flight, index %i", index);

    cur_flight = NULL;

    if (hab_ui_exists)
    {
        habCHPayload->value(-1);
        habCHPayload->clear();
        habCHPayload->deactivate();
    }

    payload_list->value(-1);
    payload_list->clear();
    payload_list->deactivate();

    select_payload(-1);

    int max = flight_docs.size();
    if (index < 0 || index >= max)
        return;

    const Json::Value &flight = flight_docs[index];

    if (!flight.isObject() || !flight.size() || !flight["_id"].isString())
        return;

    const string id = flight["_id"].asString();
    const Json::Value &payloads = flight["payloads"];

    if (!id.size() || !payloads.isObject() || !payloads.size())
        return;

    cur_flight = &flight;

    if (progdefaults.tracking_flight != id)
    {
        progdefaults.tracking_flight = id;
        progdefaults.tracking_payload = "";
        progdefaults.tracking_mode = -1;
        progdefaults.changed = true;
    }

    payload_index = payloads.getMemberNames();
    int auto_select = 0;

    vector<string>::const_iterator it;
    int i;
    for (it = payload_index.begin(), i = 0;
         it != payload_index.end();
         it++, i++)
    {
        if (hab_ui_exists)
            habCHPayload->add(escape_menu_string(*it).c_str(), (int) 0,
                              payload_choice_callback, payload_list);
        payload_list->add(escape_menu_string(*it).c_str(), (int) 0,
                          payload_choice_callback, habCHPayload);

        if ((*it).size() && (*it) == progdefaults.tracking_payload)
            auto_select = i;
    }

    if (hab_ui_exists)
    {
        habCHPayload->activate();
        habCHPayload->value(auto_select);
    }

    payload_list->activate();
    payload_list->value(auto_select);

    select_payload(auto_select);
}

static string mode_menu_name(int index, const Json::Value &settings)
{
    /* index: modulation type
     * e.g.:  2: RTTY 300 */

    ostringstream name;

    name << (index + 1) << ": ";

    if (false)
    {
bad:
        name << "Unknown";
        return escape_menu_string(name.str());
    }

    if (!settings.isObject() || !settings.size())
        goto bad;

    if (!settings["modulation"].isString())
        goto bad;

    string modulation = settings["modulation"].asString();
    string type_key;

    if (modulation == "rtty")
    {
        modulation = "RTTY";
        type_key = "baud";
    }
    else if (modulation == "dominoex")
    {
        modulation = "DOMEX";
        type_key = "type";
    }
    else
    {
        goto bad;
    }

    name << modulation;

    const Json::Value &type = settings[type_key];
    if (type.isInt())
        name << " " << type.asInt();
    else if (type.isDouble())
        name << " " << type.asDouble();

    return escape_menu_string(name.str());
}

static void mode_choice_callback(Fl_Widget *w, void *a)
{
    Fl_Choice *choice = static_cast<Fl_Choice *>(w);
    Fl_Choice *other = static_cast<Fl_Choice *>(a);

    if (other)
        other->value(choice->value());
    select_mode(choice->value());
}

static void select_payload(int index)
{
    Fl_AutoLock lock;
    LOG_DEBUG("Selecting payload %i", index);

    cur_payload = NULL;
    extrmgr->payload(NULL);
    cur_payload_modecount = -1;

    if (hab_ui_exists)
    {
        habCHMode->value(-1);
        habCHMode->clear();
        habCHMode->deactivate();
        habSwitchModes->deactivate();
    }

    payload_mode_list->value(-1);
    payload_mode_list->clear();
    payload_mode_list->deactivate();

    select_mode(-1);

    if (!cur_flight)
        return;

    int max = payload_index.size();
    if (index < 0 || index >= max)
        return;

    const string name(payload_index[index]);
    if (progdefaults.tracking_payload != name)
    {
        progdefaults.tracking_payload = name;
        progdefaults.tracking_mode = -1;
        progdefaults.changed = true;
    }

    if (!name.size())
        return;

    const Json::Value &payloads = (*cur_flight)["payloads"];
    if (!payloads.isObject() || !payloads.size())
        return;

    const Json::Value &payload = payloads[name];
    if (!payload.isObject() || !payload.size())
        return;

    cur_payload = &payload;
    extrmgr->payload(&payload);

    const Json::Value *telemetry_settings = &(payload["telemetry"]);

    if (!telemetry_settings->size())
        return;

    Json::Value temporary(Json::arrayValue);

    /* If telemetry_settings is an array, it's a list of possible choices.
     * Otherwise, it's the only choice. */
    if (telemetry_settings->isObject())
    {
        temporary.append(*telemetry_settings);
        telemetry_settings = &temporary;
    }

    if (!telemetry_settings->isArray())
        return;

    Json::Value::const_iterator it;
    int i;
    for (it = telemetry_settings->begin(), i = 0;
         it != telemetry_settings->end();
         it++, i++)
    {
        const string name = mode_menu_name(i, (*it));

        if (hab_ui_exists)
            habCHMode->add(name.c_str(), (int) 0,
                           mode_choice_callback, payload_mode_list);
        payload_mode_list->add(name.c_str(), (int) 0,
                               mode_choice_callback, habCHMode);
    }

    cur_payload_modecount = i; /* i == telemetry_settings.size() */

    int auto_select = progdefaults.tracking_mode;
    if (auto_select < 0 || auto_select >= i)
    {
        auto_select = 0;
    }

    if (hab_ui_exists)
    {
        if (i > 1)
            habSwitchModes->activate();

        habCHMode->activate();
        habCHMode->value(auto_select);
    }

    payload_mode_list->activate();
    payload_mode_list->value(auto_select);

    select_mode(auto_select);
}

static void select_mode(int index)
{
    Fl_AutoLock lock;

    cur_mode = NULL;
    cur_mode_index = -1;

    if (hab_ui_exists)
        habConfigureButton->deactivate();

    payload_autoconfigure->deactivate();

    if (!cur_flight || !cur_payload)
        return;

    const Json::Value &telemetry_settings = (*cur_payload)["telemetry"];

    if (!telemetry_settings.size())
        return;

    if (telemetry_settings.isObject())
    {
        if (index != 0)
            return;

        cur_mode = &telemetry_settings;
    }
    else if (telemetry_settings.isArray())
    {
        int max = telemetry_settings.size();
        if (index < 0 || index >= max)
            return;

        cur_mode = &(telemetry_settings[index]);
    }

    if (!cur_mode->isObject() || !cur_mode->size())
    {
        cur_mode = NULL;
        return;
    }

    cur_mode_index = index;

    if (progdefaults.tracking_mode != index)
    {
        progdefaults.tracking_mode = index;
        progdefaults.changed = true;
    }

    if (hab_ui_exists)
        habConfigureButton->activate();

    payload_autoconfigure->activate();
}

void auto_configure()
{
    Fl_AutoLock lock;

    if (!cur_mode)
        return;

    const Json::Value &settings = *cur_mode;

    if (!settings.isObject() || !settings.size())
        return;

    if (!settings["modulation"].isString())
        return;

    const string modulation = settings["modulation"].asString();

    if (modulation == "rtty")
    {
        bool configured_something = false;

        /* Shift */
        if (settings["shift"].isNumeric())
        {
            double shift = settings["shift"].asDouble();

            /* Look in the standard shifts first */
            int search;
            for (search = 0; rtty::SHIFT[search] != 0; search++)
            {
                double diff = rtty::SHIFT[search] - shift;
                /* I love floats :-( */
                if (diff < 0.1 && diff > -0.1)
                {
                    selShift->value(search);
                    selCustomShift->deactivate();
                    progdefaults.rtty_shift = search;
                    break;
                }
            }

            /* If not found (i.e., we found the terminating 0) then
             * search == the index of the "Custom" menu item */
            if (rtty::SHIFT[search] == 0)
            {
                selShift->value(search);
                selCustomShift->activate();
                progdefaults.rtty_shift = -1;
                selCustomShift->value(shift);
                progdefaults.rtty_custom_shift = shift;
            }

            configured_something = true;
        }

        /* Baud */
        if (settings["baud"].isNumeric())
        {
            double baud = settings["baud"].asDouble();
            int search;
            for (search = 0; rtty::BAUD[search] != 0; search++)
            {
                double diff = rtty::BAUD[search] - baud;
                if (diff < 0.01 && diff > -0.01)
                {
                    selBaud->value(search);
                    progdefaults.rtty_baud = search;
                    configured_something = true;
                    break;
                }
            }
        }

        /* Encoding */
        if (settings["encoding"].isString())
        {
            const string encoding = settings["encoding"].asString();
            int select = -1;

            /* rtty::BITS[] = {5, 7, 8}; */
            if (encoding == "baudot")
                select = 0;
            else if (encoding == "ascii-7")
                select = 1;
            else if (encoding == "ascii-8")
                select = 2;

            if (select != -1)
            {
                selBits->value(select);
                progdefaults.rtty_bits = select;

                /* From selBits' callback */
                if (select == 0)
                {
                    progdefaults.rtty_parity = RTTY_PARITY_NONE;
                    selParity->value(RTTY_PARITY_NONE);
                }

                configured_something = true;
            }
        }

        /* Parity: "none|even|odd|zero|one". Also, parity is disabled when
         * using baudot (rtty_bits == 0) */
        if (settings["parity"].isString() && progdefaults.rtty_bits != 0)
        {
            const string parity = settings["parity"].asString();
            int select = -1;

            if (parity == "none")
                select = RTTY_PARITY_NONE;
            else if (parity == "even")
                select = RTTY_PARITY_EVEN;
            else if (parity == "odd")
                select = RTTY_PARITY_ODD;
            else if (parity == "zero")
                select = RTTY_PARITY_ZERO;
            else if (parity == "one")
                select = RTTY_PARITY_ONE;

            if (select != -1)
            {
                selParity->value(select);
                progdefaults.rtty_parity = select;

                configured_something = true;
            }
        }

        /* Stop: "1|1.5|2". */
        if (settings["stop"].isNumeric())
        {
            int select = -1;

            if (settings["stop"].isInt())
            {
                int stop = settings["stop"].asInt();
                if (stop == 1)
                    select = 0;
                else if (stop == 2)
                    select = 2;
            }
            else
            {
                double stop = settings["stop"].asDouble();
                if (stop > 1.49 && stop < 1.51)
                    select = 1;
            }

            if (select != -1)
            {
                progdefaults.rtty_stop = select;
                selStopBits->value(select);

                configured_something = true;
            }
        }

        /* Finish up... */
        if (configured_something)
        {
            init_modem_sync(MODE_RTTY);
            resetRTTY();
            progdefaults.changed = true;
        }
    }
    else if (modulation == "dominoex")
    {
        if (settings["type"].isInt())
        {
            int type = settings["type"].asInt();
            int modem = -1;

            switch (type)
            {
                case 4:
                    modem = MODE_DOMINOEX4;
                    break;

                case 5:
                    modem = MODE_DOMINOEX5;
                    break;

                case 8:
                    modem = MODE_DOMINOEX8;
                    break;

                case 11:
                    modem = MODE_DOMINOEX11;
                    break;

                case 16:
                    modem = MODE_DOMINOEX16;
                    break;

                case 22:
                    modem = MODE_DOMINOEX22;
                    break;
            }

            if (modem != -1)
            {
                init_modem_sync(modem);
                resetDOMEX();
            }
        }
    }
}

void auto_switchmode()
{
    Fl_AutoLock lock;

    int next = cur_mode_index + 1;
    if (next >= cur_payload_modecount)
        next = 0;

    if (hab_ui_exists)
        habCHMode->value(next);
    payload_mode_list->value(next);

    select_mode(next);
    auto_configure();
}

static void reset_gps_settings()
{
    if (gps_thread != NULL)
    {
        gps_thread->shutdown();

        /* We will have to wait for the current thread to shutdown before
         * we can start it up again. thread_death will call this function. */
        return;
    }

    if (gps_thread == NULL &&
        current_location_mode == LOC_GPS &&
        progdefaults.gps_device.size() && progdefaults.gps_speed)
    {
        gps_thread = new GPSThread(progdefaults.gps_device,
                                   progdefaults.gps_speed);
        gps_thread->start();
    }
}

void changed(enum changed_groups thing)
{
    Fl_AutoLock lock;
    dirty |= thing;
}

void commit()
{
    Fl_AutoLock lock;

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
    Fl_AutoLock lock;

    LOG_DEBUG("hbtE %s", msg.c_str());

    /* Don't overwrite UploaderThread's warnings */
    if (time(NULL) - last_warn > 10)
        put_status_safe(msg.c_str());
}

static void set_jvalue(Fl_Output *widget, const Json::Value &value)
{
    /* String and null are easy. The UKHAS crude parser leaves everything as
     * a string anyway (even lat/long, no floats!), to make it easier. */
    if (value.isString())
        widget->value(value.asCString());
    else if (value.isNull())
        widget->value("");
    else
        widget->value(value.toStyledString().c_str());
}

void DExtractorManager::data(const Json::Value &d)
{
    Fl_AutoLock lock;

    if (!hab_ui_exists)
        return;

    if (d["_sentence"].isString())
    {
        string clean = d["_sentence"].asString();

        /* the \n shows up badly. remove it */
        int last = clean.size() - 1;
        if (last >= 0 && clean[last] == '\n')
            clean[last] = '\0';

        habString->value(clean.c_str());
        if (d["_parsed"].isBool() && d["_parsed"].asBool())
            habString->color(FL_GREEN);
        else
            habString->color(FL_RED);
    }
    else
    {
        habString->value("");
        habString->color(FL_WHITE);
    }

    if (d["_sentence"].isNull())
    {
        habString->color(FL_WHITE);
        habChecksum->value("");
    }
    else if (d["_parsed"].isBool() && d["_parsed"].asBool())
    {
        habString->color(FL_GREEN);
        habChecksum->value("GOOD :-)");
    }
    else
    {
        habString->color(FL_RED);
        habChecksum->value("BAD :-(");
    }

    /* UKHAS crude parser doesn't split up the time, like the real one does */
    set_jvalue(habRXPayload, d["payload"]);
    set_jvalue(habTime, d["time"]);
    set_jvalue(habLat, d["latitude"]);
    set_jvalue(habLon, d["longitude"]);
    set_jvalue(habAlt, d["altitude"]);
    habTimeSinceLastRx->value("just now");
    last_rx = time(NULL);

    if (d["latitude"].isString() && d["longitude"].isString())
    {
        istringstream lat_strm(d["latitude"].asString());
        istringstream lon_strm(d["longitude"].asString());
        lat_strm >> balloon_latitude;
        lon_strm >> balloon_longitude;
        balloon_valid = !lat_strm.fail() && !lon_strm.fail();
    }
    else
    {
        balloon_valid = false;
    }

    update_distance_bearing();
}

static void update_distance_bearing()
{
    Fl_AutoLock lock;

    if (!hab_ui_exists)
        return;

    if (!listener_valid || !balloon_valid)
    {
        habDistance->value("");
        habBearing->value("");
        return;
    }

    /* Convert everything to radians */
    double c = M_PI/180;

    double lat1, lon1, lat2, lon2;
    lat1 = listener_latitude * c;
    lon1 = listener_longitude * c;
    lat2 = balloon_latitude * c;
    lon2 = balloon_longitude * c;

    double d_lat, d_lon;
    d_lat = lat2 - lat1;
    d_lon = lon2 - lon1;

    /* haversine formula */
    double p = sin(d_lat / 2);
    p *= p;
    double q = sin(d_lon / 2);
    q *= q;
    double a = p + cos(lat1) * cos(lat2) * q;

    double t = atan2(sqrt(a), sqrt(1 - a)) * 2;
    /* 6371 = approx radius of earth in km */
    double distance = t * 6371;

    double y = sin(d_lon) * cos(lat2);
    double x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(d_lon);
    double bearing = atan2(y, x);

    /* back to degrees */
    bearing *= (180/M_PI);

    ostringstream str_distance;
    str_distance.precision(4);
    str_distance << distance << "km";

    if (bearing < 0)
        bearing += 360;

    ostringstream str_bearing;
    str_bearing.setf(ios::fixed, ios::floatfield);
    str_bearing.precision(1);
    str_bearing.fill('0');
    str_bearing.width(3 + 1 + 1);
    str_bearing << bearing;

    habDistance->value(str_distance.str().c_str());
    habBearing->value(str_bearing.str().c_str());
}

/* Modify run() to help us shutdown the thread */
void *DUploaderThread::run()
{
    void *ret = UploaderThread::run();
    Fl::awake(thread_death, this);
    return ret;
}

/* All these functions are called via a DUploaderThread pointer so
 * the fact that they are non virtual is OK. Having a different set of
 * arguments even prevents the wrong function from being selected */

void DUploaderThread::settings()
{
    Fl_AutoLock lock;

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

/* This function is used for stationary listener telemetry only */
void DUploaderThread::listener_telemetry()
{
    Fl_AutoLock lock;

    if (current_location_mode != LOC_STATIONARY)
    {
        warning("attempted to upload stationary listener "
                "telemetry while in GPS telemetry mode");
        return;
    }

    listener_valid = false;
    update_distance_bearing();

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

    /* TODO: HABITAT is it really a good idea to upload time like this? */
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

    listener_latitude = latitude;
    listener_longitude = longitude;
    listener_valid = true;
    update_distance_bearing();

    UploaderThread::listener_telemetry(data);
}

void DUploaderThread::listener_telemetry(const Json::Value &data)
{
    Fl_AutoLock lock;

    if (current_location_mode != LOC_GPS)
        throw runtime_error("Attempted to upload GPS data while not "
                            "in GPS mode");

    UploaderThread::listener_telemetry(data);
}

static void info_add(Json::Value &data, const string &key, const string &value)
{
    if (value.size())
        data[key] = value;
}

void DUploaderThread::listener_info()
{
    Fl_AutoLock lock;

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

    data["dl_fldigi"] = git_short_commit;
    UploaderThread::listener_info(data);
}

/* These functions absolutely must be thread safe. */
void DUploaderThread::log(const string &message)
{
    Fl_AutoLock lock;
    LOG_DEBUG("hbtUT %s", message.c_str());
}

void DUploaderThread::warning(const string &message)
{
    Fl_AutoLock lock;
    LOG_WARN("hbtUT %s", message.c_str());

    string temp = "WARNING " + message;
    put_status_safe(temp.c_str(), 10);
    last_warn = time(NULL);
}

void DUploaderThread::saved_id(const string &type, const string &id)
{
    /* Log as normal, but also set status */
    UploaderThread::saved_id(type, id);

    /* but don't overwrite a warning */
    if (time(NULL) - last_warn > 10)
    {
        string message = "Uploaded " + type + " successfully";
        put_status_safe(message.c_str(), 10);
    }
}

void DUploaderThread::got_flights(const vector<Json::Value> &new_flight_docs)
{
    Fl_AutoLock lock;

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
}

/* On Windows we have to wait for a timeout to wake the GPS thread up
 * to terminate it. This sucks. But on systems that support signals,
 * it will cut short the last timeout */
#ifndef __MINGW32__
void GPSThread::prepare_signals()
{
    sigset_t usr2;

    sigemptyset(&usr2);
    sigaddset(&usr2, SIGUSR2);
    pthread_sigmask(SIG_UNBLOCK, &usr2, NULL);
}

void GPSThread::send_signal()
{
    pthread_kill(thread, SIGUSR2);
}
#else
void GPSThread::prepare_signals() {}
void GPSThread::send_signal()
{
    /* TODO: HABITAT does this work as expected? */
    pthread_cancel(thread);
}
#endif

void GPSThread::wait()
{
    /* On error. Wait for 1, 2, 4... 64 seconds */
    sleep(1 << wait_exp);

    if (wait_exp < 6)
        wait_exp++;
}

void GPSThread::shutdown()
{
    set_term();
    send_signal();
}

void GPSThread::set_term()
{
    EZ::MutexLock lock(mutex);
    term = true;
}

bool GPSThread::check_term()
{
    bool b;
    EZ::MutexLock lock(mutex);
    b = term;
    if (b)
        log("term = true");
    return b;
}

void *GPSThread::run()
{
    prepare_signals();

    while (!check_term())
    {
        try
        {
            setup();
            log("Opened device " + device);
            while (!check_term())
            {
                read();
                /* Success? reset wait */
                wait_exp = 0;
            }
        }
        catch (runtime_error e)
        {
            warning(e.what());
            cleanup();
            wait();
        }
    }

    Fl::awake(thread_death, this);
    return NULL;
}

void GPSThread::warning(const string &message)
{
    Fl_AutoLock lock;
    LOG_WARN("hbtGPS %s", message.c_str());

    string temp = "WARNING GPS Error " + message;
    put_status_safe(temp.c_str(), 10);
    last_warn = time(NULL);
}

void GPSThread::log(const string &message)
{
    Fl_AutoLock lock;
    LOG_DEBUG("hbtGPS %s", message.c_str());
}

static bool check_gpgga(const vector<string> &parts)
{
    if (parts.size() < 7)
        return false;

    if (parts[0] != "$GPGGA")
        return false;

    /* Fix quality field */
    if (parts[6] == "0")
        return false;

    for (int i = 1; i < 7; i++)
        if (!parts[i].size())
            return false;

    return true;
}

static void split_nmea(const string &data, vector<string> &parts)
{
    size_t a = 0, b = 0;

    while (b != string::npos)
    {
        string part;
        b = data.find_first_of(",*", a);

        if (b == string::npos)
            part = data.substr(a);
        else
            part = data.substr(a, b - a);

        parts.push_back(part);
        a = b + 1;
    }
}

static double parse_ddm(string part, const string &dirpart)
{
    double degrees, mins;
    size_t pos = part.find('.');
    if (pos == string::npos || pos < 3)
        throw runtime_error("Bad DDM");

    /* Split degrees and minutes parts */
    part.insert(pos - 2, " ");

    istringstream tmp(part);
    tmp.exceptions(istringstream::failbit | istringstream::badbit);

    tmp >> degrees;
    tmp >> mins;

    double value = degrees + mins / 60;

    if (dirpart == "S" || dirpart == "W")
        return -value;
    else
        return value;
}

static void parse_hms(string part, int &hour, int &minute, int &second)
{
    /* Split HH MM SS with spaces */
    part.insert(6, " ");
    part.insert(4, " ");
    part.insert(2, " ");

    istringstream tmp(part);
    tmp.exceptions(istringstream::failbit | istringstream::badbit);

    tmp >> hour;
    tmp >> minute;
    tmp >> second;
}

static double parse_alt(const string &part, const string &unit_part)
{
    if (unit_part != "M")
        throw runtime_error("altitude units are not M");

    istringstream tmp;
    double value;

    tmp.exceptions(istringstream::failbit | istringstream::badbit);
    tmp.str(part);
    tmp >> value;

    return value;
}

void GPSThread::read()
{
    /* Read until a newline */
    char buf[100];
    char *result;
    result = fgets(buf, sizeof(buf), f);

    if (result != buf)
        throw runtime_error("fgets read no data: EOF or error");

    /* Find the $ (i.e., discard garbage before the $) */
    char *start = strchr(buf, '$');

    if (!start)
        throw runtime_error("Did not find start delimiter");

    string data(start);
    data.erase(data.end() - 1);

    log("Read line: " + data);

    vector<string> parts;
    split_nmea(data, parts);

    if (!check_gpgga(parts))
        return;

    int hour, minute, second;
    double latitude, longitude, altitude;

    try
    {
        parse_hms(parts[1], hour, minute, second);
        latitude = parse_ddm(parts[2], parts[3]);
        longitude = parse_ddm(parts[4], parts[5]);
        altitude = parse_alt(parts[9], parts[10]);
    }
    catch (out_of_range e)
    {
        throw runtime_error("Failed to parse data (oor)");
    }
    catch (istringstream::failure e)
    {
        throw runtime_error("Failed to parse data (fail)");
    }

    upload(hour, minute, second, latitude, longitude, altitude);
}

void GPSThread::upload(int hour, int minute, int second,
                       double latitude, double longitude, double altitude)
{
    Fl_AutoLock lock;

    /* Data OK? upload. */
    if (current_location_mode != LOC_GPS)
        throw runtime_error("GPS mode disabled mid-line");

    listener_valid = true;
    listener_latitude = latitude;
    listener_longitude = longitude;
    update_distance_bearing();

    Json::Value data(Json::objectValue);
    data["time"] = Json::Value(Json::objectValue);
    Json::Value &time = data["time"];
    time["hour"] = hour;
    time["minute"] = minute;
    time["second"] = second;

    data["latitude"] = latitude;
    data["longitude"] = longitude;
    data["altitude"] = altitude;

    uthr->listener_telemetry(data);
}

/* The open() functions for both platforms were originally written by
 * Robert Harrison */
#ifndef __MINGW32__
void GPSThread::setup()
{
    /* Open the serial port without blocking. Rely on cleanup() */
    fd = open(device.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd == -1)
        throw runtime_error("open() failed");

    f = fdopen(fd, "r");
    if (f == NULL)
        throw runtime_error("fdopen() failed");

    /* Linux requires baudrates be given as a constant */
    speed_t baudrate = B4800;
    if (baud == 9600)           baudrate = B9600;
    else if (baud == 19200)     baudrate = B19200;
    else if (baud == 38400)     baudrate = B38400;
    else if (baud == 57600)     baudrate = B57600;
    else if (baud == 115200)    baudrate = B115200;
    else if (baud == 230400)    baudrate = B230400;

    /* Set all the weird arcane settings Linux demands (boils down to 8N1) */
    struct termios port_settings;
    memset(&port_settings, 0, sizeof(port_settings));

    cfsetispeed(&port_settings, baudrate);
    cfsetospeed(&port_settings, baudrate);

    /* Enable the reciever and set local */
    port_settings.c_cflag |= (CLOCAL | CREAD);

    /* Set 8N1 */
    port_settings.c_cflag &= ~PARENB;
    port_settings.c_cflag &= ~CSTOPB;
    port_settings.c_cflag &= ~CSIZE;
    port_settings.c_cflag |= CS8;

    /* Set raw input output */
    port_settings.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    port_settings.c_oflag &= ~OPOST;

    /* Ignore CR in NMEA's CRLF */
    port_settings.c_iflag |= (IGNCR);

    /* Blocking read until 1 character arrives */
    port_settings.c_cc[VMIN] = 1;

    /* Re enable blocking for reading. */
    int set = fcntl(fd, F_SETFL, 0);
    if (set == -1)
        throw runtime_error("fcntl() failed");

    /* All baud settings */
    set = tcsetattr(fd, TCSANOW, &port_settings);
    if (set == -1)
        throw runtime_error("tcsetattr() failed");
}

void GPSThread::cleanup()
{
    /* The various things will close their underlying fds or handles (w32).
     * Close the last thing we managed to open */
    if (f)
        fclose(f);
    else if (fd != -1)
        close(fd);

    f = NULL;
    fd = -1;
}
#else
void GPSThread::setup()
{
    HANDLE handle = CreateFile(device, GENERIC_READ, 0, 0,
                               OPEN_EXISTING, 0, 0);
    if (handle == INVALID_HANDLE_VALUE)
        throw runtime_error("CreateFile() failed");

    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(handle, &dcbSerialParams))
        throw runtime_error("GetCommState() failed");

    dcbSerialParams.BaudRate = baud;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(handle, &dcbSerialParams))
        throw runtime_error("GetCommState() failed");

    COMMTIMEOUTS timeouts;
    timeouts.ReadIntervalTimeout            = 2000;
    timeouts.ReadTotalTimeoutMultiplier     = 0;
    timeouts.ReadTotalTimeoutConstant       = 5000;
    timeouts.WriteTotalTimeoutMultiplier    = 0;
    timeouts.WriteTotalTimeoutConstant      = 0;

    if (!SetCommTimeouts(handle, &timeouts))
        throw runtime_error("SetCommTimeouts() failed");

    fd = _open_osfhandle((intptr_t) serial_port_handle, _O_RDONLY);
    if (fd == -1)
        throw runtime_error("_open_osfhandle() failed");

    f = fdopen(fd, "r");
    if (!f)
        throw runtime_error("fdopen() failed");
}

void GPSThread::cleanup()
{
    if (f)
        fclose(f);
    else if (fd != -1)
        close(fd);
    else if (handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);

    f = NULL;
    fd = -1;
    handle = INVALID_HANDLE_VALUE;
}
#endif

} /* namespace dl_fldigi */
