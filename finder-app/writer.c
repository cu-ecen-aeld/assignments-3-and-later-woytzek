#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>

void print_usage(void)
{
  printf( "\n\n");
  printf( "Usage: writer arg1 arg2\n" );
  printf( "       arg1 is path to file with file name\n" );
  printf( "       arg2 is string to write into file\n" );  
}

int main(int argc, char** argv)
{
  /* open connection to log */
  openlog( "writer", 0, LOG_USER );

  /* check number of cli arguments */
  if( argc != 3 )
  {
    print_usage();
    syslog( LOG_ERR, "%s", "wrong number of parameters" );
    return 1;
  }

  /* get input parameters */
  char* fname = argv[1];
  char* fstring = argv[2];

  /* open file for writing */
  FILE *file = fopen( fname, "w" );
  if( NULL == file )
  {
    syslog( LOG_ERR, "Cannot open file %s for writing. %s\n", fname, strerror( errno ));
    return 1;
  }

  /* write to file and close */
  fprintf( file, "%s", fstring );
  syslog( LOG_DEBUG, "Writing string %s to file %s", fstring, fname );
  fclose( file );

  /* cloase connection to log */
  closelog();

  return 0;
}
