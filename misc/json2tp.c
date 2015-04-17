#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include "tp.h"
#include "tp_transcode.h"

int
main(int argc, char **argv)
{
	FILE *file = stdin;
	FILE *out_file = stdout;
	size_t size = 1024*4;
	char *data = (char *) calloc(1, size), *output = (char *) calloc(1, size);

	if (data == NULL || output == NULL) {
		fprintf(stderr,
				"Failed to allocate read/output buffer of %zu bytes, exiting.",
				size*2);
		exit(2);
	}

	int c;
	static struct option long_options[] = {
		{"in-file", required_argument, 0, 'i'},
		{"out-file", required_argument, 0, 'o'},
		{0, 0, 0, 0}
	};
	int index = 0;
	while ((c = getopt_long(argc, argv, "io:", long_options, &index)) != -1) {
		switch(c) {
		case 'i':
			file = fopen(optarg, "r");
			if (file == NULL) {
				fprintf(stderr, "Failed to open in-file '%s', error '%s'\n",
						optarg, strerror(errno));
				exit(2);
			}
			break;
		case 'o':
			out_file = fopen(optarg, "w");
			if (out_file == NULL) {
				fprintf(stderr, "Failed to open out-file '%s', error '%s'\n",
						optarg, strerror(errno));
				exit(2);
			}
			break;
		default:
			break;
		}
	}

	tp_transcode_t t;
	if (tp_transcode_init(&t, output, size, YAJL_JSON_TO_TP)
            == TP_TRANSCODE_ERROR)
    {
		fprintf(stderr, "Failed to initialize transcode, exiting\n");
		exit(2);
	}

	for (size_t s = 0;;) {
		char *it = data;
		const size_t rd = fread((void *) data, 1, 10, file);
		s += rd;
		if (rd == 0) {
			if (!feof(file)) {
				fprintf(stderr, "error reading from\n");
			}
			break;
		}

		if (tp_transcode(&t, it, rd) == TP_TRANSCODE_ERROR) {
			fprintf(stderr, "Failed to transcode: '%s'\n", t.errmsg);
			break;
		}
		it = data + size;
	}

	size_t complete_msg_size = 0;
	if (tp_transcode_complete(&t, &complete_msg_size) != TP_TRANSCODE_ERROR)
		fwrite(output, 1, complete_msg_size, out_file);
    else
        fprintf(stderr, "Failed to complete: '%s'\n", t.errmsg);

	fflush(out_file);
	fflush(stderr);

	return 0;
}
