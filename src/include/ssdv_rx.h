
#ifndef _SSDV_RX_H
#define _SSDV_RX_H

class ssdv_rx : public Fl_Double_Window
{
private:
	/* UI */
	
	Fl_Box *box;
	Fl_RGB_Image *flrgb;
	
	Fl_Box *flimageid; /* Current Image ID */
	Fl_Box *flblock; /* Last Block ID */
	Fl_Box *flsize;
	Fl_Box *flmissing;
	Fl_Box *fltodo;
	Fl_Box *flfixes;
	
	Fl_Progress *flprogress;

	/* RX buffer */
	static const int PACKET_SIZE = 256;
	static const int PAYLOAD_SIZE = 218;
	static const int BUFFER_SIZE = PACKET_SIZE * 2;

	uint8_t *buffer;
	int bc;
	int bl;
	
	/* JPEG and RGB image buffer */
	uint8_t *jpeg;
	uint8_t *image;
	
	/* Last packet details */
	int pkt_blockno;
	int pkt_imageid;
	int pkt_filesize;
	
	/* Image counters */
	int img_imageid;
	int img_filesize;
	int img_lastblock;
	int img_blocks;
	int img_errors;
	int img_missing;
	int img_received;
	
	/* Private functions */
	void feed_buffer(uint8_t byte);
	int have_packet();
	void render_image();
	void new_image();
	
public:
	ssdv_rx(int w, int h, const char *title);
	~ssdv_rx();
	
	void put_byte(uint8_t byte);
};

#endif
