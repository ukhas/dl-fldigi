/* 
 * Copyright (C) 2011 James Coxon, Daniel Richman, Robert Harrison,
 *                    Philip Heron, Adam Greig, Simrun Basuita
 * License: GNU GPL 3
 *
 * dl_fldigi.cxx: glue, startup/cleanup and misc functions
 */

#include "dl_fldigi/dl_fldigi.h"

#include <sstream>
#include <time.h>

#include <FL/Fl.H>

#include "configuration.h"
#include "debug.h"
#include "confdialog.h"
#include "fl_digi.h"

#include "dl_fldigi/flights.h"
#include "dl_fldigi/hbtint.h"
#include "dl_fldigi/location.h"
#include "dl_fldigi/gps.h"
#include "dl_fldigi/update.h"

using namespace std;

namespace dl_fldigi {

bool hab_ui_exists, shutting_down;
time_t last_rx;

static bool dl_online, first_online;
static int dirty;
static time_t last_warn;

static const time_t period = 10;

static void periodically(void *);

/*
 * Functions init, ready and cleanup should only be called from main().
 * thread_death and periodically are called by FLTK, which will have the Fl
 * lock.
 */
void init()
{
    flights::init();
    hbtint::init();
}

void ready(bool hab_mode)
{
    hab_ui_exists = hab_mode;

    flights::load_cache();
    hbtint::start();
    location::start();

    /* online will call uthr->settings() if hab_mode since it online will
     * "change" from false to true) */
    online(hab_mode);

    Fl::add_timeout(period, periodically);

    if (hab_ui_exists)
        habTimeSinceLastRx->value("ages");
}

void cleanup()
{
    LOG_DEBUG("cleaning up");
    shutting_down = true;

    gps::cleanup();
    hbtint::cleanup();
    flights::cleanup();
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

/* How does online/offline work? if online() is false, uthr->settings() will
 * reset the UploaderThread, leaving it unintialised */
void online(bool val)
{
    Fl_AutoLock lock;

    bool changed;

    changed = (dl_online != val);
    dl_online = val;

    if (changed)
    {
        hbtint::uthr->settings();
    }

    if (changed && dl_online)
    {
        if (!first_online)
        {
            if (progdefaults.check_for_updates)
                update::check();
            first_online = true;
        }

        if (!flights::downloaded_flights_once)
            hbtint::uthr->flights();
        if (!flights::downloaded_payloads_once)
            hbtint::uthr->payloads();

        hbtint::uthr->listener_information();
        hbtint::uthr->listener_telemetry();
    }

    confdialog_dl_online->value(val);
    set_menu_dl_online(val);
    set_menu_dl_refresh_active(dl_online);

    if (dl_online)
    {
        flight_docs_refresh_a->activate();
        flight_docs_refresh_b->activate();
    }
    else
    {
        flight_docs_refresh_a->deactivate();
        flight_docs_refresh_b->deactivate();
    }
}

bool online()
{
    Fl_AutoLock lock;
    return dl_online;
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
        flights::downloaded_flights_once = false;
        flights::downloaded_payloads_once = false;

        hbtint::uthr->settings();
        hbtint::uthr->flights();
        hbtint::uthr->payloads();
    }

    if (dirty & CH_LOCATION_MODE)
    {
        location::current_location_mode = location::new_location_mode;
    }

    if ((dirty & CH_LOCATION_MODE) || (dirty & CH_GPS_SETTINGS))
    {
        gps::configure_gps();
    }

    /* If the info has been updated, or the upload settings changed... */
    if (dirty & (CH_UTHR_SETTINGS | CH_INFO))
    {
        hbtint::uthr->listener_information();
    }

    /* if stationary and (settings changed, or if we just switched to
     * stationary mode from gps mode, or if the upload settings changed) */
    if (location::current_location_mode == location::LOC_STATIONARY &&
        (dirty & (CH_STATIONARY_LOCATION | CH_LOCATION_MODE |
                  CH_UTHR_SETTINGS)))
    {
        hbtint::uthr->listener_telemetry();
    }

    dirty = CH_NONE;
}

void status(const string &message)
{
    Fl_AutoLock lock;
    LOG_DEBUG("unimp status: %s", message.c_str());

    /* don't overwrite a warning */
    if (time(NULL) - last_warn > 10)
        put_status_safe(message.c_str(), 10);
}

void status_important(const string &message)
{
    Fl_AutoLock lock;
    LOG_DEBUG("warn status: %s", message.c_str());

    string temp = "WARNING! " + message;
    put_status_safe(temp.c_str(), 10);
    last_warn = time(NULL);
}

} /* namespace dl_fldigi */
