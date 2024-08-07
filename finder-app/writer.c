#include <errno.h>
#include <stdio.h>
#include <syslog.h>

int main(int argc, char *argv[]) {

  // open syslog
  openlog("writer", 0, LOG_USER);

  if (argc != 3) {
    printf("Usage: %s <writefile> <writestr>\n", argv[0]);
    syslog(LOG_ERR, "Wrong arguments provided");
    return 1;
  }

  char *write_path = argv[1];
  char *write_text = argv[2];
  FILE *f = fopen(write_path, "w");
  if (!f) {
    syslog(LOG_ERR, "could not open file %s", write_path);
    return -1;
  }

  syslog(LOG_DEBUG, "Writing %s to %s", write_text, write_text);
  if (!fputs(write_text, f)) {
    syslog(LOG_ERR, "could write to file %s. Errno %d.", write_path, errno);
  }
  if (!fputc('\n', f)) {
    syslog(LOG_ERR, "could write to file %s. Errno %d.", write_path, errno);
  }
<<<<<<< HEAD
  
=======

>>>>>>> 63871ce ( assingment2 submission)
  // do not forget to close the file
  fclose(f);

  return 0;
}