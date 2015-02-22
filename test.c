#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* R/W /dev/reverse from parallel threads to test thread-safety in modreverse */
int main(int argc, char **argv)
{
	const size_t MAX_BUFFER_SIZE = 8192;
	char buf[MAX_BUFFER_SIZE];
	int fd = open("/dev/reverse", O_RDWR);	
	const int MAX_RW = 75;
	int reads = 0;
	int writes = 0;
	if (fd != -1) {
		if (fork()) {
			while (writes++ < MAX_RW) {
				write(fd, argv[1], strlen(argv[1]));
			}
		} else {
			while (reads++ < MAX_RW) {
				if (read(fd, buf, MAX_BUFFER_SIZE) > 0) {
					printf("Read(%i): %s\n", reads, buf);
				}
			}
		}
	} else {
		printf("Failed to open /dev/reverse (try sudo?)\n");
	}	
	return 0;
}
