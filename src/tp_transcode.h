
/*
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright (C) 2015-2019 Tarantool AUTHORS:
 * please see AUTHORS file.
 */

#ifndef TP_TRANSCODE_H_INCLUDED
#define TP_TRANSCODE_H_INCLUDED 1

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <json_encoders.h>

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

  /**
   *  WARNING! TP_TO_JSON must be use only for debug
   *
   *  Tarantool message to JSON
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
  char *errmsg;
  int errcode;

  const char *method;
  size_t method_len;

  int batch_size;

  struct {
    const char *pos;
    const char *end;
    size_t len;
  } data;
} tp_transcode_t;

/**
 */
typedef struct tp_transcode_init_args {
    char *output;
    size_t output_size;
    const char *method;
    size_t method_len;
    enum tp_codec_type codec;
    mem_fun_t *mf;
} tp_transcode_init_args_t;

/** Returns codes
 */
enum tt_result {
  TP_TRANSCODE_OK    = 1,
  TP_TRANSCODE_ERROR,
  TP_TNT_ERROR,
};

ssize_t
tp_read_payload(const char * const buf, const char * const end);

/** Initialize struct tp_transcode.
 *
 * Warning. 'method' does not copy, tp_transcode_t just hold pointer to memory
 *
 * Returns TP_TRANSCODE_ERROR if codec not found or create codec failed
 * Returns TP_TRANSCODE_OK if codec found and initialize well
 */
enum tt_result tp_transcode_init(tp_transcode_t *t,
                                 const tp_transcode_init_args_t *args);

/** Free struct tp_transcode
 */
void tp_transcode_free(tp_transcode_t *t);

/** Feed transcoder (see tp_transcode_init).
 *
 * Returns TP_TRANSCODE_OK if bytes enought for finish transcoding
 * Returns TP_TRANSCODE_ERROR if error occurred
 * Returns TP_AT_ERROR if error occurred
 */
enum tt_result tp_transcode(tp_transcode_t *t, const char *b, size_t s);

/** Finalize transcoding.
 *
 * Returns TP_TRANSCODE_OK if transcoding done
 * Returns TP_TRANSCODE_ERROR if error occurred
 */
enum tt_result
tp_transcode_complete(tp_transcode_t *t, size_t *complete_msg_size);

/**
 */
void tp_transcode_bind_data(tp_transcode_t *t,
    const char *data_beg, const char *data_end);

/**
 */
void
tp_reply_to_json_set_options(tp_transcode_t *t, bool pure_result,
    size_t multireturn_skip_count);

/**
 * WARNING! tp_dump() is for debug!
 *
 * Dump Tarantool message in JSON
 * Returns true, false
 */
bool
tp_dump(char *output, size_t output_size,
        const char *input, size_t input_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* }}} */

#endif /* TP_TRANSCODE_H_INCLUDED */
