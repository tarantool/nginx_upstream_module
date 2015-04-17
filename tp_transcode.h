#ifndef TP_TRANSCODE_H_INCLUDED
#define TP_TRANSCODE_H INCLUDED

#ifdef __cplusplus
extern "C" {
#endif


/* {{{ API declaration */
struct tp_transcode;

/**
 * Underlying codec functions
 */
typedef void *(*tp_codec_create)(struct tp_transcode*, char *, size_t);
typedef void (*tp_codec_free)(void *);
typedef int (*tp_do_transcode)(void *, const char *, size_t);
typedef int (*tp_do_transcode_complete)(void *, size_t *);

/**
 * Underlying codec object
 */
typedef struct tp_codec {
	void *ctx; /* Underlying codec context */

	tp_codec_create create; /* create codec context(i.e. 'ctx') function */
	tp_codec_free free; /* free codec context(i.e. 'ctx') function */

	tp_do_transcode transcode; /* transcode function */
	tp_do_transcode_complete complete; /* complete function */
} tp_codec_t;

/**
 * Avaliable codecs
 */
enum tp_codec_type {

  /**
	 * JSON PRC to Tarantool message (Yajl engine)
	 */
	YAJL_JSON_TO_TP = 0,

	/**
	 * Tarantool reply message to JSON
	 */
	TP_REPLY_TO_JSON,

	/**
	 * Tarantool message to JSON
	 */
	TP_TO_JSON,

	TP_CODEC_MAX
};

/**
 * tp_transcode obj - underlying codec holder
 */
typedef struct tp_transcode {
	tp_codec_t codec;
	char errmsg[128];
} tp_transcode_t;

/**
 * Returns codes
 */
#define TP_TRANSCODE_OK    1
#define TP_TRANSCODE_ERROR 2
#define TP_TRANSCODE_AGAIN 3

ssize_t
tp_read_payload(const char * const buf, const char * const end);

/**
 * Initialize struct tp_transcode.
 * Returns TP_TRANSCODE_ERROR if codec not found or create codec failed
 * Returns TP_TRANSCODE_OK if codec found and initialize well
 */
int tp_transcode_init(tp_transcode_t *t, char *output, size_t output_size,
		enum tp_codec_type codec);

/**
 * Convert input data to output (see tp_transcode_init), for instance json to msgpack
 * Returns TP_TRANSCODE_OK if bytes enought for finish transcoding
 * Returns TP_TRANSOCDE_AGAIN if more bytes requered
 * Returns TP_TRANSCODE_ERROR if error occurred
 */
static inline int
tp_transcode(tp_transcode_t *t, char *b, size_t size);

/**
 * Finalize (including free memory) transcoding.
 * Returns TP_TRANSCODE_OK if transcoding done
 * Returns TP_TRANSCODE_ERROR if error occurred
 */
static inline int
tp_transcode_complete(tp_transcode_t *t, size_t *complete_msg_size);

/**
 * Dump Tarantool message to output in JSON format
 * Returns true, false
 */
static inline bool
tp_dump(char *output, size_t output_size, char *input, size_t input_size);

static inline int
tp_transcode_complete(tp_transcode_t *t, size_t *complete_msg_size)
{
	if (t != NULL) {
		const int rc = t->codec.complete(t->codec.ctx, complete_msg_size);
		t->codec.free(t->codec.ctx);
		return rc;
	}
	return TP_TRANSCODE_ERROR;
}

static inline int
tp_transcode(tp_transcode_t *t, char *b, size_t size)
{
	if (t != NULL)
		return t->codec.transcode(t->codec.ctx, b, size);
	return TP_TRANSCODE_ERROR;
}

static inline bool
tp_dump(char *output, size_t output_size, char *input, size_t input_size)
{
	tp_transcode_t t;
	if (tp_transcode_init(&t, output, output_size, TP_TO_JSON)
			== TP_TRANSCODE_ERROR)
		return false;

	if (tp_transcode(&t, input, input_size) == TP_TRANSCODE_ERROR)
		return false;

	size_t complete_msg_size = 0;
	tp_transcode_complete(&t, &complete_msg_size);
	output[complete_msg_size] = '0';

	return complete_msg_size > 0;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
