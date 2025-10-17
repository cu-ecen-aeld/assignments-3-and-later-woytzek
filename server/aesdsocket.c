
#include <stdio.h>
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

bool connected = false;
bool running = false;

static void signal_handler( int signal )
{
	if( SIGINT == signal || SIGTERM == signal )
	{
		running = false;
	}
}

int main( int argc, char** argv )
{
	int sfd; 
	int acceptedfd;
	struct addrinfo *sainfo;
	bool deamon = false;

	/* open log */
	openlog( "server", 0, LOG_USER );

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
		goto e_closelog;
	}
	if( sigaction( SIGTERM, &action, NULL ) != 0 )
	{
		/* error */
		syslog( LOG_ERR, "Cannot add SIGTERM handler: %s", strerror( errno ));
		goto e_closelog;
	}

	/* create socket */
	sfd = socket( AF_INET, SOCK_STREAM, 0 );
	if( sfd < 0 )
	{
		/* error */
		syslog( LOG_ERR, "Cannot create socket: %s", strerror( errno ));
		goto e_closelog;
	}
	
	/* bind socket */
	int serr;
	struct addrinfo hints;
	memset( &hints, 0, sizeof( sainfo ));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	hints.ai_flags = AI_PASSIVE;

	if( (serr = getaddrinfo( NULL, "9000", &hints, &sainfo )) != 0 )
	{
		/* error */
		syslog( LOG_ERR, "Failed getaddrinfo: %s", gai_strerror( serr ));
		goto e_closesocket;
	}

	if( bind( sfd, sainfo->ai_addr, sizeof( struct sockaddr )) != 0 )
	{
		/* error */
		syslog( LOG_ERR, "Cannot bind: %s", strerror( errno ));
		goto e_freeai;
	}

	/* deamonize if requested */
	if( deamon )
	{
		pid_t pid = fork();
		if( pid < 0 )
		{
			/* error */
			syslog( LOG_ERR, "Cannot fork: %s", strerror( errno ));
			goto e_freeai;
		}
		else if( pid > 0 )
		{
			/* parent */
			return 0;
		}
		/* child - deamon */ 
	}

	/* listen on socket */
	if( listen( sfd, 1 ) != 0 )
	{
		/* error */
		syslog( LOG_ERR, "Failed when listening: %s", strerror( errno ));
		goto e_freeai;
	}

	/* main loop */
	running = true;
	while( running )
	{
		/* accept connection */
		struct sockaddr_in acceptedad;
		socklen_t adlen = sizeof( struct sockaddr_in );
		acceptedfd = accept( sfd, (struct sockaddr*)&acceptedad, &adlen );
		if( !running )
		{
			/* if accept() interrupted by signal, break the loop */
			break;
		}
		if( acceptedfd < 0 )
		{
			/* error */
			syslog( LOG_ERR, "Failed on accept: %s", strerror( errno ));
			goto e_freeai;
		}

		connected = true;

		/* log connection IP */
		int _addr[4];
		_addr[0] = acceptedad.sin_addr.s_addr & 0xFF;
		_addr[1] = (acceptedad.sin_addr.s_addr >> 8) & 0xFF;
		_addr[2] = (acceptedad.sin_addr.s_addr >> 16) & 0xFF;
		_addr[3] = (acceptedad.sin_addr.s_addr >> 24) & 0xFF;
		syslog( LOG_INFO, "Accepted connection from %d:%d:%d:%d", _addr[0], _addr[1], _addr[2], _addr[3] );

		while( connected )
		{
			/* receive message */
			char buf[1025] = {0};
			int rcvlen = recv( acceptedfd, buf, 1000, 0 );
			if( rcvlen == 0 )
			{
				/* disconnected */
				syslog( LOG_INFO, "Closed connection from %d:%d:%d:%d", _addr[0], _addr[1], _addr[2], _addr[3] );
				connected = false;
				continue;
			}

			/* open file to collect message */
			int fd = open( "/var/tmp/aesdsocketdata", O_CREAT | O_APPEND | O_WRONLY, 0666 );
			if( fd < 0 )
			{
				/* error */
				syslog( LOG_ERR, "Cannot open file: %s", strerror( errno ));
				continue;
			}

			/* complete message */
			while( rcvlen > 0 )
			{
				write( fd, buf, rcvlen );
				rcvlen = recv( acceptedfd, buf, 1000, MSG_DONTWAIT );
			}

			close( fd );
			
			/* ignore EAGAIN error, it indicates 'no more data' to receive */
			if( rcvlen < 0 && errno != EAGAIN )
			{
				/* error */
				syslog( LOG_ERR, "Receive failed: %s", strerror( errno ));
				goto e_closeacc;
			}

			/* re-open file for reading */
			fd = open( "/var/tmp/aesdsocketdata", O_RDONLY );
			int rdlen = read( fd, buf, 1000 );
			/* and send back file content */
			while( rdlen > 0 )
			{
				send( acceptedfd, buf, rdlen, 0 );
				rdlen = read( fd, buf, 1000 );
			}
			close( fd );
		} /* end of connected */
	} /* end of running */

	remove( "/var/tmp/aesdsocketdata" );
	syslog( LOG_INFO, "Caught signal, exiting" );

	close( sfd );
	close( acceptedfd );
	freeaddrinfo( sainfo );
	closelog();

	return 0;

e_closeacc:
	close( acceptedfd );
e_freeai:	
	freeaddrinfo( sainfo );
e_closesocket:
	close( sfd );
e_closelog:
	closelog();
	return -1;
}

