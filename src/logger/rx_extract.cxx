// ----------------------------------------------------------------------------
// rx_extract.cxx extract delineated data stream to file
//
// Copyright 2009 W1HKJ, Dave Freese
//
// This file is part of fldigi.
//
// Fldigi is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Fldigi is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with fldigi.  If not, see <http://www.gnu.org/licenses/>.
// ----------------------------------------------------------------------------

#include <config.h>

#include <iostream>
#include <fstream>
#include <string>

#include <FL/filename.H>
#include "fileselect.h"

#include "gettext.h"
#include "rx_extract.h"
#include "main.h"
#include "status.h"
#include "fl_digi.h"
#include "configuration.h"
#include "confdialog.h"
#include "debug.h"
#include "icons.h"
#include "qrunner.h"

using namespace std;

//jcoxon
#include "extra.h"
#include <algorithm>

#include "confdialog.h"
#include "main.h"
//

#include "trx.h"

#include "dl_fldigi.h"

#include <stdio.h>   /* Standard input/output definitions */
#include <stdlib.h>  /* Standard stuff like exit */
#include <math.h>
#include <time.h>
 
#include <fcntl.h>   /* File control definitions */
#include <errno.h>   /* Error number definitions */
 
#include <unistd.h>  /* UNIX standard function definitions */

using namespace std;

//****************************************
//Taken from Steve Randall's Navsoft code.

 
typedef struct {
	float lat;
	float lon;
} coordinate;

typedef struct {
	float bearing;
	float distance;
} balloonvector;
 
#define DEG2KM(a)	(a * (float)111.226306)	// Degrees (of latitude) to Kilometers multiplier
#define DEG2RAD(a)	(a * (float)0.0174532925)	// Degrees to Radian multiplier
#define RAD2DEG(a) 	(a * (float)57.2957795)	// Radian to Degrees multiplier

float targetLat;
float targetLon;
float presentLat;
float presentLon;

balloonvector Coords_to_bearing_and_distance(coordinate posn, coordinate dest)
{
 
	float delta_lat, delta_lon;
	balloonvector result;
 
	delta_lat = dest.lat - posn.lat;
	delta_lon = (dest.lon - posn.lon) * (float)cos(DEG2RAD((dest.lat + posn.lat)/2));
 
	result.distance = DEG2KM((float)sqrt(((delta_lat) * (delta_lat)) + ((delta_lon) * (delta_lon)))); // pythagerious
 
	// calcualte compass bearing degrees clockwise from north (0 - 360)
	// atan2(y,x) produces the euclidean angle (+ve = counter-clockwise from x axis in radians)
	// atan2(d_lon,d_lat) produces the compass angle (+ve = clockwise from N /-ve counter-clockwise)
 
	result.bearing = RAD2DEG((float)atan2(delta_lon,delta_lat)); // atan2 argument inversion to get compass N based co-ordinates
 
	if (result.bearing < 0.0)
		result.bearing += 360; // convert to 0-360
 
	return(result);
}
//****************************************
//jcoxon
void UpperCase(string& str)
{
	for(unsigned int i = 0; i < str.length(); i++)
	{
		str[i] = toupper(str[i]);
	}
	return;
}
//

void TrimSpaces( string& str)  
{  
	
	// Trim Both leading and trailing spaces  
	size_t startpos = str.find_first_not_of(" "); // Find the first character position after excluding leading blank spaces  
	size_t endpos = str.find_last_not_of("\r\n");  // Find the first character position from reverse af  

	// if all spaces or empty return an empty string  
	if(( string::npos == startpos ) || ( string::npos == endpos))  
	{  
		str = "";  
	}  
	else  
		str = str.substr( startpos, endpos-startpos+1 );  
} 

const char *beg = "[WRAP:beg]";
const char *end = "[WRAP:end]";
const char *flmsg = "<flmsg>";

#ifdef __WIN32__
const char *txtWrapInfo = _("\
Detect the occurance of [WRAP:beg] and [WRAP:end]\n\
Save tags and all enclosed text to date-time stamped file, ie:\n\
    NBEMS.files\\WRAP\\recv\\extract-20090127-092515.wrap");
#else
const char *txtWrapInfo = _("\
Detect the occurance of [WRAP:beg] and [WRAP:end]\n\
Save tags and all enclosed text to date-time stamped file, ie:\n\
    ~/.nbems/WRAP/recv/extract-20090127-092515.wrap");
#endif

#define   bufsize  16
char  rx_extract_buff[bufsize + 1];
string rx_buff;
string rx_extract_msg;
bool extracting = false;
bool bInit = false;

//jcoxon
//Default rules
unsigned int total_string_length = 100;
int min_number_fields = 10;
int field_length = 10;

int dodge_data = 0;
bool validate_output;
int number_commas;
int old_i = 0, field_number = 0;
string rx_buff_edit;
string tmpfield;
//

static void rx_extract_update_ui(string rx_buff);
int test_checksum(string s);

void rx_extract_reset()
{
	rx_buff.clear();
	memset(rx_extract_buff, ' ', bufsize);
	rx_extract_buff[bufsize] = 0;
	extracting = false;
}

void rx_extract_add(int c)
{
	if (!c) return;
	check_nbems_dirs();

	if (!bInit) {
		rx_extract_reset();
		bInit = true;
	}
	char ch = (char)c;

	memmove(rx_extract_buff, &rx_extract_buff[1], bufsize - 1);
	rx_extract_buff[bufsize - 1] = ch;
//jcoxon
	//Reads the stentence delimter previously read from the xml file.
	//const char* beg = (progdefaults.xmlSentence_delimiter.empty() ? "UNKNOWN" : progdefaults.xmlSentence_delimiter.c_str());
	string beg_s = "$$" + progdefaults.xmlCallsign;
	const char* beg = beg_s.c_str();
//
	if ( strstr(rx_extract_buff, beg) != NULL ) {
		put_status("dl_fldigi: detected sentence start; extracting!", 10);
		if(extracting)
		{
			/* Was already extracting... reset */
			rx_extract_reset();
			active_modem->track_freq_lock--;
		}
		
		rx_buff = beg;
		extracting = true;
		active_modem->track_freq_lock++;
	} else if (extracting) {
		rx_buff += ch;
		if (strstr(rx_extract_buff, "\n") != NULL) {
			
//jcoxon
			//Trim Spaces
			TrimSpaces(rx_buff);
			
			// Find the sentence start marker and remove up to the end of it
			// dkjhdskdkfdakhd $$icarus,...   -> icarus,...

			rx_buff = rx_buff.substr(
				rx_buff.find(progdefaults.xmlSentence_delimiter)+
				progdefaults.xmlSentence_delimiter.length());
			//I've removed the old swap callsign function as its not needed any longer.

			//Counts number of fields
			number_commas = count(rx_buff.begin(), rx_buff.end(), progdefaults.xmlField_delimiter.at(0));
			
			//Gets info for number of fields
			min_number_fields = progdefaults.xmlFields;
			
			//Check rules - telem string length and number of fields and whether each field has been validated

			// Old copy:
			// if ((rx_buff.length() < total_string_length) and (number_commas == min_number_fields - 1)) { 

			
			if (progdefaults.xml_stringlimit > (int) total_string_length)
			{
				total_string_length = progdefaults.xml_stringlimit;
			}
			
			// FIXME: For the purposes of testing we won't check min_number_fields
			if (rx_buff.length() < total_string_length) {
					string identity_callsign = (progdefaults.myCall.empty() ? "UNKNOWN" : progdefaults.myCall.c_str());
					UpperCase (identity_callsign);

					/* RJH Post Chase Car information */
					/* Not yet implemented (TODO) dl_fldigi_post_gps(); */
					int pos, lockstatus = 1;
					string extractedField, remainingString = rx_buff;
				
					for ( int x = 1; x < (number_commas + 1); x++ ) {
						pos = remainingString.find(progdefaults.xmlField_delimiter.at(0));
						extractedField = remainingString.substr(0, pos);
						remainingString.erase(0, (pos + 1));
						if (x == progdefaults.xml_lockstatus) {
							lockstatus = atoi(extractedField.c_str());
							printf("Lockstatus = %d\n", lockstatus);
						}
					}
				
					if((test_checksum(rx_buff) == true) && (lockstatus > 0)) {
						/* dl_fldigi_post will put_status as it does its stuff */
						dl_fldigi_post(rx_buff.c_str(), identity_callsign.c_str());
					}
				
					if(bHAB)
					{
						REQ(rx_extract_update_ui, rx_buff);
						REQ(dl_fldigi_reset_rxtimer);
					}
			}
			
			rx_extract_reset();
			active_modem->track_freq_lock--;
		} else if (rx_buff.length() > 200) {
			put_status("dl_fldigi: extract buffer exeeded 200 bytes", 20, STATUS_CLEAR);
			rx_extract_reset();
			active_modem->track_freq_lock--;
		}
	}
}

/* CRC and checksum calculators -- these should really be separated out into
 * their own file. */
uint16_t crc_xmodem_update(uint16_t crc, uint8_t data)
{
	int i;
	
	crc = crc ^ ((uint16_t) data << 8);
	for(i = 0; i < 8; i++)
	{
		if(crc & 0x8000) crc = (crc << 1) ^ 0x1021;
		else crc <<= 1;
	}
	
	return(crc);
}

uint8_t gps_xor_checksum(char *s)
{
	uint8_t x;
	
	for(x = 0; *s; s++)
		x ^= (uint8_t) *s;
	
	return(x);
}

uint16_t gps_CRC16_checksum(char *s)
{
	uint16_t x;
	
	for(x = 0xFFFF; *s; s++)
		x = crc_xmodem_update(x, (uint8_t) *s);
	
	return(x);
}

int test_checksum(string s)
{
	size_t i;
	uint16_t checksum, x;
	string checkstr;
	
	/* Test both the ukhas checksum formats */
	/* See: http://ukhas.org.uk/communication:protocol */
	
	i = s.find("*");
	if(i == string::npos) return(false);
	
	checkstr = s.substr(i + 1);
	checksum = strtol(checkstr.c_str(), NULL, 16);
	
	/* Remove the checksum from the string to be tested */
	s.resize(i);
	
	if(checkstr.length() == 4) x = gps_CRC16_checksum((char *) s.c_str());
	else if(checkstr.length() == 2) x = gps_xor_checksum((char *) s.c_str());
	else return(false);
	
	if(x != checksum) return(false);
	
	return(true);
}

void rx_extract_update_ui(string rx_buff)
{
		int pos, asterixPosition = 0;
		string extractedField, remainingString = rx_buff, checksumData, customData;
		
		balloonvector target_vector;
		coordinate presentCoords;
		coordinate targetCoords = {0, 0};
		
		habCustom->value(rx_buff.c_str());
		
		/* Don't display bad data */
		if(test_checksum(rx_buff) == false)
		{
			habCustom->color(FL_RED);
			printf("Checksum failed, not displaying\n");
			return;
		}
		
		asterixPosition = rx_buff.find("*");
		if (asterixPosition > 0)
		{
			checksumData = remainingString.substr(asterixPosition);
			remainingString.erase(asterixPosition);
			habChecksum->value(checksumData.c_str());
		}
		
		for ( int x = 1; x <= (number_commas + 1); x++ ) {
			pos = remainingString.find(progdefaults.xmlField_delimiter.at(0));
			if(pos < 0) pos = remainingString.length();
			extractedField = remainingString.substr(0, pos);
			remainingString.erase(0, (pos + 1));
		if (x == progdefaults.xml_time) {
				habTime->value(extractedField.c_str());
		}
		else if (x == progdefaults.xml_latitude) {
			char s[20];
			double lat;
			
			lat = atof(extractedField.c_str());
			if(progdefaults.xml_latitude_nmea)
			{
				double in, fr = modf(lat / 100.0, &in);
				lat = in + (fr * (100.0 / 60.0));
			}
			
			snprintf(s, 20, "%.4f", lat);
			habLat->value(s);
			targetCoords.lat = lat;
		}
		else if (x == progdefaults.xml_longitude) {
			char s[20];
			double lon;
			
			lon = atof(extractedField.c_str());
			if(progdefaults.xml_longitude_nmea)
			{
				double in, fr = modf(lon / 100.0, &in);
				lon = in + (fr * (100.0 / 60.0));
			}
			targetCoords.lon = lon;
			
			snprintf(s, 20, "%.4f", lon);
			habLon->value(s);
		}
		else if (x == progdefaults.xml_altitude) {
			habAlt->value(extractedField.c_str());
		}
		else if (x == progdefaults.xml_lockstatus) {
			int lockstatus = atoi(extractedField.c_str());
			printf("Lockstatus = %d\n", lockstatus);
			if (lockstatus < 1)
			{
				habCustom->color(FL_YELLOW);
				return;
			}
		}
		habCustom->color(FL_GREEN);

	}
	if(progdefaults.myLat.length() > 0 && progdefaults.myLon.length() > 0) {
		presentCoords.lat = dl_fldigi_geotod((char *) progdefaults.myLat.c_str());
		presentCoords.lon = dl_fldigi_geotod((char *) progdefaults.myLon.c_str());
		target_vector = Coords_to_bearing_and_distance(presentCoords, targetCoords);

		printf("Target bearing = %fdeg, distance %fKm\n",target_vector.bearing,target_vector.distance);
		char target_vector_bearing[10];
		char target_vector_distance[10];
		sprintf(target_vector_bearing, "%8.1f",target_vector.bearing);
		habBearing->value(target_vector_bearing);
		sprintf(target_vector_distance, "%8.1f",target_vector.distance);
		habDistance->value(target_vector_distance);
	}
}

void select_flmsg_pathname()
{
#ifdef __APPLE__
	open_recv_folder("/Applications/");
	return;
#else
	string deffilename = progdefaults.flmsg_pathname;
	if (deffilename.empty())
#  ifdef __MINGW32__
		deffilename = "C:\\Program Files\\";
		const char *p = FSEL::select(_("Locate flmsg executable"), _("flmsg.exe\t*.exe"), deffilename.c_str());
#  else
		deffilename = "/usr/local/bin/";
		const char *p = FSEL::select(_("Locate flmsg executable"), _("flmsg\t*"), deffilename.c_str());
# endif
	if (p) {
		progdefaults.flmsg_pathname = p;
		progdefaults.changed = true;
		txt_flmsg_pathname->value(p);
	}
#endif
}
