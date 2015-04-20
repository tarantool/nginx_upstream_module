#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include "tp_transcode.h"

int
main(int argc, char **argv)
{
	FILE *file = stdin;
	FILE *out_file = stdout;
	size_t size = 1024*4;

	int c;
	static struct option long_options[] = {
		{"in-file", required_argument, 0, 'i'},
		{"out-file", required_argument, 0, 'o'},
		{"size", required_argument, 0, 's'},
		{0, 0, 0, 0}
	};
	int index = 0;
	while ((c = getopt_long(argc, argv, "ios:", long_options, &index)) != -1) {
		switch(c) {
		case 'i':
			file = fopen(optarg, "r");
			if (file == NULL) {
				fprintf(stderr, "json2tp: "
								"failed to open in-file '%s', error '%s'\n",
						optarg, strerror(errno));
				exit(2);
			}
			break;
		case 'o':
			out_file = fopen(optarg, "w");
			if (out_file == NULL) {
				fprintf(stderr, "json2tp: "
								"failed to open out-file '%s', error '%s'\n",
						optarg, strerror(errno));
				exit(2);
			}
			break;
        case 's':
           size = (size_t)atoi(optarg);
           break;
		default:
            fprintf(stderr, "json2tp: unknown option\n");
		    exit(2);
		}
	}

	char *data = (char *) calloc(1, 1024*2), *output = (char *) calloc(1, size);
	if (data == NULL || output == NULL) {
		fprintf(stderr,
			"json2tp: failed to allocate buffer of %zu bytes, exiting.",
			size*2);
		exit(2);
	}

	tp_transcode_t t;
	if (tp_transcode_init(&t, output, size, YAJL_JSON_TO_TP, NULL)
            == TP_TRANSCODE_ERROR)
	{
		fprintf(stderr, "json2tp: failed to initialize transcode, exiting\n");
		exit(2);
	}

    size_t s = 0;
	enum tt_result rc = 0;
	for (s = 0;;) {
		const size_t rd = fread((void *) &data[0], 1, 10, file);
		s += rd;
		if (rd == 0) {
			if (!feof(file)) {
				fprintf(stderr, "json2tp: error reading from\n");
			}
			break;
		}

		if ((rc = tp_transcode(&t, &data[0], rd)) == TP_TRANSCODE_ERROR) {
			fprintf(stderr, "json2tp: failed to transcode: '%s'\n", t.errmsg);
			break;
		}
	}

	if (rc == TP_TRANSCODE_OK) {
		size_t complete_msg_size = 0;
		if (tp_transcode_complete(&t, &complete_msg_size)
			!= TP_TRANSCODE_ERROR)
			fwrite(output, 1, complete_msg_size, out_file);
		else
			fprintf(stderr, "json2tp: failed to complete: '%s'\n", t.errmsg);
	}

	tp_transcode_free(&t);

	fflush(out_file);
	fflush(stderr);

	return 0;
}
