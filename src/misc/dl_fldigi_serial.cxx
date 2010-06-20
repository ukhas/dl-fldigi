#include <iostream>
#include <termios.h>
#include <fcntl.h>
#include <stdlib.h>

using namespace std;

#include "dl_fldigi_serial.h"

/**
* Open the serial port and check it succeeded.
* The named port is opened at the given baud rate using 8N1
* with no hardware flow control.
* \param port The serial port to use, e.g. COM8 or /dev/ttyUSB0
* \param baud The baud rate to use, e.g. 9600 or 4800. On linux this
* is restricted to a specific range of common values.
*/
SerialPort::SerialPort(const char* port, int baud) {
    //Open the serial port
    serial_port = open(port, O_RDONLY | O_NOCTTY | O_NDELAY);
    if( serial_port == -1 ) {
		cout << "Error opening serial port." << endl;
        exit(1);
    }

	//Initialise the port
    int serial_port_set = fcntl(serial_port, F_SETFL, 0);
	if( serial_port_set == -1 ) {
		cout << "Error initialising serial port." << endl;
		exit(1);
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
		cout << "Error configuring serial port." << endl;
		exit(1);
	}

	cout << "Serial port '" << port << "' opened successfully.\n" << endl;
	return;
}

/**
* Close the serial port handler.
*/
SerialPort::~SerialPort() {
    close(serial_port);
    cout << "Serial port closed successfully.\n" << endl;
}

int SerialPort::read_line(char *buffer, unsigned int size) {

	if( !serial_port ) {
		cout << "Error: Serial port not open." << endl;
		return -1;
	}

        /*
         * Read a whole string from the GPS device into the buffer
         * making sure that the size of the buffer is not exceeded
         * in the process.
         */

        unsigned int eos = 0;           // End of string flag
        unsigned int buffer_pos = 0;    // Buffer position
        char c;                // Char read from port

        while (!eos && (buffer_pos < size) )
        {

                if (read(serial_port, &c,1)) // Read in a char; returns true if char waiting
                {
                        *buffer = c;     // Assign char to NMEA string buffer;

                        buffer ++;       // Increment the buffer
                        buffer_pos ++;   // Increment the buffer

                        if (c == '\n')   // Check for end of string
                           eos = 1;

                }

        }

        *buffer = '\0';    // Terminate the end of the buffer

	return buffer_pos;

}

void SerialPort::flush_buffer() {
	tcflush(serial_port, TCIFLUSH);
}
