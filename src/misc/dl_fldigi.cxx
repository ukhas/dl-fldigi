#include "dl_fldigi.h"
#include <iostream>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>

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

void dlServerCommunicator()
{
	if (pipe(dl_fldigi_pfds) == -1) { perror("pipe"); exit(EXIT_FAILURE); }

	dl_fldigi_cpid = fork();

	if (dl_fldigi_cpid == -1) { perror("fork"); exit(EXIT_FAILURE); }
	if (dl_fldigi_cpid == 0) {

		char c;
		char buffer [2000];
		int charpos = 0;

		CURL *easyhandle_status;
		CURLcode result;

		// ofstream fout;

		// fout.clear();
		// fout.open ("log.txt");
		// if (fout.fail()) {
		// 	cout << "Failed to open log.txt\n";
		// }
	
		/* Close the write side of the pipe */
		close(dl_fldigi_pfds[1]);

		buffer[0] ='\0';

		while (read(dl_fldigi_pfds[0], &c, 1) > 0)
		{
			buffer[charpos] = c;
			if (charpos <1999) charpos ++;

			if (c == '\n')
			{

				if (charpos > 0)
					buffer[charpos-1] = '\0';
				else
					buffer[0] = '\0';

				charpos=0;

				printf(" CHILD: received \"%s\"\n", buffer);

				// fout << "Buffer string : '" << buffer << "'" << endl;

				if (strlen(buffer) > 0)
				{
					easyhandle_status = curl_easy_init();
					if(easyhandle_status) {
						curl_easy_setopt(easyhandle_status, CURLOPT_POSTFIELDS, buffer);
						curl_easy_setopt(easyhandle_status, CURLOPT_URL, "http://www.robertharrison.org/listen/listen.php");
						result = curl_easy_perform(easyhandle_status); /* post away! */
						std::cout << "result: " << result  << "\n";
						curl_easy_cleanup(easyhandle_status);
					}
				}
				buffer[0] = '\0';
			}
		}
		close(dl_fldigi_pfds[0]);
		_exit (0);
        } 
	
	/* Close the read side of the pipe */
	close(dl_fldigi_pfds[0]);
}
