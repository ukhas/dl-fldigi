/*
 * Robert Harrison
 * rharrison (email at sign) hgf.com
 * August 2008
 *
 * Version 0.1 Beta
 *
 * This is a small C++ class to pass NMEA 0183 output on the GPS serial port 
 * and produce sentences used by the tracker on the UKHAS wiki.
 *
 * NMEA Protocol
 *
 * NMEA data is sent in 8-bit ASCII where the MSB is set to zero (0). 
 * The specification also has a set of reserved characters. These characters 
 * assist in the formatting of the NMEA data string. 
 *
 * The specification also states valid characters and gives a table of 
 * these characters ranging from HEX 20 to HEX 7E.
 *
 * As stated in the NMEA 0183 specification version 3.01 the maximum number 
 * of characters shall be 82, consisting of a maximum of 79 characters between 
 * start of message $ and terminating delimiter <CR><LF> 
 * (HEX 0D and 0A). The minimum number of fields is one (1).
 *
 * Basic sentence format:
 *
 * $aaccc,c--c*hh<CR><LF>
 *
 * $             Start of sentence
 * aaccc         Address field/Command
 * “,”           Field delimiter (Hex 2C)
 * c--c          Data sentence block
 * *             Checksum delimiter (HEX 2A)
 * hh            Checksum field (the hexadecimal value represented in ASCII)
 * <CR><LF>      End of sentence (HEX OD OA)
 *
 */

#define DL_FLDIGI_DEBUG

#include "dl_fldigi_gps.h"
#include <iostream>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

using namespace std;

enum ts_status {ts_ready, ts_pending, ts_printed}; 
enum ts_options {ts_hms_fmt, ts_dec_fmt, ts_url_fmt, ts_email_fmt}; 

GPS::GPS ()
{
	gps_time=0;
        gps_date[0]='\0';
        gps_lat=0;
        gps_lng=0;
        gps_alt=0;
        gps_hdg=0;
        gps_spd=0;
	gps_status=ts_printed;
}

void GPS::lat_lng_alt (double &lat, double &lng, long &alt)
{
	lat = gps_lat;
	lng = gps_lng;
	alt = gps_alt;
	gps_status=ts_printed;
}

void GPS::print_string (void)
{
	cout << "time:" << gps_time << endl;
	cout << "date:" << gps_date << endl;
	cout << "latitude:" << gps_lat << endl;
	cout << "longitude:" << gps_lng << endl;
	cout << "altitude:" << gps_alt << endl;
	cout << "heading:" << gps_hdg << endl;
	cout << "speed:" << gps_spd << endl;
	cout << "status:" << gps_status << endl;
}

int GPS::check_string (const char * gps_string)
{

	/*
	 * Check the string contains only valid NMEA chars
	 * and count the number of fields.
	 *
	 * Returns number of fields on success 0 on failure
	 */

	int char_count = 0;
	int field_count = 0;
	bool invalid_chars = false;
	char c = 32;

	while ((char_count < MAX_NMEA_STRING) && (c != '\0'))
	{
		c = gps_string[char_count];
	
		if (c != '\0') 
		{
			/* Check if chars are valid */
			if ( (c < 0x20 || c > 0x7e) && (c !=0xa ) ) 
				invalid_chars = true;
			
			/* Increment the field count if comma */
			if (c == ',') field_count ++;
		}

		char_count ++;
	}

	if (invalid_chars) 
		return 0; 
	else
		return field_count ++;

}

void GPS::parse_GGA (const char * string)
{
        /*
         * Efficiency single pass of string
         */

        int char_count = 0;  // Char counter for parsing string
        int field_count = 1; // We start in the first field
        int field_pos = 0;   // Char counter within field
        char c = ' ';        // Must be init to anything other than \0
	char tmp_string [20];// Temporary storage for fields
	long int mins = 0;
		
			
	while ((char_count < MAX_NMEA_STRING) && (c != '\0'))
	{
		c = string[char_count];

		if (c != '\0')
		{
			
			switch (field_count)
			{
				case 1: // Sentence type ($GPGGA in this case)
				break;
				case 2: // Time ignore any values after dp
					if (c == ',')
					{
						tmp_string[field_pos] = '\0';
						gps_time = atol(tmp_string);
					}
					else
					{
						if (c == '.')
						{
							tmp_string[field_pos] = '\0';
						}
						else
						{
							tmp_string[field_pos] = c;
						}
						field_pos ++;
					}
				break;

				case 3: // Latitude
					if (c == ',')
					{
						tmp_string[field_pos] = '\0';
										
						gps_lat = floor(atof(tmp_string)/100);		

						// Set string to the minutes value
						tmp_string[0]=tmp_string[2];
						tmp_string[1]=tmp_string[3];
						tmp_string[2]=tmp_string[5];
						tmp_string[3]=tmp_string[6];
						tmp_string[4]=tmp_string[7];
						tmp_string[5]=tmp_string[8];
						tmp_string[6]='\0';

						mins = atol(tmp_string);

						// Convert to degree decimal by dividing by 60
						mins = mins*10/6;	
	
						gps_lat += (mins/1000000.0);
					}
					else
					{
						tmp_string[field_pos] = c;
						field_pos ++;
					}
				break;

				case 4: // Latitude Direction
					if (c == 'S')
					{
						gps_lat *= -1;
					}
				break;

				case 5: // Longitude
					if (c == ',')
					{
						tmp_string[field_pos] = '\0';

						gps_lng = floor(atof(tmp_string)/100);	
													
						// Set string to the minutes value
						tmp_string[0]=tmp_string[3];
						tmp_string[1]=tmp_string[4];
						tmp_string[2]=tmp_string[6];
						tmp_string[3]=tmp_string[7];
						tmp_string[4]=tmp_string[8];
						tmp_string[5]=tmp_string[9];
						tmp_string[6]='\0';

						mins = atol(tmp_string);

						// Convert to degree decimal by dividing by 60
						mins = mins*10/6;			
								
						gps_lng += (mins/1000000.0);
					}
					else
					{
						tmp_string[field_pos] = c;
						field_pos ++;
					}
				break;

				case 6: // Longitude Direction
					if (c == 'W')
					{
						gps_lng *= -1;
					}
				break;

				case 7: // Fix quality
				break;

				case 8: // Satellites being tracked
				break;

				case 9: // Hoz. dilution of position
				break;

				case 10: // Altitude in meters with respect to sea level
					if (c == ',')
					{
						tmp_string[field_pos] = '\0';
							
						gps_alt = atol(tmp_string);
							
					}
					else
					{
						tmp_string[field_pos] = c;
						field_pos ++;
					}
				break;

				case 11: // Altitude units (Meters)
				break;

				case 12: // Height of geiod
				break;

				case 13: // Height Units (Meters)
				break;

				case 14: // Time in seconds since last DGPS update (empty field)
				break;

				case 15: // DGPS station ID number (empty field)
				break;

				case 16: // Checksum
				break;

			}

			// Increment the field count if comma
			// and set the field position to 0 
			if (c == ',')
			{
				field_count ++;
				field_pos = 0;
			}
		}
		char_count ++;
	} // End while
		
        /*
         * Set the status for the track string
         * NB we need to parse a GGA and a RMC
         * NMEA string before the track string
         * is valid.
         */

        if (gps_status == ts_printed)
        {
                gps_status = ts_pending;
        }
        else
        {
                gps_status = ts_ready;
        }

}


void GPS::parse_RMC (const char * string)
{
        /*
         * Efficiency single pass of string
         */

        int char_count = 0;  // Char counter for parsing string
        int field_count = 1; // We start in the first field
        int field_pos = 0;   // Char counter within field
        char c = ' ';        // Must be init to anything other than \0
	char tmp_string [20];// Temporary storage for fields
		
	/* Test that lat & lng is set before updating the structure
	*/
	/* Re using vars here to save space 
	*/
	
	// If lat and long less than this then assume no GPS fix

	while ((char_count < MAX_NMEA_STRING) && (c != '\0'))
	{
		c = string[char_count];

		if (c != '\0')
		{
			switch (field_count)
			{
				case 1: // Sentence type ($GPRMC in this case)
				break;

				case 2: // Time ignore any values after dp
				break;
								
				case 4: // Latitude
				break;

				case 5: // Latitude Direction
				break;

				case 6: // Longitude
				break;

				case 7: // Longitude Direction
				break;	
									
				case 8: // Speed x.xx
					if (c == ',')
					{
						tmp_string[field_pos] = '\0';

						gps_spd = atof(tmp_string);
													
						// Convert from knots to km/h
						gps_spd *= 1.852;

						if (gps_spd < 1) gps_spd = 0;
					}
					else
					{
						tmp_string[field_pos] = c;
						field_pos ++;
					}
				break;

				case 9: // Heading
					if (c == ',')
					{
						tmp_string[field_pos] = '\0';
						gps_hdg = atof(tmp_string);

						if (gps_spd < 1) gps_hdg = 0;

					}
					else
					{
						tmp_string[field_pos] = c;
						field_pos ++;
					}
				break;

				case 10: // Date
					if (c == ',')
					{
						gps_date[field_pos] = '\0';
					}
					else
					{
						gps_date[field_pos] = c;
						field_pos ++;
					}
				break;

				case 11: // Magnetic Variation
				break;

				case 12: // Checksum
				break;

			}

			/* Increment the field count if comma
			* and set the field position to 0 */
			if (c == ',')
			{
				field_count ++;
				field_pos = 0;
			}
		}
		char_count ++;
	} // End While
		
        /*
         * Set the status for the track string
         * NB we need to parse a GGA and a RMC
         * NMEA string before the track string
         * is valid.
         */

        if (gps_status == ts_printed)
        {
                gps_status = ts_pending;
        }
        else
        {
                gps_status = ts_ready;
        }

}

bool GPS::parse_string (const char * gps_string)
{
	/*
	 * Check for GGA and RMC sentences and use data
	 * to fill the track_string structure
	 */

        #ifdef DL_FLDIGI_DEBUG
                fprintf(stderr, "dl_fldigi: GPS parsing string\n", gps_string);
        #endif

	if (check_string(gps_string))
	{
		if ( strncmp("$GPGGA", gps_string,6) == 0 )
		{
        	        #ifdef DL_FLDIGI_DEBUG
                		fprintf(stderr, "dl_fldigi: GPS found GPGGA\n", gps_string);
		        #endif

			parse_GGA (gps_string);
			return true;
		}

                #ifdef DL_FLDIGI_DEBUG
                        fprintf(stderr, "dl_fldigi: GPS discarded non GPGGA string.\n", gps_string);
                #endif

	/*
 		if ( strncmp("$GPRMC", gps_string,6) == 0 )
		{
			parse_RMC (gps_string);
		}
	 */
	}
	else
	{
		fprintf(stderr, "dl_fldigi: GPS: Corrupted or invalid string\n");
	}

	return false;
}

bool GPS::data_ready (void)
{
	/*
	 * For the GPS structure to be up to date it 
         * requires information from $GPGGA and $GPRMC
         * The status is set to ts_ready when both sentances
         * have been processed.
         *
         * This function returns TRUE when the status = tx_ready
         * else it returns FALSE
         */

	 if (gps_status == ts_ready)
		return true;
	 else
		return false;
}
//
