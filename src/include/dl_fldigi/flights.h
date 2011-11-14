#ifndef DL_FLDIGI_FLIGHTS_H
#define DL_FLDIGI_FLIGHTS_H

namespace dl_fldigi {
namespace flights {

extern bool show_testing;

void init();
void cleanup();

void load_cache();
void populate_flights();
void flight_search(bool next);
void select_flight(int index);
void auto_configure();
void auto_switchmode();

} /* namespace flights */
} /* namespace dl_fldigi */

#endif /* DL_FLDIGI_FLIGHTS_H */
