#ifndef DL_FLDIGI_FLIGHTS_H
#define DL_FLDIGI_FLIGHTS_H

#include <vector>
#include <jsoncpp/json.h>

namespace dl_fldigi {
namespace flights {

extern bool downloaded_once;

void init();
void cleanup();

void new_docs(const std::vector<Json::Value> &new_flight_docs);
void load_cache();
void populate_flights();
void flight_search(bool next);
void select_flight(int index);
void auto_configure();
void auto_switchmode();

} /* namespace flights */
} /* namespace dl_fldigi */

#endif /* DL_FLDIGI_FLIGHTS_H */
