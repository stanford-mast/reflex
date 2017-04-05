#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main()
{
  char my_write_str[] = "1234567890";
  char my_read_str[100];
  char my_filename[] = "/mnt/reflex/flush_test.txt";
  int my_file_descriptor, close_err;

  /* Open the file.  Clobber it if it exists. */
  my_file_descriptor = open (my_filename, O_RDWR | O_CREAT | O_TRUNC);

  /* Write 10 bytes of data and make sure it's written */
  write (my_file_descriptor, (void *) my_write_str, 10);
  printf("Do fsync()\n");
  fsync (my_file_descriptor);
  printf("fsync done\n");
  /* Seek the beginning of the file */
  lseek (my_file_descriptor, 0, SEEK_SET);

  /* Read 10 bytes of data */
  read (my_file_descriptor, (void *) my_read_str, 10);

  /* Terminate the data we've read with a null character */
  my_read_str[10] = '\0';

  printf ("String read = %s.\n", my_read_str);

  close (my_file_descriptor);

  return 0;
}
