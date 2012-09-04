#ifndef DL_FLDIGI_UPDATE_H
#define DL_FLDIGI_UPDATE_H

#include "habitat/EZ.h"

namespace dl_fldigi {
namespace update {

void check();
void cleanup();

class UpdateThread : public EZ::SimpleThread
{
public:
    void *run();
};

} /* namespace gps */
} /* namespace dl_fldigi */

#endif /* DL_FLDIGI_GPS_H */
