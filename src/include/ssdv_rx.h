
#include <time.h>

#ifndef _SSDV_RX_H
#define _SSDV_RX_H

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Image.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Scroll.H>

#include "ssdv.h"

class ssdv_rx : public Fl_Double_Window
{
private:
	/* UI */
	
	Fl_Scroll *scroll;
	Fl_Box *box;
	Fl_RGB_Image *flrgb;
	
	Fl_Box *flimageid; /* Current Image ID */
	Fl_Box *flreceived;
	Fl_Box *flcallsign;
	Fl_Box *flsize;
	Fl_Box *flmissing;
	Fl_Box *flfixes;
	
	Fl_Progress *flprogress;
	
	/* RX buffer */
	static const int BUFFER_SIZE = SSDV_PKT_SIZE * 2;
	
	uint8_t *buffer;
	uint8_t *erasures;
	int bc;
	int bl;
	
	/* Packet and RGB image buffer */
	uint8_t *packets;
	int packets_len;
	uint8_t *image;
	size_t image_len;
	
	/* Last packet details */
	ssdv_packet_info_t pkt_info;
	
	/* Image details */
	time_t image_timestamp;
	uint32_t image_callsign;
	int image_id;
	int image_width;
	int image_height;
	int image_mcu_mode;
	int image_received_packets;
	int image_lost_packets;
	int image_errors;
	
	/* Private functions */
	void feed_buffer(uint8_t byte, uint8_t erasure);
	void clear_buffer();
	void upload_packet(int fixes);
	void save_image(uint8_t *jpeg, size_t length);
	void render_image(uint8_t *jpeg, size_t length);
	
public:
	ssdv_rx(int w, int h, const char *title);
	~ssdv_rx();
	
	void put_byte(uint8_t byte, int lost);
};

#endif
