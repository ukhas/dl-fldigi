
#include <cstdio>
#include <cstring>
#include <stdint.h>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Progress.H>
#include <setjmp.h>
#include "ssdv_rx.h"
#include "rs8.h"

#include <curl/curl.h>

/* For put_status() */
#include "fl_digi.h"

/* For progdefaults */
#include "configuration.h"

/* Used for passing curl data to post thread */
typedef struct {
	CURL *curl;
	struct curl_httppost* post;
} ssdv_post_data_t;

#if 1

#ifdef __cplusplus
extern "C" {
#endif

#include <jpeglib.h>
#include <jerror.h>

/* JPEG MEMORY SOURCE */

typedef struct {
  struct jpeg_source_mgr pub;	/* public fields */
} my_source_mgr;

METHODDEF(void)
init_source (j_decompress_ptr cinfo)
{
	/* no work necessary here */
}

METHODDEF(boolean)
fill_input_buffer (j_decompress_ptr cinfo)
{
	my_source_mgr *src = (my_source_mgr *) cinfo->src;
	static JOCTET fakeeoi[4];
	
	WARNMS(cinfo, JWRN_JPEG_EOF);
	
	/* Insert a fake EOI marker */
	fakeeoi[0] = (JOCTET) 0xFF;
	fakeeoi[1] = (JOCTET) JPEG_EOI;
	
	src->pub.next_input_byte = fakeeoi;
	src->pub.bytes_in_buffer = 2;
	
	return TRUE;
}

METHODDEF(void)
skip_input_data (j_decompress_ptr cinfo, long num_bytes)
{
	my_source_mgr *src = (my_source_mgr *) cinfo->src;
	
	if (num_bytes > 0) {
		while (num_bytes > (long) src->pub.bytes_in_buffer) {
			num_bytes -= (long) src->pub.bytes_in_buffer;
			(void) fill_input_buffer(cinfo);
			/* note we assume that fill_input_buffer will never return FALSE,
			 * so suspension need not be handled.
			 */
		}
		src->pub.next_input_byte += (size_t) num_bytes;
		src->pub.bytes_in_buffer -= (size_t) num_bytes;
	}
}

METHODDEF(void)
term_source (j_decompress_ptr cinfo)
{
	/* no work necessary here */
}

GLOBAL(void)
jpeg_memory_src(j_decompress_ptr cinfo, unsigned char *inbuffer, size_t insize)
{
	my_source_mgr *src;
	
	if(inbuffer == NULL || insize == 0) /* Treat empty input as fatal error */
		ERREXIT(cinfo, JERR_INPUT_EMPTY);
	
	/* The source object is made permanent so that a series of JPEG images
	 * can be read from a single buffer by calling jpeg_memory_src
	 * only before the first one.
	 */
	if(cinfo->src == NULL) { /* first time for this JPEG object? */
		cinfo->src = (struct jpeg_source_mgr *) (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT, sizeof(my_source_mgr));
	}
	
	src = (my_source_mgr *) cinfo->src;
	src->pub.init_source = init_source;
	src->pub.fill_input_buffer = fill_input_buffer;
	src->pub.skip_input_data = skip_input_data;
	src->pub.resync_to_restart = jpeg_resync_to_restart; /* use default method */
	src->pub.term_source = term_source;
	
	src->pub.next_input_byte = inbuffer;
	src->pub.bytes_in_buffer = insize;
}

/**** JPEG ERROR HANDLER ****/
struct ssdv_error_mgr {
	struct jpeg_error_mgr pub; /* "public" fields */
	jmp_buf setjmp_buffer; /* for return to caller */
};

typedef struct ssdv_error_mgr* ssdv_error_ptr;

METHODDEF(void)
ssdv_error_exit (j_common_ptr cinfo)
{
	ssdv_error_ptr myerr = (ssdv_error_ptr) cinfo->err;
	longjmp(myerr->setjmp_buffer, 1);
}

#ifdef __cplusplus
}
#endif

#endif

/**** END JPEG STUFF *****/

#define IMG_WIDTH (320)
#define IMG_HEIGHT (240)
#define IMG_SIZE (IMG_WIDTH * IMG_HEIGHT * 3)
#define JPEG_SIZE (64 * 1024)

ssdv_rx::ssdv_rx(int w, int h, const char *title)
	: Fl_Double_Window(w, h, title)
{
	image = new unsigned char[IMG_SIZE];
	buffer = new uint8_t[BUFFER_SIZE];
	jpeg = new uint8_t[JPEG_SIZE]; /* Maximum JPEG image size is < 64k */
	
	/* Empty receive buffer */
	clear_buffer();
	
	/* Clear the image buffer, make it grey */
	memset(image, 0x80, IMG_SIZE);
	
	/* No image yet */
	img_imageid = -1;
	
	begin();
	
	box = new Fl_Box(0, 0, IMG_WIDTH, IMG_HEIGHT);
	flrgb = new Fl_RGB_Image(image, IMG_WIDTH, IMG_HEIGHT, 3);
	box->image(flrgb);
	
	int y = IMG_HEIGHT;
	int x1 = 0;
	int x2 = x1 + 95;
	int x3 = x2 + 95;
	
	/* Current Image ID: */
	{
		Fl_Box* o = new Fl_Box(x1 + 2, y + 2, 72, 20, "Image ID:");
		o->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
		
		flimageid = new Fl_Box(x1 + 75, y + 2, 60, 20, "");
		flimageid->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	}
	
	/* Last Block: */
	{
		Fl_Box* o = new Fl_Box(x1 + 2, y + 22, 72, 20, "Last Block:");
		o->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
		
		flblock = new Fl_Box(x1 + 75, y + 22, 60, 20, "");
		flblock->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	}
	
	/* Size: */
	{
		Fl_Box* o = new Fl_Box(x2 + 2, y + 2, 72, 20, "Size:");
		o->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
		
		flsize = new Fl_Box(x2 + 75, y + 2, 60, 20, "");
		flsize->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	}
	
	/* Lost: */
	{
		Fl_Box* o = new Fl_Box(x2 + 2, y + 22, 72, 20, "Lost:");
		o->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
		
		flmissing = new Fl_Box(x2 + 75, y + 22, 60, 20, "");
		flmissing->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	}     
	
	/* Something: */
	{     
		Fl_Box* o = new Fl_Box(x3 + 2, y + 2, 72, 20, "???:");
		o->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
		
		fltodo = new Fl_Box(x3 + 75, y + 2, 60, 20, "");
		fltodo->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	}
	
	/* Fixes: */
	{
		Fl_Box* o = new Fl_Box(x3 + 2, y + 22, 72, 20, "Fixed:");
		o->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
		
		flfixes = new Fl_Box(x3 + 75, y + 22, 60, 20, "");
		flfixes->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	}
	
	/* Progress Bar */
	flprogress = new Fl_Progress(0, h - 12, w, 12);
	flprogress->minimum(0);
	flprogress->maximum(1);
	flprogress->value(0);
	
	end();
}

ssdv_rx::~ssdv_rx()
{
	delete image;
	delete buffer;
	delete jpeg;
}

void ssdv_rx::feed_buffer(uint8_t byte)
{
	int bp = bc + bl;
	
	buffer[bp] = byte;
	if((bp -= PACKET_SIZE) >= 0) buffer[bp] = byte;
	
	if(bl < PACKET_SIZE) bl++;
	else if(++bc == PACKET_SIZE) bc = 0;
}

void ssdv_rx::clear_buffer()
{
	bc = 0;
	bl = 0;
}

int ssdv_rx::have_packet()
{
	int i;
	uint8_t *b = &buffer[bc];
	
	/* Enough data present? */
	if(bl < PACKET_SIZE) return(-1);
	
	/* Headers present? TODO: Check for fuzzy headers */
	if(b[0] != 0x55 || b[1] != 0x66) return(-1);
	
	/* Looks like a packet header, try the reed-solomon decoder */
	i = decode_rs_8(&b[1], 0, 0, 0);
	if(i < 0) return(-1);
	if(i > 0)
	{
		fprintf(stderr, "ssdv: %i bytes corrected\n", i);
		img_errors += i;
	}
	
	/* Looks like we got a packet! */
	return(0);
}

static void *upload_packet_thread(void *arg)
{
	ssdv_post_data_t *t = (ssdv_post_data_t *) arg;
	struct curl_httppost* post = t->post;
	CURL *curl = t->curl;
	CURLcode r;
	
	r = curl_easy_perform(curl);
	if(r == 0)
	{
		fprintf(stderr, "SSDV: packet uploaded!\n");
	}
	else
	{
		fprintf(stderr, "CURL upload failed! \"%s\"\n", curl_easy_strerror(r));
	}
	
	curl_easy_cleanup(curl);
	curl_formfree(post);
	
	pthread_exit(0);
}

void ssdv_rx::upload_packet()
{
	ssdv_post_data_t *t;
	pthread_t thread;
	const char *callsign, *payload;
	char *packet;
	struct curl_httppost* post = NULL;
	struct curl_httppost* last = NULL;
	CURL *curl;
	
	/* Don't upload if no URL is present */
	if(progdefaults.ssdv_packet_url.length() <= 0) return;
	
	/* Get the callsign, or "UNKNOWN" if none is set */
	callsign = (progdefaults.myCall.empty() ? "UNKNOWN" : progdefaults.myCall.c_str());
	curl_formadd(&post, &last, CURLFORM_COPYNAME, "callsign",
		CURLFORM_COPYCONTENTS, callsign, CURLFORM_END);
	
	/* Get the payload name */
	payload = (progdefaults.xmlPayloadname.empty() ? "UNKNOWN" : progdefaults.xmlPayloadname.c_str());
	curl_formadd(&post, &last, CURLFORM_COPYNAME, "payload",
		CURLFORM_COPYCONTENTS, payload, CURLFORM_END);
	
	/* The encoding used on the packet */
	curl_formadd(&post, &last, CURLFORM_COPYNAME, "encoding",
		CURLFORM_COPYCONTENTS, "hex", CURLFORM_END);
	
	/* URL-encode the packet */
	packet = (char *) malloc((PACKET_SIZE * 2) + 1);
	if(!packet)
	{
		fprintf(stderr, "ssdv_rx::upload_packet(): failed to allocate memory for packet\n");
		curl_formfree(post);
		return;
	}
	
	for(int i = 0; i < PACKET_SIZE; i++)
		snprintf(packet + (i * 2), 3, "%02X", buffer[bc + i]);
	
	/* Add a copy of the packet */
	curl_formadd(&post, &last, CURLFORM_COPYNAME, "packet",
		CURLFORM_COPYCONTENTS, packet, CURLFORM_END);
	
	free(packet);
	
	/* Initialise libcurl */
	curl = curl_easy_init();
	if(!curl)
	{
		fprintf(stderr, "ssdv_rx::upload_packet(): curl_easy_init() failed\n");
		curl_formfree(post);
		return;
	}
	
	curl_easy_setopt(curl, CURLOPT_URL, progdefaults.ssdv_packet_url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPPOST, post); 
	
	/* Begin the thread to do the post */
	t = (ssdv_post_data_t *) malloc(sizeof(ssdv_post_data_t));
	if(!t)
	{
		fprintf(stderr, "ssdv_rx::upload_packet(): failed to allocate memory for thread data\n");
		curl_easy_cleanup(curl);
		curl_formfree(post);
		return;
	}
	
	t->curl = curl;
	t->post = post;
	
	if(pthread_create(&thread, NULL, upload_packet_thread, (void *) t) != 0)
	{
		fprintf(stderr, "ssdv_rx::upload_packet(): failed to start post thread\n");
		curl_easy_cleanup(curl);
		curl_formfree(post);
		free(t);
		return;
	}
	
	/* All done! */
	
	return;
}

void ssdv_rx::put_byte(uint8_t byte, int lost)
{
	/* If more than 16 bytes where lost clear the buffer */
	if(lost > 16) clear_buffer();
	
	/* Fill in the lost bytes */
	for(int i = 0; i < lost; i++)
		feed_buffer(0x00);
	
	/* Feed the byte into the buffer */
	feed_buffer(byte);
	
	/* Test if a packet is present */
	if(have_packet() == 0)
	{
		uint8_t *b = &buffer[bc];
		
		/* Packet received.. upload to server */
		if(progdefaults.dl_online) upload_packet();
		
		/* Read the header */
		pkt_blockno = b[3];
		pkt_imageid = b[2];
		pkt_filesize = b[4] + (b[5] << 8);
		
		/* Is this a new image? */
		if(pkt_imageid != img_imageid ||
		   pkt_filesize != img_filesize) new_image();
		
		/* Have we missed a block? */
		/* Assumes blocks are transmitted sequentially */
		img_missing += pkt_blockno - (img_lastblock + 1);
		
		/* Increase counters */
		img_lastblock = pkt_blockno;
		img_received++;
		
		/* Display a message on the fldigi interface */
		put_status("SSDV: Decoded image packet!", 10);
		if(bHAB)
		{
			char msg[100];
			snprintf(msg, 100, "Decoded image packet. Image ID: %02X, Block: %i/%i, Image size: %i bytes", pkt_imageid, pkt_blockno + 1, img_blocks, pkt_filesize);
			
			habCustom->value(msg);
			habCustom->color(FL_GREEN);
		}
		
		/* Copy payload into jpeg buffer */
		memcpy(jpeg + pkt_blockno * PAYLOAD_SIZE, &b[6], PAYLOAD_SIZE);
		
		/* Attempt to render the image */
		render_image();
		
		/* Update values on display */
		char s[16];
		
		snprintf(s, 16, "%d/%d", pkt_blockno + 1, img_blocks);
		flblock->copy_label(s);
		
		snprintf(s, 16, "0x%02X", pkt_imageid);
		flimageid->copy_label(s);
		
		snprintf(s, 16, "%d", pkt_filesize);
		flsize->copy_label(s);
		
		snprintf(s, 16, "%d", img_missing);
		flmissing->copy_label(s);
		
		snprintf(s, 16, "%d", img_errors);
		flfixes->copy_label(s);
		
		flprogress->maximum(img_blocks);
		flprogress->value(pkt_blockno + 1);
		
		/* Update the image */
		flrgb->uncache();
		box->redraw();
	}
}

void ssdv_rx::new_image()
{
	if(img_imageid != -1)
	{
		/* TODO: Save previous image, trigger upload */
	}
	
	/* Set details for new image */
	img_imageid   = pkt_imageid;
	img_filesize  = pkt_filesize;
	img_blocks    = (img_filesize / PAYLOAD_SIZE);
	img_blocks   += (img_filesize % PAYLOAD_SIZE == 0 ? 0 : 1);
	img_lastblock = -1;
	img_errors    = 0;
	img_missing   = 0;
	img_received  = 0;
	
	/* Clear the image buffer */
	memset(image, 0x80, IMG_SIZE);
	
	/* Fill the JPEG buffer with 0xFF */
	/* 0xFF bytes are used for padding purposes in the JPEG format */
	/* (see JPEG specification section F.1.2.3 for details). */
	memset(jpeg, 0xFF, JPEG_SIZE);
}

void ssdv_rx::render_image()
{
	int r;
	struct jpeg_decompress_struct cinfo;
	struct ssdv_error_mgr jerr;
	
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = ssdv_error_exit;
	
	if(setjmp(jerr.setjmp_buffer))
	{
		/* JPEG decoding failed */
		jpeg_destroy_decompress(&cinfo);
		return;
	}
	
	jpeg_create_decompress(&cinfo);
	jpeg_memory_src(&cinfo, jpeg, img_filesize);
	
	r = jpeg_read_header(&cinfo, TRUE);
	if(r != JPEG_HEADER_OK)
	{
		jpeg_destroy_decompress(&cinfo);
		return;
	}
	
	int row_stride;
	
	/* Force RGB output and use floating point DCT */
	cinfo.out_color_space = JCS_RGB;
	cinfo.dct_method = JDCT_FLOAT;
	
	jpeg_start_decompress(&cinfo);
	
	/* Fail if the image doesn't match our requirements */
	if(cinfo.output_width != IMG_WIDTH && cinfo.output_height != IMG_HEIGHT &&
	   cinfo.output_components != 3)
	{
		jpeg_finish_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		return;
	}
	
	row_stride = cinfo.output_width * cinfo.output_components;
	
	while(cinfo.output_scanline < cinfo.output_height)
	{
		uint8_t *b = &image[cinfo.output_scanline * row_stride];
		jpeg_read_scanlines(&cinfo, &b, 1);
	}
	
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
}

