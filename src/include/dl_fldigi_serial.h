#include <stdio.h>

using namespace std;

/**
* Interfaces to a serial port. Holds a file descriptor/handler.
* Constructed with a port name, e.g. COM8 on Windows, or
* /dev/ttyUSB0 on Linux. Provides read and write functionality.
*/

FILE *dl_fldigi_open_serial_port(const char *port, int baud);
