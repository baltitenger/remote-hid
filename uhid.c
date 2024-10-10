#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/uhid.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <uuid/uuid.h>

typedef uint8_t u8;

// this is a bit of a hack that filters out bogus values in vendor usage pages.
// only classifies usage pages >0xff as vendor
static int sanitize_rep_desc(u8 *rep_desc, int size) {
	u8 *p = rep_desc, *q = p, *end = p + size;
	int depth = 0, bad = 0;
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
	return q - rep_desc;
}


int main(int argc, char *argv[]) {
	if (argc != 3)
		errx(1, "usage: %s rep_desc port", argv[0]);

	int res;

	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		err(1, "socket");

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_addr = { INADDR_ANY },
		.sin_port = htons(atoi(argv[2])),
		.sin_zero = {0},
	};

	res = bind(sock, (struct sockaddr *)&addr, sizeof addr);
	if (res < 0)
		err(1, "bind");

	struct uhid_event ev = {
		.type = UHID_CREATE2,
		.u.create2 = {
			.name = "remote touch",
			.uniq = "uuid_",
			.bus = BUS_VIRTUAL,
			.vendor  = 0xDEAD,
			.product = 0xBEEF,
			.version = 0x42069,
			.country = 0,
		},
	};
	uuid_t uuid;
	uuid_generate_random(uuid);
	uuid_unparse(uuid, (char *)ev.u.create2.uniq+5);

	int descfd = open(argv[1], O_RDONLY);
	if (descfd < 0)
		err(1, "open desc");
	res = read(descfd, ev.u.create2.rd_data, sizeof ev.u.create2.rd_data);
	if (res < 0)
		err(1, "read desc");
	ev.u.create2.rd_size = sanitize_rep_desc(ev.u.create2.rd_data, res);
	close(descfd);

	int fd = open("/dev/uhid", O_RDWR | O_CLOEXEC);
	if (fd < 0)
		err(1, "open uhid");

	res = write(fd, &ev, sizeof ev);
	if (res < 0)
		err(1, "write UHID_CREATE2");

	struct pollfd pfds[] = {
		{ .fd = sock, .events = POLLIN },
		{ .fd = fd,   .events = POLLIN },
	};

	for (;;) {
		res = poll(pfds, 2, -1);
		if (res < 0)
			err(1, "poll");
		if (pfds[0].revents & POLLIN) {
			res = recv(sock, ev.u.input2.data, sizeof ev.u.input2.data, 0);
			if (res < 0)
				err(1, "recv");
			ev.type = UHID_INPUT2;
			ev.u.input2.size = res;
			res = write(fd, &ev, sizeof ev);
			if (res < 0)
				err(1, "write UHID_INPUT2");
		}
		if (pfds[1].revents & POLLIN) {
			res = read(fd, &ev, sizeof ev);
			if (res < 0)
				err(1, "read uinput");
			switch (ev.type) {
				case UHID_START:
					printf("UHID_START\n");
					break;
				case UHID_OPEN:
					printf("UHID_OPEN\n");
					break;
				case UHID_CLOSE:
					printf("UHID_CLOSE\n");
					break;
				case UHID_GET_REPORT:
					;
					struct uhid_get_report_req req = ev.u.get_report;
					printf("get report: %i, %i, %i\n", req.id, req.rnum, req.rtype);
					ev.type = UHID_GET_REPORT_REPLY;
					ev.u.get_report_reply = (struct uhid_get_report_reply_req) {
						.id = req.id,
						.err = EIO,
					};
					res = write(fd, &ev, sizeof ev);
					if (res < 0)
						err(1, "write report reply");
					break;
				default:
					printf("unhandled uhid event: %i\n", ev.type);
			}
		}
	}
}
