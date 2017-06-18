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
 * Copyright (C) 2015-2017 Tarantool AUTHORS:
 * please see AUTHORS file.
 */

#ifndef NGX_HTTP_TNT_CREATE_REQUEST_H_INCLUDED
#define NGX_HTTP_TNT_CREATE_REQUEST_H_INCLUDED 1

#include <ngx_core.h>
#include <ngx_http.h>
#include <tp_transcode.h>


typedef enum ngx_tnt_conf_states {
    NGX_TNT_CONF_ON               = 1,
    NGX_TNT_CONF_OFF              = 2,
    NGX_TNT_CONF_PARSE_ARGS       = 4,
    NGX_TNT_CONF_UNESCAPE         = 8,
    NGX_TNT_CONF_PASS_BODY        = 16,
    NGX_TNT_CONF_PASS_HEADERS_OUT = 32,
} ngx_tnt_conf_states_e;

typedef struct {
    ngx_array_t                   *flushes;
    ngx_array_t                   *lengths;
    ngx_array_t                   *values;
    ngx_hash_t                     hash;
} ngx_http_tnt_headers_t;

/** The structure hold the nginx location variables, e.g. loc_conf.
 */
typedef struct {
    ngx_http_upstream_conf_t upstream;
    ngx_int_t                index;

    size_t                   in_multiplier;
    size_t                   out_multiplier;

    /** Preset method name
     *
     * If this is set then tp_transcode use only this method name and
     * tp_transcode will ignore the method name from the json or/and uri
     */
    ngx_str_t                method;

    /** This is max allowed size of query + headers + body, the size in bytes
     */
    size_t                   pass_http_request_buffer_size;

    /** Pass query/headers to tarantool
     *
     *  If this is set, then Tarantool recv. query args as the lua table,
     *  e.g. /tnt_method?arg1=X&arg2=123
     *
     *  Tarantool
     *  function tnt_method(http_req)
     *  {
     *    http_req['args']['arg1'] -- eq 'Y'
     *    http_req['args']['arg2'] -- eq '123'
     *  }
     */
    ngx_uint_t               pass_http_request;

    /** Http REST methods[default GET|PUT]
     *
     * if incoming HTTP method is in this set, then
     * the tp_transcode expect the tarantool method name in url,
     * i.e. HOST/METHOD_NAME/TAIL?ARGS
     *
     * XXX Also see method
     */
    ngx_uint_t               http_rest_methods;

    /** Set of http methods[default POST|DELETE]
     *
     * If incoming HTTP method is in this set,
     * then the tp_transcode expect method name in JSON protocol,
     * i.e. {"method":STR}
     *
     * XXX Also see method
     */
    ngx_uint_t               http_methods;

    ngx_array_t              *headers_source;

    ngx_http_tnt_headers_t   headers;

} ngx_http_tnt_loc_conf_t;


/** Set of allowed REST methods
 */
static const ngx_uint_t ngx_http_tnt_allowed_methods =
    (NGX_HTTP_POST|NGX_HTTP_GET|NGX_HTTP_PUT|NGX_HTTP_DELETE);

/** Upstream states
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

    /** This is a reference to Tarantool payload data,
     *  e.g. size of TP message
     */
    struct {
        u_char mem[6];
        u_char *p, *e;
    } payload;

    enum ctx_state     state;
    /** in_err - the error buffer
     *  tp_cache - the buffer for store parts of TP message
     */
    ngx_buf_t          *in_err, *tp_cache;

    /** rest - bytes, until transcoding is end
     *  payload_size - the payload, as integer value
     *  rest_batch_size - the number(count), until batch is end
     *  batch_size - the number, parts in batch
     */
    ssize_t            rest, payload_size;
    int                rest_batch_size, batch_size;

    /** The "Greeting" from the Tarantool
     */
    ngx_int_t          greeting:1;

    /** The preset method and its length
     */
    u_char             preset_method[128];
    u_char             preset_method_len;
} ngx_http_tnt_ctx_t;

ngx_http_tnt_ctx_t * ngx_http_tnt_create_ctx(ngx_http_request_t *r);
void ngx_http_tnt_reset_ctx(ngx_http_tnt_ctx_t *ctx);

ngx_int_t ngx_http_tnt_init_handlers(ngx_http_request_t *r,
                                     ngx_http_upstream_t *u,
                                     ngx_http_tnt_loc_conf_t *tlcf);

/** Request handlers [
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

/** Known errors. And, function allow get the know error by type [[
 */
typedef struct ngx_http_tnt_error {
    const ngx_str_t msg;
    int code;
} ngx_http_tnt_error_t;

/** The known error types
 */
enum ngx_http_tnt_err_messages_idx {
    REQUEST_TOO_LARGE   = 0,
    UNKNOWN_PARSE_ERROR = 1,
    HTTP_REQUEST_TOO_LARGE = 2
};

const ngx_http_tnt_error_t *get_error_text(int type);
/** ]]
 */

/** Get size of overhead of JSON protocol
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

#endif /* NGX_HTTP_TNT_CREATE_REQUEST_H_INCLUDED */
