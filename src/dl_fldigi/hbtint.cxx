/* 
 * Copyright (C) 2011 Daniel Richman
 * License: GNU GPL 3
 *
 * hbtint.cxx: habitat integration (see habitat_extensions/cpp_uploader)
 */

#include "dl_fldigi/hbtint.h"

#include <string>
#include <sstream>
#include <jsoncpp/json.h>

#include <FL/Fl.H>

#include "configuration.h"
#include "debug.h"
#include "fl_digi.h"

#include "habitat/EZ.h"
#include "habitat/UKHASExtractor.h"

#include "dl_fldigi/dl_fldigi.h"
#include "dl_fldigi/version.h"
#include "dl_fldigi/location.h"
#include "dl_fldigi/flights.h"

using namespace std;

namespace dl_fldigi {
namespace hbtint {

static EZ::cURLGlobal *cgl;
DExtractorManager *extrmgr;
DUploaderThread *uthr;
static habitat::UKHASExtractor *ukhas;

void init()
{
    cgl = new EZ::cURLGlobal();

    uthr = new DUploaderThread();
    extrmgr = new DExtractorManager(*uthr);

    ukhas = new habitat::UKHASExtractor();
    extrmgr->add(*ukhas);
}

void start()
{
    uthr->start();
}

void cleanup()
{
    delete extrmgr;
    delete ukhas;

    extrmgr = 0;
    ukhas = 0;

    if (uthr)
        uthr->shutdown();

    while (uthr)
        Fl::wait();

    delete cgl;
    cgl = 0;
}

static void uthr_thread_death(void *what)
{
    if (what != uthr)
    {
        LOG_ERROR("unknown thread");
        return;
    }

    LOG_INFO("cleaning up");
    uthr->join();
    delete uthr;
    uthr = 0;
}

/* Modify run() to help us shutdown the thread */
void *DUploaderThread::run()
{
    void *ret = UploaderThread::run();
    Fl::awake(uthr_thread_death, this);
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

    if (location::current_location_mode != location::LOC_STATIONARY)
    {
        warning("attempted to upload stationary listener "
                "telemetry while in GPS telemetry mode");
        return;
    }

    location::update_stationary();

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

    data["latitude"] = location::listener_latitude;
    data["longitude"] = location::listener_longitude;

    UploaderThread::listener_telemetry(data);
}

void DUploaderThread::listener_telemetry(const Json::Value &data)
{
    Fl_AutoLock lock;

    if (location::current_location_mode != location::LOC_GPS)
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
    status_important(message);
}

void DUploaderThread::saved_id(const string &type, const string &id)
{
    /* Log as normal, but also set status */
    UploaderThread::saved_id(type, id);
    status("Uploaded " + type + " successfully");
}

void DUploaderThread::got_flights(const vector<Json::Value> &new_flight_docs)
{
    ostringstream ltmp;
    ltmp << "Downloaded " << new_flight_docs.size() << " flight docs";
    log(ltmp.str());

    flights::new_docs(new_flight_docs);
}

/* Be careful not to call this function instead of dl_fldigi::status() */
void DExtractorManager::status(const string &msg)
{
    Fl_AutoLock lock;
    LOG_DEBUG("hbtE %s", msg.c_str());
}

static void set_jvalue(Fl_Output *widget, const Json::Value &value)
{
    if (value.isString())
    {
        widget->value(value.asCString());
    }
    else if (value.isBool() || value.isIntegral())
    {
        ostringstream tmp;

        if (value.isDouble())
        {
            tmp.setf(ios::fixed);
            tmp.precision(6);
            tmp << value.asDouble();
        }
        else if (value.isIntegral())
        {
            tmp << value.asInt();
        }
        else if (value.isBool())
        {
            tmp << value.asBool();
        }

        widget->value(tmp.str().c_str());
    }
    else
    {
        /* We can't print objects or arrays, and null is just "" */
        widget->value("");
    }
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
        lat_strm >> location::balloon_latitude;
        lon_strm >> location::balloon_longitude;
        location::balloon_valid = !lat_strm.fail() && !lon_strm.fail();
    }
    else
    {
        location::balloon_valid = false;
    }

    location::update_distance_bearing();
}

} /* namespace hbtint */
} /* namespace dl_fldigi */
