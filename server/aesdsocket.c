#include <stdio.h>
#include <errno.h>
#include <sys/syslog.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>

/*
    WELCOME TO THE SPAGETTINI CODE
	This is what happens when I know the code will be never used in the future
	By Alfonso Arbona Gimeno

	- OK Bind to 9000. Return -1 on error
	- Fork if -d is passed
	- OK Listen an accept A connection
	- OK Syslog: "Accepted connection from <ip>"
	- OK Append data to /var/tmp/aesdsocketdata (create if needed)
	- OK Packet ends in \n (included in the file)
	- OK Data does not contain \0
	- OK Length of a packet < heap (aka use malloc)
	- OK Return all /var/tmp/aesdsocketdata after the \n
		- OK Read and alloc one line at a time
	- OK Log "Closed connection from <ip>"
	- OK Restart accepting connections
	- OK Exit on SIGINT or SIGTERM
		- OK Close everything
		- OK Delete /var/tmp/aesdsocketdata
*/

bool running = true;
const char* SERVICE = "9000"; // aka port number as a string
const size_t RCV_BUFFER_LENGTH = 1; // I need to see char by char to check for \n
const char* TEMPFILE = "/var/tmp/aesdsocketdata";

static
void signal_handler(int signum, siginfo_t *info, void *ucontext)
{
	running = false;
}

void send_file_to_socket(int outsock, FILE* infile)
{
	ssize_t length;
	char* buffer = NULL;
	size_t allocsize = 0;
	
	rewind(infile);
	
	while((length = getline(&buffer, &allocsize, infile)) > 0)
	{
		send(outsock, buffer, length, 0);
		free(buffer);
		buffer = NULL;
		allocsize = 0;
	}

	free(buffer); // According to the man page, must be freed even on error

	fseek(infile, 0, SEEK_END);
}

int main(int argc, char** argv)
{
	bool should_fork = false;
	struct addrinfo hints;
	struct addrinfo *servinfo;
	struct sockaddr_storage cliinfo;
	socklen_t cliinfo_size = sizeof(cliinfo);
	struct sigaction sa;
	int retval;
	int srvsock;
	int clisock;
	char buffer[RCV_BUFFER_LENGTH];
	char* recv_data = NULL;
	size_t recv_data_len = 0;
	ssize_t bytes_recv;
	char their_ip[] = "255.255.255.255";
	FILE* tmpfile;

	openlog(NULL, 0, LOG_USER);
	syslog(LOG_DEBUG, "Starting aesdsocket server");

	if (argc >= 2 && strcmp(argv[1], "-d") == 0)
	{
		should_fork = true;
	}

	syslog(LOG_DEBUG, "Set up server");

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; // AF_INET or AF_INET6 or AF_UNSPEC
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	retval = getaddrinfo(NULL, SERVICE, &hints, &servinfo);
	if (retval != 0)
	{
		syslog(LOG_ERR, "getaddrinf error: %s", gai_strerror(retval));
		return -1;
	}

	srvsock = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
	if (srvsock == -1)
	{
		retval = errno;
		syslog(LOG_ERR, "socket error: %s", strerror(retval));
		return -1;
	}

	retval = bind(srvsock, servinfo->ai_addr, servinfo->ai_addrlen);
	if (retval == -1)
	{
		retval = errno;
		syslog(LOG_ERR, "bind error: %s", strerror(retval));
		return -1;
	}

	if (should_fork)
	{
		syslog(LOG_DEBUG, "Forking");
		pid_t pid = fork();

		if (pid != 0) // We are the parent
		{
			close(srvsock);
			freeaddrinfo(servinfo);
			return (pid > 0) ? 0 : -1;
		}
	}

	syslog(LOG_DEBUG, "Set up signals");
	
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = signal_handler;

	if (sigaction(SIGINT, &sa, NULL) == -1)
	{
		syslog(LOG_ERR, "sigaction(SIGINT) failed");
		return -1;
	}

	if (sigaction(SIGTERM, &sa, NULL) == -1)
	{
		syslog(LOG_ERR, "sigaction(SIGTERM) failed");
		return -1;
	}

	syslog(LOG_DEBUG, "Opening the temp file");
	tmpfile = fopen(TEMPFILE, "w+");
	if (!tmpfile)
	{
		retval = errno;
		syslog(LOG_ERR, "Error opening the temp file: %s", strerror(retval));
		return -1;
	}

	syslog(LOG_DEBUG, "Listening for connections");
	retval = listen(srvsock, 1);
	if (retval == -1)
	{
		retval = errno;
		syslog(LOG_ERR, "listen error: %s", strerror(retval));
		return -1;
	}

	while (running)
	{
		if (recv_data)
		{
			free(recv_data);
			recv_data = NULL;
			recv_data_len = 0;
		}

		syslog(LOG_DEBUG, "Waiting for connections");
		//printf("Waiting for connections\n");
		clisock = accept(srvsock, (struct sockaddr*) &cliinfo, &cliinfo_size);
		if (clisock == -1)
		{
			retval = errno;
			syslog(LOG_ERR, "accept error: %s", strerror(retval));
			continue;
		}

		retval = getnameinfo((struct sockaddr*) &cliinfo, cliinfo_size, their_ip, sizeof(their_ip), NULL, 0i, NI_NUMERICHOST);
		if (retval != 0)
		{
			syslog(LOG_ERR, "getnameinfo error: %s", gai_strerror(retval));
			close(clisock);
			continue;
		}

		syslog(LOG_INFO, "Accepted connection from %s", their_ip);
		//printf("Accepted connection from %s\n", their_ip);

		while (running)
		{
			bytes_recv = recv(clisock, (void*) buffer, RCV_BUFFER_LENGTH, 0);
			if (bytes_recv < 0)
			{
				retval = errno;
				syslog(LOG_ERR, "recv error: %s", strerror(retval));
				close(clisock);
				break;
			}
			else if (bytes_recv == 0)
			{
				syslog(LOG_INFO, "Closed connection from %s", their_ip);
				//printf("DISCONNECTED\n");
				close(clisock);
				break;
			}
			else
			{
				// I should optimize this to allocate only every so often but... meh
				recv_data = (char*)realloc((void*) recv_data, recv_data_len + bytes_recv +1);
				if (!recv_data)
				{
					syslog(LOG_ERR, "Failed to allocate %ld bytes", recv_data_len+bytes_recv+1);
					close(clisock);
					running = false;
					break;
				}

				memcpy(recv_data + recv_data_len, buffer, bytes_recv);
				recv_data_len += bytes_recv;
				recv_data[recv_data_len] = '\0'; // Note that I allocated 1 extra byte for this
				
				//printf("%2ld: %c\n", recv_data_len-1, buffer[0]);
				
				// WARNING: I should check all bytes but I know that the buffer is only 1 byte so I hardcoded checking only the last byte... This is not correct if bytes_recv > 1 but... meh
				if (recv_data[recv_data_len-1] == '\n')
				{
					if (fwrite(recv_data, 1, recv_data_len, tmpfile) != recv_data_len)
					{
						retval = errno;
						syslog(LOG_ERR, "Error writing to the tmpfile: %s", strerror(retval));
					}
					//printf("\nGot: %s", recv_data);
					fflush(stdout);

					free(recv_data);
					recv_data = NULL;
					recv_data_len = 0;

					send_file_to_socket(clisock, tmpfile);
				}
			}
		}
	}

	syslog(LOG_DEBUG, "Closing");
	fclose(tmpfile);
	remove(TEMPFILE);
	close(srvsock);
	freeaddrinfo(servinfo);


	return 0;

 }

