#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <getopt.h>

#define TP_H_AUTH_OFF 1
#include "tp.h"
#include "tp_transcode.h"

void
usage(char **argv)
{
    printf(
            "USAGE: %s [in file name] -- stdin if empty\n\n"
            "Warning!\n"
            "This program is not stream based.\n"
            "Use this program only for fixed size tarantool protocol messages\n"
            "\n",
            argv[0]
    );
    exit(0);
}

int
main(int argc, char ** argv)
{
    FILE *in = stdin;

    int c;
    static struct option long_options[] = {
        {"in-file", required_argument, 0, 'i'},
        {0, 0, 0, 0}
    };

    int index = 0;
    while ((c = getopt_long(argc, argv, "ih:", long_options, &index)) != -1) {
        switch(c){
        case 'i':
            in = fopen(optarg, "r");
            if (in == NULL) {
                fprintf(stderr,
                        "tp_dump: failed to open file '%s', error '%s'\n",
                        optarg, strerror(errno));
                exit(2);
            }
            break;
        case 'h':
            usage(argv);
        }
    }

    char err[255], obuf[1024*2], ibuf[1024*2];
    memset(err, 0, sizeof(err));
    memset(obuf, 0, sizeof(obuf));
    memset(ibuf, 0, sizeof(ibuf));

    tp_transcode_t tc;
    tp_transcode_init_args_t args = {
        .output = obuf,
        .output_size = sizeof(obuf) - 1,
        .method = NULL,
        .method_len = 0,
        .codec = TP_TO_JSON,
        .mf = NULL
    };
    if (tp_transcode_init(&tc, &args) == TP_TRANSCODE_ERROR)
    {
        fprintf(stderr, "tp_dump: failed to initialize transcode\n");
        exit(2);
    }

    size_t size = 0, rd = 0;
    for (;;) {
        size += rd = fread((void *)&ibuf[size], 1, sizeof(ibuf) - size, in);
        if (rd == 0) {
            if (!feof(in)) {
                fprintf(stderr, "tp_dump: failed to read file, error: '%s'\n",
                        strerror(ferror(in)));
                exit(2);
            }
            break;
        }
    }

    if (tp_transcode(&tc, ibuf, size) != TP_TRANSCODE_OK) {
        fprintf(stderr, "tp_dump: failed to transcode , msg: '%s'\n",
                tc.errmsg);
        exit(2);
    } else {
        size_t complete_msg_size = 0;
        if (tp_transcode_complete(&tc, &complete_msg_size)
                == TP_TRANSCODE_ERROR)
        {
            fprintf(stderr, "tp_dump: failed to complete transcode, msg '%s'\n",
                    tc.errmsg);
            exit(2);
        }
        printf("%.*s", (int)complete_msg_size, obuf);
    }

    tp_transcode_free(&tc);

    fclose(in);

    return 0;
}
