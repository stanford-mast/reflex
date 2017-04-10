/*
 * Copyright (c) 2015-2017, Stanford University
 *  
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *  * Redistributions of source code must retain the above copyright notice, 
 *    this list of conditions and the following disclaimer.
 * 
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 *  * Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
