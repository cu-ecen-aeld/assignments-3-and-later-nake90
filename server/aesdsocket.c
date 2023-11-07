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
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include "queue.h" // Using FreeBSD queue instead of sys/queue.h

/*
    WELCOME TO THE SPAGETTINI CODE
	This is what happens when I know the code will be never used in the future
	By Alfonso Arbona Gimeno

	- OK Bind to 9000. Return -1 on error
	- OK Fork if -d is passed
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

	New on assignment 6-1
	- OK Each new connection should spawn a thread
	- OK Writes to /var/tmp/aesdsocketdata protected with a mutex (on a packet level, not connection)
	- OK Thread exit on disconnection from client or error on send/recv
	- OK Handle also SIGINT or SIGTERM correctly
	- OK Handle threads in a linked list or similar
	- OK Use pthread_join
	- OK Append to /var/tmp/aesdsocketdata every 10s "timestamp:time" with time = strftime, format "%a, %d %b %Y %T %z\n"
*/

atomic_bool running; // Single thread write, multiple thread read so just atomicity is needed
const char* SERVICE = "9000"; // aka port number as a string
const size_t RCV_BUFFER_LENGTH = 1; // I need to see char by char to check for \n
const char* TEMPFILE = "/var/tmp/aesdsocketdata";
const unsigned int SECONDS_BETWEEN_TIMESTAMPS = 10;
typedef STAILQ_HEAD(head_s, s_thread_node) queue_thread_t;

struct s_thread_data
{
	pthread_mutex_t* mutex_thread;
	bool thread_completed; // Protected by mutex_thread

	int clisock; // Socket of our client (not mutex protected, write before thread creation)
	int retval; // Return value (not mutex protected, read only after join)
	char their_ip[46]; // Not protected, set before mutex creation (https://stackoverflow.com/questions/1076714/max-length-for-client-ip-address)
	
	pthread_mutex_t* mutex_data;
	FILE* tmpfile; // Protected by mutex_data
};

struct s_thread_node
{
	pthread_t thread_id;
	struct s_thread_data thread_data;
	STAILQ_ENTRY(s_thread_node) nodes;
};

// Will block until all have terminated if wait_all_terminate is true
void delete_finished_threads(queue_thread_t* queue, bool wait_all_terminate)
{
	struct s_thread_node* node = NULL;
	struct s_thread_node* next = NULL;

	STAILQ_FOREACH_SAFE(node, queue, nodes, next)
	{
		bool completed;

		pthread_mutex_lock(node->thread_data.mutex_thread);
		completed = node->thread_data.thread_completed;
		pthread_mutex_unlock(node->thread_data.mutex_thread);

		// If the thread has finished, or we force to join all
		if (wait_all_terminate || completed)
		{
			// First, join (this might block)
			int retval = pthread_join(node->thread_id, NULL);
			if (retval != 0)
			{
				syslog(LOG_ERR, "pthread_join() failed with error code %d", retval);
			}
			if (node->thread_data.retval != 0)
			{
				syslog(LOG_ERR, "The thread for %s finished with error code %d",
				       node->thread_data.their_ip, node->thread_data.retval);
			}
			pthread_mutex_destroy(node->thread_data.mutex_thread);
			free(node->thread_data.mutex_thread);

			STAILQ_REMOVE(queue, node, s_thread_node, nodes);
			free(node);
			node = NULL;
		}
	}
}

static
void signal_handler(int signum, siginfo_t *info, void *ucontext)
{
	atomic_store(&running, false);
}

void send_file_to_socket(int outsock, FILE* infile) // LOCK MUTEX BEFORE CALLING THIS
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

	fseek(infile, 0, SEEK_END);

	free(buffer); // According to the man page, must be freed even on error
}

void* timer_thread_function(void* thread_data)
{
	unsigned int remaining_seconds = 0;
	size_t length_of_timestamp;
	char timestamp_buffer[64];

	if (!thread_data)
	{
		syslog(LOG_ERR, "Thread received null parameter data");
		return NULL;
	}

	struct s_thread_data* data = (struct s_thread_data*)thread_data;
	data->retval = 0;

	// We get out of this loop if running is false (quit requested), or on break (error or client quit)
	while (atomic_load(&running))
	{
		remaining_seconds = sleep(SECONDS_BETWEEN_TIMESTAMPS - remaining_seconds);

		const time_t now = time(NULL);
		const struct tm* tp = localtime(&now);

		length_of_timestamp = strftime(timestamp_buffer, sizeof(timestamp_buffer), "timestamp:%a, %d %b %Y %T %z\n", tp);
		
		pthread_mutex_lock(data->mutex_data);
		if (fwrite(timestamp_buffer, 1, length_of_timestamp, data->tmpfile) != length_of_timestamp)
		{
			pthread_mutex_unlock(data->mutex_data);
			data->retval = errno;
			syslog(LOG_ERR, "Error writing to the tmpfile: %s", strerror(data->retval));
			break;
		}

		pthread_mutex_unlock(data->mutex_data);
	}

	pthread_mutex_lock(data->mutex_thread);
	data->thread_completed = true;
	pthread_mutex_unlock(data->mutex_thread);

	return thread_data;
}

void* thread_function(void* thread_data)
{
	char buffer[RCV_BUFFER_LENGTH];
	char* recv_data = NULL;
	size_t recv_data_len = 0;
	ssize_t bytes_recv;

	if (!thread_data)
	{
		syslog(LOG_ERR, "Thread received null parameter data");
		return NULL;
	}

	struct s_thread_data* data = (struct s_thread_data*)thread_data;

	data->retval = 0; // Default
	// thread_completed is already set to false for us in the main thread

	// We get out of this loop if running is false (quit requested), or on break (error or client quit)
	while (atomic_load(&running))
	{
		bytes_recv = recv(data->clisock, (void*) buffer, RCV_BUFFER_LENGTH, 0);
		if (bytes_recv < 0)
		{
			data->retval = errno;
			syslog(LOG_ERR, "recv error: %s", strerror(data->retval));
			close(data->clisock);
			break;
		}
		else if (bytes_recv == 0)
		{
			syslog(LOG_INFO, "Closed connection from %s", data->their_ip);
			//printf("DISCONNECTED\n");
			close(data->clisock);
			break;
		}
		else
		{
			// I should optimize this to allocate only every so often but... meh
			recv_data = (char*)realloc((void*) recv_data, recv_data_len + bytes_recv +1);
			if (!recv_data)
			{
				data->retval = ENOMEM;
				syslog(LOG_ERR, "Failed to allocate %ld bytes", recv_data_len+bytes_recv+1);
				close(data->clisock);
				break;
			}

			memcpy(recv_data + recv_data_len, buffer, bytes_recv);
			recv_data_len += bytes_recv;
			recv_data[recv_data_len] = '\0'; // Note that I allocated 1 extra byte for this
			
			//printf("%2ld: %c\n", recv_data_len-1, buffer[0]);
			
			// WARNING: I should check all bytes but I know that the buffer is only 1 byte so I hardcoded checking only the last byte... This is not correct if bytes_recv > 1 but... meh
			if (recv_data[recv_data_len-1] == '\n')
			{
				// Full packet received so we lock the file and write to it. Then send it and unlock
				pthread_mutex_lock(data->mutex_data);
				if (fwrite(recv_data, 1, recv_data_len, data->tmpfile) != recv_data_len)
				{
					pthread_mutex_unlock(data->mutex_data);
					data->retval = errno;
					syslog(LOG_ERR, "Error writing to the tmpfile: %s", strerror(data->retval));
					break;
				}

				send_file_to_socket(data->clisock, data->tmpfile);
				pthread_mutex_unlock(data->mutex_data);

				//printf("\nGot: %s", recv_data);
				//fflush(stdout);

				free(recv_data);
				recv_data = NULL;
				recv_data_len = 0;
			}
		}
	}

	if (recv_data)
	{
		free(recv_data);
	}

	pthread_mutex_lock(data->mutex_thread);
	data->thread_completed = true;
	pthread_mutex_unlock(data->mutex_thread);

	return data;
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
	FILE* tmpfile;
	pthread_mutex_t mutex_data = PTHREAD_MUTEX_INITIALIZER;
	queue_thread_t thread_queue;

	STAILQ_INIT(&thread_queue);
	atomic_init(&running, true);

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
		pthread_mutex_destroy(&mutex_data);
		return -1;
	}

	srvsock = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
	if (srvsock == -1)
	{
		retval = errno;
		syslog(LOG_ERR, "socket error: %s", strerror(retval));
		pthread_mutex_destroy(&mutex_data);
		freeaddrinfo(servinfo);
		return -1;
	}
	
	int yes = 1;
	if (setsockopt(srvsock, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(int)) == -1)
	{
	    retval = errno;
		syslog(LOG_ERR, "setsockopt error: %s", strerror(retval));
		pthread_mutex_destroy(&mutex_data);
		freeaddrinfo(servinfo);
		return -1;
	}

	retval = bind(srvsock, servinfo->ai_addr, servinfo->ai_addrlen);
	if (retval == -1)
	{
		retval = errno;
		syslog(LOG_ERR, "bind error: %s", strerror(retval));
		pthread_mutex_destroy(&mutex_data);
		freeaddrinfo(servinfo);
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
			pthread_mutex_destroy(&mutex_data);
			return (pid > 0) ? 0 : -1;
		}
	}

	syslog(LOG_DEBUG, "Set up signals");
	
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = signal_handler;

	if (sigaction(SIGINT, &sa, NULL) == -1)
	{
		close(srvsock);
		freeaddrinfo(servinfo);
		syslog(LOG_ERR, "sigaction(SIGINT) failed");
	    pthread_mutex_destroy(&mutex_data);
		return -1;
	}

	if (sigaction(SIGTERM, &sa, NULL) == -1)
	{
		close(srvsock);
		freeaddrinfo(servinfo);
		syslog(LOG_ERR, "sigaction(SIGTERM) failed");
	    pthread_mutex_destroy(&mutex_data);
		return -1;
	}

	syslog(LOG_DEBUG, "Opening the temp file");
	tmpfile = fopen(TEMPFILE, "w+");
	if (!tmpfile)
	{
		retval = errno;
		close(srvsock);
		freeaddrinfo(servinfo);
		syslog(LOG_ERR, "Error opening the temp file: %s", strerror(retval));
	    pthread_mutex_destroy(&mutex_data);
		return -1;
	}

	syslog(LOG_DEBUG, "Listening for connections");
	retval = listen(srvsock, 1);
	if (retval == -1)
	{
		retval = errno;
		close(srvsock);
		freeaddrinfo(servinfo);
		syslog(LOG_ERR, "listen error: %s", strerror(retval));
	    pthread_mutex_destroy(&mutex_data);
		return -1;
	}

	// Create the timer thread
	struct s_thread_node* node = malloc(sizeof(struct s_thread_node));
	if (!node)
	{
		retval = ENOMEM;
		close(srvsock);
		freeaddrinfo(servinfo);
		syslog(LOG_ERR, "Could not allocate a new node");
	    pthread_mutex_destroy(&mutex_data);
		return -1;
	}
	
	node->thread_data.mutex_thread = malloc(sizeof(pthread_mutex_t));
	if (!node->thread_data.mutex_thread)
	{
		retval = ENOMEM;
		close(srvsock);
		freeaddrinfo(servinfo);
		syslog(LOG_ERR, "Could not allocate a new node's mutex");
	    pthread_mutex_destroy(&mutex_data);
		return -1;
	}
	pthread_mutex_init(node->thread_data.mutex_thread, NULL); // Always succeeds according to the manpage
	node->thread_data.thread_completed = false;
	node->thread_data.retval = 0;
	node->thread_data.mutex_data = &mutex_data;
	node->thread_data.tmpfile = tmpfile;

	retval = pthread_create(&node->thread_id, NULL, timer_thread_function, (void*)&node->thread_data);
	if (retval != 0)
	{
		syslog(LOG_ERR, "pthread_create() failed with error code %d", retval);
		pthread_mutex_destroy(node->thread_data.mutex_thread);
		free(node);
		close(srvsock);
		freeaddrinfo(servinfo);
	    pthread_mutex_destroy(&mutex_data);
		return -1;
	}

	STAILQ_INSERT_TAIL(&thread_queue, node, nodes);

	while (atomic_load(&running))
	{
		syslog(LOG_DEBUG, "Waiting for connections");
		//printf("Waiting for connections\n");
		clisock = accept(srvsock, (struct sockaddr*) &cliinfo, &cliinfo_size);
		if (clisock == -1)
		{
			retval = errno;
			syslog(LOG_ERR, "accept error: %s", strerror(retval));
			continue;
		}

		// Create a new queue item
		struct s_thread_node* node = malloc(sizeof(struct s_thread_node));
		if (!node)
		{
			retval = ENOMEM;
			syslog(LOG_ERR, "Could not allocate a new node");
			break;
		}
		
		node->thread_data.mutex_thread = malloc(sizeof(pthread_mutex_t));
		if (!node->thread_data.mutex_thread)
		{
			retval = ENOMEM;
			syslog(LOG_ERR, "Could not allocate a new node's mutex");
			break;
		}
		pthread_mutex_init(node->thread_data.mutex_thread, NULL); // Always succeeds according to the manpage
		node->thread_data.thread_completed = false;
		node->thread_data.clisock = clisock;
		node->thread_data.retval = 0;
		node->thread_data.mutex_data = &mutex_data;
		node->thread_data.tmpfile = tmpfile;

		int retval = getnameinfo((struct sockaddr*) &cliinfo, cliinfo_size, node->thread_data.their_ip,
		                         sizeof(node->thread_data.their_ip), NULL, 0i, NI_NUMERICHOST);
		if (retval != 0)
		{
			syslog(LOG_ERR, "getnameinfo error: %s", gai_strerror(retval));
			close(clisock);
			pthread_mutex_destroy(node->thread_data.mutex_thread);
			free(node);
			continue;
		}

		syslog(LOG_INFO, "Accepted connection from %s", node->thread_data.their_ip);
		//printf("Accepted connection from %s\n", their_ip);

		retval = pthread_create(&node->thread_id, NULL, thread_function, (void*)&node->thread_data);
		if (retval != 0)
		{
			syslog(LOG_ERR, "pthread_create() failed with error code %d", retval);
			close(clisock);
			pthread_mutex_destroy(node->thread_data.mutex_thread);
			free(node);
			continue;
		}

		STAILQ_INSERT_TAIL(&thread_queue, node, nodes);

		// Check if there are threads that have terminated to free their resources
		delete_finished_threads(&thread_queue, false);
	}
	atomic_store(&running, false); // To make sure all clients are notified

	delete_finished_threads(&thread_queue, true);

	syslog(LOG_DEBUG, "Closing");
	fclose(tmpfile);
	remove(TEMPFILE);
	pthread_mutex_destroy(&mutex_data);
	close(srvsock);
	freeaddrinfo(servinfo);


	return 0;

 }

