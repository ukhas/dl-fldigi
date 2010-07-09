
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

/* For put_status() */
#include "fl_digi.h"

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
		
		/* Read the header */
		pkt_blockno = b[3];
		pkt_imageid = b[2];
		pkt_filesize = b[4] + (b[5] << 8);
		
		put_status("SSDV: Decoded image packet!", 10);
		if(bHAB)
		{
			char msg[100];
			snprintf(msg, 100, "Decoded image packet. Image ID: %02X, Block: %02X/%02X, Image size: %i bytes", pkt_imageid, pkt_blockno, 0, pkt_filesize);
			
			habCustom->value(msg);
			habCustom->color(FL_GREEN);
		}
		
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

