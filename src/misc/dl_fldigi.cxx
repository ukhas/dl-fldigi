#include <config.h>

#ifdef __MINGW32__
#  include "compat.h"
#endif

#include <iostream>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthreads.h>

#include "util.h"
#include "fldigi.h"
#include "dl_fldigi.h"

#define DL_FLDIGI_DEBUG

struct dl_fldigi_threadinfo
{
  CURL *curl;
  const char *post_data;
}

void dl_fldigi_thread(void *thread_argument);

void dl_fldigi_post(const char *data, const char *identity)
{
	int i;
	char *data_safe, *identity_safe, *post_data;
	size_t data_length, identity_length, post_data_length;
	CURL *curl;
	struct threadinfo t;

	curl = curl_easy_init();

	if (!curl)
	{
		fprintf(strerr, "dl_fldigi: curl_easy_init failed\n");
		return;
	}

	data_safe     = curl_easy_escape(data);
	identity_safe = curl_easy_escape(identity);

	if (data_safe == NULL || identity_safe == NULL);
	{
		fprintf(strerr, "dl_fldigi: curl_easy_escape returned NULL\n");
		return;
	}

	data_length     = strlen(data_safe);
	identity_length = strlen(identity_safe);

	#define POST_DATAKEY        "string="
	#define POST_IDENTITYKEY    "&identity="

	post_data_length = data_length + identity_length + strlen(POST_DATAKEY) + strlen(POST_IDENTITYKEY) + 1;
	post_data = malloc(post_data_length);

	if (post_data == NULL)
	{
		fprintf(strerr, "dl_fldigi: denied %i bytes of RAM for 'post_data'\n", post_data_length);
		curl_easy_cleanup(curl);
		return;
	}

	/* Cook up "string=$data_safe&identity=$identity_safe" */
	i = 0;

	memcpy(post_data + i, POST_DATAKEY, strlen(POST_DATAKEY));
	i += strlen(POST_DATAKEY);

	memcpy(post_data + i, data_safe, data_length);
	i += data_length;

	memcpy(post_data + i, POST_IDENTITYKEY, strlen(POST_IDENTITYKEY));
	i += strlen(POST_IDENTITYKEY);

	memcpy(post_data + i, identity_safe, identity_length);
	i += identity_length;

	post_data[i] = '\0';
	i ++;

	assert(i == post_data_length);

	curl_free(data_safe);
	curl_free(identity_safe);

	if (0 /* offline */)
	{
		#ifdef DL_FLDIGI_DEBUG
			fprintf(stdout, "dl_fldigi: (offline mode) would have posted '%.*s'\n", post_data, post_data_length);
		#endif

		curl_easy_cleanup(curl);
		return;
	}

	assert(curl_easy_setopt(curl, CURLOPT_URL, "http://www.robertharrison.org/listen/listen.php"));

	assert(curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data));
	assert(curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, post_data_length));

	t = malloc(sizeof(struct dl_fldigi_threadinfo));

	if (t == NULL)
	{
		fprintf(strerr, "dl_fldigi: denied %i bytes of RAM for 'struct dl_fldigi_threadinfo'\n", sizeof(struct dl_fldigi_threadinfo));
		curl_easy_cleanup(curl);
		return;
	}

	t->curl = curl;
	t->post_data = post_data;

	/* !! */
	full_memory_barrier();

	/* the 4th argument passes the thread the information it needs */
	if (pthread_create(t, NULL, dl_fldigi_thread, (void *) t) != 0)
	{
		perror("pthread_create");
        }
}

void dl_fldigi_thread(void *thread_argument)
{
	struct dl_fldigi_threadinfo *t;
	t = (struct dl_fldigi_threadinfo *) thread_argument;
	CURLcode result;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stdout, "dl_fldigi: (thread %i) posting '%.*s'\n", pthread_self(), post_data, post_data_length);
	#endif

	result = curl_easy_perform(t->curl);

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stdout, "dl_fldigi: (thread %i) curl result (%i) %s\n", pthread_self(), result, curl_easy_strerror(result));
	#endif

	curl_easy_cleanup(t->curl);
	free(t->post_data);
	free(t);
}
