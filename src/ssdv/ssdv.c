
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "ssdv.h"
#include "rs8.h"

/* Recognised JPEG markers */
enum {
	J_TEM = 0xFF01,
	J_SOF0 = 0xFFC0, J_SOF1,  J_SOF2,  J_SOF3,  J_DHT,   J_SOF5,  J_SOF6, J_SOF7,
	J_JPG,  J_SOF9,  J_SOF10, J_SOF11, J_DAC,   J_SOF13, J_SOF14, J_SOF15,
	J_RST0, J_RST1,  J_RST2,  J_RST3,  J_RST4,  J_RST5,  J_RST6,  J_RST7,
	J_SOI,  J_EOI,   J_SOS,   J_DQT,   J_DNL,   J_DRI,   J_DHP,   J_EXP,
	J_APP0, J_APP1,  J_APP2,  J_APP3,  J_APP4,  J_APP5,  J_APP6,  J_APP7,
	J_APP8, J_APP9,  J_APP10, J_APP11, J_APP12, J_APP13, J_APP14, J_APP15,
	J_JPG0, J_JPG1,  J_JPG2,  J_JPG3,  J_JPG4,  J_JPG5,  J_JPG6,  J_SOF48,
	J_LSE,  J_JPG9,  J_JPG10, J_JPG11, J_JPG12, J_JPG13, J_COM,
} jpeg_marker_t;

#if 1
/* APP0 header data */
static uint8_t app0[14] = {
0x4A,0x46,0x49,0x46,0x00,0x01,0x01,0x01,0x00,0x48,0x00,0x48,0x00,0x00,
};

/* SOF0 header data */
static uint8_t sof0[15] = {
0x08,0x00,0xF0,0x01,0x40,0x03,0x01,0x22,0x00,0x02,0x11,0x01,0x03,0x11,0x01,
};

/* SOS header data */
static uint8_t sos[10] = {
0x03,0x01,0x00,0x02,0x11,0x03,0x11,0x00,0x3F,0x00,
};

/* Quantisation tables */
static uint8_t dqt0[65] = {
0x00,0x10,0x0C,0x0C,0x0E,0x0C,0x0A,0x10,0x0E,0x0E,0x0E,0x12,0x12,0x10,0x14,0x18,
0x28,0x1A,0x18,0x16,0x16,0x18,0x32,0x24,0x26,0x1E,0x28,0x3A,0x34,0x3E,0x3C,0x3A,
0x34,0x38,0x38,0x40,0x48,0x5C,0x4E,0x40,0x44,0x58,0x46,0x38,0x38,0x50,0x6E,0x52,
0x58,0x60,0x62,0x68,0x68,0x68,0x3E,0x4E,0x72,0x7A,0x70,0x64,0x78,0x5C,0x66,0x68,
0x64,
};

static uint8_t dqt1[65] = {
0x01,0x12,0x12,0x12,0x16,0x16,0x16,0x30,0x1A,0x1A,0x30,0x64,0x42,0x38,0x42,0x64,
0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,
0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,
0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,
0x64,
};
#endif

/* Huffman tables */
static uint8_t dht00[29] = {
0x00,0x00,0x01,0x05,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,
};

static uint8_t dht01[29] = {
0x01,0x00,0x03,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,
0x00,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,
};

static uint8_t dht10[179] = {
0x10,0x00,0x02,0x01,0x03,0x03,0x02,0x04,0x03,0x05,0x05,0x04,0x04,0x00,0x00,0x01,
0x7D,0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,
0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xA1,0x08,0x23,0x42,0xB1,0xC1,0x15,0x52,0xD1,
0xF0,0x24,0x33,0x62,0x72,0x82,0x09,0x0A,0x16,0x17,0x18,0x19,0x1A,0x25,0x26,0x27,
0x28,0x29,0x2A,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x43,0x44,0x45,0x46,0x47,0x48,
0x49,0x4A,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x63,0x64,0x65,0x66,0x67,0x68,
0x69,0x6A,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x83,0x84,0x85,0x86,0x87,0x88,
0x89,0x8A,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0xA2,0xA3,0xA4,0xA5,0xA6,
0xA7,0xA8,0xA9,0xAA,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xC2,0xC3,0xC4,
0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xE1,
0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,
0xF8,0xF9,0xFA,
};

static uint8_t dht11[179] = {
0x11,0x00,0x02,0x01,0x02,0x04,0x04,0x03,0x04,0x07,0x05,0x04,0x04,0x00,0x01,0x02,
0x77,0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,
0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xA1,0xB1,0xC1,0x09,0x23,0x33,0x52,
0xF0,0x15,0x62,0x72,0xD1,0x0A,0x16,0x24,0x34,0xE1,0x25,0xF1,0x17,0x18,0x19,0x1A,
0x26,0x27,0x28,0x29,0x2A,0x35,0x36,0x37,0x38,0x39,0x3A,0x43,0x44,0x45,0x46,0x47,
0x48,0x49,0x4A,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x63,0x64,0x65,0x66,0x67,
0x68,0x69,0x6A,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x82,0x83,0x84,0x85,0x86,
0x87,0x88,0x89,0x8A,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0xA2,0xA3,0xA4,
0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xC2,
0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,
0xDA,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,
0xF8,0xF9,0xFA,
};

static uint8_t *dht_dc[3] = { dht00, dht01, dht01 };
static uint8_t *dht_ac[3] = { dht10, dht11, dht11 };

static inline char jpeg_dht_lookup(ssdv_t *s, uint8_t *symbol, uint8_t *width)
{
	uint16_t code = 0;
	uint8_t cw, n;
	uint8_t *dht, *ss;
	
	/* Select the appropriate huffman table */
	if(s->acpart == 0) dht = dht_dc[s->component];
	else               dht = dht_ac[s->component];
	ss = &dht[17];
	
	for(cw = 1; cw <= 16; cw++)
	{
		/* Got enough bits? */
		if(cw >= s->worklen) return(SSDV_FEED_ME);
		
		/* Compare against each code 'cw' bits wide */
		for(n = dht[cw]; n > 0; n--)
		{
			if(s->workbits >> (s->worklen - cw) == code)
			{
				/* Found a match */
				*symbol = *ss;
				*width = cw;
				return(SSDV_OK);
			}
			ss++; code++;
		}
		
		code <<= 1;
	}
	
	/* No match found - error */
	return(SSDV_ERROR);
}

static inline char jpeg_dht_lookup_symbol(uint8_t *dht, uint8_t symbol, uint16_t *bits, uint8_t *width)
{
	uint16_t code = 0;
	uint8_t cw, n, *ss = &dht[17];
	
	for(cw = 1; cw <= 16; cw++)
	{
		for(n = dht[cw]; n > 0; n--)
		{
			if(*ss == symbol)
			{
				/* Found a match */
				*bits = code;
				*width = cw;
				return(SSDV_OK);
			}
			ss++; code++;
		}
		
		code <<= 1;
	}
	
	/* No match found - error */
	return(SSDV_ERROR);
}

static inline int jpeg_int(int bits, int width)
{
	int b = (1 << width) - 1;
	if(bits <= b >> 1) bits = -(bits ^ b);
	return(bits);
}

static inline void jpeg_encode_int(int value, int *bits, uint8_t *width)
{
	*bits = value;
	
	/* Calculate the number of bits */
	if(value < 0) value = -value;
	for(*width = 0; value; value >>= 1) (*width)++;
	
	/* Fix negative values */
	if(*bits < 0) *bits = -*bits ^ ((1 << *width) - 1);
}

/*****************************************************************************/

static char ssdv_outbits(ssdv_t *s, uint16_t bits, uint8_t length)
{
	uint8_t b;
	
	if(length)
	{
		s->outbits <<= length;
		s->outbits |= bits & ((1 << length) - 1);
		s->outlen += length;
	}
	
	while(s->outlen >= 8 && s->out_len > 0)
	{
		b = s->outbits >> (s->outlen - 8);
		
		/* Put the byte into the output buffer */
		*(s->outp++) = b;
		s->outlen -= 8;
		s->out_len--;
		
		/* Insert stuffing byte if needed */
		if(s->out_stuff && b == 0xFF)
		{
			s->outbits &= (1 << s->outlen) - 1;
			s->outlen += 8;
		}
	}
	
	return(s->out_len ? SSDV_OK : SSDV_BUFFER_FULL);
}

static char ssdv_outbits_sync(ssdv_t *s)
{
	uint8_t b = s->outlen % 8;
	if(b) return(ssdv_outbits(s, 0xFF, 8 - b));
	return(SSDV_OK);
}

static char ssdv_out_jpeg_int(ssdv_t *s, uint8_t rle, int value)
{
	uint16_t huffbits = 0;
	int intbits;
	uint8_t hufflen = 0, intlen;
	uint8_t *dht;
	int r;
	
	/* Select the appropriate huffman table */
	/* TODO: Do this elsewhere */
	if(s->acpart == 0) dht = dht_dc[s->component];
	else               dht = dht_ac[s->component];
	
	jpeg_encode_int(value, &intbits, &intlen);
	r = jpeg_dht_lookup_symbol(dht, (rle << 4) | (intlen & 0x0F), &huffbits, &hufflen);
	
	ssdv_outbits(s, huffbits, hufflen);
	if(intlen) ssdv_outbits(s, intbits, intlen);
	
	return(SSDV_OK);
}

static char ssdv_process(ssdv_t *s)
{
	if(s->state == J_HUFF)
	{
		uint8_t symbol, width;
		int r;
		
		/* Lookup the code, return if error or not enough bits yet */
		if((r = jpeg_dht_lookup(s, &symbol, &width)) != SSDV_OK)
			return(r);
		
		if(s->acpart == 0) /* DC */
		{
			if(symbol == 0x00)
			{
				/* No change in DC from last block */
				if(s->mcupart == 0 || s->mcupart == 4 || s->mcupart == 5)
				{
					if(s->dcmode == 0) ssdv_out_jpeg_int(s, 0, s->dc[s->component]);
					else
					{
						ssdv_out_jpeg_int(s, 0, 0 - s->dc[s->component]);
						s->dc[s->component] = 0;
					}
				}
				else ssdv_out_jpeg_int(s, 0, 0);
				
				/* skip to the next AC part immediately */
				s->acpart++;
			}
			else
			{
				/* DC value follows, 'symbol' bits wide */
				s->state = J_INT;
				s->needbits = symbol;
			}
		}
		else /* AC */
		{
			s->acrle = 0;
			if(symbol == 0x00)
			{
				/* EOB -- all remaining AC parts are zero */
				ssdv_out_jpeg_int(s, 0, 0);
				s->acpart = 64;
			}
			else if(symbol == 0xF0)
			{
				/* The next 15 AC parts are zero */
				ssdv_out_jpeg_int(s, 15, 0);
				s->acpart += 15;
			}
			else
			{
				/* Next bits are an integer value */
				s->state = J_INT;
				s->acrle = symbol >> 4;
				s->acpart += s->acrle;
				s->needbits = symbol & 0x0F;
			}
		}
		
		/* Clear processed bits */
		s->worklen -= width;
		s->workbits &= (1 << s->worklen) - 1;
	}
	else if(s->state == J_INT)
	{
		int i;
		
		/* Not enough bits yet? */
		if(s->worklen < s->needbits) return(SSDV_FEED_ME);
		
		/* Decode the integer */
		i = jpeg_int(s->workbits >> (s->worklen - s->needbits), s->needbits);
		
		if(s->acpart == 0) /* DC */
		{
			if(s->mcupart == 0 || s->mcupart == 4 || s->mcupart == 5)
			{
				if(s->dcmode == 0)
				{
					/* Output absolute DC value */
					s->dc[s->component] += i;
					ssdv_out_jpeg_int(s, 0, s->dc[s->component]);
				}
				else
				{
					/* Output relative DC value */
					ssdv_out_jpeg_int(s, 0, i - s->dc[s->component]);
					s->dc[s->component] = i;
				}
			}
			else
			{
				/* Output relative DC value */
				s->dc[s->component] += i;
				ssdv_out_jpeg_int(s, 0, i);
			}
		}
		else /* AC */
		{
			/* Output AC codes directly */
			ssdv_out_jpeg_int(s, s->acrle, i);
		}
		
		/* Next AC part to expect */
		s->acpart++;
		
		/* Next bits are a huffman code */
		s->state = J_HUFF;
		
		/* Clear processed bits */
		s->worklen -= s->needbits;
		s->workbits &= (1 << s->worklen) - 1;
	}
	
	if(s->acpart >= 64)
	{
		/* Reached the end of this MCU part */
		if(++s->mcupart == 6)
		{
			s->mcupart = 0;
			s->mcu_id++;
			
			/* Test for the end of image */
			if(s->mcu_id >= s->mcu_count) return(SSDV_EOI);
			
			/* Set the packet MCU marker - encoder only */
			if(s->packet_mcu_id == 0xFFFF)
			{
				s->packet_mcu_id = s->mcu_id;
				s->packet_mcu_offset =
					(SSDV_PKT_SIZE_PAYLOAD - s->out_len) * 8 + s->outlen;
			}
		}
		
		if(s->mcupart < 4) s->component = 0;
		else s->component = s->mcupart - 3;
		
		s->acpart = 0;
	}
	
	if(s->out_len == 0) return(SSDV_BUFFER_FULL);
	
	return(SSDV_OK);
}

/*****************************************************************************/

static char ssdv_have_marker(ssdv_t *s)
{
	switch(s->marker)
	{
	case J_SOF0:
	case J_SOS:
		/* Copy the data before processing */
		if(s->marker_len > HBUFF_LEN)
		{
			/* Not enough memory ... shouldn't happen! */
			return(SSDV_ERROR);
		}
		
		s->marker_data     = s->hbuff;
		s->marker_data_len = 0;
		s->state           = J_MARKER_DATA;
		break;
	
	case J_DHT:
	case J_DQT:
		/* Copy the tables into memory */
		s->marker_data     = malloc(s->marker_len);
		s->marker_data_len = 0;
		s->state           = J_MARKER_DATA;
		break;
	
	default:
		/* Ignore other marks, skipping any associated data */
		s->in_skip = s->marker_len;
		s->state   = J_MARKER;
		break;
	}
	
	return(SSDV_OK);
}

static char ssdv_have_marker_data(ssdv_t *s)
{
	uint8_t *d = s->marker_data;
	int i;
	
	switch(s->marker)
	{
	case J_SOF0:
		s->width  = (d[3] << 8) | d[4];
		s->height = (d[1] << 8) | d[2];
		
		/* Calculate number of MCU blocks in this image -- assumes 16x16 blocks */
		s->mcu_count = (s->width >> 4) * (s->height >> 4);
		
		/* The image must have a precision of 8 */
		if(d[0] != 8)
		{
			fprintf(stderr, "Error: The image must have a precision of 8\n");
			return(SSDV_ERROR);
		}
		
		/* The image must have 3 components (Y'Cb'Cr) */
		if(d[5] != 3)
		{
			fprintf(stderr, "Error: The image must have 3 components\n");
			return(SSDV_ERROR);
		}
		
		/* The image dimensions must be a multiple of 16 */
		if((s->width & 0x0F) || (s->height & 0x0F))
		{
			fprintf(stderr, "Error: The image dimensions must be a multiple of 16\n");
			return(SSDV_ERROR);
		}
		
		/* TODO: Read in the quantisation table ID for each component */
		// 01 22 00 02 11 01 03 11 01
		for(i = 0; i < 3; i++)
		{
			uint8_t *dq = &d[i * 3 + 6];
			if(dq[0] != i + 1)
			{
				fprintf(stderr, "Error: Components are not in order in the SOF0 header\n");
				return(SSDV_ERROR);
			}
		}
		
		break;
	
	case J_SOS:
		/* The image must have 3 components (Y'Cb'Cr) */
		if(d[0] != 3)
		{
			fprintf(stderr, "Error: The image must have 3 components\n");
			return(SSDV_ERROR);
		}
		
		for(i = 0; i < 3; i++)
		{
			uint8_t *dh = &d[i * 2 + 1];
			if(dh[0] != i + 1)
			{
				fprintf(stderr, "Error: Components are not in order in the SOF0 header\n");
				return(SSDV_ERROR);
			}
		}
		
		/* Do I need to look at the last three bytes of the SOS data? */
		/* 00 3F 00 */
		
		/* The SOS data is followed by the image data */
		s->state = J_HUFF;
		
		return(SSDV_OK);
	
	case J_DHT:
		{
			uint8_t *dh;
			
			switch(d[0])
			{
			case 0x00: dh = dht00; break;
			case 0x01: dh = dht01; break;
			case 0x10: dh = dht10; break;
			case 0x11: dh = dht11; break;
			}
		}
	case J_DQT:
		free(d);
		break;
	}
	
	s->state = J_MARKER;
	return(SSDV_OK);
}

char ssdv_enc_init(ssdv_t *s, uint8_t image_id)
{
	memset(s, 0, sizeof(ssdv_t));
	s->image_id = image_id;
	return(SSDV_OK);
}

char ssdv_enc_set_buffer(ssdv_t *s, uint8_t *buffer)
{
	s->out     = buffer;
	s->outp    = buffer + SSDV_PKT_SIZE_HEADER;
	s->out_len = SSDV_PKT_SIZE_PAYLOAD;
	
	/* Zero the payload memory */
	memset(s->out, 0, SSDV_PKT_SIZE);
	
	/* Flush the output bits */
	ssdv_outbits(s, 0, 0);
	
	return(SSDV_OK);
}

char ssdv_enc_get_packet(ssdv_t *s)
{
	int r;
	uint8_t b;
	
	/* If the output buffer is empty, re-initialise */
	if(s->out_len == 0) ssdv_enc_set_buffer(s, s->out);
	
	while(s->in_len)
	{
		b = *(s->inp++);
		s->in_len--;
		
		/* Skip bytes if necessary */
		if(s->in_skip) { s->in_skip--; continue; }
		
		switch(s->state)
		{
		case J_MARKER:
			s->marker = (s->marker << 8) | b;
			
			if(s->marker == J_TEM ||
			   (s->marker >= J_RST0 && s->marker <= J_EOI))
			{
				/* Marker without data */
				s->marker_len = 0;
				ssdv_have_marker(s);
			}
			else if(s->marker >= J_SOF0 && s->marker <= J_COM)
			{
				/* All other markers are followed by data */
				s->marker_len = 0;
				s->state = J_MARKER_LEN;
				s->needbits = 16;
			}
			break;
		
		case J_MARKER_LEN:
			s->marker_len = (s->marker_len << 8) | b;
			if((s->needbits -= 8) == 0)
			{
				s->marker_len -= 2;
				ssdv_have_marker(s);
			}
			break;
		
		case J_MARKER_DATA:
			s->marker_data[s->marker_data_len++] = b;
			if(s->marker_data_len == s->marker_len)
			{
				ssdv_have_marker_data(s);
			}
			break;
		
		case J_HUFF:
		case J_INT:
			/* Is the next byte a stuffing byte? Skip it */
			/* TODO: Test the next byte is actually 0x00 */
			if(b == 0xFF) s->in_skip++;
			
			/* Add the new byte to the work area */
			s->workbits = (s->workbits << 8) | b;
			s->worklen += 8;
			
			/* Process the new data until more needed, or an error occurs */
			while((r = ssdv_process(s)) == SSDV_OK);
			
			if(r == SSDV_BUFFER_FULL || r == SSDV_EOI)
			{
				uint16_t mcu_id     = s->packet_mcu_id;
				uint16_t mcu_offset = s->packet_mcu_offset;
				
				if(mcu_offset != 0xFFFF && mcu_offset >= SSDV_PKT_SIZE_PAYLOAD * 8)
				{
					/* The first MCU begins in the next packet, not this one */
					mcu_id = mcu_offset = 0xFFFF;
					s->packet_mcu_offset -= SSDV_PKT_SIZE_PAYLOAD * 8;
				}
				else
				{
					/* Clear the MCU data for the next packet */
					s->packet_mcu_id = s->packet_mcu_offset = 0xFFFF;
				}
				
				/* A packet is ready, create the headers */
				s->out[0]  = 0x55;                /* Sync */
				s->out[1]  = 0x66;                /* Type */
				s->out[2]  = s->image_id;         /* Image ID */
				s->out[3]  = s->packet_id >> 8;   /* Packet ID MSB */
				s->out[4]  = s->packet_id & 0xFF; /* Packet ID LSB */
				s->out[5]  = s->width >> 4;       /* Width / 16 */
				s->out[6]  = s->height >> 4;      /* Height / 16 */
				s->out[7]  = mcu_offset >> 8;     /* Next MCU offset MSB */
				s->out[8]  = mcu_offset & 0xFF;   /* Next MCU offset LSB */
				s->out[9]  = mcu_id >> 8;         /* MCU ID MSB */
				s->out[10] = mcu_id & 0xFF;       /* MCU ID LSB */
				
				/* Generate the RS codes */
				encode_rs_8(&s->out[1], &s->out[SSDV_PKT_SIZE - SSDV_PKT_SIZE_RSCODES], 0);
				
				s->packet_id++;
				
				/* Have we reached the end of the image data? */
				if(r == SSDV_EOI) s->state = J_MARKER;
				
				return(SSDV_OK);
			}
			else if(r != SSDV_FEED_ME)
			{
				/* An error occured */
				fprintf(stderr, "ssdv_process() failed: %i\n", r);
				return(SSDV_ERROR);
			}
			break;
		}
	}
	
	/* Need more data */
	return(SSDV_FEED_ME);
}

char ssdv_enc_feed(ssdv_t *s, uint8_t *buffer, size_t length)
{
	s->inp    = buffer;
	s->in_len = length;
	return(SSDV_OK);
}

/*****************************************************************************/

static void ssdv_write_marker(ssdv_t *s, uint16_t id, uint16_t length, uint8_t *data)
{
	ssdv_outbits(s, id, 16);
	if(length > 0)
	{
		ssdv_outbits(s, length + 2, 16);
		while(length--) ssdv_outbits(s, *(data++), 8);
	}
}

static void ssdv_out_headers(ssdv_t *s)
{
	ssdv_write_marker(s, J_SOI,    0, 0);
	ssdv_write_marker(s, J_APP0,  14, app0);
	ssdv_write_marker(s, J_DQT,   65, dqt0);  /* DQT Luminance       */
	ssdv_write_marker(s, J_DQT,   65, dqt1);  /* DQT Chrominance     */
	ssdv_write_marker(s, J_SOF0,  15, sof0);  /* SOF0 (Baseline DCT) */
	ssdv_write_marker(s, J_DHT,   29, dht00); /* DHT (DC Luminance)  */
	ssdv_write_marker(s, J_DHT,  179, dht10); /* DHT (AC Luminance)  */
	ssdv_write_marker(s, J_DHT,   29, dht01); /* DHT (DC Chrominance */
	ssdv_write_marker(s, J_DHT,  179, dht11); /* DHT (AC Chrominance */
	ssdv_write_marker(s, J_SOS,   10, sos);
}

static void ssdv_fill_gap(ssdv_t *s, uint16_t next_mcu)
{
	if(s->mcupart > 0 || s->acpart > 0)
	{
		/* Cleanly end the current MCU part */
		if(s->acpart > 0)
		{
			ssdv_out_jpeg_int(s, 0, 0);
			s->mcupart++;
		}
		
		/* End the current MCU block */
		for(; s->mcupart < 6; s->mcupart++)
		{
			if(s->mcupart < 4) s->component = 0;
			else s->component = s->mcupart - 3;
			s->acpart = 0; ssdv_out_jpeg_int(s, 0, 0); /* DC */
			s->acpart = 1; ssdv_out_jpeg_int(s, 0, 0); /* AC */
		}
		
		s->mcu_id++;
	}
	
	/* Pad out missing MCUs */
	for(; s->mcu_id < next_mcu; s->mcu_id++)
	{
		/* End the current MCU block */
		for(s->mcupart = 0; s->mcupart < 6; s->mcupart++)
		{
			if(s->mcupart < 4) s->component = 0;
			else s->component = s->mcupart - 3;
			s->acpart = 0; ssdv_out_jpeg_int(s, 0, 0); /* DC */
			s->acpart = 1; ssdv_out_jpeg_int(s, 0, 0); /* AC */
		}                         
	}
}

char ssdv_dec_init(ssdv_t *s)
{
	memset(s, 0, sizeof(ssdv_t));
	
	/* The packet data should contain only scan data, no headers */
	s->state = J_HUFF;
	
	/* Converting absolute values to relative */
	s->dcmode = 1;
	
	/* Allocate the memory for the JPEG */
	s->out_len = 64 * 1024;
	s->out = s->outp = (uint8_t *) malloc(s->out_len);
	if(!s->out)
	{
		fprintf(stderr, "ssdv_dec_init(): Out of memory\n");
		return(SSDV_ERROR);
	}
	
	return(SSDV_OK);
}

//char ssdv_dec_set_buffer(ssdv_t *s, uint8_t *buffer, size_t length)
//{
//	size_t c = s->outp - s->out;
//	
//	s->outp = buffer + c;
//	s->out = buffer;
//	s->out_len = length - c;
//	
//	/* Flush the output bits */
//	ssdv_outbits(s, 0, 0);
//	
//	return(SSDV_OK);
//}

char ssdv_dec_feed(ssdv_t *s, uint8_t *packet)
{
	int i = 0, r;
	uint8_t b;
	uint16_t packet_id;
	
	/* Read the packet header */
	packet_id            = (packet[3] << 8) | packet[4];
	s->packet_mcu_offset = (packet[7] << 8) | packet[8];
	s->packet_mcu_id     = (packet[9] << 8) | packet[10];
	
	/* If this is the first packet, write the JPEG headers */
	if(s->packet_id == 0)
	{
		/* Read the fixed headers from the packet */
		s->image_id  = packet[2];
		s->width     = packet[5] << 4;
		s->height    = packet[6] << 4;
		s->mcu_count = packet[5] * packet[6];
		
		/* Output JPEG headers and enable byte stuffing */
		ssdv_out_headers(s);
		s->out_stuff = 1;
	}
	
	/* Is this not the packet we expected? */
	if(packet_id != s->packet_id)
	{
		/* One or more packets have been lost! */
		
		/* If this packet has no new MCU, ignore */
		if(s->packet_mcu_offset == 0xFFFF) return(SSDV_FEED_ME);
		
		/* Fill the gap left by the missing packet */
		ssdv_fill_gap(s, s->packet_mcu_id);
		
		/* Clear the workbits */
		s->workbits = s->worklen = 0;
		
		/* Skip the bytes of the lost MCU */
		i = s->packet_mcu_offset >> 3;
		
		/* Skip any remaining bits too */
		if((r = s->packet_mcu_offset & 7))
		{
			/* Add the first bits of the new MCU */
			r = 8 - r;
			b = packet[SSDV_PKT_SIZE_HEADER + i++];
			b &= ((1 << r) - 1);
			s->workbits = b;
			s->worklen += r;
		}
		
		/* Reset the JPEG decoder state */
		s->state = J_HUFF;
		s->component = 0;
		s->mcupart = 0;
		s->acpart = 0;
		
		s->packet_id = packet_id;
	}
	
	/* Feed the JPEG data into the processor */
	for(; i < SSDV_PKT_SIZE_PAYLOAD; i++)
	{
		b = packet[SSDV_PKT_SIZE_HEADER + i];
		
		/* Add the new byte to the work area */
		s->workbits = (s->workbits << 8) | b;
		s->worklen += 8;
		
		/* Process the new data until more needed, or an error occurs */
		while((r = ssdv_process(s)) == SSDV_OK);
		
		if(r == SSDV_BUFFER_FULL)
		{
			/* Buffer is full, reallocate some more memory */
			size_t c = s->outp - s->out;
			void *a = realloc(s->out, c * 2);
			
			if(!a)
			{
				fprintf(stderr, "Error reallocating memory\n");
				perror("realloc");
				return(SSDV_ERROR);
			}
			
			s->out     = (uint8_t *) a;
			s->outp    = s->out + c;
			s->out_len = c;
		}
		else if(r == SSDV_EOI)
		{
			/* All done! */
			return(SSDV_OK);
		}
		else if(r != SSDV_FEED_ME)
		{
			/* An error occured */
			fprintf(stderr, "ssdv_process() failed: %i\n", r);
			return(SSDV_ERROR);
		}
	}
	
	/* The next packet to expect... */
	s->packet_id++;
	
	return(SSDV_FEED_ME);
}

char ssdv_dec_get_jpeg(ssdv_t *s, uint8_t **jpeg, size_t *length)
{
	/* Is the image complete? */
	if(s->mcu_id < s->mcu_count) ssdv_fill_gap(s, s->mcu_count);
	
	/* Sync, and final EOI header and return */
	ssdv_outbits_sync(s);
	s->out_stuff = 0;
	ssdv_write_marker(s, J_EOI, 0, 0);
	
	*jpeg = s->out;
	*length = (size_t) (s->outp - s->out);
	
	return(SSDV_OK);
}

char ssdv_dec_is_packet(uint8_t *packet)
{
	int i;
	
	/* Headers present? TODO: Check for fuzzy headers */
	if(packet[0] != 0x55 || packet[1] != 0x66) return(-1);
	
	/* Looks like a packet header, try the reed-solomon decoder */
	i = decode_rs_8(&packet[1], 0, 0, 0);
	if(i < 0) return(-1); /* Reed-solomon decoder failed */
	if(i > 0) fprintf(stderr, "ssdv: %i bytes corrected\n", i);
	
	/* Appears to be a valid packet */
	return(0);
}

void ssdv_dec_header(ssdv_packet_info_t *info, uint8_t *packet)
{
	info->image_id   = packet[2];
	info->packet_id  = (packet[3] << 8) | packet[4];
	info->width      = packet[5] << 4;
	info->height     = packet[6] << 4;
	info->mcu_offset = (packet[7] << 8) | packet[8];
	info->mcu_id     = (packet[9] << 8) | packet[10];
	info->mcu_count  = packet[5] * packet[6];
}

/*****************************************************************************/

