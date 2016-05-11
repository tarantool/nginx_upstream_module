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
 * Copyright (C) 2015-2016 Tarantool AUTHORS:
 * please see AUTHORS file.
 */

#ifndef NGX_HTTP_TNT_CREATE_REQUEST_H_INCLUDED
#define NGX_HTTP_TNT_CREATE_REQUEST_H_INCLUDED

#include <ngx_core.h>
#include <ngx_http.h>
#include <tp_transcode.h>

typedef enum ngx_tnt_conf_states {
    NGX_TNT_CONF_ON         = 0x0001,
    NGX_TNT_CONF_OFF        = 0x0002,
    NGX_TNT_CONF_PARSE_ARGS = 0x0004
} ngx_tnt_conf_states_e;

typedef struct {
    ngx_http_upstream_conf_t upstream;
    ngx_int_t                index;

    size_t                   in_multiplier;
    size_t                   out_multiplier;

    /** Preset method
     */
    ngx_str_t                method;

    /** Max allowed query/headers size which can be passed to tarantool
     */
    size_t                   pass_http_request_buffer_size;

    /** Pass query/headers to tarantool
     *
     *  If set Tarantool get query args as lua table, e.g.
     *  /tnt_method?arg1=X&arg2=123
     *
     *  Tarantool
     *  function tnt_method(http_req)
     *  {
     *    http_req['args']['arg1'] -- eq 'Y'
     *    http_req['args']['arg2'] -- eq '123'
     *  }
     */
    ngx_uint_t               pass_http_request;

    /** On http REST methods
     */
    ngx_uint_t               http_rest_methods;

    /** Tarantool allowed methods
     */
    tp_allowed_methods_t     allowed_methods;
} ngx_http_tnt_loc_conf_t;


/** Set of allowed rest methods
 */
static const ngx_uint_t ngx_http_tnt_allowed_rest_methods =
    (NGX_HTTP_POST|NGX_HTTP_GET|NGX_HTTP_PUT|NGX_HTTP_DELETE);

/** Current upstream state
 */
enum ctx_state {
    OK = 0,

    INPUT_JSON_PARSE_FAILED,
    INPUT_TO_LARGE,
    INPUT_EMPTY,

    READ_PAYLOAD,
    READ_BODY,
    SEND_REPLY
};

typedef struct ngx_http_tnt_ctx {

    /** Reference to Tarantool payload e.g. size of TP message
     */
    struct {
        u_char mem[6];
        u_char *p, *e;
    } payload;

    enum ctx_state     state;
    /** in_err - error buffer
     *  tp_cache - buffer for store parts of TP message
     */
    ngx_buf_t          *in_err, *tp_cache;

    /** rest - how many bytes until transcoding is end
     *  payload_size - payload as int val
     *  rest_batch_size - how many parts until batch is end
     *  batch_size - number of parts in batch
     */
    ssize_t            rest, payload_size;
    int                rest_batch_size, batch_size;

    /** Greeting from tarantool
     */
    ngx_int_t          greeting:1;

    /** preset method & len
     */
    u_char             preset_method[128];
    u_char             preset_method_len;
} ngx_http_tnt_ctx_t;

ngx_http_tnt_ctx_t * ngx_http_tnt_create_ctx(ngx_http_request_t *r);
void ngx_http_tnt_reset_ctx(ngx_http_tnt_ctx_t *ctx);

ngx_int_t ngx_http_tnt_init_handlers(ngx_http_request_t *r,
                                     ngx_http_upstream_t *u,
                                     ngx_http_tnt_loc_conf_t *tlcf);

/** create tarantool requests handlers [
 */
ngx_int_t ngx_http_tnt_body_json_handler(ngx_http_request_t *r);
ngx_int_t ngx_http_tnt_query_handler(ngx_http_request_t *r);
/* ] */

ngx_int_t ngx_http_tnt_reinit_request(ngx_http_request_t *r);
ngx_int_t ngx_http_tnt_process_header(ngx_http_request_t *r);
void ngx_http_tnt_abort_request(ngx_http_request_t *r);
void ngx_http_tnt_finalize_request(ngx_http_request_t *r, ngx_int_t rc);

ngx_buf_t* ngx_http_tnt_set_err(ngx_http_request_t *r,
                                int errcode,
                                const u_char *msg,
                                size_t msglen);

/** Rrror & error code holder, functions [[
 */
typedef struct ngx_http_tnt_error {
    const ngx_str_t msg;
    int code;
} ngx_http_tnt_error_t;

enum ngx_http_tnt_err_messages_idx {
    REQUEST_TOO_LARGE   = 0,
    UNKNOWN_PARSE_ERROR = 1,
    HTTP_REQUEST_TOO_LARGE = 2
};

const ngx_http_tnt_error_t *get_error_text(int type);
/** ]]
 */

/** Size of JSON proto overhead
 */
static inline size_t
ngx_http_tnt_overhead(void)
{
    return sizeof("[{"
        "'error': {"
            "'code':-XXXXX,"
            "'message':''"
        "},"
        "[['result':{},"
        "'id':4294967295]]"
    "}");
}

#endif
