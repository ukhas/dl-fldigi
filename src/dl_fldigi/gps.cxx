/* 
 * Copyright (C) 2011 Daniel Richman, Robert Harrison,
 * License: GNU GPL 3
 *
 * gps.cxx: Threaded (async) serial GPS uploading support
 */

#include "dl_fldigi/gps.h"

#include <string>
#include <sstream>
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef __MINGW32__
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#else
#include <windows.h>
#endif

#include <FL/Fl.H>

#include "configuration.h"
#include "confdialog.h"
#include "debug.h"
#include "fl_digi.h"

#include "jsoncpp.h"

#include "dl_fldigi/dl_fldigi.h"
#include "dl_fldigi/location.h"
#include "dl_fldigi/hbtint.h"

using namespace std;

namespace dl_fldigi {
namespace gps {

static GPSThread *gps_thread;

void cleanup()
{
    if (gps_thread)
        gps_thread->shutdown();

    while (gps_thread)
        Fl::wait();
}

void configure_gps()
{
    /* If it's running we need to shut it down to either stop GPS or change
     * its settings. */
    if (gps_thread != NULL)
    {
        /* Wait for it to die. gps_thread_death will call this function
         * again */
        gps_thread->shutdown();
        return;
    }

    if (gps_thread == NULL &&
        location::current_location_mode == location::LOC_GPS &&
        progdefaults.gps_device.size() && progdefaults.gps_speed >= 0 &&
        progdefaults.gps_period >= 1)
    {
        gps_thread = new GPSThread(progdefaults.gps_device,
                                   progdefaults.gps_speed,
                                   progdefaults.gps_period);
        gps_thread->start();
    }
}

static void gps_thread_death(void *what)
{
    if (what != gps_thread)
    {
        LOG_ERROR("unknown thread");
        return;
    }

    LOG_INFO("cleaning up");

    gps_thread->join();
    delete gps_thread;
    gps_thread = 0;

    if (!dl_fldigi::shutting_down)
        configure_gps();
}

/* On Windows we have to wait for a timeout to wake the GPS thread up
 * to terminate it. This sucks. But on systems that support signals,
 * it will cut short the last timeout */
#ifndef __MINGW32__
void GPSThread::prepare_signals()
{
    sigset_t usr2;

    sigemptyset(&usr2);
    sigaddset(&usr2, SIGUSR2);
    pthread_sigmask(SIG_UNBLOCK, &usr2, NULL);
}

void GPSThread::send_signal()
{
    pthread_kill(thread, SIGUSR2);
}

void GPSThread::wait()
{
    /* On error. Wait for 1, 2, 4... 64 seconds */
    sleep(1 << wait_exp);

    if (wait_exp < 6)
        wait_exp++;
}
#else
void GPSThread::prepare_signals() {}
void GPSThread::send_signal() {}

void GPSThread::wait()
{
    int wait_seconds = 1 << wait_exp;

    if (wait_exp < 6)
        wait_exp++;

    for (int i = 0; i < wait_seconds && !check_term(); i++)
        Sleep(1000);
}
#endif

void GPSThread::shutdown()
{
    set_term();
    send_signal();
}

void GPSThread::set_term()
{
    EZ::MutexLock lock(mutex);
    term = true;
}

bool GPSThread::check_term()
{
    bool b;
    EZ::MutexLock lock(mutex);
    b = term;
    if (b)
        log("term = true");
    return b;
}

void *GPSThread::run()
{
    prepare_signals();

    while (!check_term())
    {
        try
        {
            setup();
            log("Opened device " + device);
            while (!check_term())
            {
                read();
                /* Success? reset wait */
                wait_exp = 0;
            }
        }
        catch (runtime_error e)
        {
            warning(e.what());
            cleanup();
            wait();
        }
    }

    Fl::awake(gps_thread_death, this);
    return NULL;
}

void GPSThread::warning(const string &message)
{
    Fl_AutoLock lock;
    LOG_WARN("hbtGPS %s", message.c_str());

    string temp = "GPS Error " + message;
    status_important(temp);
}

void GPSThread::log(const string &message)
{
    Fl_AutoLock lock;
    LOG_DEBUG("hbtGPS %s", message.c_str());
}

static bool check_gpgga(const vector<string> &parts)
{
    if (parts.size() < 7)
        return false;

    if (parts[0] != "$GPGGA")
        return false;

    /* Fix quality field */
    if (parts[6] == "0")
        return false;

    for (int i = 1; i < 7; i++)
        if (!parts[i].size())
            return false;

    return true;
}

static void split_nmea(const string &data, vector<string> &parts)
{
    size_t a = 0, b = 0;

    while (b != string::npos)
    {
        string part;
        b = data.find_first_of(",*", a);

        if (b == string::npos)
            part = data.substr(a);
        else
            part = data.substr(a, b - a);

        parts.push_back(part);
        a = b + 1;
    }
}

static string parse_hms(const string &part)
{
    ostringstream temp;
    temp << part.substr(0, 2) << ':'
        << part.substr(2, 2) << ':'
        << part.substr(4, 2);
    if (temp.str().size() != 8)
        throw runtime_error("bad NMEA time");
    return temp.str();
}

static double parse_ddm(string part, const string &dirpart)
{
    double degrees, mins;
    size_t pos = part.find('.');
    if (pos == string::npos || pos < 3)
        throw runtime_error("Bad DDM");

    /* Split degrees and minutes parts */
    part.insert(pos - 2, " ");

    istringstream tmp(part);
    tmp.exceptions(istringstream::failbit | istringstream::badbit);

    tmp >> degrees;
    tmp >> mins;

    double value = degrees + mins / 60;

    if (dirpart == "S" || dirpart == "W")
        return -value;
    else
        return value;
}

static double parse_alt(const string &part, const string &unit_part)
{
    if (unit_part != "M")
        throw runtime_error("altitude units are not M");

    istringstream tmp;
    double value;

    tmp.exceptions(istringstream::failbit | istringstream::badbit);
    tmp.str(part);
    tmp >> value;

    return value;
}

void GPSThread::read()
{
    /* Read until a newline */
    char buf[100];
    char *result;
    result = fgets(buf, sizeof(buf), f);

    if (result != buf)
        throw runtime_error("fgets read no data: EOF or error");

    /* Find the $ (i.e., discard garbage before the $) */
    char *start = strchr(buf, '$');

    if (!start)
        throw runtime_error("Did not find start delimiter");

    string data(start);
    data.erase(data.end() - 1);

    log("Read line: " + data);

    vector<string> parts;
    split_nmea(data, parts);

    if (!check_gpgga(parts))
        return;

    string time_str;
    double latitude, longitude, altitude;

    try
    {
        time_str = parse_hms(parts[1]);
        latitude = parse_ddm(parts[2], parts[3]);
        longitude = parse_ddm(parts[4], parts[5]);
        altitude = parse_alt(parts[9], parts[10]);
    }
    catch (out_of_range e)
    {
        throw runtime_error("Failed to parse data (oor)");
    }
    catch (istringstream::failure e)
    {
        throw runtime_error("Failed to parse data (fail)");
    }

    update_ui(time_str, latitude, longitude, altitude);
    upload(time_str, latitude, longitude, altitude);
}

void GPSThread::update_ui(const string &time_str,
                          double latitude, double longitude, double altitude)
{
    ostringstream lat_tmp, lon_tmp, alt_tmp;
	lat_tmp << latitude;
    lon_tmp << longitude;
    alt_tmp << altitude;

    gps_pos_time->value(time_str.c_str());
    gps_pos_lat->value(lat_tmp.str().c_str());
    gps_pos_lon->value(lon_tmp.str().c_str());
    gps_pos_altitude->value(alt_tmp.str().c_str());

    gps_pos_save->activate();
}

void GPSThread::upload(const string &time_str,
                       double latitude, double longitude, double altitude)
{
    Fl_AutoLock lock;

    LOG_DEBUG("GPS position: %s %f %f, %fM",
              time_str.c_str(), latitude, longitude, altitude);

    if (time(NULL) - last_upload < rate)
        return;

    /* Data OK? upload. */
    if (location::current_location_mode != location::LOC_GPS)
        throw runtime_error("GPS mode disabled mid-line");

    last_upload = time(NULL);

    location::listener_valid = true;
    location::listener_latitude = latitude;
    location::listener_longitude = longitude;
    location::listener_altitude = altitude;
    location::update_distance_bearing();

    ostringstream temp;

    Json::Value data(Json::objectValue);
    // data["time"] = time_str;
    data["latitude"] = latitude;
    data["longitude"] = longitude;
    data["altitude"] = altitude;
    data["chase"] = true;

    hbtint::uthr->listener_telemetry(data);
}

#ifndef __MINGW32__
void GPSThread::setup()
{
    /* Open the serial port without blocking. Rely on cleanup() */
    fd = open(device.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd == -1)
        throw runtime_error("open() failed");

    f = fdopen(fd, "r");
    if (f == NULL)
        throw runtime_error("fdopen() failed");

    /* Line buffering */
    int lbf = setvbuf(f, (char *) NULL, _IOLBF, 0);
    if (lbf != 0)
        throw runtime_error("setvbuf() failed");

    /* Linux requires baudrates be given as a constant */
    speed_t baudrate = B4800;
    if (baud == 9600)           baudrate = B9600;
    else if (baud == 19200)     baudrate = B19200;
    else if (baud == 38400)     baudrate = B38400;
    else if (baud == 57600)     baudrate = B57600;
    else if (baud == 115200)    baudrate = B115200;
    else if (baud == 230400)    baudrate = B230400;

    /* Set all the weird arcane settings Linux demands (boils down to 8N1) */
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

    /* Set raw input output */
    port_settings.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    port_settings.c_oflag &= ~OPOST;

    /* Ignore CR in NMEA's CRLF */
    port_settings.c_iflag |= (IGNCR);

    /* Blocking read until 1 character arrives */
    port_settings.c_cc[VMIN] = 1;

    /* Re enable blocking for reading. */
    int set = fcntl(fd, F_SETFL, 0);
    if (set == -1)
        throw runtime_error("fcntl() failed");

    /* All baud settings */
    set = tcsetattr(fd, TCSANOW, &port_settings);
    if (set == -1)
        throw runtime_error("tcsetattr() failed");
}

void GPSThread::cleanup()
{
    /* The various things will close their underlying fds or handles (w32).
     * Close the last thing we managed to open */
    if (f)
        fclose(f);
    else if (fd != -1)
        close(fd);

    f = NULL;
    fd = -1;
}
#else
void GPSThread::setup()
{
    HANDLE handle = CreateFile(device.c_str(), GENERIC_READ, 0, 0,
                               OPEN_EXISTING, 0, 0);
    if (handle == INVALID_HANDLE_VALUE)
        throw runtime_error("CreateFile() failed");

    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(handle, &dcbSerialParams))
        throw runtime_error("GetCommState() failed");

    dcbSerialParams.BaudRate = baud;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(handle, &dcbSerialParams))
        throw runtime_error("GetCommState() failed");

    COMMTIMEOUTS timeouts;
    timeouts.ReadIntervalTimeout            = 2000;
    timeouts.ReadTotalTimeoutMultiplier     = 0;
    timeouts.ReadTotalTimeoutConstant       = 5000;
    timeouts.WriteTotalTimeoutMultiplier    = 0;
    timeouts.WriteTotalTimeoutConstant      = 0;

    if (!SetCommTimeouts(handle, &timeouts))
        throw runtime_error("SetCommTimeouts() failed");

    fd = _open_osfhandle((intptr_t) handle, _O_RDONLY);
    if (fd == -1)
        throw runtime_error("_open_osfhandle() failed");

    f = fdopen(fd, "r");
    if (!f)
        throw runtime_error("fdopen() failed");

    /* Line buffering */
    int lbf = setvbuf(f, (char *) NULL, _IOLBF, 0);
    if (lbf != 0)
        throw runtime_error("setvbuf() failed");
}

void GPSThread::cleanup()
{
    if (f)
        fclose(f);
    else if (fd != -1)
        close(fd);
    else if (handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);

    f = NULL;
    fd = -1;
    handle = INVALID_HANDLE_VALUE;
}
#endif

} /* namespace gps */
} /* namespace dl_fldigi */
