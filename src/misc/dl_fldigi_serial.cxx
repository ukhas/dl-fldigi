// TODO: Windoze it up
#ifndef __MINGW32__

#include <stdio.h>
#include <termios.h>
#include <fcntl.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dl_fldigi_serial.h"

// Thread local storage for the FD of the port we open.
// This is so that it can be cleaned up and closed by a signal handler.
#if USE_TLS
  #define TFD_TYPE __thread void *
  #define CREATE_TFD() tfd_ = NULL
  #define SET_TFD(x)   tfd_ = (x);
  #define GET_TFD()    tfd_
#else
  #define TFD_TYPE pthread_key_t
  #define CREATE_TFD() pthread_key_create(&tfd_, NULL)
  #define SET_TFD(x)   pthread_setspecific(tfd_, (x))
  #define GET_TFD()    pthread_getspecific(tfd_)
#endif
TFD_TYPE tfd_;

void dl_fldigi_serial_cleanup(int s);

void dl_fldigi_serial_init()
{
	CREATE_TFD();

	// TODO: testme.

	#ifndef __WOE32__
		struct sigaction action;
		memset(&action, 0, sizeof(struct sigaction));

		action.sa_handler = dl_fldigi_serial_cleanup;
		sigaction(SIGUSR1, &action, NULL);
	#else
		signal(SIGUSR1, dl_fldigi_serial_cleanup);
	#endif
}

void dl_fldigi_serial_cleanup(int s)
{
	FILE *f;

	f = (FILE *) GET_TFD();

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: (thread %li) dl_fldigi_serial_cleanup:\n", pthread_self());
	#endif

	if (f != NULL)
	{
		#ifdef DL_FLDIGI_DEBUG
			fprintf(stderr, "dl_fldigi: (thread %li) dl_fldigi_serial_cleanup: tfd: %p\n", pthread_self(), fd);
		#endif

		fclose(f);
	}
	else
	{
		#ifdef DL_FLDIGI_DEBUG
			fprintf(stderr, "dl_fldigi: (thread %li) dl_fldigi_serial_cleanup: tfd is null\n", pthread_self());
		#endif		
	}

	pthread_exit(0);
}

/**
* Open the serial port and check it succeeded.
* The named port is opened at the given baud rate using 8N1
* with no hardware flow control.
* \param port The serial port to use, e.g. COM8 or /dev/ttyUSB0
* \param baud The baud rate to use, e.g. 9600 or 4800. On linux this
* is restricted to a specific range of common values.
*/
FILE *dl_fldigi_open_serial_port(const char *port, int baud)
{
	//Open the serial port
	int serial_port = open(port, O_RDONLY | O_NOCTTY | O_NDELAY);
	if( serial_port == -1 ) {
		fprintf(stderr, "Error opening serial port.\n");
		return NULL;
	}

	FILE *f = fdopen(serial_port, "r");
	if (f == NULL)
	{
		fprintf(stderr, "Error fdopening serial port as a FILE\n");
		close(serial_port);
		return NULL;
	}

	// This is so that it can be cleaned up and closed by a signal handler.
	SET_TFD(f);

	//Initialise the port
	int serial_port_set = fcntl(serial_port, F_SETFL, 0);
	if( serial_port_set == -1 ) {
		fprintf(stderr, "Error initialising serial port.\n");
		SET_TFD(NULL);
		fclose(f);
		return NULL;
	}
		
	//Linux requires baudrates be given as a constant
	speed_t baudrate = B4800;
	if( baud == 9600 ) baudrate = B9600;
	else if( baud == 19200 ) baudrate = B19200;
	else if( baud == 38400 ) baudrate = B38400;
	else if( baud == 57600 ) baudrate = B57600;
	else if( baud == 115200 ) baudrate = B115200;
	else if( baud == 230400 ) baudrate = B230400;

	//Set all the weird arcane settings Linux demands (boils down to 8N1)
	struct termios port_settings;
	memset(&port_settings, 0, sizeof(port_settings));

	cfsetispeed(&port_settings, baudrate);
	cfsetospeed(&port_settings, baudrate);

	/* Enable the reciever and set local */
	port_settings.c_cflag |= (CLOCAL | CREAD);

	/* Set 8N1 */
	port_settings.c_cflag &= ~PARENB;
	port_settings.c_cflag &= ~CSTOPB;
	port_settings.c_cflag &= ~CSIZE;
	port_settings.c_cflag |= CS8;

	/* Set raw input */
	port_settings.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

	/* Ignore CR NMEA does CR LF at end of each sentence */
	port_settings.c_iflag |= (IGNCR);

	/* Set raw output (mute point as we don't send stuff to gps in here) */
	port_settings.c_oflag &= ~OPOST;

	//Apply settings
	serial_port_set = tcsetattr(serial_port, TCSANOW, &port_settings);
	if( serial_port_set == -1 ) {
		fprintf(stderr, "Error configuring serial port.\n");
		SET_TFD(NULL);
		fclose(f);
		return NULL;
	}

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "Serial port '%s' opened successfully as %p (%i==%i).\n", port, f, fileno(f), serial_port);
	#endif

	return f;
}

#endif /* __MINGW32__ */
