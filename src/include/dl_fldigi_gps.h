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

#define MAX_NMEA_STRING 82+1

class GPS {
    private:
        long gps_time ;
        char gps_date [9];
        double gps_lat;
        double gps_lng;
        long gps_alt;
        double gps_hdg;
        double gps_spd;
        int  gps_sats;
        int  gps_fmt; 
        int  gps_status;
        int  gps_format_pos;

        void parse_GGA (const char * gps_string);
        void parse_RMC (const char * gps_string);
    public:
        GPS();
//        ~GPS();
        int check_string (const char * gps_string);
        void parse_string (const char * gps_string);
        void print_string (void);
	void lat_lng_alt (double &lat, double &lng, long &alt);
        void post_string (char * string);
        bool data_ready (void);

};

