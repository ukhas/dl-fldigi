/* 
 * Copyright (C) 2011 James Coxon, Daniel Richman, Robert Harrison,
 *                    Philip Heron, Adam Greig, Simrun Basuita
 * License: GNU GPL 3
 *
 * location.cxx: Stationary and moving location management
 */

#include "dl_fldigi/location.h"

#include <sstream>

#include "configuration.h"
#include "fl_digi.h"
#include "confdialog.h"

#include "dl_fldigi/dl_fldigi.h"
#include "dl_fldigi/gps.h"

using namespace std;

namespace dl_fldigi {
namespace location {

enum location_mode new_location_mode, current_location_mode;
double listener_latitude, listener_longitude, listener_altitude,
       balloon_latitude, balloon_longitude, balloon_altitude;
bool listener_valid, balloon_valid;

void start()
{
    if (progdefaults.gps_start_enabled)
        current_location_mode = LOC_GPS;
    else
        current_location_mode = LOC_STATIONARY;

    gps::configure_gps();
}

void update_distance_bearing()
{
    Fl_AutoLock lock;

    if (!hab_ui_exists)
        return;

    if (!listener_valid || !balloon_valid)
    {
        //habDistance->value("");
        //habBearing->value("");
        //habElevation->value("");
        return;
    }

    /* See habitat-autotracker/autotracker/earthmaths.py. */
    double c = M_PI/180;
    double lat1, lon1, lat2, lon2, alt1, alt2;
    lat1 = listener_latitude * c;
    lon1 = listener_longitude * c;
    alt1 = listener_altitude;
    lat2 = balloon_latitude * c;
    lon2 = balloon_longitude * c;
    alt2 = balloon_altitude;

    double radius, d_lon, sa, sb, bearing, aa, ab, angle_at_centre,
           ta, tb, ea, eb, elevation, distance;

    radius = 6371000.0;

    d_lon = lon2 - lon1;
    sa = cos(lat2) * sin(d_lon);
    sb = (cos(lat1) * sin(lat2)) - (sin(lat1) * cos(lat2) * cos(d_lon));
    bearing = atan2(sa, sb);
    aa = sqrt((sa * sa) + (sb * sb));
    ab = (sin(lat1) * sin(lat2)) + (cos(lat1) * cos(lat2) * cos(d_lon));
    angle_at_centre = atan2(aa, ab);

    ta = radius + alt1;
    tb = radius + alt2;
    ea = (cos(angle_at_centre) * tb) - ta;
    eb = sin(angle_at_centre) * tb;
    elevation = atan2(ea, eb);

    distance = sqrt((ta * ta) + (tb * tb) -
                    2 * tb * ta * cos(angle_at_centre));

    bearing *= (180/M_PI);
    elevation *= (180/M_PI);
    distance /= 1000;

    if (bearing < 0)
        bearing += 360;

    ostringstream str_distance;
    str_distance.precision(4);
    str_distance << distance << "km";

    ostringstream str_bearing;
    str_bearing.setf(ios::fixed, ios::floatfield);
    str_bearing.precision(1);
    str_bearing.fill('0');
    str_bearing.width(3 + 1 + 1);
    str_bearing << bearing;

    ostringstream str_elevation;
    str_elevation.setf(ios::fixed, ios::floatfield);
    str_elevation.precision(1);
    str_elevation << elevation;

    habDistance->value(str_distance.str().c_str());
    habBearing->value(str_bearing.str().c_str());
    habElevation->value(str_elevation.str().c_str());
}

void update_stationary()
{
    if (current_location_mode != LOC_STATIONARY)
    {
        throw runtime_error("attempted to update stationary location "
                            "while in GPS mode");
    }

    istringstream lat_strm(progdefaults.myLat), lon_strm(progdefaults.myLon),
                  alt_strm(progdefaults.myAlt);

    lat_strm >> listener_latitude;
    lon_strm >> listener_longitude;
    alt_strm >> listener_altitude;

    if (lat_strm.fail())
        stationary_lat->color(252);
    else
        stationary_lat->color(255);

    if (lon_strm.fail())
        stationary_lon->color(252);
    else
        stationary_lon->color(255);

    if (alt_strm.fail())
        stationary_alt->color(252);
    else
        stationary_alt->color(255);

    stationary_lat->redraw();
    stationary_lon->redraw();
    stationary_alt->redraw();

    if (lat_strm.fail() || lon_strm.fail() || alt_strm.fail())
    {
        status_important("couldn't set stationary location: invalid float");
        listener_valid = false;
        update_distance_bearing();
    }
    else
    {
        listener_valid = true;
        update_distance_bearing();
    }
}

} /* namespace location */
} /* namespace dl_fldigi */
