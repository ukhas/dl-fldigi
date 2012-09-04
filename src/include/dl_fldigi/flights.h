#ifndef DL_FLDIGI_FLIGHTS_H
#define DL_FLDIGI_FLIGHTS_H

#include <vector>
#include "jsoncpp.h"

namespace dl_fldigi {
namespace flights {

enum tracking_type_enum
{
    TRACKING_NOTHING,
    TRACKING_FLIGHT,
    TRACKING_PAYLOAD
};

extern bool downloaded_flights_once, downloaded_payloads_once;

void init();
void cleanup();

void new_flight_docs(const std::vector<Json::Value> &docs);
void new_payload_docs(const std::vector<Json::Value> &docs);
void load_cache();
void payload_search(bool next);
void select_flight(int index);
void select_payload(int index);
void auto_configure();
void auto_switchmode();

} /* namespace flights */
} /* namespace dl_fldigi */

#endif /* DL_FLDIGI_FLIGHTS_H */
