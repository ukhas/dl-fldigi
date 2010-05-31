#include <stdio.h>
#include <signal.h>
#include <pthread.h>

/**
* Interfaces to a serial port. Holds a file descriptor/handler.
* Constructed with a port name, e.g. COM8 on Windows, or
* /dev/ttyUSB0 on Linux. Provides read and write functionality.
*/

#define dl_fldigi_serial_cleanupkill(t__) pthread_kill(t__, SIGUSR1)

FILE *dl_fldigi_open_serial_port(const char *port, int baud);
void dl_fldigi_serial_init();
