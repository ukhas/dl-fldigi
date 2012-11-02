/* 
 * Copyright (C) 2011 Daniel Richman
 * License: GNU GPL 3
 *
 * flights.cxx: flight and payload document management, GUI and autoconfig.
 */

#include "dl_fldigi/flights.h"

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <set>
#include <unistd.h>

#include "main.h"
#include "debug.h"
#include "fl_digi.h"
#include "configuration.h"
#include "confdialog.h"

#include "jsoncpp.h"
#include "habitat/RFC3339.h"
#include "dl_fldigi/dl_fldigi.h"
#include "dl_fldigi/hbtint.h"

using namespace std;

namespace dl_fldigi {
namespace flights {

bool downloaded_flights_once, downloaded_payloads_once;

static string flight_cache_file, payload_cache_file;
static vector<Json::Value> flight_docs, payload_docs;

/* These pointers just point at some part of the heap allocated by something
 * in either the flight_docs vector (if cur_heap == TRACKING_FLIGHT) or 
 * the payload_docs vector (if cur_heap == TRACKING_PAYLOAD).
 * They're invalidated when the relevant vector is modified. When new data is
 * downloaded, the relvant populate_{flights,payloads} function will update
 * these if necessary.
 * hbtint::extrmgr->payload should be called when cur_payload is updated.
 * The data pointed to must not be modified at all while extrmgr has a
 * pointer to it. populate_*'s cleanup actions remove it before modifying. */
static const Json::Value *cur_flight, *cur_payload, *cur_transmission;
static int cur_transmission_index, cur_transmission_count;
static int payload_search_first = 1;
/* managed by select_flight and select_payload. Checked by
 * select_flight_payload, populate_flights and populate_payloads */
static enum tracking_type_enum cur_heap = TRACKING_NOTHING;

static void load_cache_file(const string &name, vector<Json::Value> &target);
static void write_cache_file(const string &name,
                             const vector<Json::Value> &docs);

/* Note: these functions, in the menus they populate, store the index of the
 * Json::Value in the array it's contained in as the userdata of the item,
 * cast (int) -> (void *) */
static void populate_flights();
static void populate_payloads();

static void select_flight_payload(int index);
static void do_select_payload(const Json::Value &payload);
static void select_transmission(int index);

static void autoconfigure_rtty(const Json::Value &settings);
static void autoconfigure_rtty_shift(const Json::Value &value);
static void autoconfigure_rtty_baud(const Json::Value &value);
static void autoconfigure_rtty_encoding(const Json::Value &value);
static void autoconfigure_rtty_parity(const Json::Value &value);
static void autoconfigure_rtty_stop(const Json::Value &value);
static void autoconfigure_dominoex(const Json::Value &settings);
static void autoconfigure_hellschreiber(const Json::Value &settings);

static string escape_menu_string(const string &s_);
static string escape_browser_string(const string &s_);
static string squash_string(const char *str);
static string join_set(const set<string> &items, const string &sep=", ");

static string flight_choice_item(const string &name,
                                 const string &callsign_list,
                                 int attempt);
static string flight_browser_item(const string &name, const string &date,
                                  const string &callsign_list);
static string payload_browser_item(const string &name,
                                   const string &callsign_list,
                                   const string &description);
static string flight_payload_menu_item(const string &name,
                                       const string &callsign_list,
                                       int attempt);
static string mode_menu_name(int index, const Json::Value &settings);

static string flight_callsign_list(const Json::Value &flight);
static string flight_launch_date(const Json::Value &flight);
static string payload_callsign_list(const Json::Value &payload);

static void flight_choice_callback(Fl_Widget *w, void *a);
static void flight_payload_choice_callback(Fl_Widget *w, void *a);
static void mode_choice_callback(Fl_Widget *w, void *a);

void init()
{
    /* called with Fl lock acquired */

    flight_cache_file = HomeDir + "flight_docs.json";
    payload_cache_file = HomeDir + "payload_configuration_docs.json";
}

void cleanup()
{
    /* called with Fl lock acquired */

    flight_docs.clear();
    payload_docs.clear();
    cur_flight = NULL;
    cur_payload = NULL;
    cur_transmission = NULL;
    cur_transmission_index = 0;
    cur_transmission_count = 0;
    cur_heap = TRACKING_NOTHING;
}

void load_cache()
{
    /* resets everything */
    select_flight(-1);

    /* called with Fl lock acquired */

    load_cache_file(flight_cache_file, flight_docs);
    load_cache_file(payload_cache_file, payload_docs);

    populate_flights();
    populate_payloads();
}

void new_flight_docs(const vector<Json::Value> &new_flights)
{
    Fl_AutoLock lock;
    flight_docs = new_flights;
    downloaded_flights_once = true;
    write_cache_file(flight_cache_file, flight_docs);
    populate_flights();
}

void new_payload_docs(const vector<Json::Value> &new_payloads)
{
    Fl_AutoLock lock;
    payload_docs = new_payloads;
    downloaded_payloads_once = true;
    write_cache_file(payload_cache_file, payload_docs);
    populate_payloads();
}

void payload_search(bool next)
{
    /* Searching the payload_browser rather than looking through our JSON docs?
     * Well: it's easier in several ways, and quicker. We've already extracted
     * the important strings in populate_flights and filtered for testing
     * flights; that would have to be duplicated. */

    Fl_AutoLock lock;

    const string search(squash_string(payload_search_text->value()));
    if (!search.size())
        return;

    int n = payload_browser->size();

    if (!n)
        return;

    if (!next || payload_search_first > n)
        payload_search_first = 1;

    int i = payload_search_first;

    do
    {
        const string line(squash_string(payload_browser->text(i)));

        if (line.find(search) != string::npos)
        {
            payload_browser->value(i);
            select_payload(i - 1);

            payload_search_first = i + 1;
            break;
        }

        i++;
        if (i > n)
            i = 1;
    }
    while (i != payload_search_first);
}

void select_flight(int index)
{
    Fl_AutoLock lock;

    LOG_DEBUG("Selecting flight, index %i", index);

    /* Reset */
    cur_flight = NULL;
    cur_heap = TRACKING_NOTHING;

    if (hab_ui_exists)
    {
        habCHPayload->value(-1);
        habCHPayload->clear();
        habCHPayload->deactivate();
    }

    flight_payload_list->value(-1);
    flight_payload_list->clear();
    flight_payload_list->deactivate();

    do_select_payload(Json::Value::null);

    /* Tests */
    if (index < 0 || index >= int(flight_docs.size()))
        return;

    const Json::Value &flight = flight_docs[index];

    if (!flight.isObject() || !flight.size() || !flight["_id"].isString())
        return;

    const string id = flight["_id"].asString();
    const Json::Value &payloads = flight["_payload_docs"];

    if (!id.size() || !payloads.isArray() || !payloads.size())
        return;

    LOG_DEBUG("flight OK. contains %i payloads", payloads.size());

    /* Doc looks ok, so set up */
    cur_flight = &flight;
    cur_heap = TRACKING_FLIGHT;

    if (progdefaults.tracking_type != TRACKING_FLIGHT || 
        progdefaults.tracking_doc != id)
    {
        progdefaults.tracking_type = TRACKING_FLIGHT;
        progdefaults.tracking_doc = id;
        progdefaults.tracking_flight_payload = -1;
        progdefaults.tracking_transmission = -1;
        progdefaults.changed = true;
    }

    set<string> choice_items;

    for (Json::Value::const_iterator it = payloads.begin();
            it != payloads.end(); ++it)
    {
        const Json::Value &root = *it;
        string name, callsigns;

        if (root.isObject() && root["name"].isString())
        {
            name = root["name"].asString();
            callsigns = payload_callsign_list(root);
        }
        else
        {
            name = "Unknown";
        }

        int attempt = 1;
        string item;

        do
        {
            item = flight_payload_menu_item(name, callsigns, attempt);
            attempt++;
        }
        while(choice_items.count(item));
        choice_items.insert(item);

        if (hab_ui_exists)
            habCHPayload->add(item.c_str(), (int) 0,
                              flight_payload_choice_callback, NULL);
        flight_payload_list->add(item.c_str(), (int) 0,
                                 flight_payload_choice_callback, NULL);
    }

    int auto_select = progdefaults.tracking_flight_payload;
    if (auto_select < 0 || auto_select >= int(payloads.size()))
        auto_select = 0;

    if (hab_ui_exists)
    {
        habCHPayload->activate();
        habCHPayload->value(auto_select);
    }

    flight_payload_list->activate();
    flight_payload_list->value(auto_select);

    select_flight_payload(auto_select);
}

void select_payload(int index)
{
    select_flight(-1);
    do_select_payload(Json::Value::null);

    cur_heap = TRACKING_NOTHING;

    LOG_DEBUG("Selecting payload, index %i", index);

    if (index < 0 || index >= int(payload_docs.size()))
        return;

    const Json::Value &payload = payload_docs[index];

    if (!payload.isObject() || !payload.size() || !payload["_id"].isString())
        return;

    const string id = payload["_id"].asString();

    if (progdefaults.tracking_type != TRACKING_PAYLOAD ||
        progdefaults.tracking_doc != id)
    {
        progdefaults.tracking_type = TRACKING_PAYLOAD;
        progdefaults.tracking_doc = id;
        progdefaults.tracking_flight_payload = -1;
        progdefaults.tracking_transmission = -1;
        progdefaults.changed = true;
    }

    cur_heap = TRACKING_PAYLOAD;
    do_select_payload(payload);
}

void auto_configure()
{
    Fl_AutoLock lock;

    LOG_DEBUG("autoconfiguring");

    if (!cur_transmission)
        return;

    const Json::Value &settings = *cur_transmission;

    if (!settings.isObject() || !settings.size())
        return;

    if (!settings["modulation"].isString())
        return;

    const string modulation = settings["modulation"].asString();

    if (modulation == "RTTY")
        autoconfigure_rtty(settings);
    else if (modulation == "DominoEX")
        autoconfigure_dominoex(settings);
    else if (modulation == "Hellschreiber")
        autoconfigure_hellschreiber(settings);
}

void auto_switchmode()
{
    Fl_AutoLock lock;

    LOG_DEBUG("autoswitchmoding");

    int next = cur_transmission_index + 1;
    if (next >= cur_transmission_count)
        next = 0;

    if (hab_ui_exists)
        habCHTransmission->value(next);

    if (cur_heap == TRACKING_PAYLOAD)
        payload_transmission_list->value(next);

    if (cur_heap == TRACKING_FLIGHT)
        flight_payload_transmission_list->value(next);

    select_transmission(next);
    auto_configure();
}

static void load_cache_file(const string &name, vector<Json::Value> &target)
{
    ifstream cf(name.c_str());

    if (cf.fail())
    {
        LOG_DEBUG("Failed to open cache file %s", name.c_str());
        return;
    }

    target.clear();

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

        target.push_back(root);
    }

    bool failed = cf.fail() || !cf.eof();

    cf.close();

    if (failed)
    {
        target.clear();
        LOG_WARN("Failed to load %s", name.c_str());
    }
    else
    {
        long int n = target.size();
        LOG_DEBUG("Loaded %li docs from file %s", n, name.c_str());
    }
}

static void write_cache_file(const string &name,
                             const vector<Json::Value> &docs)
{
    ofstream cf(name.c_str(), ios_base::out | ios_base::trunc);

    for (vector<Json::Value>::const_iterator it = docs.begin();
         it != docs.end() && cf.good();
         it++)
    {
        Json::FastWriter writer;
        cf << writer.write(*it);
    }

    bool success = cf.good();

    cf.close();

    if (!success)
    {
        LOG_WARN("unable to save docs to %s", name.c_str());
        unlink(name.c_str());
    }
}

static void populate_flights()
{
    Fl_AutoLock lock;

    LOG_DEBUG("populating flights (%zi)", flight_docs.size());

    set<string> choice_items;

    if (hab_ui_exists)
    {
        habFlight->value(-1);
        habFlight->clear();
    }

    flight_browser->clear();

    if (cur_heap == TRACKING_FLIGHT)
        select_flight(-1);

    for (int i = 0; i < int(flight_docs.size()); i++)
    {
        const Json::Value &root = flight_docs[i];

        string id, name, callsign_list, date;
        bool root_ok = false;

        if (root.isObject() && root.size() &&
            root["_id"].isString() && root["name"].isString())
        {
            id = root["_id"].asString();
            name = root["name"].asString();
            callsign_list = flight_callsign_list(root);
            date = flight_launch_date(root);
            root_ok = true;
        }

        if (!id.size() || !name.size())
        {
            root_ok = false;
            name = "Invalid flight doc";
            LOG_WARN("invalid flight doc");
        }

        if (hab_ui_exists)
        {
            string item;
            int attempt = 1;

            /* Avoid duplicate menu items: fltk removes them */
            do
            {
                item = flight_choice_item(name, callsign_list, attempt);
                attempt++;
            }
            while (choice_items.count(item));
            choice_items.insert(item);

            habFlight->add(item.c_str(), (int) 0, flight_choice_callback,
                           NULL);
        }

        string browser_item = flight_browser_item(name, date, callsign_list);
        flight_browser->add(browser_item.c_str(), NULL);

        if (root_ok &&
            progdefaults.tracking_type == TRACKING_FLIGHT &&
            progdefaults.tracking_doc == id)
        {
            if (hab_ui_exists)
                habFlight->value(i);
            flight_browser->value(i + 1);
            select_flight(i);
        }
    }
}

static void populate_payloads()
{
    Fl_AutoLock lock;

    LOG_DEBUG("populating payloads (%zi)", payload_docs.size());

    payload_browser->clear();

    if (cur_heap == TRACKING_PAYLOAD)
        select_payload(-1);

    for (int i = 0; i < int(payload_docs.size()); i++)
    {
        const Json::Value &root = payload_docs[i];

        string id, name, callsign_list, description;
        bool root_ok;

        if (root.isObject() && root.size() &&
            root["_id"].isString() && root["name"].isString())
        {
            id = root["_id"].asString();
            name = root["name"].asString();
            callsign_list = payload_callsign_list(root);

            if (root["metadata"]["description"].isString())
                description = root["metadata"]["description"].asString();
        }

        if (!id.size() || !name.size())
        {
            LOG_WARN("invalid payload doc");
            root_ok = false;
        }

        string browser_item = payload_browser_item(name, callsign_list,
                                                   description);
        payload_browser->add(browser_item.c_str(), NULL);

        if (root_ok &&
            progdefaults.tracking_type == TRACKING_PAYLOAD &&
            progdefaults.tracking_doc == id)
        {
            payload_browser->value(i + 1);
            select_payload(i);
        }
    }
}

static void select_flight_payload(int index)
{
    Fl_AutoLock lock;
    LOG_DEBUG("Selecting flight payload %i", index);

    if (!cur_flight || cur_heap != TRACKING_FLIGHT)
        return;

    const Json::Value &payload_list = (*cur_flight)["_payload_docs"];

    if (!payload_list.isArray() || !payload_list.size())
        return;

    if (index < 0 || index >= int(payload_list.size()))
        return;

    if (progdefaults.tracking_flight_payload != index)
    {
        progdefaults.tracking_flight_payload = index;
        progdefaults.tracking_transmission = -1;
        progdefaults.changed = true;
    }

    do_select_payload(payload_list[index]);
}

static void do_select_payload(const Json::Value &payload)
{
    if (payload.isNull())
        LOG_DEBUG("do_select_payload(null)");
    else
        LOG_DEBUG("do_select_payload(object)");

    /* Disable stuff, incase tests fail */
    cur_payload = NULL;
    hbtint::extrmgr->payload(NULL);

    if (hab_ui_exists)
    {
        habCHTransmission->value(-1);
        habCHTransmission->clear();
        habCHTransmission->deactivate();
        habSwitchModes->deactivate();
    }

    payload_transmission_list->value(-1);
    payload_transmission_list->clear();
    payload_transmission_list->deactivate();

    flight_payload_transmission_list->value(-1);
    flight_payload_transmission_list->clear();
    flight_payload_transmission_list->deactivate();

    select_transmission(-1);

    /* Sanity checks */
    if (!payload.isObject() || !payload.size())
        return;

    /* OK. Setup */
    cur_payload = &payload;
    hbtint::extrmgr->payload(&payload);

    LOG_DEBUG("payload OK, checking transmissions");

    const Json::Value &transmissions = payload["transmissions"];

    if (!transmissions.isArray() || !transmissions.size())
        return;

    cur_transmission_count = transmissions.size();
    if (cur_transmission_count < 0)
        cur_transmission_count = 0;

    LOG_DEBUG("populating transmissions (%i)", cur_transmission_count);

    for (int i = 0; i < cur_transmission_count; i++)
    {
        const Json::Value &transmission = transmissions[i];

        const string name = mode_menu_name(i + 1, transmission);

        if (hab_ui_exists)
            habCHTransmission->add(name.c_str(), (int) 0,
                                   mode_choice_callback, NULL);
        if (cur_heap == TRACKING_FLIGHT)
            flight_payload_transmission_list->add(name.c_str(), (int) 0,
                                                  mode_choice_callback, NULL);
        if (cur_heap == TRACKING_PAYLOAD)
            payload_transmission_list->add(name.c_str(), (int) 0,
                                           mode_choice_callback, NULL);
    }

    int auto_select = progdefaults.tracking_transmission;
    if (auto_select < 0 || auto_select >= cur_transmission_count)
        auto_select = 0;

    if (hab_ui_exists)
    {
        if (cur_transmission_count > 1)
            habSwitchModes->activate();

        habCHTransmission->activate();
        habCHTransmission->value(auto_select);
    }

    if (cur_heap == TRACKING_FLIGHT)
    {
        flight_payload_transmission_list->activate();
        flight_payload_transmission_list->value(auto_select);
    }

    if (cur_heap == TRACKING_PAYLOAD)
    {
        payload_transmission_list->activate();
        payload_transmission_list->value(auto_select);
    }

    select_transmission(auto_select);
}

static void select_transmission(int index)
{
    Fl_AutoLock lock;

    LOG_DEBUG("selecting transmission %i", index);

    /* Reset */
    cur_transmission = NULL;
    cur_transmission_index = 0;

    if (hab_ui_exists)
        habConfigureButton->deactivate();

    payload_autoconfigure_a->deactivate();
    payload_autoconfigure_b->deactivate();

    /* Checks */
    if (!cur_payload)
        return;

    const Json::Value &transmissions = (*cur_payload)["transmissions"];

    if (!transmissions.isArray() ||
            int(transmissions.size()) != cur_transmission_count)
        return;

    if (index < 0 || index >= cur_transmission_count)
        return;

    LOG_DEBUG("transmission OK");

    /* OK. Set stuff */
    cur_transmission = &(transmissions[index]);
    cur_transmission_index = index;

    if (progdefaults.tracking_transmission != index)
    {
        progdefaults.tracking_transmission = index;
        progdefaults.changed = true;
    }

    if (hab_ui_exists)
        habConfigureButton->activate();

    payload_autoconfigure_a->activate();
    payload_autoconfigure_b->activate();
}

static void autoconfigure_rtty(const Json::Value &settings)
{
    LOG_DEBUG("autoconfigure rtty");

    autoconfigure_rtty_shift(settings["shift"]);
    autoconfigure_rtty_baud(settings["baud"]);
    autoconfigure_rtty_encoding(settings["encoding"]);
    autoconfigure_rtty_parity(settings["parity"]);
    autoconfigure_rtty_stop(settings["stop"]);

    init_modem_sync(MODE_RTTY);
    resetRTTY();
    progdefaults.changed = true;
}

static void autoconfigure_rtty_shift(const Json::Value &value)
{
    if (!value.isNumeric())
        return;

    double shift = value.asDouble();

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
            return;
        }
    }

    /* If not found (i.e., we found the terminating 0, and haven't returned)
     * then search == the index of the "Custom" menu item */
    selShift->value(search);
    selCustomShift->activate();
    progdefaults.rtty_shift = -1;
    selCustomShift->value(shift);
    progdefaults.rtty_custom_shift = shift;
}

static void autoconfigure_rtty_baud(const Json::Value &value)
{
    if (!value.isNumeric())
        return;

    double baud = value.asDouble();
    int search;
    for (search = 0; rtty::BAUD[search] != 0; search++)
    {
        double diff = rtty::BAUD[search] - baud;
        if (diff < 0.01 && diff > -0.01)
        {
            selBaud->value(search);
            progdefaults.rtty_baud = search;
            return;
        }
    }
}

static void autoconfigure_rtty_encoding(const Json::Value &value)
{
    if (!value.isString())
        return;

    const string encoding = value.asString();
    int select = -1;

    /* rtty::BITS[] = {5, 7, 8}; */
    if (encoding == "BAUDOT")
        select = 0;
    else if (encoding == "ASCII-7")
        select = 1;
    else if (encoding == "ASCII-8")
        select = 2;
    else
        return;

    selBits->value(select);
    progdefaults.rtty_bits = select;

    /* From selBits' callback */
    if (select == 0)
    {
        progdefaults.rtty_parity = RTTY_PARITY_NONE;
        selParity->value(RTTY_PARITY_NONE);
    }
}

static void autoconfigure_rtty_parity(const Json::Value &value)
{
    if (!value.isString())
        return;

    /* Parity is disabled when using baudot (rtty_bits == 0) */
    if (progdefaults.rtty_bits == 0)
        return;

    const string parity = value.asString();
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
    else
        return;

    selParity->value(select);
    progdefaults.rtty_parity = select;
}

static void autoconfigure_rtty_stop(const Json::Value &value)
{
    if (!value.isNumeric())
        return;

    int select = -1;

    if (value.isInt())
    {
#ifdef JSON_HAS_INT64   /* test if jsoncpp 0.6 */
        int stop = value.asLargestInt();
#else
        int stop = value.asInt();
#endif
        if (stop == 1)
            select = 0;
        else if (stop == 2)
            select = 2;
    }
    else
    {
        double stop = value.asDouble();
        if (stop > 1.49 && stop < 1.51)
            select = 1;
    }

    if (select == -1)
        return;

    progdefaults.rtty_stop = select;
    selStopBits->value(select);
}

static void autoconfigure_dominoex(const Json::Value &settings)
{
    LOG_DEBUG("autoconfigure dominoex");

    if (!settings["speed"].isInt())
        return;

#ifdef JSON_HAS_INT64   /* jsoncpp 0.6 */
    int type = settings["speed"].asLargestInt();
#else
    int type = settings["speed"].asInt();
#endif
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

static void autoconfigure_hellschreiber(const Json::Value &settings)
{
    LOG_DEBUG("autoconfigure hellschreiber");

    if (!settings["variant"].isString())
        return;

    const string variant = settings["variant"].asString();
    int modem = -1;

    if (variant == "slowhell")
        modem = MODE_SLOWHELL;
    else if (variant == "feldhell")
        modem = MODE_FELDHELL;

    if (modem != -1)
        init_modem_sync(modem);
}

static string escape_menu_string(const string &orig_str)
{
    string new_str;
    size_t pos = 0;

    do
    {
        size_t start = pos;

        pos = orig_str.find_first_of("&/\\_", start);
        new_str.append(orig_str.substr(start, pos - start));

        if (pos != string::npos)
        {
            new_str.push_back('\\');
            new_str.push_back(orig_str[pos]);
            pos++;
        }
    }
    while (pos != string::npos);

    return new_str;
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

static string join_set(const set<string> &items, const string &sep)
{
    string result;

    for (set<string>::const_iterator it = items.begin();
            it != items.end(); ++it)
    {
        if (it != items.begin())
            result.append(sep);

        result.append(*it);
    }

    return result;
}

static string flight_choice_item(const string &name,
                                 const string &callsign_list,
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

    return escape_menu_string(name + ": " + callsign_list + attempt_suffix);
}

static string flight_browser_item(const string &name, const string &date,
                                  const string &callsign_list)
{
    /* "<name>\t<optional date>\t<callsign>,<callsign>,..." */
    return "@." + escape_browser_string(name) + "\t" +
           "@." + date + "\t" +
           "@." + escape_browser_string(callsign_list);
}

static string payload_browser_item(const string &name,
                                   const string &callsign_list,
                                   const string &description)
{
    return "@." + escape_browser_string(name) + "\t"
         + "@." + "(" + escape_browser_string(callsign_list) + ") "
                + escape_browser_string(description);
}

static string flight_payload_menu_item(const string &name,
                                       const string &callsign_list,
                                       int attempt)
{
    /* "<name>" or "<name> (callsign)" + (attempt) */

    string attempt_suffix;

    if (attempt != 1)
    {
        ostringstream sfx;
        sfx << " (" << attempt << ")";
        attempt_suffix = sfx.str();
    }

    return escape_menu_string(name + " (" + callsign_list + ") "
                               + attempt_suffix);
}

static string mode_menu_name(int index, const Json::Value &settings)
{
    /* index: modulation type
     * e.g.:  2: RTTY 300 */

    ostringstream name;

    name << index << ": ";

    string modulation;

    if (settings.isObject() && settings.size() &&
        settings["modulation"].isString())
    {
        modulation = settings["modulation"].asString();
    }

    if (modulation == "RTTY")
    {
        name << "RTTY";

        if (settings["baud"].isInt())
#ifdef JSON_HAS_INT64   /* jsoncpp 0.6 */
            name << ' ' << settings["baud"].asLargestInt();
#else
            name << ' ' << settings["baud"].asInt();
#endif
        else if (settings["baud"].isDouble())
            name << ' ' << settings["baud"].asDouble();
    }
    else if (modulation == "DominoEX" && settings["speed"].isInt())
    {
        name << "DomEX " << settings["speed"].asInt();
    }
    else if (modulation == "Hellschreiber" && settings["variant"].isString())
    {
        string variant = settings["variant"].asString();
        if (variant == "feldhell")
            name << "FeldHell";
        else if (variant == "slowhell")
            name << "SlowHell";
        else
            name << "Hellschreiber";
    }
    else
    {
        name << "Unknown";
    }

    return escape_menu_string(name.str());
}

static string flight_callsign_list(const Json::Value &flight)
{
    const Json::Value &payloads = flight["_payload_docs"];

    if (!payloads.isArray() || !payloads.size())
        return "";

    set<string> callsigns;

    for (Json::Value::const_iterator it = payloads.begin();
            it != payloads.end(); ++it)
    {
        const Json::Value &payload = *it;

        if (!payload.isObject())
            continue;

        const Json::Value &sentences = payload["sentences"];

        if (!sentences.isArray())
            continue;

        for (Json::Value::const_iterator it2 = sentences.begin();
                it2 != sentences.end(); ++it2)
        {
            const Json::Value &sentence = *it2;

            if (!sentence.isObject())
                continue;

            const Json::Value &callsign = sentence["callsign"];

            if (!callsign.isString())
                continue;

            callsigns.insert(callsign.asString());
        }
    }

    /* sets are sorted, so this comes out in the right order: */
    return join_set(callsigns);
}

static string flight_launch_date(const Json::Value &flight)
{
    const Json::Value &launch = flight["launch"];

    if (!launch.isObject() || !launch.size())
        return "";

    if (!launch.isMember("time") || !launch["time"].isString())
        return "";

    time_t date = RFC3339::rfc3339_to_timestamp(launch["time"].asString());
    char buf[20];
    struct tm tm;

    if (localtime_r(&date, &tm) != &tm)
        return "";

    if (strftime(buf, sizeof(buf), "%a %d %b %y", &tm) <= 0)
        return "";

    return buf;
}

static string payload_callsign_list(const Json::Value &payload)
{
    const Json::Value &sentences = payload["sentences"];
    if (!sentences.isArray())
        return "";

    set<string> callsign_list;

    for (Json::Value::const_iterator it = sentences.begin();
            it != sentences.end(); ++it)
    {
        const Json::Value &sentence = *it;

        if (!sentence.isObject())
            continue;

        const Json::Value &callsign = sentence["callsign"];

        if (!callsign.isString())
            continue;

        callsign_list.insert(callsign.asString());
    }

    return join_set(callsign_list);
}

static void flight_payload_choice_callback(Fl_Widget *w, void *a)
{
    Fl_AutoLock lock;

    Fl_Choice *choice = static_cast<Fl_Choice *>(w);

    if (choice != flight_payload_list)
        flight_payload_list->value(choice->value());
    if (hab_ui_exists && choice != habCHPayload)
        habCHPayload->value(choice->value());

    select_flight_payload(choice->value());
}

static void mode_choice_callback(Fl_Widget *w, void *a)
{
    Fl_AutoLock lock;

    Fl_Choice *choice = static_cast<Fl_Choice *>(w);

    if (hab_ui_exists && choice != habCHTransmission)
        habCHTransmission->value(choice->value());
    if (cur_heap == TRACKING_FLIGHT &&
            choice != flight_payload_transmission_list)
        flight_payload_transmission_list->value(choice->value());
    if (cur_heap == TRACKING_PAYLOAD && choice != payload_transmission_list)
        payload_transmission_list->value(choice->value());

    select_transmission(choice->value());
}

static void flight_choice_callback(Fl_Widget *w, void *a)
{
    Fl_AutoLock lock;

    Fl_Choice *choice = static_cast<Fl_Choice *>(w);
    flight_browser->value(choice->value() + 1);

    payload_browser->deselect();
    select_flight(choice->value());
}


} /* namespace flights */
} /* namespace dl_fldigi */
