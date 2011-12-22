/* 
 * Copyright (C) 2011 Daniel Richman
 * License: GNU GPL 3
 *
 * flights.cxx: flight document management, selection, GUI and autoconfiguring
 */

#include "dl_fldigi/flights.h"

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <set>
#include <unistd.h>
#include <jsoncpp/json.h>

#include "main.h"
#include "debug.h"
#include "fl_digi.h"
#include "confdialog.h"

#include "dl_fldigi/dl_fldigi.h"
#include "dl_fldigi/hbtint.h"

using namespace std;

namespace dl_fldigi {
namespace flights {

bool show_testing, downloaded_once;

static string cache_file;
static vector<Json::Value> flight_docs;
static vector<string> payload_index;

/* These pointers just point at some part of the heap allocated by something
 * in the flight_docs vector; they're invalidated when filght_docs is modified
 * but we make sure to update them when that happens (only happens when
 * new data is downloaded; populate_flights cleans up). */
static const Json::Value *cur_flight, *cur_payload, *cur_mode;
static int cur_mode_index, cur_payload_modecount;
static int flight_search_first = 1;

static void select_payload(int index);
static void select_mode(int index);

void init()
{
    cache_file = HomeDir + "flight_docs.json";
}

void cleanup()
{
    flight_docs.clear();
    payload_index.clear();
    cur_flight = NULL;
    cur_payload = NULL;
    cur_mode = NULL;
    cur_mode_index = 0;
    cur_payload_modecount = 0;
}

void load_cache()
{
    /* initialise an empty and disabled UI: */
    populate_flights();

    ifstream cf(cache_file.c_str());

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

void new_docs(const vector<Json::Value> &new_flight_docs)
{
    Fl_AutoLock lock;

    flight_docs = new_flight_docs;
    downloaded_once = true;

    ofstream cf(cache_file.c_str(), ios_base::out | ios_base::trunc);

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
        LOG_WARN("unable to save flights data");
        unlink(cache_file.c_str());
    }

    populate_flights();
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

        if (is_testing_flight(root) && !show_testing)
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
    hbtint::extrmgr->payload(NULL);
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
    hbtint::extrmgr->payload(&payload);

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

} /* namespace flights */
} /* namespace dl_fldigi */
