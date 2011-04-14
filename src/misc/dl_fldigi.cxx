#include <config.h>

#ifdef __MINGW32__
#  include "compat.h"
#endif

#include <iostream>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/file.h>
#include <time.h>

#include <FL/Fl_Choice.H>

#include "configuration.h"
#include "util.h"
#include "fl_digi.h"
#include "dl_fldigi.h"
#include "dl_fldigi_gps.h"
#include "util.h"
#include "confdialog.h"
#include "fl_digi.h"
#include "main.h"
#include "threads.h"
#include "qrunner.h"

#include "trx.h"

#include "irrXML.h"
using namespace std;
using namespace irr; // irrXML is located 
using namespace io;  // in the namespace irr::io

#define DL_FLDIGI_CACHE_FILE "dl_fldigi_cache.xml"

struct dl_fldigi_post_threadinfo
{
	CURL *curl;
	char *post_data;
};

struct dl_fldigi_download_threadinfo
{
	CURL *curl;
	FILE *file;
};

struct payload
{
	char *name;
	char *sentence_delimiter;
	char *field_delimiter;
	int fields;
	int string_limit;
	char *callsign;
	int shift;
	int baud;
	int baud0;
	int baud1;
	int parity;
	int	stopbits;
	int coding;
	int rtty_enabled;
	int domino_mode;
	int time;
	int latitude;
	int longitude;
	int latitude_nmea;
	int longitude_nmea;
	int altitude;
	int lockstatus;
	struct payload *next;
};

bool dl_fldigi_downloaded_once = false;
int dl_fldigi_initialised = 0;
const char *dl_fldigi_cache_file;
struct payload *payload_list = NULL;
struct payload *current_payload = NULL;
time_t rxTimer = 0;

pthread_mutex_t dl_fldigi_tid_mutex = PTHREAD_MUTEX_INITIALIZER;

static long int thread_identifier();
static void *dl_fldigi_post_thread(void *thread_argument);
static void *dl_fldigi_download_thread(void *thread_argument);
static void put_status_safe(const char *msg, double timeout = 0.0, status_timeout action = STATUS_CLEAR);
static void print_put_status(const char *msg, double timeout = 0.0, status_timeout action = STATUS_CLEAR);
static void dl_fldigi_delete_payloads();
static void dl_fldigi_enable_rtty();
static void dl_fldigi_enable_domex();

static long int thread_identifier()
{
	/* WARNING: Horrible hack below! This was the option that required the least effort ! */
	/* Unlike the majority of fldigi we create and destroy threads for one off tasks regularly
	 * and rely on the OS, rather than the enum in threads.h, to identify threads in debug
	 * messages. Unfortunatly it is never guaranteed that pthread_t is an int; on MINGW32 for
	 * example it is infact a struct containing a pointer and an integer. This makes using
	 * pthread_self directly with fprintf difficult; so this function simply grabs the first
	 * few byes of the pthread_t; a bit crude but seemingly effective. See http://gist.github.com/489883 */

	long int identifier;
	pthread_t thread_id;

	thread_id = pthread_self();
	identifier = 0;

	if (sizeof(long int) > sizeof(pthread_t))
	{
		memcpy(&identifier, &thread_id, sizeof(pthread_t));
	}
	else
	{
		memcpy(&identifier, &thread_id, sizeof(long int));
	}

	return identifier;
}

void dl_fldigi_init()
{
	CURLcode r;
	const char *home;
	char *cache_file;
	size_t i, fsz;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: dl_fldigi_init() was executed in thread %li\n", thread_identifier());
	#endif

	/* The only thread-unsafe step of dl_fldigi. Needs to be run once, at the start, when there are no other threads. */
	r = curl_global_init(CURL_GLOBAL_ALL);

	if (r != 0)
	{
		fprintf(stderr, "dl_fldigi: curl_global_init failed: (%i) %s\n", r, curl_easy_strerror(r));
		exit(EXIT_FAILURE);
	}

	home = HomeDir.c_str();

	fsz = strlen(home) + strlen(DL_FLDIGI_CACHE_FILE) + 1;
	i = 0;

	cache_file = (char *) malloc(fsz);

	if (cache_file == NULL)
	{
		fprintf(stderr, "dl_fldigi: denied %zi bytes of RAM for 'cache_file'\n", fsz);
		exit(EXIT_FAILURE);
	}

	memcpy(cache_file + i, home, strlen(home));
	i += strlen(home);

	memcpy(cache_file + i, DL_FLDIGI_CACHE_FILE, strlen(DL_FLDIGI_CACHE_FILE));
	i += strlen(DL_FLDIGI_CACHE_FILE);

	cache_file[i] = '\0';
	i++;

	if (i != fsz)
	{
		fprintf(stderr, "dl_fldigi: assertion failed \"i == fsz\" (i = %zi, fsz = %zi) \n", i, fsz);
	}

	dl_fldigi_cache_file = cache_file;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: cache file is '%s'\n", dl_fldigi_cache_file);
	#endif

	dl_fldigi_initialised = 1;
	full_memory_barrier();
}

void cb_dl_fldigi_toggle_dl_online()
{
	progdefaults.dl_online = !progdefaults.dl_online;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: set dl_online to %s\n", progdefaults.dl_online ? "true" : "false");
	#endif

	/* If this is the first time we've come online... */
	if (progdefaults.dl_online && !dl_fldigi_downloaded_once)
	{
		dl_fldigi_download();
		dl_fldigi_downloaded_once = 1;
	}
}

static void put_status_safe(const char *msg, double timeout, status_timeout action)
{
	ENSURE_THREAD(DL_FLDIGI_TID);

	pthread_mutex_lock(&dl_fldigi_tid_mutex);
	print_put_status(msg, timeout, action);
	full_memory_barrier();
	pthread_mutex_unlock(&dl_fldigi_tid_mutex);
}

static void print_put_status(const char *msg, double timeout, status_timeout action)
{
	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: UI status: '%s'\n", msg);
	#endif

	put_status(msg, timeout, action);
}

void dl_fldigi_post(const char *data, const char *identity)
{
	char *data_safe, *identity_safe, *post_data;
	size_t i, data_length, identity_length, post_data_length;
	struct dl_fldigi_post_threadinfo *t;
	pthread_attr_t attr;
	pthread_t thread;
	CURL *curl;
	CURLcode r1, r2, r3;
	string server_location_complete = progdefaults.server_location;
	/* The first of two globals accessed by this function */
	if (!dl_fldigi_initialised)
	{
		fprintf(stderr, "dl_fldigi: a call to dl_fldigi_post was aborted; dl_fldigi has not been initialised\n");
		return;
	}

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: dl_fldigi_post() was executed in \"parent\" thread %li\n", thread_identifier());
		fprintf(stderr, "dl_fldigi: begin attempting to post string '%s' and identity '%s'\n", data, identity);
	#endif

	curl = curl_easy_init();

	if (!curl)
	{
		fprintf(stderr, "dl_fldigi: curl_easy_init failed\n");
		return;
	}

	data_safe     = curl_easy_escape(curl, data, 0);
	identity_safe = curl_easy_escape(curl, identity, 0);

	if (data_safe != NULL)
	{
		data_length     = strlen(data_safe);
	}
	else
	{
		data_length = 0;
	}

	if (identity_safe != NULL)
	{
		identity_length = strlen(identity_safe);
	}
	else
	{
		identity_length = 0;
	}

	#define POST_DATAKEY        "string="
	#define POST_IDENTITYKEY    "&identity="

	post_data_length = data_length + identity_length + strlen(POST_DATAKEY) + strlen(POST_IDENTITYKEY) + 1;
	post_data = (char *) malloc(post_data_length);

	if (post_data == NULL)
	{
		fprintf(stderr, "dl_fldigi: denied %zi bytes of RAM for 'post_data'\n", post_data_length);
		curl_easy_cleanup(curl);
		return;
	}

	/* Cook up "string=$data_safe&identity=$identity_safe" */
	i = 0;

	memcpy(post_data + i, POST_DATAKEY, strlen(POST_DATAKEY));
	i += strlen(POST_DATAKEY);

	if (data_length != 0)
	{
		memcpy(post_data + i, data_safe, data_length);
		i += data_length;
	}

	memcpy(post_data + i, POST_IDENTITYKEY, strlen(POST_IDENTITYKEY));
	i += strlen(POST_IDENTITYKEY);

	if (identity_length != 0)
	{
		memcpy(post_data + i, identity_safe, identity_length);
		i += identity_length;
	}

	post_data[i] = '\0';
	i ++;

	if (i != post_data_length)
	{
		fprintf(stderr, "dl_fldigi: assertion failed \"i == post_data_length\" (i = %zi, post_data_length = %zi) \n", i, post_data_length);
	}

	curl_free(data_safe);
	curl_free(identity_safe);

	/* The second of two globals accessed by this function: progdefaults.dl_online */
	if (progdefaults.dl_online)
	{
		#ifdef DL_FLDIGI_DEBUG
			fprintf(stderr, "dl_fldigi: preparing to post '%s'\n", post_data);
		#endif
	}
	else
	{
		fprintf(stderr, "dl_fldigi: (offline mode) would have posted '%s'\n", post_data);
		curl_easy_cleanup(curl);
		free(post_data);
		return;
	}

	r1 = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
	if (r1 != 0)
	{
		fprintf(stderr, "dl_fldigi: curl_easy_setopt (CURLOPT_POSTFIELDS) failed: %s\n", curl_easy_strerror(r1));
		curl_easy_cleanup(curl);
		free(post_data);
		return;
	}

	r2 = curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, post_data_length - 1);
	if (r2 != 0)
	{
		fprintf(stderr, "dl_fldigi: curl_easy_setopt (CURLOPT_POSTFIELDSIZE) failed: %s\n", curl_easy_strerror(r2));
		curl_easy_cleanup(curl);
		free(post_data);
		return;
	}
	
	r3 = curl_easy_setopt(curl, CURLOPT_URL, server_location_complete.append("listen.php").c_str());
	if (r3 != 0)
	{
		fprintf(stderr, "dl_fldigi: curl_easy_setopt (CURLOPT_URL) failed: %s\n", curl_easy_strerror(r3));
		curl_easy_cleanup(curl);
		free(post_data);
		return;
	}

	t = (struct dl_fldigi_post_threadinfo *) malloc(sizeof(struct dl_fldigi_post_threadinfo));

	if (t == NULL)
	{
		fprintf(stderr, "dl_fldigi: denied %zi bytes of RAM for 'struct dl_fldigi_post_threadinfo'\n", sizeof(struct dl_fldigi_post_threadinfo));
		curl_easy_cleanup(curl);
		free(post_data);
		return;
	}

	t->curl = curl;
	t->post_data = post_data;

	/* !! */
	full_memory_barrier();
	
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	
	/* the 4th argument passes the thread the information it needs */
	if (pthread_create(&thread, &attr, dl_fldigi_post_thread, (void *) t) != 0)
	{
		perror("dl_fldigi: post pthread_create");
		curl_easy_cleanup(curl);
		free(post_data);
		free(t);
	}
	else
	{
	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: created a thread to finish the posting, returning now\n");
	#endif
	}
	
	pthread_attr_destroy(&attr);
}

static void *dl_fldigi_post_thread(void *thread_argument)
{
	struct dl_fldigi_post_threadinfo *t;
	t = (struct dl_fldigi_post_threadinfo *) thread_argument;
	CURLcode result;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: (thread %li) posting '%s'\n", thread_identifier(), t->post_data);
	#endif

	SET_THREAD_ID(DL_FLDIGI_TID);

	put_status_safe("dl_fldigi: sentence uploading...", 10);

	result = curl_easy_perform(t->curl);

	/* tests CURL-success only */
	if (result == 0)
	{
		#ifdef DL_FLDIGI_DEBUG
			fprintf(stderr, "dl_fldigi: (thread %li) curl result (%i) Success!\n", thread_identifier(), result);
		#endif

		put_status_safe("dl_fldigi: sentence uploaded!", 10);
	}
	else
	{
		#ifdef DL_FLDIGI_DEBUG
			fprintf(stderr, "dl_fldigi: (thread %li) curl result (%i) %s\n", thread_identifier(), result, curl_easy_strerror(result));	
		#endif

		put_status_safe("dl_fldigi: sentence upload failed", 10);
	}

	curl_easy_cleanup(t->curl);
	free(t->post_data);
	free(t);

	pthread_exit(0);
}

void dl_fldigi_download()
{
	pthread_attr_t attr;
	pthread_t thread;
	struct dl_fldigi_download_threadinfo *t;
	CURL *curl;
	CURLcode r1, r3;
	FILE *file;
	int r2;
	string server_location_complete = progdefaults.server_location;

	if (!dl_fldigi_initialised)
	{
		fprintf(stderr, "dl_fldigi: a call to dl_fldigi_download was aborted; dl_fldigi has not been initialised\n");
		return;
	}

	if (!progdefaults.dl_online)
	{
		fprintf(stderr, "dl_fldigi: a call to dl_fldigi_download was aborted: refusing to download a file whist in offline mode.\n");
		return;
	}

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: dl_fldigi_download() was executed in \"parent\" thread %li\n", thread_identifier());
		fprintf(stderr, "dl_fldigi: begin download attempt...\n");
	#endif

	curl = curl_easy_init();

	if (!curl)
	{
		fprintf(stderr, "dl_fldigi: curl_easy_init failed\n");
		return;
	}

	r1 = curl_easy_setopt(curl, CURLOPT_URL, server_location_complete.append("allpayloads.php").c_str());
	if (r1 != 0)
	{
		fprintf(stderr, "dl_fldigi: curl_easy_setopt (CURLOPT_URL) failed: %s\n", curl_easy_strerror(r1));
		curl_easy_cleanup(curl);
		return;
	}

	t = (struct dl_fldigi_download_threadinfo *) malloc(sizeof(struct dl_fldigi_download_threadinfo));

	if (t == NULL)
	{
		fprintf(stderr, "dl_fldigi: denied %zi bytes of RAM for 'struct dl_fldigi_download_threadinfo'\n", sizeof(struct dl_fldigi_download_threadinfo));
		curl_easy_cleanup(curl);
		return;
	}

	file = fopen(dl_fldigi_cache_file, "w");

	if (file == NULL)
	{
		perror("dl_fldigi: fopen cache file");
		curl_easy_cleanup(curl);
		free(t);
		return;
	}

	r2 = flock(fileno(file), LOCK_EX | LOCK_NB);

	if (r2 == EWOULDBLOCK || r2 == EAGAIN)
	{
		fprintf(stderr, "dl_fldigi: cache file is locked; not downloading\n");
		curl_easy_cleanup(curl);
		fclose(file);
		free(t);
		return;
	}
	else if (r2 != 0)
	{
		perror("dl_fldigi: flock cache file");
		curl_easy_cleanup(curl);
		fclose(file);
		free(t);
		return;
	}

	r3 = curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
	if (r3 != 0)
	{
		fprintf(stderr, "dl_fldigi: curl_easy_setopt (CURLOPT_WRITEDATA) failed: %s\n", curl_easy_strerror(r3));
		curl_easy_cleanup(curl);
		flock(fileno(file), LOCK_UN);
		fclose(file);
		free(t);
		return;
	}

	t->curl = curl;
	t->file = file;

	/* !! */
	full_memory_barrier();
	
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	
	/* the 4th argument passes the thread the information it needs */
	if (pthread_create(&thread, &attr, dl_fldigi_download_thread, (void *) t) != 0)
	{
		perror("dl_fldigi: download pthread_create");
		curl_easy_cleanup(curl);
		flock(fileno(file), LOCK_UN);
		fclose(file);
		free(t);
	}
	else
	{
	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: created a thread to perform the download, returning now\n");
	#endif
	}
	
	pthread_attr_destroy(&attr);
}

static void *dl_fldigi_download_thread(void *thread_argument)
{
	struct dl_fldigi_download_threadinfo *t;
	t = (struct dl_fldigi_download_threadinfo *) thread_argument;
	CURLcode result;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: (thread %li) performing download...\n", thread_identifier());
	#endif

	SET_THREAD_ID(DL_FLDIGI_TID);

	put_status_safe("dl_fldigi: payload information: downloading...", 10);

	result = curl_easy_perform(t->curl);
	curl_easy_cleanup(t->curl);

	if (result == 0)
	{
		#ifdef DL_FLDIGI_DEBUG
			fprintf(stderr, "dl_fldigi: (thread %li) curl result (%i) Success!\n", thread_identifier(), result);
		#endif

		pthread_mutex_lock(&dl_fldigi_tid_mutex);
		put_status("dl_fldigi: payload information: downloaded!", 10);
		REQ(dl_fldigi_update_payloads);
		full_memory_barrier();
		pthread_mutex_unlock(&dl_fldigi_tid_mutex);
	}
	else
	{
		#ifdef DL_FLDIGI_DEBUG
			fprintf(stderr, "dl_fldigi: (thread %li) curl result (%i) %s\n", thread_identifier(), result, curl_easy_strerror(result));	
		#endif

		put_status_safe("dl_fldigi: payload information: download failed", 10);
	}

	flock(fileno(t->file), LOCK_UN);
	fclose(t->file);
	free(t);

	pthread_exit(0);
}

static void dl_fldigi_delete_payloads()
{
	struct payload *d, *n;
	int i;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: (thread %li) cleaning in-memory payload information (structs)...\n", thread_identifier());
	#endif

	if (bHAB)
	{
		habFlightXML->clear();
	}

	habFlightXML_conf->clear();

	d = payload_list;
	i = 0;

	while (d != NULL)
	{
		#define auto_free(x)   do { void *a = (x); if (a != NULL) { free(a); } } while (0)

		auto_free(d->name);
		auto_free(d->sentence_delimiter);
		auto_free(d->field_delimiter);
		auto_free(d->callsign);

		n = d->next;
		free(d);
		d = n;

		i++;
	}

	payload_list = NULL;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: (thread %li) cleaned %i payload structs...\n", thread_identifier(), i);
	#endif
}

void dl_fldigi_update_payloads()
{
	FILE *file;
	int r1, r_shift, r_baud, r_baud1;
	const char *r_coding, *r_parity, *r_stopbits;
	IrrXMLReader *xml;
	struct payload *p, *n;
	int i, dbfield_no;
	char *dbfield_name;
	char *dbfield_format;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: (thread %li) attempting to update UI...\n", thread_identifier());
	#endif

	file = fopen(dl_fldigi_cache_file, "r");

	if (file == NULL)
	{
		perror("dl_fldigi: fopen cache file (read)");
		return;
	}

	r1 = flock(fileno(file), LOCK_SH | LOCK_NB);

	if (r1 == EWOULDBLOCK || r1 == EAGAIN)
	{
		fprintf(stderr, "dl_fldigi: cache file is locked; not updating UI\n");
		fclose(file);
		return;
	}
	else if (r1 != 0)
	{
		perror("dl_fldigi: flock cache file (read)");
		fclose(file);
		return;
	}

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: opened file, now updating UI...\n");
	#endif

	dl_fldigi_delete_payloads();

	xml = createIrrXMLReader(file);

	if (!xml)
	{
		fprintf(stderr, "dl_fldigi: parsing payload info: createIrrXMLReader failed\n");
		flock(fileno(file), LOCK_UN);
		fclose(file);
		return;
	}

	p = payload_list;
	i = 0;
	dbfield_no = 1;
	dbfield_name = NULL;
	dbfield_format = NULL;

	while (xml->read())
	{
		if (xml->getNodeType() == EXN_ELEMENT)
		{
			if (strcmp("name", xml->getNodeName()) == 0)
			{
				n = (struct payload *) malloc(sizeof(struct payload));
				
				if (n == NULL)
				{
					fprintf(stderr, "dl_fldigi: denied %zi bytes of RAM for 'struct payload'\n", sizeof(struct payload));
					dl_fldigi_delete_payloads();
					delete xml;
					flock(fileno(file), LOCK_UN);
					fclose(file);
					return;
				}

				/* Nulls all the char pointers, the next pointer, zeroes the ints */
				memset(n, 0, sizeof(struct payload));

				if (p == NULL)
				{
					payload_list = n;
					p = n;
				}
				else
				{
					p->next = n;
					p = p->next;
				}

				xml->read();
				p->name = strdup(xml->getNodeData());
				xml->read();

				if (bHAB)
				{
					habFlightXML->add(p->name);
				}

				habFlightXML_conf->add(p->name);

				dbfield_no = 1;
				
				#ifdef DL_FLDIGI_DEBUG
					fprintf(stderr, "dl_fldigi: adding payload %i: '%s'\n", i, p->name);
				#endif

				i++;
			}
			
			else if (strcmp("sentence_delimiter", xml->getNodeName()) == 0)
			{
				xml->read();
				p->sentence_delimiter = strdup(xml->getNodeData());
				xml->read();
			}
			
			else if (strcmp("string_limit", xml->getNodeName()) == 0)
			{
				xml->read();
				p->string_limit = atoi(xml->getNodeData());
				xml->read();
			}
			
			else if (strcmp("field_delimiter", xml->getNodeName()) == 0)
			{
				xml->read();
				p->field_delimiter = strdup(xml->getNodeData());
				xml->read();
			}
			
			else if (strcmp("fields", xml->getNodeName()) == 0)
			{
				xml->read();
				p->fields = atoi(xml->getNodeData());
				xml->read();
			}
			
			else if (strcmp("callsign", xml->getNodeName()) == 0)
			{
				xml->read();
				p->callsign = strdup(xml->getNodeData());
				xml->read();
			}
			
			else if (strcmp("rtty", xml->getNodeName()) == 0)
			{
				p->rtty_enabled = 1;
				xml->read();
			}
			
			else if (strcmp("dominoex", xml->getNodeName()) == 0)
			{
				xml->read();
				p->domino_mode = atoi(xml->getNodeData());
				xml->read();
			}
			
			else if (strcmp("shift", xml->getNodeName()) == 0)
			{
				xml->read();
				r_shift = atoi(xml->getNodeData());
				xml->read();

				switch ( r_shift )
				{
						case 23:
							p->shift = 0;
							break;
						case 85:
							p->shift = 1;
							break;
						case 160:
							p->shift = 2;
							break;
						case 170:
							p->shift = 3;
							break;
						case 182:
							p->shift = 4;
							break;
						case 200:
							p->shift = 5;
							break;
						case 240:
							p->shift = 6;
							break;
						case 350:
							p->shift = 7;
							break;
						case 425:
							p->shift = 8;
							break;
						case 600:
							p->shift = 9;
							break;
						case 850:
							p->shift = 10;
							break;
						}
			}
			
			else if (strcmp("baud", xml->getNodeName()) == 0)
			{
				xml->read();
				r_baud = atoi(xml->getNodeData());
				xml->read();
				switch	(r_baud)
				{
						case 45:
							p->baud = 0;
							break;
						//case 45.45: //of course switch/case only works with integers - dirty hack to save me changing everything.
						//	p->baud = 1;
						//	break;
						case 50:
							p->baud = 2;
							break;
						case 56:
							p->baud = 3;
							break;
						case 75:
							p->baud = 4;
							break;
						case 100:
							p->baud = 5;
							break;
						case 110:
							p->baud = 6;
							break;
						case 150:
							p->baud = 7;
							break;
						case 200:
							p->baud = 8;
							break;
						case 300:
							p->baud = 9;
							break;
				}
			}
			
			else if (strcmp("baud1", xml->getNodeName()) == 0)
			{
				xml->read();
				r_baud1 = atoi(xml->getNodeData());
				xml->read();
				switch	(r_baud1)
				{
					case 45:
						p->baud1 = 0;
						break;
						//case 45.45: //of course switch/case only works with integers - dirty hack to save me changing everything.
						//	p->baud1 = 1;
						//	break;
					case 50:
						p->baud1 = 2;
						break;
					case 56:
						p->baud1 = 3;
						break;
					case 75:
						p->baud1 = 4;
						break;
					case 100:
						p->baud1 = 5;
						break;
					case 110:
						p->baud1 = 6;
						break;
					case 150:
						p->baud1 = 7;
						break;
					case 200:
						p->baud1 = 8;
						break;
					case 300:
						p->baud1 = 9;
						break;
				}
			}
			
			else if (strcmp("parity", xml->getNodeName()) == 0)
			{
				xml->read();
				r_parity = xml->getNodeData();
				/*for some reason this breaks the parsing of the data.*/
				//xml->read(); 
				if (strcmp("none", r_parity) == 0)
				{
					p->parity = 0;
				}
				else if (strcmp("even", r_parity) == 0)
				{
					p->parity = 1;
				}
				else if (strcmp("odd", r_parity) == 0)
				{
					p->parity = 2;
				}
				else if (strcmp("zero", r_parity) == 0)
				{
					p->parity = 3;
				}
				else if (strcmp("one", r_parity) == 0)
				{
					p->parity = 4;
				}
			}
			
			else if (strcmp("stop", xml->getNodeName()) == 0)
			{
				xml->read();
				r_stopbits = xml->getNodeData();
				
				//xml->read();
				if (strcmp("1", r_stopbits) == 0) 
				{
					p->stopbits = 0;
				}
				else if (strcmp("1.5", r_stopbits) == 0) 
				{
					p->stopbits = 1;
				}
				else if (strcmp("2", r_stopbits) == 0) 
				{
					p->stopbits = 2;
				}
			}
			
			else if (strcmp("coding", xml->getNodeName()) == 0)
			{
				xml->read();
				r_coding = xml->getNodeData();

				if (strcmp("baudot", r_coding) == 0)
				{
					p->coding = 0;
				}
				else if (strcmp("ascii-7", r_coding) == 0)
				{
					p->coding = 1;
				}
				else if (strcmp("ascii-8", r_coding) == 0)
				{
					p->coding = 2;
				}
			}
			
			else if (strcmp("dbfield", xml->getNodeName()) == 0)
			{
				xml->read();
				
				if(dbfield_name) free(dbfield_name);
				dbfield_name = strdup(xml->getNodeData());
			}
			
			else if (strcmp("format", xml->getNodeName()) == 0)
			{
				xml->read();
				
				if(dbfield_format) free(dbfield_format);
				dbfield_format = strdup(xml->getNodeData());
			}
		}
		
		else if (xml->getNodeType() == EXN_ELEMENT_END)
		{
			if (strcmp("field", xml->getNodeName()) == 0)
			{
				if (strcmp("time", dbfield_name) == 0)
				{
					p->time = dbfield_no;
				}
				else if (strcmp("latitude", dbfield_name) == 0)
				{
					p->latitude = dbfield_no;
					p->latitude_nmea = 0;
					if(dbfield_format && strcmp("ddmm.mm", dbfield_format) == 0)
						p->latitude_nmea = 1;
				}
				else if (strcmp("longitude", dbfield_name) == 0)
				{
					p->longitude = dbfield_no;
					p->longitude_nmea = 0;
					if(dbfield_format && strcmp("dddmm.mm", dbfield_format) == 0)
						p->longitude_nmea = 1;
				}
				else if (strcmp("altitude", dbfield_name) == 0)
				{
					p->altitude = dbfield_no;
				}
				else if (strcmp("lockstatus", dbfield_name) == 0)
				{
					p->lockstatus = dbfield_no;
				}
				
				if(dbfield_name) free(dbfield_name);
				dbfield_name = NULL;
				
				if(dbfield_format) free(dbfield_format);
				dbfield_format = NULL;
				
				dbfield_no++;
			}
		}
	}
	
	if(dbfield_name) free(dbfield_name);
	if(dbfield_format) free(dbfield_format);
	
	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: UI updated: added %i payloads.\n", i);
		fprintf(stderr, "dl_fldigi: post UI update: attempting to re-select (but not configure) payload '%s'\n", progdefaults.xmlPayloadname.c_str());
	#endif

	if (bHAB && progdefaults.xmlPayloadname.length() != 0)
	{
		const Fl_Menu_Item *item = habFlightXML->find_item(progdefaults.xmlPayloadname.c_str());
		if (item != NULL) habFlightXML->value(item);
		habFlightXML->redraw();
	}

	const Fl_Menu_Item *item = habFlightXML_conf->find_item(progdefaults.xmlPayloadname.c_str());
	if (item != NULL) habFlightXML_conf->value(item);
	habFlightXML_conf->redraw();

	put_status("dl_fldigi: payload information loaded", 10);

	delete xml;
	flock(fileno(file), LOCK_UN);
	fclose(file);
}

void cb_dl_fldigi_select_payload(Fl_Widget *o, void *a)
{
	progdefaults.xmlPayloadname = ((Fl_Choice *) o)->text();
	progdefaults.changed = true;

	if (bHAB && o == habFlightXML_conf)
	{	
		const Fl_Menu_Item *item = habFlightXML->find_item(progdefaults.xmlPayloadname.c_str());
		if (item != NULL) habFlightXML->value(item);
		habFlightXML->redraw();
	}

	if (bHAB && o == habFlightXML)
	{
		const Fl_Menu_Item *item = habFlightXML_conf->find_item(progdefaults.xmlPayloadname.c_str());
		if (item != NULL) habFlightXML_conf->value(item);
		habFlightXML_conf->redraw();
	}

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: set current payload name to '%s' (have not configured)\n", progdefaults.xmlPayloadname.c_str());
	#endif
}

void cb_dl_fldigi_select_payload_manual(char *payload)
{
	progdefaults.xmlPayloadname = payload;
	free(payload);
	progdefaults.changed = true;

	if (bHAB)
	{	
		const Fl_Menu_Item *item = habFlightXML->find_item(progdefaults.xmlPayloadname.c_str());
		if (item != NULL) habFlightXML->value(item);
		habFlightXML->redraw();
	}

	if (bHAB)
	{
		const Fl_Menu_Item *item = habFlightXML_conf->find_item(progdefaults.xmlPayloadname.c_str());
		if (item != NULL) habFlightXML_conf->value(item);
		habFlightXML_conf->redraw();
	}

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: set current payload name to '%s' (have not configured) (manual)\n", progdefaults.xmlPayloadname.c_str());
	#endif
}

void cb_dl_fldigi_configure_payload(Fl_Widget *o, void *a)
{
	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: configuring current payload name '%s'\n", progdefaults.xmlPayloadname.c_str());
	#endif

	dl_fldigi_select_payload(progdefaults.xmlPayloadname.c_str());
}

static void dl_fldigi_enable_rtty()
{
	if (current_payload->baud1 > 0) 
	{
		if (current_payload->baud == current_payload->baud1)
		{
			progdefaults.rtty_baud = current_payload->baud0;
			current_payload->baud = current_payload->baud0;
		}
		else
		{
			progdefaults.rtty_baud = current_payload->baud1;
			current_payload->baud = current_payload->baud1;
		}
		
		selBaud->value(progdefaults.rtty_baud);
	}
	init_modem_sync(MODE_RTTY);
	resetRTTY();

	if (bHAB)
	{
		switch	(current_payload->baud)
		{
			case 0:
				habSwitchModes->label("RTTY 45");
				break;
			case 1:
				habSwitchModes->label("RTTY 45.45");
				break;
			case 2:
				habSwitchModes->label("RTTY 50");
				break;
			case 3:
				habSwitchModes->label("RTTY 56");
				break;
			case 4:
				habSwitchModes->label("RTTY 75");
				break;
			case 5:
				habSwitchModes->label("RTTY 100");
				break;
			case 6:
				habSwitchModes->label("RTTY 110");
				break;
			case 7:
				habSwitchModes->label("RTTY 150");
				break;
			case 8:
				habSwitchModes->label("RTTY 200");
				break;
			case 9:
				habSwitchModes->label("RTTY 300");
				break;
		}
	}
}

static void dl_fldigi_enable_domex()
{
	if (current_payload->domino_mode > 0) {
		switch ( current_payload->domino_mode )
		{
				case 4:
					init_modem_sync(MODE_DOMINOEX4);
					if (bHAB)
					{
						habSwitchModes->label("DomX4");
					}
					break;
				case 5:
					init_modem_sync(MODE_DOMINOEX5);
					{
						habSwitchModes->label("DomX5");
					}
					break;
				case 8:
					init_modem_sync(MODE_DOMINOEX8);
					{
						habSwitchModes->label("DomX8");
					}
					break;
				case 11:
					init_modem_sync(MODE_DOMINOEX11);
					{
						habSwitchModes->label("DomX11");
					}
					break;
				case 16:
					init_modem_sync(MODE_DOMINOEX16);
					{
						habSwitchModes->label("DomX16");
					}
					break;
				case 22:
					init_modem_sync(MODE_DOMINOEX22);
					{
						habSwitchModes->label("DomX22");
					}
					break;
		}
	}

	resetDOMEX();


}

void cb_dl_fldigi_switch_modes(Fl_Widget *o, void *a)
{
	if (current_payload == NULL)
		return;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: switching modes '%d, %d'\n", current_payload->rtty_enabled, current_payload->domino_mode);
	#endif

	if (current_payload->rtty_enabled and current_payload->domino_mode > 0) {
		if (active_modem->get_mode() == MODE_RTTY) {
			#ifdef DL_FLDIGI_DEBUG
			fprintf(stderr, "dl_fldigi: currently in RTTY mode\n");
			#endif
			
			dl_fldigi_enable_domex();
		}
	else {
		// else if (active_modem->get_mode() == MODE_DOMINOEX22) {  -- if a totally random other mode is chosen this will break.
			
			#ifdef DL_FLDIGI_DEBUG
			fprintf(stderr, "dl_fldigi: currently in DominoEX mode\n");
			#endif
			
			dl_fldigi_enable_rtty();
		}
	}
	else if (current_payload->rtty_enabled and current_payload->baud1 > 0) {
		dl_fldigi_enable_rtty();
		#ifdef DL_FLDIGI_DEBUG
			fprintf(stderr, "dl_fldigi: dual RTTY baud rates\n");
		#endif
	}
	
}

void dl_fldigi_select_payload(const char *name)
{
	struct payload *p;
	string s;
	int i;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: (thread %li) attempting to find and configure payload '%s'...\n", thread_identifier(), name);
	#endif

	p = payload_list;
	i = 0;

	while (p != NULL)
	{
		if (p->name != NULL && strcmp(p->name, name) == 0)
		{
			#ifdef DL_FLDIGI_DEBUG
				fprintf(stderr, "dl_fldigi: found payload '%s' at link %i...\n", p->name, i);
				fprintf(stderr, "dl_fldigi: configuring payload '%s':\n", p->name);
				fprintf(stderr, "dl_fldigi: {\n");

				#define print_i(n)  fprintf(stderr, "dl_fldigi:   " #n ": %i\n", p->n)
				#define print_s(n)  do \
				                    { \
				                    	if (p->n != NULL) \
				                        { \
				                    		fprintf(stderr, "dl_fldigi:   " #n ": '%s'\n", p->n); \
				                        } \
				                    	else \
				                        { \
				                    		fprintf(stderr, "dl_fldigi:   " #n ": NULL\n"); \
				                        } \
				                    } \
				                    while (0)

				print_s(name);
				print_s(sentence_delimiter);
				print_i(string_limit);
				print_s(field_delimiter);
				print_i(fields);
				print_s(callsign);
				if (p->rtty_enabled == 1) {
					print_i(shift);
					print_i(baud);
					print_i(coding);
					print_i(parity);
					print_i(stopbits);
					if(p->baud1 > 0) {
						print_i(baud0);
						print_i(baud1);
					}
				}
				if (p->domino_mode > 0) {
					print_i(domino_mode);
				}
				print_i(time);
				print_i(latitude);
				print_i(longitude);
				print_i(latitude_nmea);
				print_i(longitude_nmea);
				print_i(altitude);
				print_i(lockstatus);

				fprintf(stderr, "dl_fldigi: }\n");
			#endif

			/* TODO: Currently, we don't set progdefaults.changed (I think it might become annoying?).
			 * This might be a bad idea. */

			current_payload = p;

			progdefaults.xmlSentence_delimiter = p->sentence_delimiter;
			progdefaults.xml_stringlimit = p->string_limit;
			progdefaults.xmlField_delimiter = p->field_delimiter;
			progdefaults.xmlFields = p->fields;
			progdefaults.xmlCallsign = p->callsign;

			if (p->rtty_enabled == 1)
			{
				progdefaults.rtty_shift = p->shift;
				progdefaults.rtty_baud = p->baud;
				if(p->baud1 > 0) {
					p->baud0 = p->baud;
					progdefaults.rtty_baud0 = p->baud0;
					progdefaults.rtty_baud1 = p->baud1;
				}
				progdefaults.rtty_bits = p->coding;
				progdefaults.rtty_parity = p->parity;
				progdefaults.rtty_stop = p->stopbits;

				selShift->value(progdefaults.rtty_shift);
				selBaud->value(progdefaults.rtty_baud);
				selBits->value(progdefaults.rtty_bits);
				selParity->value(progdefaults.rtty_parity);
				selStopBits->value(progdefaults.rtty_stop);
			}

			if (p->domino_mode > 0 and p->rtty_enabled == 1)
			{
				/* Default to RTTY */
				dl_fldigi_enable_rtty();

				if (bHAB)
				{
					habSwitchModes->tooltip("RTTY, DomX22");
				}
			}
			else if (p->domino_mode > 0)
			{
				dl_fldigi_enable_domex();

				if (bHAB)
				{
					habSwitchModes->tooltip("DomX22 only");
				}
			}
			else
			{
				dl_fldigi_enable_rtty();

				if (bHAB)
				{
					habSwitchModes->tooltip("RTTY only");
				}
			}

			progdefaults.xml_time = p->time;
			progdefaults.xml_latitude = p->latitude;
			progdefaults.xml_longitude = p->longitude;
			progdefaults.xml_latitude_nmea = p->latitude_nmea;
			progdefaults.xml_longitude_nmea = p->longitude_nmea;
			progdefaults.xml_altitude = p->altitude;
			progdefaults.xml_lockstatus = p->lockstatus;

			#define update_conf_ui_s(n)  cd_ ## n->value(progdefaults.n.c_str());
			#define update_conf_ui_i(n)  cd_ ## n->value(progdefaults.n);
			update_conf_ui_s(xmlSentence_delimiter);
			update_conf_ui_s(xmlField_delimiter);
			update_conf_ui_i(xmlFields);
			update_conf_ui_s(xmlCallsign);
			update_conf_ui_i(xml_time);
			update_conf_ui_i(xml_latitude);
			update_conf_ui_i(xml_longitude);
			update_conf_ui_i(xml_latitude_nmea);
			update_conf_ui_i(xml_longitude_nmea);
			update_conf_ui_i(xml_altitude);


			#ifdef DL_FLDIGI_DEBUG
				fprintf(stderr, "dl_fldigi: configured payload '%s'...\n", p->name);
			#endif

			/* This way of doing concatenation is a bit ugly. */
			s = "dl_fldigi: configured modem for payload ";
			s += p->name;
			put_status(s.c_str(), 10);

			return;
		}

		p = p->next;
		i++;
	}

	fprintf(stderr, "dl_fldigi: searched %i payloads; unable to find '%s' for configuring\n", i, name);
}

void dl_fldigi_reset_rxtimer()
{
	rxTimer = time(NULL);

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: reset rxTimer to %li\n", rxTimer);
	#endif

	dl_fldigi_update_rxtimer();
}

void dl_fldigi_update_rxtimer()
{
	time_t now, delta, seconds, minutes;
	char buf[16];

	now = time(NULL);
	delta = now - rxTimer;

	seconds = delta % 60;
	minutes = delta / 60;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: rxTimer update: rxtimer=%li, now=%li, delta=%li, minutes=%li, seconds=%li\n", 
		                rxTimer, now, delta, minutes, seconds);
	#endif

	if (minutes > 60)
	{
		if (bHAB)
		{
			#ifdef DL_FLDIGI_DEBUG
				fprintf(stderr, "dl_fldigi: setting habTimeSinceLastRx to 'ages'\n");
			#endif

			habTimeSinceLastRx->value("ages");
		}
		else
		{
			#ifdef DL_FLDIGI_DEBUG
				fprintf(stderr, "dl_fldigi: would have set habTimeSinceLastRx to 'ages'\n");
			#endif
		}
		
		return;
	}

	snprintf(buf, 16, "%ldm %lds", minutes, seconds);

	if (bHAB)
	{
		#ifdef DL_FLDIGI_DEBUG
			fprintf(stderr, "dl_fldigi: setting habTimeSinceLastRx to '%s'\n", buf);
		#endif

		habTimeSinceLastRx->value(buf);
	}
	else
	{
		#ifdef DL_FLDIGI_DEBUG
			fprintf(stderr, "dl_fldigi: would have set habTimeSinceLastRx to '%s'\n", buf);
		#endif
	}
}

/* Converts any geographic coordinate string (DMS) to degrees (double) */
double dl_fldigi_geotod(char *s)
{
	int i = 0, neg = 0;
	double r = 0;
	
	while(*s)
	{
		if(*s == 'S' || *s == 's' || *s == 'W' || *s == 'w') neg = -1;
		else if((*s >= '0' && *s <= '9') || *s == '-' || *s == '+' || *s == '.')
		{
			char *es = s;
			double d = strtod(s, &es);
			
			/* Give up if no conversion was performed */
			if(es == s || es == NULL) return(r);
			
			/* Add the value to the total */
			switch(i++)
			{
			case 0: r  = d; break; /* Degrees */
			case 1: r += d / 60; break; /* Minutes */
			case 2: r += d / 3600; break; /* Seconds */
			}
			
			s = es;
			continue;
		}
		
		s++;
	}
	
	if(neg) r = -r;
	return(r);
}

void dl_fldigi_gps_save_position()
{
	progdefaults.myLat = gpsTLat->value();
	progdefaults.myLon = gpsTLon->value();
	
	MyLat->value(progdefaults.myLat.c_str());
	MyLon->value(progdefaults.myLon.c_str());
	
	progdefaults.changed = true;
}
