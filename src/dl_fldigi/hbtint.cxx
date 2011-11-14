/* 
 * Copyright (C) 2011 James Coxon, Daniel Richman, Robert Harrison,
 *                    Philip Heron, Adam Greig, Simrun Basuita
 * License: GNU GPL 3
 */

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

} /* namespace hbtint */
} /* namespace dl_fldigi */
