/* writer.c
 *
 * By: Alfonso Arbona Gimeno <nake@nakerium.com>
 * Copyright: Public Domain
 * 2023-09-21
 *
 * Linux System Programming
 * Week 1
 * Assignment 2.3
 */

#include <stdio.h>
#include <errno.h>
#include <syslog.h>

int main(int argc, char** argv)
{
	FILE* f;
	char* writefile;
	char* writestr;

	openlog(NULL, 0, LOG_USER); // openlog() is optional according to the man page, but good to use to set defaults
	// closelog() is also optional according to the man page, so I do not use it

	if (argc != 3)
	{
		syslog(LOG_ERR, "Invalid number of arguments: %d", argc);
		return 1;
	}

	writefile = argv[1];
	writestr = argv[2];

	f = fopen(writefile, "w");

	if (!f)
	{
		syslog(LOG_ERR, "Could not open the file: %m");
		return 1;
	}

	int bytes_written = fprintf(f, "%s", writestr);
	if (bytes_written < 0)
	{
		syslog(LOG_ERR, "Error writing the data to the file");
		return 1;
	}

	fclose(f);

	syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

	return 0;
}
