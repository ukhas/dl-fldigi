
#include <cstdio>
#include <cstring>
#include <stdint.h>
#include <setjmp.h>
#include "ssdv_rx.h"

#include <curl/curl.h>

/* For put_status() */
#include "fl_digi.h"

/* For progdefaults */
#include "configuration.h"

/* For online() getter */
#include "dl_fldigi/dl_fldigi.h"

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

#define UI_HEIGHT (60)
#define WIN_MIN_WIDTH (320)
#define WIN_MIN_HEIGHT (32 + UI_HEIGHT)
#define WIN_MAX_WIDTH (800)
#define WIN_MAX_HEIGHT (600 + UI_HEIGHT)

ssdv_rx::ssdv_rx(int w, int h, const char *title)
	: Fl_Double_Window(w, h, title)
{
	buffer = new uint8_t[BUFFER_SIZE];
	erasures = new uint8_t[BUFFER_SIZE];
	
	/* Empty receive buffer */
	clear_buffer();
	
	/* No image yet */
	packets = NULL;
	image = NULL;
	flrgb = NULL;
	image_id = -1;
	
	begin();
	
	scroll = new Fl_Scroll(0, 0, w, h - UI_HEIGHT);
	scroll->begin();
	box = new Fl_Box(0, 0, w, h - UI_HEIGHT);
	box->box(FL_FLAT_BOX);
	box->color(fl_rgb_color(0x80, 0x80, 0x80));
	scroll->end();
	
	int y = h - UI_HEIGHT;
	int x1 = 0;
	int x2 = x1 + 75;
	int x3 = x2 + 100;
	
	/* Current Image Callsign: */
	{
		Fl_Box* o = new Fl_Box(x1 + 2, y + 2, 72, 20, "Callsign:");
		o->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
		
		flcallsign = new Fl_Box(x1 + 71, y + 2, 60, 20, "");
		flcallsign->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	}
	
	/* Current Image ID: */
	{
		Fl_Box* o = new Fl_Box(x2 + 2, y + 2, 72, 20, "ID:");
		o->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
		
		flimageid = new Fl_Box(x2 + 71, y + 2, 60, 20, "");
		flimageid->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	}
	
	/* Received: */
	{
		Fl_Box* o = new Fl_Box(x1 + 2, y + 22, 72, 20, "Received:");
		o->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
		
		flreceived = new Fl_Box(x1 + 71, y + 22, 60, 20, "");
		flreceived->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	}
	
	/* Size: */
	{
		Fl_Box* o = new Fl_Box(x3 + 2, y + 2, 72, 20, "Size:");
		o->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
		
		flsize = new Fl_Box(x3 + 71, y + 2, 60, 20, "");
		flsize->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	}
	
	/* Fixes: */
	{
		Fl_Box* o = new Fl_Box(x2 + 2, y + 22, 72, 20, "Fixes:");
		o->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
		
		flfixes = new Fl_Box(x2 + 71, y + 22, 60, 20, "");
		flfixes->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	}
	
	/* Lost: */
	{
		Fl_Box* o = new Fl_Box(x3 + 2, y + 22, 72, 20, "Lost:");
		o->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
		
		flmissing = new Fl_Box(x3 + 71, y + 22, 60, 20, "");
		flmissing->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	}
	
	/* Progress Bar */
	flprogress = new Fl_Progress(0, h - 12, w, 12);
	flprogress->minimum(0);
	flprogress->maximum(1);
	flprogress->value(0);
	
	end();
	
	size_range(WIN_MIN_WIDTH, WIN_MIN_HEIGHT, 0, 0, 0, 0, 0);
	resizable(scroll);
}

ssdv_rx::~ssdv_rx()
{
	if(flrgb) delete flrgb;
	if(image) delete image;
	if(buffer) delete buffer;
	if(erasures) delete erasures;
}

void ssdv_rx::feed_buffer(uint8_t byte, uint8_t erasure)
{
	int bp = bc + bl;
	
	buffer[bp] = byte;
	erasures[bp] = (erasure ? 1 : 0);
	if((bp -= SSDV_PKT_SIZE) >= 0)
	{
		buffer[bp] = byte;
		erasures[bp] = (erasure ? 1 : 0);
	}
	
	if(bl < SSDV_PKT_SIZE) bl++;
	else if(++bc == SSDV_PKT_SIZE) bc = 0;
}

void ssdv_rx::clear_buffer()
{
	bc = 0;
	bl = 0;
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
	
	free(t);
	
	pthread_exit(0);
}

/* TODO: HABITAT-LATER upload using habitat */
void ssdv_rx::upload_packet(int fixes)
{
	ssdv_post_data_t *t;
	pthread_attr_t attr;
	pthread_t thread;
	const char *callsign;
	char *packet;
	char data[10];
	struct curl_httppost* post = NULL;
	struct curl_httppost* last = NULL;
	CURL *curl;
	
	/* Don't upload if no URL is present */
	if(progdefaults.ssdv_packet_url.length() <= 0) return;
	
	/* Get the callsign, or "UNKNOWN" if none is set */
	callsign = (progdefaults.myCall.empty() ? "UNKNOWN" : progdefaults.myCall.c_str());
	curl_formadd(&post, &last, CURLFORM_COPYNAME, "callsign",
		CURLFORM_COPYCONTENTS, callsign, CURLFORM_END);
	
	/* The encoding used on the packet */
	curl_formadd(&post, &last, CURLFORM_COPYNAME, "encoding",
		CURLFORM_COPYCONTENTS, "hex", CURLFORM_END);
	
	/* Include the number of bytes corrected by the FEC */
	snprintf(data, 10, "%i", fixes);
	curl_formadd(&post, &last, CURLFORM_COPYNAME, "fixes",
		CURLFORM_COPYCONTENTS, data, CURLFORM_END);
	
	/* URL-encode the packet */
	packet = (char *) malloc((SSDV_PKT_SIZE * 2) + 1);
	if(!packet)
	{
		fprintf(stderr, "ssdv_rx::upload_packet(): failed to allocate memory for packet\n");
		curl_formfree(post);
		return;
	}
	
	for(int i = 0; i < SSDV_PKT_SIZE; i++)
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
	
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	
	if(pthread_create(&thread, &attr, upload_packet_thread, (void *) t) != 0)
	{
		fprintf(stderr, "ssdv_rx::upload_packet(): failed to start post thread\n");
		curl_easy_cleanup(curl);
		curl_formfree(post);
		free(t);
	}
	
	pthread_attr_destroy(&attr);
	
	/* All done! */
	
	return;
}

void ssdv_rx::put_byte(uint8_t byte, int lost)
{
	int i;
	
	/* If more than 32 bytes where lost clear the buffer */
	if(lost > 32) clear_buffer();
	
	/* Fill in the lost bytes */
	for(i = 0; i < lost; i++)
		feed_buffer(0x00, 1);
	
	/* Feed the byte into the buffer */
	feed_buffer(byte, 0);
	
	/* Enough data yet to form a packet? */
	if(bl < SSDV_PKT_SIZE) return;
	
	/* Test if this is a packet and is valid */
	uint8_t *b = &buffer[bc];
	if(ssdv_dec_is_packet(b, &i, &erasures[bc]) != 0) return;
	
	/* Make a note of the number of errors */
	image_errors += i;
	
	/* Packet received.. upload to server */
	if (dl_fldigi::online()) upload_packet(i);
	
	/* Read the header */
	ssdv_dec_header(&pkt_info, b);
	
	/* Does this belong to the same image? */
	if(pkt_info.callsign != image_callsign ||
	   pkt_info.image_id != image_id ||
	   pkt_info.width != image_width ||
	   pkt_info.height != image_height ||
	   pkt_info.mcu_mode != image_mcu_mode)
	{
		/* Prepare the new image */
		image_timestamp      = time(NULL);
		image_callsign       = pkt_info.callsign;
		image_id             = pkt_info.image_id;
		image_width          = pkt_info.width;
		image_height         = pkt_info.height;
		image_mcu_mode       = pkt_info.mcu_mode;
		image_lost_packets   = 0;
		image_errors         = i;
		
		/* Initialise and clear the image buffer */
		if(image) delete image;
		image_len = image_width * image_height * 3;
		image = new unsigned char[image_len];
		
		/* Create the Fl_RGB_Image object */
		if(flrgb) delete flrgb;
		flrgb = new Fl_RGB_Image(image, image_width, image_height, 3);
		box->size(image_width, image_height);
		box->image(flrgb);
		
		/* Snap the window to the new image size */
		size(
			CLAMP(image_width, WIN_MIN_WIDTH, WIN_MAX_WIDTH),
			CLAMP(image_height + UI_HEIGHT, WIN_MIN_HEIGHT, WIN_MAX_HEIGHT)
		);
		
		/* Clear the packet buffer */
		if(packets != NULL) free(packets);
		packets = NULL;
		packets_len = 0;
	}
	
	/* Realloc packet buffer for new packet */
	if(pkt_info.packet_id + 1 > packets_len)
	{
		size_t l = SSDV_PKT_SIZE * (pkt_info.packet_id + 1);
		void *a = realloc(packets, l);
		if(!a)
		{
			fprintf(stderr, "Error reallocating memory\n");
			perror("realloc");
			return;
		}
		
		packets = (uint8_t *) a;
		
		size_t s = SSDV_PKT_SIZE * packets_len;
		memset(packets + s, 0, l - s);
		
		packets_len = pkt_info.packet_id + 1;
	}
	
	/* Copy it into place */
	memcpy(packets + (pkt_info.packet_id * SSDV_PKT_SIZE), b, SSDV_PKT_SIZE);
	
	/* Done with the receive buffer */
	clear_buffer();	
	
	/* Display a message on the fldigi interface */
	put_status("SSDV: Decoded image packet!", 10);
	if(bHAB)
	{
		char msg[200], callsign[10];
		snprintf(msg, 200, "Decoded image packet. Callsign: %s, Image ID: %02X, Resolution: %dx%d, Packet ID: %d",
			ssdv_decode_callsign(callsign, pkt_info.callsign),
			pkt_info.image_id,
			pkt_info.width,
			pkt_info.height,
			pkt_info.packet_id);
		
		habString->value(msg);
		habString->color(FL_GREEN);
        habString->damage(FL_DAMAGE_ALL);
	}
	
	/* Initialise the decoder */
	ssdv_t dec;
	ssdv_dec_init(&dec);
	
	image_received_packets = 0;
	image_lost_packets = 0;
	for(i = 0; i < packets_len; i++)
	{
		int r;
		uint8_t *p = packets + (i * SSDV_PKT_SIZE);
		if(p[0] != 0x55) { image_lost_packets++; continue; }
		image_received_packets++;
		r = ssdv_dec_feed(&dec, p);
	}
	
	/* Store the last decoded MCU, for the progress bar */
	int mcu_id = dec.mcu_id;
	
	/* Get the final image */
	uint8_t *jpeg;
	size_t length;
	
	i = ssdv_dec_get_jpeg(&dec, &jpeg, &length);
	
	/* Save the image to disk */
	save_image(jpeg, length);
	
	/* Render the image to screen */
	render_image(jpeg, length);
	flrgb->uncache();
	box->redraw();
	
	free(jpeg);
	
	/* Update values on display */
	char s[16];
	
	ssdv_decode_callsign(s, image_callsign);
	flcallsign->copy_label(s);
	
	snprintf(s, 16, "%d", image_received_packets);
	flreceived->copy_label(s);
	
	snprintf(s, 16, "0x%02X", pkt_info.image_id);
	flimageid->copy_label(s);
	
	snprintf(s, 16, "%d", image_lost_packets);
	flmissing->copy_label(s);
	
	snprintf(s, 16, "%d byte%s", image_errors, (image_errors == 1 ? "" : "s"));
	flfixes->copy_label(s);
	
	snprintf(s, 16, "%ix%i", image_width, image_height);
	flsize->copy_label(s);
	
	flprogress->maximum(dec.mcu_count);
	flprogress->value(mcu_id);
}

void ssdv_rx::save_image(uint8_t *jpeg, size_t length)
{
	char fname[FILENAME_MAX];
	char payload[10];
	const char *savedir;
	struct tm tm;
	FILE *f;
	uint16_t l;
	
	/* Does the user want the image saved? */
	if(!progdefaults.ssdv_save_image) return;
	
	/* Save Directory - user specified or pwd */
	savedir = (progdefaults.ssdv_save_dir.empty() ?
		"." : progdefaults.ssdv_save_dir.c_str());
	
	/* Get the payload name */
	if(!image_callsign) strcpy(payload, "UNKNOWN");
	ssdv_decode_callsign(payload, image_callsign);
	
	/* Construct the filename */
	gmtime_r(&image_timestamp, &tm);
	snprintf(fname, FILENAME_MAX - 1,
		"%s/%04i-%02i-%02i-%02i-%02i-%02i-%s-%02X.jpeg",
		savedir,
		1900 + tm.tm_year, 1 + tm.tm_mon, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec,
		payload, image_id);
	
	f = fopen(fname, "wb");
	if(!f)
	{
		fprintf(stderr, "Error saving image to %s\n", fname);
		/* TODO: Error reporting -- is strerror thread safe? */
		return;
	}
	
	/* Loop until all bytes are written */
	l = 0;
	while(l < length)
	{
		size_t r = fwrite(jpeg + l, 1, length - l, f);
		if(r == 0) break;
		l += r;
	}
	fclose(f);
	
	/* Job done */
}

void ssdv_rx::render_image(uint8_t *jpeg, size_t length)
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
	jpeg_memory_src(&cinfo, jpeg, length);
	
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
	if(cinfo.output_components != 3)
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

