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

#include "configuration.h"
#include "dl_fldigi.h"
#include "util.h"
#include "fl_digi.h"
#include "qrunner.h"

#define DL_FLDIGI_DEBUG
#define DL_FLDIGI_CACHE_FILE "/.dl_fldigi_cache.xml"

int dl_fldigi_initialised = 0;
const char *dl_fldigi_cache_file;

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

static void *dl_fldigi_post_thread(void *thread_argument);
static void *dl_fldigi_download_thread(void *thread_argument);

void dl_fldigi_init()
{
	CURLcode r;
	char *home;
	size_t i, fsz;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: dl_fldigi_init() was executed in thread %li\n", pthread_self());
	#endif

	/* The only thread-unsafe step of dl_fldigi. Needs to be run once, at the start, when there are no other threads. */
	r = curl_global_init(CURL_GLOBAL_ALL);

	if (r != 0)
	{
		fprintf(stderr, "dl_fldigi: curl_global_init failed: (%i) %s\n", r, curl_easy_strerror(r));

		exit(EXIT_FAILURE);
	}

	home = getenv("HOME");
	if (home == NULL || strlen(home) == 0)
	{
		fprintf(stderr, "dl_fldigi: getenv(\"HOME\") failed.");
		exit(EXIT_FAILURE);
	}

	fsz = strlen(home) + strlen(DL_FLDIGI_CACHE_FILE) + 1;
	i = 0;

	dl_fldigi_cache_file = malloc(fsz);

	memcpy(dl_fldigi_cache_file + i, home, strlen(home));
	i += strlen(home);

	memcpy(dl_fldigi_cache_file + i, DL_FLDIGI_CACHE_FILE, strlen(DL_FLDIGI_CACHE_FILE));
	i += strlen(DL_FLDIGI_CACHE_FILE);

	dl_fldigi_cache_file[i] = '\0';
	i++;

	if (i != fsz)
	{
		fprintf(stderr, "dl_fldigi: assertion failed \"i == fsz\" (i = %zi, fsz = %zi) \n", i, fsz);
	}

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: cache file is '%s'\n", dl_fldigi_cache_file);
	#endif

	dl_fldigi_initialised = 1;
	full_memory_barrier();
}

void dl_fldigi_post(const char *data, const char *identity)
{
	char *data_safe, *identity_safe, *post_data;
	size_t i, data_length, identity_length, post_data_length;
	struct dl_fldigi_post_threadinfo *t;
	pthread_t thread;
	CURL *curl;
	CURLcode r1, r2, r3;

	/* The first of two globals accessed by this function */
	if (!dl_fldigi_initialised)
	{
		fprintf(stderr, "dl_fldigi: a call to dl_fldigi_post was aborted; dl_fldigi has not been initialised\n");
		return;
	}

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: dl_fldigi_post() was executed in \"parent\" thread %li\n", pthread_self());
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
		return;
	}

	r1 = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
	if (r1 != 0)
	{
		fprintf(stderr, "dl_fldigi: curl_easy_setopt (CURLOPT_POSTFIELDS) failed: %s\n", curl_easy_strerror(r1));
		curl_easy_cleanup(curl);
		return;
	}

	r2 = curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, post_data_length - 1);
	if (r2 != 0)
	{
		fprintf(stderr, "dl_fldigi: curl_easy_setopt (CURLOPT_POSTFIELDSIZE) failed: %s\n", curl_easy_strerror(r2));
		curl_easy_cleanup(curl);
		return;
	}

	r3 = curl_easy_setopt(curl, CURLOPT_URL, "http://www.robertharrison.org/listen/listen.php");
	if (r3 != 0)
	{
		fprintf(stderr, "dl_fldigi: curl_easy_setopt (CURLOPT_URL) failed: %s\n", curl_easy_strerror(r3));
		curl_easy_cleanup(curl);
		return;
	}

	t = (struct dl_fldigi_post_threadinfo *) malloc(sizeof(struct dl_fldigi_post_threadinfo));

	if (t == NULL)
	{
		fprintf(stderr, "dl_fldigi: denied %zi bytes of RAM for 'struct dl_fldigi_post_threadinfo'\n", sizeof(struct dl_fldigi_post_threadinfo));
		curl_easy_cleanup(curl);
		return;
	}

	t->curl = curl;
	t->post_data = post_data;

	/* !! */
	full_memory_barrier();

	/* the 4th argument passes the thread the information it needs */
	if (pthread_create(&thread, NULL, dl_fldigi_post_thread, (void *) t) != 0)
	{
		perror("dl_fldigi: post pthread_create");
		curl_easy_cleanup(curl);
		return;
	}

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: created a thread to finish the posting, returning now\n");
	#endif
}

void *dl_fldigi_post_thread(void *thread_argument)
{
	struct dl_fldigi_post_threadinfo *t;
	t = (struct dl_fldigi_post_threadinfo *) thread_argument;
	CURLcode result;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: (thread %li) posting '%s'\n", pthread_self(), t->post_data);
	#endif

	result = curl_easy_perform(t->curl);

	#ifdef DL_FLDIGI_DEBUG
		if (result == 0)
		{
			fprintf(stderr, "dl_fldigi: (thread %li) curl result (%i) Success!\n", pthread_self(), result);
		}
		else
		{
			fprintf(stderr, "dl_fldigi: (thread %li) curl result (%i) %s\n", pthread_self(), result, curl_easy_strerror(result));	
		}
	#endif

	curl_easy_cleanup(t->curl);
	free(t->post_data);
	free(t);

	pthread_exit(0);
}


void dl_fldigi_download()
{
	pthread_t thread;
	CURL *curl;
	CURLcode r1, r3;
	FILE *file;
	int r2;

	if (!dl_fldigi_initialised)
	{
		fprintf(stderr, "dl_fldigi: a call to dl_fldigi_download was aborted; dl_fldigi has not been initialised\n");
		callback(NULL);
		return;
	}

	if (!progdefaults.dl_online)
	{
		fprintf(stderr, "dl_fldigi: a call to dl_fldigi_download was aborted: refusing to download a file whist in offline mode.\n");
		return;
	}

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: dl_fldigi_download() was executed in \"parent\" thread %li\n", pthread_self());
		fprintf(stderr, "dl_fldigi: begin download attempt...\n");
	#endif

	curl = curl_easy_init();

	if (!curl)
	{
		fprintf(stderr, "dl_fldigi: curl_easy_init failed\n");
		return;
	}

	r1 = curl_easy_setopt(curl, CURLOPT_URL, "http://www.robertharrison.org/listen/allpayloads.php");
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
		return;
	}

	r2 = flock(fileno(file), LOCK_EX | LOG_NB);

	if (r2 == EWOULDBLOCK)
	{
		fprintf(stderr, "dl_fldigi: cache file is locked; not downloading\n");
		curl_easy_cleanup(curl);
		fclose(file);
		return;
	}
	else if (r2 != 0)
	{
		perror("dl_fldigi: flock cache file");
		curl_easy_cleanup(curl);
		fclose(file);
		return;
	}

	r3 = curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
	if (r3 != 0)
	{
		fprintf(stderr, "dl_fldigi: curl_easy_setopt (CURLOPT_WRITEDATA) failed: %s\n", curl_easy_strerror(r3));
		curl_easy_cleanup(curl);
		flock(fileno(file), LOCK_UN);
		fclose(file);
		return;
	}

	t->curl = curl;
	t->file = file;

	/* !! */
	full_memory_barrier();

	/* the 4th argument passes the thread the information it needs */
	if (pthread_create(&thread, NULL, dl_fldigi_download_thread, (void *) t) != 0)
	{
		perror("dl_fldigi: download pthread_create");
		flock(fileno(file), LOCK_UN);
		fclose(file);
		return;
	}

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: created a thread to perform the download, returning now\n");
	#endif
}

void *dl_fldigi_download_thread(void *thread_argument)
{
	struct dl_fldigi_download_threadinfo *t;
	t = (struct dl_fldigi_download_threadinfo *) thread_argument;
	CURLcode result;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: (thread %li) performing download...\n");
	#endif

	result = curl_easy_perform(t->curl);

	curl_easy_cleanup(t->curl);
	flock(fileno(t->file), LOCK_UN);
	fclose(t->file);
	free(t);

	if (result == 0)
	{
		#ifdef DL_FLDIGI_DEBUG
			fprintf(stderr, "dl_fldigi: (thread %li) curl result (%i) Success!\n", pthread_self(), result);
		#endif

		/* ask qrunner to deal with this */
		REQ(dl_fldigi_update_payloads);
	}
	else
	{
		#ifdef DL_FLDIGI_DEBUG
			fprintf(stderr, "dl_fldigi: (thread %li) curl result (%i) %s\n", pthread_self(), result, curl_easy_strerror(result));	
		#endif
	}

	pthread_exit(0);
}

void dl_fldigi_update_payloads()
{
	FILE *file;
	int r1;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: (thread %li) attempting to update UI...\n");
	#endif

	file = fopen(dl_fldigi_cache_file, "r");

	if (file == NULL)
	{
		perror("dl_fldigi: fopen cache file (read)");
		return;
	}

	r1 = flock(fileno(file), LOCK_SH | LOG_NB);

	if (r1 == EWOULDBLOCK)
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
		fprintf(stderr, "dl_fldigi: updating UI...");
	#endif

	/* Do the stuff */

	flock(fileno(file), LOCK_UN | LOG_NB);
	fclose(file);
}
