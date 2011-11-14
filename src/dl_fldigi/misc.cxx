/* 
 * Copyright (C) 2011 James Coxon, Daniel Richman, Robert Harrison,
 *                    Philip Heron, Adam Greig, Simrun Basuita
 * License: GNU GPL 3
 */

using namespace std;

namespace dl_fldigi {

/* FLTK doesn't provide something like this, as far as I can tell. */

static bool dl_online, hab_ui_exists, shutting_down;
static int dirty;
static time_t last_rx, last_warn;

static const time_t period = 10;

static void periodically(void *);
static void update_distance_bearing();

/*
 * Functions init, ready and cleanup should only be called from main().
 * thread_death and periodically are called by FLTK, which will have the lock.
 */
void init()
{
    flights::init();
    hbtint::init();
}

void ready(bool hab_mode)
{
    flights::load_cache();
    hbtint::start();

    /* XXX before gps configure */ location::ready();

    gps::configure_gps();


    /* XXX move to small functions */
    /* if --hab was specified, default online to true, and update ui */
    hab_ui_exists = hab_mode;

    if (progdefaults.gps_start_enabled)
        current_location_mode = LOC_GPS;
    else
        current_location_mode = LOC_STATIONARY;

    reset_gps_settings();

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

} /* namespace dl_fldigi */
