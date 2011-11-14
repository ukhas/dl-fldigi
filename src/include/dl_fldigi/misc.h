#ifndef DL_FLDIGI_MISC_H
#define DL_FLDIGI_MISC_H

namespace dl_fldigi {

/* FLTK doesn't provide something like this, as far as I can tell. */
class Fl_AutoLock
{
public:
    Fl_AutoLock() { Fl::lock(); };
    ~Fl_AutoLock() { Fl::unlock(); };
};

enum changed_groups
{
    CH_NONE = 0x00,
    CH_UTHR_SETTINGS = 0x01,
    CH_INFO = 0x02,
    CH_LOCATION_MODE = 0x04,
    CH_STATIONARY_LOCATION = 0x08,
    CH_GPS_SETTINGS = 0x10
};

enum location_mode
{
    LOC_STATIONARY,
    LOC_GPS
};

void init();                 /* Create globals and stuff; before UI init */
void ready(bool hab_mode);   /* After UI init, start stuff */
void cleanup();

void online(bool value);
bool online();

void changed(enum changed_groups thing);
void commit();

} /* namespace dl_fldigi */

#endif /* DL_FLDIGI_MISC_H */
