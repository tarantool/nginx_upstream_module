#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <getopt.h>
#include <sys/socket.h>

#include "tp.h"
#include "tp_transcode.h"

int
tnt_connect(char* host, int port)
{
	struct hostent *server = gethostbyname(host);
	if (server == NULL) {
		fprintf(stderr, "gethostbyname failed, msg: %s\n", strerror(errno));
		exit(1);
	}

	int s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	bcopy((char *)server->h_addr, (char *)&sa.sin_addr.s_addr, server->h_length);
		sa.sin_port = htons(port);
	if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
		fprintf(stderr, "failed to connect: %s\n", strerror(errno));
		exit(errno);
	}

	const int gbufsize = 128;
	char gbuf[gbufsize];
	struct tpgreeting greet;

	int pos = 0;
	do {
		int rres = read(s, gbuf + pos, gbufsize - pos);
		assert(rres > 0);
		if (rres <= 0) {
			fprintf(stderr, "failed to tpgreeting: %s\n", strerror(errno));
			exit(errno);
			}
		pos += rres;
	} while (tp_greeting(&greet, gbuf, pos) < 0);

	return s;
}

void
usage(char **argv) {
	fprintf(stderr, "USAGE %s -h [hostname] -p [port]\n",
			argv[1]);
	exit(1);
}

int
main(int argc, char **argv)
{
	char *host = NULL;
	int port = -1;
	int c;

	static struct option long_options[] = {
		{"host", required_argument, 0, 'h'},
		{"port", required_argument, 0, 'p'},
		{0, 0, 0, 0}
	};
	int index = 0;
	while ((c = getopt_long(argc, argv, "h:p:", long_options, &index)) != -1){
		switch(c){
		case 'h':
			host = optarg;
			break;
		case 'p':
			port = atoi(optarg);
			break;
		default:
			usage(argv);
		}
	}

	if (host == NULL) {
		host = "127.0.0.1";
	}

	if (port == -1) {
		port = 9999;
	}

	int tnt_fd = tnt_connect(host, port);

	char data[1024];
	FILE *file = stdin;
	size_t pos = 0;

	for (;;) {
		const size_t rd = fread((void *) data, 1, sizeof(data), file);
		if (rd == 0) {
			if (!feof(file)) {
				fprintf(stderr, "Read input failed, msg: %s\n",
						strerror(ferror(file)));
			}
			break;
		}

		do {
			const int w = write(tnt_fd, data, rd);
			assert(w > 0);
			if (w <= 0)
				exit(errno);
			pos += w;
		} while (pos < rd);
	}

	char obuf[1024*2];

	tp_transcode_t t;
	tp_transcode_init(&t, &obuf[0], sizeof(obuf), TP_REPLY_TO_JSON);

	pos = 0;
	int rc = 0;
	for (;;) {
		int rres = read(tnt_fd, obuf + pos, 10);
		assert(rres > 0);
		if (rres <= 0)
			exit(errno);
		pos += rres;
		if ((rc = tp_transcode(&t, obuf, pos)) != TP_TRANSCODE_AGAIN)
			break;
	}
	if (rc == TP_TRANSCODE_ERROR)
		fprintf(stderr, "tp_transcode error: %s\n", t.errmsg);

	size_t complete_msg_size = 0;
	tp_transcode_complete(&t, &complete_msg_size);

	printf("Result: '%.*s'\n", (int)complete_msg_size, obuf);

	return 0;
}
