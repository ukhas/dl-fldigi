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
#include "confdialog.h"
#include "fl_digi.h"
#include "main.h"

#include "irrXML.h"

using namespace std;
using namespace irr; // irrXML is located 
using namespace io;  // in the namespace irr::io

#define DL_FLDIGI_DEBUG

int dl_fldigi_initialised = 0;

struct dl_fldigi_threadinfo
{
	CURL *curl;
	char *post_data;
};

void *dl_fldigi_thread(void *thread_argument);

void dl_fldigi_init()
{
	CURLcode r;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: dl_fldigi_init() was executed in thread %li\n", pthread_self());
	#endif

	/* The only thread-unsafe step of dl_fldigi. Needs to be run once, at the start, when there are no other threads. */
	r = curl_global_init(CURL_GLOBAL_ALL);

	if (r != 0)
	{
		fprintf(stderr, "dl_fldigi: curl_global_init failed: (%i) %s\n", r, curl_easy_strerror(r));

		/* The only scenario in which we exit. */
		exit(EXIT_FAILURE);
	}

	dl_fldigi_initialised = 1;
	full_memory_barrier();
}

void dl_fldigi_post(const char *data, const char *identity)
{
	char *data_safe, *identity_safe, *post_data;
	size_t i, data_length, identity_length, post_data_length;
	struct dl_fldigi_threadinfo *t;
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
			fprintf(stdout, "dl_fldigi: preparing to post '%s'\n", post_data);
		#endif
	}
	else
	{
		#ifdef DL_FLDIGI_DEBUG
			fprintf(stdout, "dl_fldigi: (offline mode) would have posted '%s'\n", post_data);
		#endif

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

	t = (struct dl_fldigi_threadinfo *) malloc(sizeof(struct dl_fldigi_threadinfo));

	if (t == NULL)
	{
		fprintf(stderr, "dl_fldigi: denied %zi bytes of RAM for 'struct dl_fldigi_threadinfo'\n", sizeof(struct dl_fldigi_threadinfo));
		curl_easy_cleanup(curl);
		return;
	}

	t->curl = curl;
	t->post_data = post_data;

	/* !! */
	full_memory_barrier();

	/* the 4th argument passes the thread the information it needs */
	if (pthread_create(&thread, NULL, dl_fldigi_thread, (void *) t) != 0)
	{
		perror("pthread_create");
		curl_easy_cleanup(curl);
		return;
	}

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stderr, "dl_fldigi: created a thread to finish the posting, returning now\n");
	#endif
}

void *dl_fldigi_thread(void *thread_argument)
{
	struct dl_fldigi_threadinfo *t;
	t = (struct dl_fldigi_threadinfo *) thread_argument;
	CURLcode result;

	#ifdef DL_FLDIGI_DEBUG
		fprintf(stdout, "dl_fldigi: (thread %li) posting '%s'\n", pthread_self(), t->post_data);
	#endif

	result = curl_easy_perform(t->curl);

	#ifdef DL_FLDIGI_DEBUG
		if (result == 0)
		{
			fprintf(stdout, "dl_fldigi: (thread %li) curl result (%i) Success!\n", pthread_self(), result);
		}
		else
		{
			fprintf(stdout, "dl_fldigi: (thread %li) curl result (%i) %s\n", pthread_self(), result, curl_easy_strerror(result));	
		}
	#endif

	curl_easy_cleanup(t->curl);
	free(t->post_data);
	free(t);

	pthread_exit(0);
}

void dl_selFlightXML(Fl_Choice* o, void*) {
	progdefaults.flight_sel = o->text();
	progdefaults.flight_sel_num = o->value();
#if !defined(__CYGWIN__)
	cout << progdefaults.flight_sel.c_str() << endl;
#endif
	CURL *curl;
	CURLcode res;
	FILE * xmlFile;
	string server_address = "http://www.robertharrison.org/listen/";
	string xml_file, xml_file_dir;
  	curl = curl_easy_init();
	if(curl) {
		//Also in here we need to add a function to check that we have the most recent version
		xml_file = progdefaults.flight_sel;
		xml_file.append(".xml");
		//make string of directory and file
		xml_file_dir = FlightXMLDir;
		xml_file_dir.append(xml_file);
#if !defined(__CYGWIN__)
		cout << xml_file_dir << endl;
#endif
		//make string of server address and file
		server_address.append(xml_file);
#if !defined(__CYGWIN__)
		cout << server_address << endl;
#endif
		xmlFile = fopen (xml_file_dir.c_str(),"w");
		curl_easy_setopt(curl, CURLOPT_URL, server_address.c_str());
		curl_easy_setopt(curl , CURLOPT_WRITEDATA , xmlFile );
		res = curl_easy_perform(curl);
		//always cleanup
		fclose(xmlFile);
		curl_easy_cleanup(curl);
	}
	IrrXMLReader* xml = createIrrXMLReader(xml_file_dir.c_str());
	// strings for storing the data we want to get out of the file
	string sentence_delimiter;
	string field_delimiter;
	string fields;
	string callsign;
	string xmldata;
	string xmlfielddata;
	string seqnumber;
	
	while(xml && xml->read())
	{
		if (!strcmp("sentence_delimiter", xml->getNodeName())) {
			xml->read();
			sentence_delimiter = xml->getNodeData();
			progdefaults.xmlSentence_delimiter = sentence_delimiter;
			//telemSentence_delimiter->value(progdefaults.xmlSentence_delimiter.c_str());
			xml->read();
		}
		else if (!strcmp("field_delimiter", xml->getNodeName())) {
			xml->read();
			field_delimiter = xml->getNodeData();
			progdefaults.xmlField_delimiter = field_delimiter;
			//telemField_delimiter->value(progdefaults.xmlField_delimiter.c_str());
			xml->read();
		}
		else if (!strcmp("fields", xml->getNodeName())) {
			xml->read();
			fields = xml->getNodeData();
			progdefaults.xmlFields = fields;
			//telemFields->value(progdefaults.xmlFields.c_str());
			xml->read();
		}
		else if (!strcmp("callsign", xml->getNodeName())) {
			xml->read();
			callsign = xml->getNodeData();
			if (callsign != "dbfield") {
				progdefaults.xmlCallsign = callsign;
			}
			xml->read();
		}
		else if (!strcmp("shift", xml->getNodeName())) {
			xml->read();
			fields = xml->getNodeData();
			 int shift_int= atoi(fields.c_str());
			if (shift_int == 170) {
				progdefaults.rtty_shift = 4;
				}
			else if (shift_int == 350) {
				progdefaults.rtty_shift = 7;
				}
			else if (shift_int == 425) {
				progdefaults.rtty_shift = 8;
				}
			selShift->value(progdefaults.rtty_shift);
			resetRTTY();
			xml->read();
		}
		else if (!strcmp("baud", xml->getNodeName())) {
			xml->read();
			fields = xml->getNodeData();
			int baud_int = atoi(fields.c_str());
			if (baud_int == 45) {
				progdefaults.rtty_baud = 0;
				}
			else if (baud_int == 50) {
				progdefaults.rtty_baud = 2;
				}
			else if (baud_int == 100) {
				progdefaults.rtty_baud = 5;
				}
			else if (baud_int == 150) {
				progdefaults.rtty_baud = 7;
				}
			else if (baud_int == 200) {
				progdefaults.rtty_baud = 8;
				}
			else if (baud_int == 300) {
				progdefaults.rtty_baud = 9;
				}
			selBaud->value(progdefaults.rtty_baud);
			resetRTTY();
			xml->read();
		}
		else if (!strcmp("coding", xml->getNodeName())) {
			xml->read();
			fields = xml->getNodeData();
			// "5 (baudot)|7 (ascii)|8 (ascii)";
			if (fields == "baudot") {
				progdefaults.rtty_bits = 0;
				}
			else if (fields == "ascii-7") {
				progdefaults.rtty_bits = 1;
				}
			else if (fields == "ascii-8") {
				progdefaults.rtty_bits = 2;
				}
			selBits->value(progdefaults.rtty_bits);
			resetRTTY();
			xml->read();
		}
		}
#if !defined(__CYGWIN__)
	cout << "Done" << endl;
#endif
	// delete the xml parser after usage
	delete xml;
	progdefaults.changed = true;
}
 // This is the writer call back function used by curl  
 static int writer(char *data, size_t size, size_t nmemb, std::string *buffer)  
 {  
   // What we will return  
   int result = 0;  
   
   // Is there anything in the buffer?  
   if (buffer != NULL)  
   {  
     // Append the data to the buffer  
     buffer->append(data, size * nmemb);  
   
     // How much did we write?  
     result = size * nmemb;  
   }  
   
   return result;  
 } 

void dl_xmlList() {
	CURL *curl;
	CURLcode res;
	string buffer;
	int i=0;
	curl = curl_easy_init();
	if(curl) {
		//Also in here we need to add a function to check that we have the most recent version
		curl_easy_setopt(curl, CURLOPT_URL, "http://www.robertharrison.org/listen/payload.php");
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writer);  
		curl_easy_setopt(curl , CURLOPT_WRITEDATA , &buffer );
		res = curl_easy_perform(curl);
		//
		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	//Remove \n and add | (needed for GUI selection
	for(i = buffer.find("\n", 0); i != string::npos; i = buffer.find("\n", i))
	{
    i++;  // Move past the last discovered instance to avoid finding same
          // string
	buffer.erase(i-1, 1);
	buffer.insert(i-1, "|");
	}
	progdefaults.flightsAvaliable = buffer;
	//cout << buffer << endl; // Print out flightsAvailable string

	//bool have_config = progdefaults.readDefaultsXML();
}
