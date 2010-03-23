#include "dl_fldigi.h"
#include <iostream>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>

bool offline = false;

void dlServerCommunicator()
{
	if (pipe(dl_fldigi_pfds) == -1) { perror("pipe"); exit(EXIT_FAILURE); }

	dl_fldigi_cpid = fork();

	if (dl_fldigi_cpid == -1) { perror("fork"); exit(EXIT_FAILURE); }

	if (dl_fldigi_cpid == 0) // Test to see if this is the child process
	{

		/* 
		    Code to be excuted by child process
                */

		char c;
		char buffer [2000];
		int charpos = 0;

		/* Initialise the pipe */
		buffer[0] ='\0';

		close(dl_fldigi_pfds[1]); // Close the write side of the pipe 

		CURL *easyhandle_status;
		CURLcode result;

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

				if (strlen(buffer) > 0) // Only post data if there is some
				{
					easyhandle_status = curl_easy_init();

					if(easyhandle_status) 
					{
						curl_easy_setopt(easyhandle_status, CURLOPT_POSTFIELDS, buffer);
						curl_easy_setopt(easyhandle_status, CURLOPT_URL, "http://www.robertharrison.org/listen/listen.php");
						if (!offline) {
							result = curl_easy_perform(easyhandle_status); /* post away! */
							std::cout << "result: " << result  << "\n";
						} else {
							std::cout << "        Post inhibited due to --offline mode\n";
						}
						curl_easy_cleanup(easyhandle_status);
					}

				}
				buffer[0] = '\0';
			}
		}
		close(dl_fldigi_pfds[0]);
		_exit (0);
        } 
	
	/* 
	    Code to be excuted by parent process
        */
	close(dl_fldigi_pfds[0]); // Close the read side of the pipe 

}
