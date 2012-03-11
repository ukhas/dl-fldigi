#ifndef DL_FLDIGI_LOCATION_H
#define DL_FLDIGI_LOCATION_H

namespace dl_fldigi {
namespace location {

enum location_mode
{
    LOC_STATIONARY,
    LOC_GPS
};

extern enum location_mode new_location_mode, current_location_mode;

/* Keep the last listener lat/lon that we uploaded. */
/* Call update_distance_bearing whenever it is changed! */
extern double listener_latitude, listener_longitude, listener_altitude,
              balloon_latitude, balloon_longitude, balloon_altitude;
extern bool listener_valid, balloon_valid;

void start();
void update_distance_bearing();
void update_stationary();

} /* namespace location */
} /* namespace dl_fldigi */

#endif /* DL_FLDIGI_LOCATION_H */
