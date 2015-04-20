#ifndef TP_TRANSCODE_H_INCLUDED
#define TP_TRANSCODE_H INCLUDED

#include <assert.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


/* {{{ API declaration */
struct tp_transcode;
enum tt_result;


/** Underlying codec functions
 */
typedef void *(*tp_codec_create)(struct tp_transcode*, char *, size_t);
typedef void (*tp_codec_free)(void *);
typedef enum tt_result (*tp_do_transcode)(void *, const char *, size_t);
typedef enum tt_result (*tp_do_transcode_complete)(void *, size_t *);

/** Underlying codec object
 */
typedef struct tp_codec {
  void *ctx; /* Underlying codec context */

  tp_codec_create create; /* create codec context(i.e. 'ctx') function */
  tp_codec_free free; /* free codec context(i.e. 'ctx') function */

  tp_do_transcode transcode; /* transcode function */
  tp_do_transcode_complete complete; /* complete function */
} tp_codec_t;

/** Avaliable codecs
 */
enum tp_codec_type {

  /** JSON PRC to Tarantool message (Yajl engine)
   */
  YAJL_JSON_TO_TP = 0,

  /** Tarantool reply message to JSON
   */
  TP_REPLY_TO_JSON,

  /** Tarantool message to JSON
   */
  TP_TO_JSON,

  TP_CODEC_MAX
};

/** Memory functions
 */
typedef struct mem_fun {
  void *ctx;
  void*(*alloc)(void*, size_t);
  void*(*realloc)(void*, void *, size_t);
  void(*free)(void*, void *);
} mem_fun_t;

/** tp_transcode obj - underlying codec holder
 */
typedef struct tp_transcode {
  tp_codec_t codec;
  mem_fun_t mf;
  char errmsg[128];
} tp_transcode_t;

/** Returns codes
 */
enum tt_result {
  TP_TRANSCODE_OK    = 1,
  TP_TRANSCODE_ERROR = 2
};

ssize_t
tp_read_payload(const char * const buf, const char * const end);

/** Initialize struct tp_transcode.
 *
 * Returns TP_TRANSCODE_ERROR if codec not found or create codec failed
 * Returns TP_TRANSCODE_OK if codec found and initialize well
 */
enum tt_result tp_transcode_init(tp_transcode_t *t,
    char *output, size_t output_size,
    enum tp_codec_type codec,
    mem_fun_t *mf);

/** Free struct tp_transcode
 */
static inline void
tp_transcode_free(tp_transcode_t *t);

/** Feed transcoder (see tp_transcode_init).
 *
 * Returns TP_TRANSCODE_OK if bytes enought for finish transcoding
 * Returns TP_TRANSCODE_ERROR if error occurred
 */
static inline enum tt_result
tp_transcode(tp_transcode_t *t, const char *b, size_t s);

/** Finalize transcoding.
 *
 * Returns TP_TRANSCODE_OK if transcoding done
 * Returns TP_TRANSCODE_ERROR if error occurred
 */
static inline enum tt_result
tp_transcode_complete(tp_transcode_t *t, size_t *complete_msg_size);

/**
 * Dump Tarantool message to output in JSON format
 * Returns true, false
 */
static inline bool
tp_dump(char *output, size_t output_size,
        const char *input, size_t input_size);

static inline void
tp_transcode_free(tp_transcode_t *t)
{
  assert(t);
  assert(t->codec.ctx);
  t->codec.free(t->codec.ctx);
}

static inline enum tt_result
tp_transcode_complete(tp_transcode_t *t, size_t *complete_msg_size)
{
  assert(t);
  assert(t->codec.ctx);
  return t->codec.complete(t->codec.ctx, complete_msg_size);
}

static inline enum tt_result
tp_transcode(tp_transcode_t *t, const char *b, size_t s)
{
  assert(t);
  assert(t->codec.ctx);
  return t->codec.transcode(t->codec.ctx, b, s);
}

static inline bool
tp_dump(char *output, size_t output_size,
        const char *input, size_t input_size)
{
  tp_transcode_t t;
  if (tp_transcode_init(&t, output, output_size, TP_TO_JSON, NULL)
      == TP_TRANSCODE_ERROR)
    return false;

  if (tp_transcode(&t, input, input_size) == TP_TRANSCODE_ERROR) {
    tp_transcode_free(&t);
    return false;
  }

  size_t complete_msg_size = 0;
  tp_transcode_complete(&t, &complete_msg_size);
  output[complete_msg_size] = '0';

  tp_transcode_free(&t);

  return complete_msg_size > 0;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

/* }}} */

#endif
