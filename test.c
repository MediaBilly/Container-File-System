#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char const *argv[])
{
  int fd = open("test.txt",O_RDWR,0755);
  lseek(fd,2,SEEK_SET);
  write(fd,"s",1);
  close(fd);
  return 0;
}
