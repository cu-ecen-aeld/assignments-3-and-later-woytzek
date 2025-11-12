#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sys/queue.h>
#include "aesd_ioctl.h"

#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1
#endif

#define PORT_NUMBER "9000"
#if USE_AESD_CHAR_DEVICE == 1
	#define AESD_DATA_FILE "/dev/aesdchar"
#else
	#define AESD_DATA_FILE "/var/tmp/aesdsocketdata"
#endif
#define RECV_BUF_SIZE 1024

struct thread_data
{
	bool success;
	bool finished;
	int acceptedfd;
	char addrbuf[INET_ADDRSTRLEN];
	pthread_mutex_t *file_mutex;
};

struct slist_data_s
{
	SLIST_ENTRY(slist_data_s) entries;
	struct thread_data *tdata;
	pthread_t thread;
};
SLIST_HEAD(slisthead_t, slist_data_s) head;

atomic_bool running = ATOMIC_VAR_INIT(false);
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

static void signal_handler( int signal )
{
	if( SIGINT == signal || SIGTERM == signal )
	{
		atomic_store(&running, false);
	}
}

static int handle_message( int acceptedfd, bool *connected, pthread_mutex_t *mutex )
{
	int fd;

	/* receive message */
	char buf[RECV_BUF_SIZE] = {0};
	int rcvlen = recv( acceptedfd, buf, RECV_BUF_SIZE, 0 );
	syslog( LOG_DEBUG, "Received %d bytes on fd %d", rcvlen, acceptedfd );
	if( rcvlen == 0 )
	{
		/* disconnected */
		*connected = false;
		syslog( LOG_DEBUG, "Client disconnected on fd %d", acceptedfd );
		return 0;
	}

	bool is_ioctl = false;
#if USE_AESD_CHAR_DEVICE == 1
	unsigned int write_cmd = 0;
	unsigned int write_cmd_offset = 0;
	/* check for ioctl command: */
	/*   check if buf content equals to AESDCHAR_IOCSEEKTO:X,Y */
	if( rcvlen > 18 && strncmp( buf, "AESDCHAR_IOCSEEKTO:", 18 ) == 0 )
	{
		/* parse write_cmd and write_cmd_offset */
		int ret = sscanf( buf + 18, "%u,%u", &write_cmd, &write_cmd_offset );
		if( ret == 2 )
		{
			is_ioctl = true;
			syslog( LOG_DEBUG, "Parsed IOCTL command on fd %d: write_cmd=%u, write_cmd_offset=%u", acceptedfd, write_cmd, write_cmd_offset );
		}
	}
#endif

	/* open file to collect message */
	pthread_mutex_lock(mutex);

	/* complete message */
#if USE_AESD_CHAR_DEVICE == 1
	fd = open( AESD_DATA_FILE, O_WRONLY | O_APPEND );
#else
	fd = open( AESD_DATA_FILE, O_CREAT | O_APPEND | O_WRONLY, 0666 );
#endif
	if( fd < 0 )
	{
		pthread_mutex_unlock(mutex);
		/* error */
		syslog( LOG_ERR, "Cannot open file %s: %s", AESD_DATA_FILE, strerror( errno ));
		return -1;
	}

	if( is_ioctl )
	{
		/* perform ioctl */
		syslog( LOG_DEBUG, "Performing IOCTL on fd %d", acceptedfd );
		struct aesd_seekto seekto;
		seekto.write_cmd = write_cmd;
		seekto.write_cmd_offset = write_cmd_offset;
		int ioctlret = ioctl( fd, AESDCHAR_IOCSEEKTO, &seekto );
		if( ioctlret != 0 )
		{
			pthread_mutex_unlock(mutex);
			/* error */
			syslog( LOG_ERR, "IOCTL failed on fd %d: %s", acceptedfd, strerror( errno ));
			close(fd);
			return -1;
		}
		syslog( LOG_DEBUG, "IOCTL completed on fd %d", acceptedfd );
	}
	else
	{
		while( rcvlen > 0 )
		{
			write( fd, buf, rcvlen );
			rcvlen = recv( acceptedfd, buf, RECV_BUF_SIZE, MSG_DONTWAIT );
		}
		close(fd);
	}
	syslog( LOG_DEBUG, "Completed receiving message on fd %d", acceptedfd );

	/* ignore EAGAIN error, it indicates 'no more data' to receive */
	if( rcvlen < 0 && errno != EAGAIN )
	{
		pthread_mutex_unlock(mutex);
		/* error */
		syslog( LOG_ERR, "Receive failed: %s", strerror( errno ));
		return -1;
	}

	/* re-open file for reading */
	syslog( LOG_DEBUG, "Sending back file content on fd %d", acceptedfd );
	if( !is_ioctl )
	{
		fd = open( AESD_DATA_FILE, O_RDONLY );
	}
	int rdlen = read( fd, buf, RECV_BUF_SIZE );
	syslog( LOG_DEBUG, "Read %d bytes from file %s", rdlen, AESD_DATA_FILE );
	/* and send back file content */
	while( rdlen > 0 )
	{
		send( acceptedfd, buf, rdlen, 0 );
		syslog( LOG_DEBUG, "Sent back %d bytes on fd %d", rdlen, acceptedfd );
		rdlen = read( fd, buf, RECV_BUF_SIZE );
		syslog( LOG_DEBUG, "Read %d bytes from file %s", rdlen, AESD_DATA_FILE );
	}
	close(fd);
	syslog( LOG_DEBUG, "Sent back file content on fd %d", acceptedfd );
	pthread_mutex_unlock(mutex);

	return 0;
}

void* connection_thread( void* arg )
{
	struct thread_data *tdata = (struct thread_data *)arg;
	int acceptedfd = tdata->acceptedfd;

	syslog( LOG_INFO, "Accepted connection from %s (thread: %ld)", tdata->addrbuf, pthread_self() );

	bool connected = true;

	// while( connected )
	while( connected )
	{
		int s;
		if( (s = handle_message( acceptedfd, &connected, tdata->file_mutex )) != 0 )
		{
			/* error */
			syslog( LOG_ERR, "Error handling message: %s", strerror( errno ));
			goto cleanup;
		}
		syslog( LOG_DEBUG, "Handled message on fd %d", acceptedfd );
		if( !connected )
		{
			/* client disconnected */
			tdata->success = true;
			tdata->finished = true;
			syslog( LOG_DEBUG, "Client disconnected on fd %d", acceptedfd );
		}
	} /* end of connected */
	return (void*)tdata;

cleanup:
	tdata->success = false;
	tdata->finished = true;
	return (void*)tdata;
}

static int handle_connection( int sfd )
{
	int acceptedfd = -1;

	/* accept connection */
	struct sockaddr_in acceptedad;
	socklen_t adlen = sizeof( struct sockaddr_in );
	acceptedfd = accept( sfd, (struct sockaddr*)&acceptedad, &adlen );
	if( !atomic_load(&running) )
	{
		/* if accept() interrupted by signal, break the loop */
		syslog( LOG_DEBUG, "Not running anymore, exiting accept" );
		return 0;
	}
	if( acceptedfd < 0 )
	{
		/* error */
		syslog( LOG_ERR, "Failed on accept: %s", strerror( errno ));
		goto cleanup;
	}
	syslog( LOG_DEBUG, "Accepted connection on fd %d", acceptedfd );

	/* create thread data and single-list node */
	struct slist_data_s *node = calloc( 1, sizeof( struct slist_data_s ));
	if( node == NULL )
	{
		syslog( LOG_ERR, "Failed to allocate memory for list node" );
		goto cleanup;
	}
	node->tdata = calloc( 1, sizeof( struct thread_data ));
	if( node->tdata == NULL )
	{
		syslog( LOG_ERR, "Failed to allocate memory for thread data" );
		free( node );
		goto cleanup;
	}
	/* insert node to list */
	node->tdata->success = false;
	node->tdata->finished = false;
	node->tdata->acceptedfd = acceptedfd;
	node->tdata->file_mutex = &file_mutex;
	SLIST_INSERT_HEAD( &head, node, entries );

	/* log connection IP */
	inet_ntop(AF_INET, &acceptedad.sin_addr, node->tdata->addrbuf, sizeof(node->tdata->addrbuf));

	/* create thread */
	if( 0!= pthread_create( &(node->thread), NULL, connection_thread, (void*)(node->tdata) ))
	{
		syslog( LOG_ERR, "Failed to create thread: %s", strerror( errno ));
		free( node->tdata );
		free( node );
		goto cleanup;
	}
	syslog( LOG_DEBUG, "Created thread %ld for connection", node->thread );

	/* check all threads in single-list */
	bool node_removed;
	do
	{
		node_removed = false;
		SLIST_FOREACH( node, &head, entries )
		{
			/* if thread finished, join and remove from list */
			if( node->tdata->finished )
			{
				struct thread_data *tdata = NULL;
				pthread_join( node->thread, (void**)&tdata );
				SLIST_REMOVE( &head, node, slist_data_s, entries );
				if( tdata->success )
				{
					syslog( LOG_INFO, "Closed connection from %s (thread %ld)", tdata->addrbuf, node->thread );
				}
				close( tdata->acceptedfd );
				free( tdata );
				free( node );
				node_removed = true;
				break; /* list modified, break and start over */
			}
		}
	} while( node_removed );
	syslog( LOG_DEBUG, "Finished handling connection on fd %d", acceptedfd );

	return 0;

cleanup:
	syslog( LOG_INFO, "Closing connection and cleanup\n" );
	if( acceptedfd >= 0 )
		close( acceptedfd );
	return -1;
}

#if USE_AESD_CHAR_DEVICE != 1
static void* timestamp_thread( void* arg )
{
	syslog( LOG_DEBUG, "Timestamp thread started" );
	/* sleep 10 seconds */
	sleep(10);

	while( atomic_load(&running) )
	{
		/* get time */
		syslog( LOG_DEBUG, "Getting timestamp" );
		time_t curr_time = time( NULL );
		struct tm local_tm;
		if( NULL == localtime_r( &curr_time, &local_tm ))
		{
			syslog( LOG_ERR, "Cannot get local time: %s", strerror( errno ));
			continue;
		}
		char timestr[64];
		if( 0 == strftime( timestr, sizeof( timestr), "%Y/%m/%d %H:%M:%S", &local_tm ))
		{
			syslog( LOG_ERR, "Cannot format time string" );
			continue;
		}

		/* write to file */
		pthread_mutex_lock( &file_mutex );
		int fd = open( AESD_DATA_FILE, O_CREAT | O_APPEND | O_WRONLY, 0666 );
		if( fd < 0 )
		{
			syslog( LOG_ERR, "Cannot open file for timestamp: %s", strerror( errno ));
			pthread_mutex_unlock( &file_mutex );
			continue;
		}
		dprintf( fd, "timestamp: %s\n", timestr );
		close( fd );
		pthread_mutex_unlock( &file_mutex );

		int rem = sleep(10);
		if( !atomic_load(&running) )
		{
			syslog( LOG_DEBUG, "Timestamp thread exiting, remaining %d", rem );
			break;
		}
	}
	
	syslog( LOG_DEBUG, "Timestamp thread exiting" );
	return NULL;
}
#endif

int main( int argc, char** argv )
{
	int sfd = -1; 
	struct addrinfo *sainfo = NULL;
	bool deamon = false;
#if USE_AESD_CHAR_DEVICE != 1	
	pthread_t timestamp_tid = 0;
#endif

	/* open log */
	openlog( "server", 0, LOG_USER );
#if USE_AESD_CHAR_DEVICE == 1
	syslog( LOG_DEBUG, "AESD Socket Server Starting (using aeasdchar)" );
#else
	syslog( LOG_DEBUG, "AESD Socket Server Starting" );
#endif

	/* initialize single-list */
	SLIST_INIT( &head );

	/* CLI arguments */
	if( argc > 1 )
	{
		if( strcmp( "-d", argv[1] ) == 0 )
		{
			deamon = true;
		}
	}

	/* sigaction */
	struct sigaction action;
	memset( &action, 0, sizeof( struct sigaction ));
	action.sa_handler = signal_handler;
	if( sigaction( SIGINT, &action, NULL ) != 0 )
	{
		/* error */
		syslog( LOG_ERR, "Cannot add SIGINT handler: %s", strerror( errno ));
		goto cleanup;
	}
	if( sigaction( SIGTERM, &action, NULL ) != 0 )
	{
		/* error */
		syslog( LOG_ERR, "Cannot add SIGTERM handler: %s", strerror( errno ));
		goto cleanup;
	}
	syslog( LOG_DEBUG, "Signal handlers installed" );

	/* create socket */
	sfd = socket( AF_INET, SOCK_STREAM, 0 );
	if( sfd < 0 )
	{
		/* error */
		syslog( LOG_ERR, "Cannot create socket: %s", strerror( errno ));
		goto cleanup;
	}
	syslog( LOG_DEBUG, "Socket created" );
	
	/* bind socket */
	int serr;
	struct addrinfo hints;
	memset( &hints, 0, sizeof( struct addrinfo ));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	hints.ai_flags = AI_PASSIVE;

    if ((serr = getaddrinfo(NULL, PORT_NUMBER, &hints, &sainfo)) != 0)
	{
		/* error */
		syslog( LOG_ERR, "Failed getaddrinfo: %s", gai_strerror( serr ));
		goto cleanup;
	}

	if( bind( sfd, sainfo->ai_addr, sizeof( struct sockaddr )) != 0 )
	{
		/* error */
		syslog( LOG_ERR, "Cannot bind: %s", strerror( errno ));
		goto cleanup;
	}
	syslog( LOG_DEBUG, "Socket bound to port %s", PORT_NUMBER );

	/* deamonize if requested */
	if( deamon )
	{
		pid_t pid = fork();
		if( pid < 0 )
		{
			/* error */
			syslog( LOG_ERR, "Cannot fork: %s", strerror( errno ));
			goto cleanup;
		}
		else if( pid > 0 )
		{
			/* parent */
			syslog( LOG_DEBUG, "Deamonized, parent exiting" );
			return 0;
		}
		/* child - deamon */ 
	}

	/* listen on socket */
	if( listen( sfd, 1 ) != 0 )
	{
		/* error */
		syslog( LOG_ERR, "Failed when listening: %s", strerror( errno ));
		goto cleanup;
	}
	syslog( LOG_DEBUG, "Socket listening" );

#if USE_AESD_CHAR_DEVICE != 1
	/* create timestamp thread */
	if( 0 != pthread_create( &timestamp_tid, NULL, &timestamp_thread, NULL ))
	{
		syslog( LOG_ERR, "Cannot create timestamp thread: %s", strerror( errno ));
		goto cleanup;
	}
	syslog( LOG_DEBUG, "Timestamp thread created" );
#endif

	/* main loop */
	atomic_store(&running, true);

	while( atomic_load(&running) )
	{
		if( handle_connection( sfd ) != 0 )
		{
			/* error */
			syslog( LOG_ERR, "Error handling connection" );
			goto cleanup;
		}
	} /* end of running */

#if USE_AESD_CHAR_DEVICE != 1
	remove( AESD_DATA_FILE );
#endif
	syslog( LOG_INFO, "Caught signal, exiting" );

	close( sfd );
	freeaddrinfo( sainfo );
#if USE_AESD_CHAR_DEVICE != 1
	if( timestamp_tid != 0 )
	{
		pthread_kill( timestamp_tid, SIGTERM );
		pthread_join( timestamp_tid, NULL );
	}
#endif
	closelog();

	return 0;

cleanup:
	if( sfd >= 0 )
		close( sfd );
	if( sainfo )
		freeaddrinfo( sainfo );
#if USE_AESD_CHAR_DEVICE != 1
	if( timestamp_tid != 0 )
	{
		pthread_kill( timestamp_tid, SIGTERM );
		pthread_join( timestamp_tid, NULL );
	}
#endif
	closelog();
	return -1;
}

