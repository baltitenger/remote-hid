#include <arpa/inet.h>
#include <err.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

typedef uint8_t u8;

// this is a bit of a hack that filters out bogus values in vendor usage pages.
// only classifies usage pages >0xff as vendor
static void sanitize_rep_desc(struct hidraw_report_descriptor *rep_desc) {
	u8 *p = rep_desc->value, *q = p, *end = p + rep_desc->size;
	int depth = 0;
	int bad = 0;
	while (p != end) {
		u8 size = *p & 3;
		u8 typ = *p & ~3;
		if (size == 3)
			size = 4;
		// end coll
		if (typ == 0xc0)
			--depth;
		// begin coll
		else if (typ == 0xa0)
			++depth;
		// top level vendor usage page
		else if (typ == 0x04 && depth == 0 && size > 1)
			bad = 1;
		if (bad)
			p += 1+size;
		else
			for (int i = 0; i < 1+size; ++i)
				*q++ = *p++;
		if (typ == 0xc0 && depth == 0)
			bad = 0;
	}
	rep_desc->size = q - rep_desc->value;
}

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

	struct hidraw_report_descriptor rep_desc;
	res = ioctl(fd, HIDIOCGRDESCSIZE, &rep_desc.size);
	if (res < 0)
		err(1, "HIDIOCGRDESCSIZE");

	res = ioctl(fd, HIDIOCGRDESC, &rep_desc);
	if (res < 0)
		err(1, "HIDIOCGRDESC");

	sanitize_rep_desc(&rep_desc);

	res = send(sock, &rep_desc.value, rep_desc.size, 0);
	if (res < 0)
		err(1, "send rep_desc");

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
