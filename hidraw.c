#include <arpa/inet.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
	if (argc != 4)
		errx(1, "usage: %s dev host port", argv[0]);

	int res;

	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		err(1, "socket");

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_addr = { inet_addr(argv[2]) },
		.sin_port = htons(atoi(argv[3])),
		.sin_zero = {0},
	};
	res = connect(sock, (void *)&addr, sizeof addr);
	if (res < 0)
		err(1, "connect");

	int fd = open(argv[1], O_RDONLY);
	if (fd < 0)
		err(1, "open %s", argv[1]);

	char buf[256];
	for (;;) {
		res = read(fd, buf, sizeof buf);
		if (res < 0)
			err(1, "read");
		res = send(sock, buf, res, 0);
		if (res < 0)
			err(1, "send rep");
	}
}
