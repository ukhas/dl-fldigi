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

#include "rx_extract.h"
#include "main.h"
#include "status.h"
#include "fl_digi.h"
#include "configuration.h"

//jcoxon
#include "extra.h"
#include <algorithm>

#include "confdialog.h"
#include "main.h"
//

#include "dl_fldigi.h"


using namespace std;

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

const char *end = "\n";
#ifdef __WIN32__
const char *txtWrapInfo = "\
Detect the occurance of [WRAP:beg] and [WRAP:end]\n\
Save tags and all enclosed text to date-time stamped file, ie:\n\n\
    NBEMS.files\\WRAP\\recv\\extract-20090127-092515.wrap";
#else
const char *txtWrapInfo = "\
Detect the occurance of [WRAP:beg] and [WRAP:end]\n\
Save tags and all enclosed text to date-time stamped file, ie:\n\n\
    ~/NBEMS.files/WRAP/recv/extract-20090127-092515.wrap";
#endif

#define   bufsize  16
char  rx_extract_buff[bufsize + 1];
string rx_buff;
string rx_extract_msg;
bool extracting = false;
bool bInit = false;

char dttm[64];

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
	const char* beg = "$$";
//
	if ( strstr(rx_extract_buff, beg) != NULL ) {
		rx_buff = beg;
		rx_extract_msg = "Extracting";

		put_status(rx_extract_msg.c_str(), 60, STATUS_CLEAR);

		memset(rx_extract_buff, ' ', bufsize);
		extracting = true;
	} else if (extracting) {
		rx_buff += ch;
		if (strstr(rx_extract_buff, end) != NULL) {
			struct tm tim;
			time_t t;
			time(&t);
	        gmtime_r(&t, &tim);
			strftime(dttm, sizeof(dttm), "%Y%m%d-%H%M%S", &tim);

			string outfilename = WRAP_recv_dir;
			outfilename.append("extract-");
			outfilename.append(dttm);
			outfilename.append(".wrap");
			ofstream extractstream(outfilename.c_str(), ios::binary);
			if (extractstream) {
				extractstream << rx_buff;
				extractstream.close();
			}
			rx_extract_msg = "File saved in ";
			rx_extract_msg.append(WRAP_recv_dir);
			put_status(rx_extract_msg.c_str(), 20, STATUS_CLEAR);

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
			min_number_fields = atoi(progdefaults.xmlFields.c_str());
			
			//Check rules - telem string length and number of fields and whether each field has been validated
			if ((rx_buff.length() < total_string_length) and (number_commas == min_number_fields - 1)) {
					string identity_callsign = (progdefaults.myCall.empty() ? "UNKNOWN" : progdefaults.myCall.c_str());
					UpperCase (identity_callsign);

					//We really don't want people sending status updates from UNKNOWN - somehow need to remind people to change their callsign
					if (identity_callsign != "UNKNOWN") { 
						dl_fldigi_post(rx_buff.c_str(), identity_callsign.c_str());

						rx_extract_msg = "Data posted to server";
						put_status(rx_extract_msg.c_str(), 20, STATUS_CLEAR);
					}
					else {
#if !defined(__CYGWIN__)
						cout << "Need to enter a callsign, please go to 'Configure' then 'Operator' and add a callsign/nickname.\n";
#endif
					}
					
			
					habCustom->value(rx_buff.c_str());
			}
//
			rx_extract_reset();
		} else if (rx_buff.length() > 16384) {
			rx_extract_msg = "Extract length exceeded 16384 bytes";
			put_status(rx_extract_msg.c_str(), 20, STATUS_CLEAR);
			rx_extract_reset();
		}
	}
}
