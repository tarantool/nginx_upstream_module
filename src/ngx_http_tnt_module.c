
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


#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_config.h>

#include <debug.h>
#include <tp_ext.h>
#include <tp_transcode.h>
#include <ngx_http_tnt_version.h>


typedef enum ngx_tnt_conf_states {
    NGX_TNT_CONF_ON                  = 1,
    NGX_TNT_CONF_OFF                 = 2,
    NGX_TNT_CONF_PARSE_ARGS          = 4,
    NGX_TNT_CONF_UNESCAPE            = 8,
    NGX_TNT_CONF_PASS_BODY           = 16,
    NGX_TNT_CONF_PASS_HEADERS_OUT    = 32,
    NGX_TNT_CONF_PARSE_URLENCODED    = 64,
    NGX_TNT_CONF_PASS_SUBREQUEST_URI = 128,
} ngx_tnt_conf_states_e;


typedef struct ngx_http_tnt_header_val_s ngx_http_tnt_header_val_t;


typedef ngx_int_t (*ngx_http_set_header_pt)(ngx_http_request_t *r,
    ngx_http_tnt_header_val_t *hv, ngx_str_t *value);


struct ngx_http_tnt_header_val_s {
    ngx_http_complex_value_t   value;
    ngx_str_t                  key;
    ngx_http_set_header_pt     handler;
};


typedef struct ngx_http_tnt_prepared_result {
    ngx_str_t  in;
    ngx_uint_t limit;
    ngx_uint_t offset;
    ngx_uint_t index_id;
    ngx_uint_t space_id;
    ngx_uint_t iter_type;
    ngx_uint_t tuples_count;
    ngx_uint_t update_keys_count;
    ngx_uint_t upsert_tuples_count;
}  ngx_http_tnt_prepared_result_t;


typedef struct ngx_http_tnt_format_value {
    ngx_str_t       name;
    ngx_str_t       value;
    enum tp_type    type;
    ngx_int_t       update_key:1;
    ngx_int_t       upsert_op:1;
} ngx_http_tnt_format_value_t;


typedef struct ngx_http_tnt_next_arg {
  u_char *it, *value;
} ngx_http_tnt_next_arg_t;


/** The structure hold the nginx location variables, e.g. loc_conf.
 */
typedef struct {
    ngx_http_upstream_conf_t upstream;
    ngx_int_t                index;

    size_t                   in_multiplier;
    size_t                   out_multiplier;

    /** Preset method name
     *
     *  If this is set then tp_transcode use only this method name and
     *  tp_transcode will ignore the method name from the json or/and uri
     */
    ngx_http_complex_value_t *method_ccv;
    ngx_str_t method;

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
     *  if incoming HTTP method is in this set, then
     *  the tp_transcode expect the tarantool method name in url,
     *  i.e. HOST/METHOD_NAME/TAIL?ARGS
     *
     *  XXX Also see method
     */
    ngx_uint_t               http_rest_methods;

    /** Set of http methods[default POST|DELETE]
     *
     *  If incoming HTTP method is in this set,
     *  then the tp_transcode expect method name in JSON protocol,
     *  i.e. {"method":STR}
     *
     *  XXX Also see method
     */
    ngx_uint_t               http_methods;

    /** If it is set, then the client will recv. a pure result, e.g. {}
     *  otherwise {"result":[], "id": NUM}id
     */
    ngx_uint_t               pure_result;

    /** Tarantool returns array of array as the result set,
     *  this option will help avoid "array of array" behavior.
     *  For instance.
     *  If this option is set to 2, then the result will: result:{}.
     *  If this option is set to 0, then the result will: result:[[{}]].
     */
    ngx_uint_t                multireturn_skip_count;

    ngx_array_t               *headers;

    /* Format is a feature which allows to convert a query data to MSGPack.
     *
     * Structure of configuration has two parts
     *
     * The first one is the 'values' -- an array for storing variables names
     * and types of expected data.
     *
     * The second one is the *_name -- variables for stroring names of special
     * variables[1].
     *
     * [1] Special variables are limit, offset, space id, index id, iterator
     * type.
     *
     */
    /** enum tp_request_type */
    ngx_uint_t  req_type;

    ngx_uint_t  space_id;
    ngx_uint_t  index_id;

    ngx_uint_t  select_offset;
    ngx_uint_t  select_limit;

    /** Max allowed select per request */
    ngx_uint_t  select_limit_max;

    /**  enum tp_iterator_type */
    ngx_uint_t  iter_type;

    ngx_array_t            *format_values;

    ngx_str_t              limit_name;
    ngx_str_t              offset_name;
    ngx_str_t              iter_type_name;
    ngx_str_t              space_id_name;
    ngx_str_t              index_id_name;

    ngx_array_t            *allowed_spaces;
    ngx_array_t            *allowed_indexes;

} ngx_http_tnt_loc_conf_t;


/** Upstream states
 */
enum ctx_state {
    OK = 0,

    INPUT_JSON_PARSE_FAILED,
    INPUT_TO_LARGE,
    INPUT_EMPTY,
    INPUT_FMT_CANT_READ_INPUT,

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

    /** User defined format and its values
     */
    ngx_array_t *format_values;

} ngx_http_tnt_ctx_t;

/** Struct for stroring human-readable error message
 */
typedef struct ngx_http_tnt_error {
    const ngx_str_t msg;
    int code;
} ngx_http_tnt_error_t;

/** ngx_http_tnt_error index
 */
enum ngx_http_tnt_error_idx {
    REQUEST_TOO_LARGE = 0,
    UNKNOWN_PARSE_ERROR = 1,
    HTTP_REQUEST_TOO_LARGE = 2,
    DML_HANDLER_FMT_ERROR = 3,
    DML_HANDLER_FMT_LIMIT_ERROR = 4,
};

/** Filters */
static ngx_int_t ngx_http_tnt_filter_init(void *data);
static ngx_int_t ngx_http_tnt_send_reply(ngx_http_request_t *r,
        ngx_http_upstream_t *u, ngx_http_tnt_ctx_t *ctx);
static ngx_int_t ngx_http_tnt_filter_reply(ngx_http_request_t *r,
        ngx_http_upstream_t *u, ngx_buf_t *b);
static ngx_int_t ngx_http_tnt_filter(void *data, ssize_t bytes);

/** Nginx handlers */
static ngx_int_t ngx_http_tnt_preconfiguration(ngx_conf_t *cf);
static void *ngx_http_tnt_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_tnt_merge_loc_conf(ngx_conf_t *cf, void *parent,
        void *child);
static char * ngx_http_tnt_method(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf);
static char * ngx_http_tnt_headers_add(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf);
static ngx_int_t ngx_http_tnt_add_header_in(ngx_http_request_t *r,
    ngx_http_tnt_header_val_t *hv, ngx_str_t *value);
static ngx_int_t ngx_http_tnt_process_headers(ngx_http_request_t *r,
        ngx_http_tnt_loc_conf_t *tlcf);
static char *ngx_http_tnt_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_tnt_insert_add(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf);
static ngx_int_t ngx_http_tnt_set_iter_type(ngx_str_t *v,
        ngx_uint_t *iter_type);
static char *ngx_http_tnt_select_add(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf);
static char *ngx_http_tnt_replace_add(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf);
static char *ngx_http_tnt_delete_add(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf);
static char *ngx_http_tnt_update_add(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf);
static char *ngx_http_tnt_upsert_add(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf);
static char *ngx_http_tnt_read_array_of_uint(ngx_pool_t *pool,
        ngx_array_t *arr, ngx_str_t *str, u_char sep);
static ngx_int_t ngx_http_tnt_test_allowed(ngx_array_t *arr, ngx_uint_t with);
static char *ngx_http_tnt_allowed_spaces_add(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf);
static char *ngx_http_tnt_allowed_indexes_add(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf);

/** Ctx */
static ngx_http_tnt_ctx_t *ngx_http_tnt_create_ctx(ngx_http_request_t *r);
static void ngx_http_tnt_reset_ctx(ngx_http_tnt_ctx_t *ctx);

/** Input handlers */
static ngx_int_t ngx_http_tnt_init_handlers(ngx_http_request_t *r,
        ngx_http_upstream_t *u, ngx_http_tnt_loc_conf_t *tlcf);
static ngx_int_t ngx_http_tnt_body_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_tnt_query_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_tnt_dml_handler(ngx_http_request_t *r);

/** Upstream handlers */
static ngx_int_t ngx_http_tnt_reinit_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_tnt_process_header(ngx_http_request_t *r);
static void ngx_http_tnt_abort_request(ngx_http_request_t *r);
static void ngx_http_tnt_finalize_request(ngx_http_request_t *r, ngx_int_t rc);

/** Some helpers */
static ngx_int_t ngx_http_tnt_str_match(ngx_str_t *a, const char *b,
        size_t len);
static ngx_int_t ngx_http_tnt_set_err(ngx_http_request_t *r, int errcode,
        const u_char *msg, size_t msglen);
#define ngx_http_tnt_set_err_str(r, code, str) \
    ngx_http_tnt_set_err((r), (code), (str).data, (str).len)

static const ngx_http_tnt_error_t *ngx_http_tnt_get_error_text(ngx_uint_t type);

static ngx_str_t ngx_http_tnt_urldecode(ngx_http_request_t *r, ngx_str_t *src);
static ngx_int_t ngx_http_tnt_unescape_uri(ngx_http_request_t *r,
        ngx_str_t *dst, ngx_str_t *src);

static ngx_int_t ngx_http_tnt_encode_query_args(ngx_http_request_t *r,
        ngx_http_tnt_loc_conf_t *tlcf, struct tp *tp, ngx_uint_t *args_items);
static ngx_http_tnt_next_arg_t ngx_http_tnt_get_next_arg(u_char *it,
        u_char *end);

static ngx_int_t ngx_http_tnt_wakeup_dying_upstream(ngx_http_request_t *r,
        ngx_chain_t *out_chain);

static ngx_int_t ngx_http_tnt_send_once(ngx_http_request_t *r,
        ngx_http_tnt_ctx_t *ctx, ngx_chain_t *out_chain, const u_char *buf,
        size_t len);

static ngx_int_t ngx_http_tnt_output_err(ngx_http_request_t *r,
        ngx_http_tnt_ctx_t *ctx, ngx_int_t code);

static size_t ngx_http_tnt_overhead(void);

static ngx_buf_t *ngx_http_tnt_create_mem_buf(ngx_http_request_t *r,
        ngx_http_upstream_t *u, size_t size);

static ngx_int_t ngx_http_tnt_output(ngx_http_request_t *r,
        ngx_http_upstream_t *u, ngx_buf_t *b);

/** Format functions.
 *  Functions are existing for helping to conversation between HTTP
 *  and MsgPack.
 */
static ngx_int_t ngx_http_tnt_format_read_input(ngx_http_request_t *r,
        ngx_str_t *dst);
static char *ngx_http_tnt_format_compile(ngx_conf_t *cf,
        ngx_http_tnt_loc_conf_t *conf, ngx_str_t *format);
static ngx_int_t ngx_http_tnt_format_init(ngx_http_tnt_loc_conf_t *conf,
        ngx_http_request_t *r, ngx_http_tnt_prepared_result_t *prepared_result);
static ngx_int_t ngx_http_tnt_format_prepare(ngx_http_tnt_loc_conf_t *conf,
        ngx_http_request_t *r, ngx_http_tnt_prepared_result_t *prepared_result);
static ngx_int_t ngx_http_tnt_format_prepare_kv(ngx_http_request_t *r,
        ngx_http_tnt_prepared_result_t *prepared_result,
        ngx_str_t *key, ngx_str_t *value);
static u_char * ngx_http_tnt_read_next(ngx_str_t *str, u_char sep);
static ngx_int_t ngx_http_tnt_format_bind_bad_request(ngx_http_request_t *r,
        ngx_str_t *name, const char *msg);
static ngx_int_t ngx_http_tnt_format_bind_operation(ngx_http_request_t *r,
        struct tp *tp, ngx_str_t *name, ngx_str_t *val);

static ngx_int_t ngx_http_tnt_format_bind(ngx_http_request_t *r,
        ngx_http_tnt_prepared_result_t *prepared_result, struct tp *tp);

/** Module's objects {{{
 */

/** A set of allowed REST methods */
static const ngx_uint_t ngx_http_tnt_allowed_methods =
    (NGX_HTTP_POST|NGX_HTTP_GET|NGX_HTTP_PUT|NGX_HTTP_PATCH|NGX_HTTP_DELETE);


static ngx_conf_bitmask_t  ngx_http_tnt_next_upstream_masks[] = {
    { ngx_string("error"), NGX_HTTP_UPSTREAM_FT_ERROR },
    { ngx_string("timeout"), NGX_HTTP_UPSTREAM_FT_TIMEOUT },
    { ngx_string("invalid_response"), NGX_HTTP_UPSTREAM_FT_INVALID_HEADER },
    { ngx_string("off"), NGX_HTTP_UPSTREAM_FT_OFF },
    { ngx_null_string, 0 }
};


static ngx_conf_bitmask_t  ngx_http_tnt_pass_http_request_masks[] = {
    { ngx_string("on"), NGX_TNT_CONF_ON },
    { ngx_string("off"), NGX_TNT_CONF_OFF },
    { ngx_string("parse_args"), NGX_TNT_CONF_PARSE_ARGS },
    { ngx_string("unescape"), NGX_TNT_CONF_UNESCAPE },
    { ngx_string("pass_body"), NGX_TNT_CONF_PASS_BODY },
    { ngx_string("pass_headers_out"), NGX_TNT_CONF_PASS_HEADERS_OUT },
    { ngx_string("parse_urlencoded"), NGX_TNT_CONF_PARSE_URLENCODED },
    { ngx_string("pass_subrequest_uri"), NGX_TNT_CONF_PASS_SUBREQUEST_URI },
    { ngx_null_string, 0 }
};


static ngx_conf_bitmask_t  ngx_http_tnt_methods[] = {
    { ngx_string("get"), NGX_HTTP_GET },
    { ngx_string("post"), NGX_HTTP_POST },
    { ngx_string("put"), NGX_HTTP_PUT },
    { ngx_string("patch"), NGX_HTTP_PATCH },
    { ngx_string("delete"), NGX_HTTP_DELETE },
    { ngx_string("all"), (NGX_CONF_BITMASK_SET
                          |NGX_HTTP_GET
                          |NGX_HTTP_POST
                          |NGX_HTTP_PUT
                          |NGX_HTTP_PATCH
                          |NGX_HTTP_DELETE) },
    { ngx_null_string, 0 }
};


static ngx_command_t  ngx_http_tnt_commands[] = {

    { ngx_string("tnt_pass"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_http_tnt_pass,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("tnt_connect_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, upstream.connect_timeout),
      NULL },

    { ngx_string("tnt_send_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, upstream.send_timeout),
      NULL },

    { ngx_string("tnt_read_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, upstream.read_timeout),
      NULL },

    { ngx_string("tnt_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, upstream.buffer_size),
      NULL },

    { ngx_string("tnt_next_upstream"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, upstream.next_upstream),
      &ngx_http_tnt_next_upstream_masks },

    { ngx_string("tnt_next_upstream_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, upstream.next_upstream_timeout),
      NULL },

    { ngx_string("tnt_next_upstream_tries"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, upstream.next_upstream_tries),
      NULL },

    { ngx_string("tnt_in_multiplier"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, in_multiplier),
      NULL },

    { ngx_string("tnt_out_multiplier"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, out_multiplier),
      NULL },

    { ngx_string("tnt_method"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_http_tnt_method,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("tnt_pass_http_request_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, pass_http_request_buffer_size),
      NULL },

    { ngx_string("tnt_pass_http_request"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF
          |NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, pass_http_request),
      &ngx_http_tnt_pass_http_request_masks },

    { ngx_string("tnt_http_rest_methods"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, http_rest_methods),
      &ngx_http_tnt_methods },

    { ngx_string("tnt_pure_result"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, pure_result),
      &ngx_http_tnt_pass_http_request_masks },

    { ngx_string("tnt_multireturn_skip_count"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF
          |NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, multireturn_skip_count),
      NULL },

    { ngx_string("tnt_http_methods"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, http_methods),
      &ngx_http_tnt_methods },

    { ngx_string("tnt_set_header"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_http_tnt_headers_add,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("tnt_insert"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE2,
      ngx_http_tnt_insert_add,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("tnt_select"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE6,
      ngx_http_tnt_select_add,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("tnt_replace"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE2,
      ngx_http_tnt_replace_add,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("tnt_delete"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE3,
      ngx_http_tnt_delete_add,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("tnt_update"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE3,
      ngx_http_tnt_update_add,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("tnt_upsert"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE3,
      ngx_http_tnt_upsert_add,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("tnt_select_limit_max"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, select_limit_max),
      NULL },

    { ngx_string("tnt_allowed_spaces"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_tnt_allowed_spaces_add,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("tnt_allowed_indexes"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_tnt_allowed_indexes_add,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_tnt_module_ctx = {
    ngx_http_tnt_preconfiguration,  /* preconfiguration */
    NULL,                           /* postconfiguration */

    NULL,                           /* create main configuration */
    NULL,                           /* init main configuration */

    NULL,                           /* create server configuration */
    NULL,                           /* merge server configuration */

    ngx_http_tnt_create_loc_conf,   /* create location configuration */
    ngx_http_tnt_merge_loc_conf     /* merge location configuration */
};


ngx_module_t  ngx_http_tnt_module = {
    NGX_MODULE_V1,
    &ngx_http_tnt_module_ctx,   /* module context */
    ngx_http_tnt_commands,      /* module directives */
    NGX_HTTP_MODULE,            /* module type */
    NULL,                       /* init master */
    NULL,                       /* init module */
    NULL,                       /* init process */
    NULL,                       /* init thread */
    NULL,                       /* exit thread */
    NULL,                       /* exit process */
    NULL,                       /* exit master */
    NGX_MODULE_V1_PADDING
};
/** }}}
 */


/** Entry point {{{
 */
static ngx_int_t
ngx_http_tnt_handler(ngx_http_request_t *r)
{
    ngx_int_t               rc;
    ngx_http_upstream_t     *u;
    ngx_http_tnt_loc_conf_t *tlcf;

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_tnt_module);

    if (tlcf->method_ccv != NULL) {

        if (ngx_http_complex_value(r, tlcf->method_ccv, &tlcf->method)
                    != NGX_OK)
        {
            return NGX_ERROR;
        }
    }

    if (ngx_http_tnt_process_headers(r, tlcf) != NGX_OK) {
        return NGX_ERROR;
    }

    if ((!tlcf->method.len && r->uri.len <= 1 /* i.e '/' */) ||
        !(r->method & ngx_http_tnt_allowed_methods))
    {
        return NGX_HTTP_NOT_ALLOWED;
    }

    if (
      /* NOTE https://github.com/tarantool/nginx_upstream_module/issues/43
       */
      !(r->method & tlcf->http_methods) &&
      !(r->method & tlcf->http_rest_methods)
      )
    {
        return NGX_HTTP_NOT_ALLOWED;
    }

    if (ngx_http_set_content_type(r) != NGX_OK
        || ngx_http_upstream_create(r) != NGX_OK)
    {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    u = r->upstream;

    ngx_str_set(&u->schema, "tnt://");
    u->output.tag = (ngx_buf_tag_t) &ngx_http_tnt_module;

    u->conf = &tlcf->upstream;

    rc = ngx_http_tnt_init_handlers(r, u, tlcf);
    if (rc != NGX_OK){
        return rc;
    }

    u->input_filter_init = ngx_http_tnt_filter_init;
    u->input_filter = ngx_http_tnt_filter;
    u->input_filter_ctx = r;

    u->length = 0;
    u->state = 0;

    rc = ngx_http_read_client_request_body(r, ngx_http_upstream_init);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    return NGX_DONE;
}

/** }}}
 */

/** Confs {{{
 */
static ngx_int_t
ngx_http_tnt_preconfiguration(ngx_conf_t *cf)
{
    ngx_conf_log_error(NGX_LOG_NOTICE, cf, 0,
                    "Tarantool upstream module, version: '%s'",
                    NGX_HTTP_TNT_MODULE_VERSION_STRING);
    return NGX_OK;
}


static void *
ngx_http_tnt_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_tnt_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_tnt_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->upstream.bufs.num = 0;
     *     conf->upstream.next_upstream = 0;
     *     conf->upstream.temp_path = NULL;
     *     conf->upstream.uri = { 0, NULL };
     *     conf->upstream.location = NULL;
     *     conf->method = { 0, NULL };
     *     conf->queries = NULL;
     *     ...
     */

    conf->upstream.local = NGX_CONF_UNSET_PTR;

    conf->upstream.connect_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.send_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.read_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.next_upstream_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.next_upstream_tries = NGX_CONF_UNSET;

    conf->upstream.buffer_size = NGX_CONF_UNSET_SIZE;
    conf->in_multiplier = NGX_CONF_UNSET_SIZE;
    conf->out_multiplier = NGX_CONF_UNSET_SIZE;
    conf->multireturn_skip_count = NGX_CONF_UNSET_SIZE;
    conf->pass_http_request_buffer_size = NGX_CONF_UNSET_SIZE;

    conf->req_type = NGX_CONF_UNSET_SIZE;
    conf->iter_type = NGX_CONF_UNSET_SIZE;
    conf->select_limit = NGX_CONF_UNSET_SIZE;
    conf->select_limit_max = NGX_CONF_UNSET_SIZE;
    conf->select_offset = NGX_CONF_UNSET_SIZE;
    conf->space_id = NGX_CONF_UNSET_SIZE;
    conf->index_id = NGX_CONF_UNSET_SIZE;

    /*
     * Hardcoded values
     */
    conf->upstream.cyclic_temp_file = 0;
    conf->upstream.buffering = 0;
    conf->upstream.ignore_client_abort = 0;
    conf->upstream.send_lowat = 0;
    conf->upstream.bufs.num = 0;
    conf->upstream.busy_buffers_size = 0;
    conf->upstream.max_temp_file_size = 0;
    conf->upstream.temp_file_write_size = 0;
    conf->upstream.intercept_errors = 1;
    conf->upstream.intercept_404 = 1;

    conf->upstream.pass_request_headers = 0;
    conf->upstream.pass_request_body = 0;

    conf->index = NGX_CONF_UNSET;

    return conf;
}


static char *
ngx_http_tnt_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_tnt_loc_conf_t *prev = parent;
    ngx_http_tnt_loc_conf_t *conf = child;

    ngx_conf_merge_ptr_value(conf->upstream.local,
                  prev->upstream.local, NULL);
    ngx_conf_merge_msec_value(conf->upstream.connect_timeout,
                  prev->upstream.connect_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.send_timeout,
                  prev->upstream.send_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.read_timeout,
                  prev->upstream.read_timeout, 60000);

    ngx_conf_merge_size_value(conf->upstream.buffer_size,
                  prev->upstream.buffer_size,
                  (size_t) ngx_pagesize);

    ngx_conf_merge_bitmask_value(conf->upstream.next_upstream,
                  prev->upstream.next_upstream,
                              (NGX_CONF_BITMASK_SET
                               |NGX_HTTP_UPSTREAM_FT_ERROR
                               |NGX_HTTP_UPSTREAM_FT_TIMEOUT));

    if (conf->upstream.next_upstream & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream.next_upstream = NGX_CONF_BITMASK_SET
                                       |NGX_HTTP_UPSTREAM_FT_OFF;
    }

    if (conf->upstream.upstream == NULL) {
        conf->upstream.upstream = prev->upstream.upstream;
    }

    ngx_conf_merge_size_value(conf->in_multiplier, prev->in_multiplier, 2);

    ngx_conf_merge_size_value(conf->out_multiplier, prev->out_multiplier, 2);

    if (conf->method_ccv == NULL) {
        conf->method_ccv = prev->method_ccv;
    }

    ngx_conf_merge_size_value(conf->pass_http_request_buffer_size,
                  prev->pass_http_request_buffer_size, 4096*2);

    ngx_conf_merge_bitmask_value(conf->pass_http_request,
                  prev->pass_http_request, NGX_TNT_CONF_OFF);

    ngx_conf_merge_bitmask_value(conf->http_rest_methods,
                  prev->http_rest_methods,
                              (NGX_HTTP_GET
                               |NGX_HTTP_PUT));

    ngx_conf_merge_bitmask_value(conf->http_methods,
                  prev->http_methods,
                              (NGX_HTTP_POST
                               |NGX_HTTP_DELETE));

    ngx_conf_merge_bitmask_value(conf->pure_result, prev->pure_result,
                NGX_TNT_CONF_OFF);

    ngx_conf_merge_size_value(conf->multireturn_skip_count,
                  prev->multireturn_skip_count, 0);

    if (conf->headers == NULL) {
        conf->headers = prev->headers;
    }

    ngx_conf_merge_uint_value(conf->req_type, prev->req_type, 0);
    ngx_conf_merge_uint_value(conf->space_id, prev->space_id, 513);
    ngx_conf_merge_uint_value(conf->index_id, prev->index_id, 0);
    ngx_conf_merge_uint_value(conf->select_offset, prev->select_offset, 0);
    ngx_conf_merge_uint_value(conf->select_limit, prev->select_limit, 0);
    ngx_conf_merge_uint_value(conf->select_limit_max, prev->select_limit_max,
            100);
    ngx_conf_merge_uint_value(conf->iter_type, prev->iter_type,
            (ngx_uint_t) TP_ITERATOR_EQ);

    if (conf->allowed_spaces == NULL) {
        conf->allowed_spaces = prev->allowed_spaces;
    }

    if (conf->allowed_indexes == NULL) {
        conf->allowed_indexes = prev->allowed_indexes;
    }

    if (conf->format_values == NULL) {
        conf->format_values = prev->format_values;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_tnt_method(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_tnt_loc_conf_t *tlcf = conf;

    ngx_http_compile_complex_value_t   ccv;
    ngx_str_t                          *value;
    ngx_http_complex_value_t           cv;

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));
    value = cf->args->elts;

    ccv.cf = cf;
    ccv.value = &value[1];
    ccv.complex_value = &cv;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    tlcf->method_ccv = ngx_palloc(cf->pool, sizeof(ngx_http_complex_value_t));
    if (tlcf->method_ccv == NULL) {
        return NGX_CONF_ERROR;
    }

    *tlcf->method_ccv = cv;

    return NGX_CONF_OK;
}


static char *
ngx_http_tnt_headers_add(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_tnt_loc_conf_t *tlcf = conf;

    ngx_str_t                          *value;
    ngx_http_tnt_header_val_t          *hv;
    ngx_http_compile_complex_value_t   ccv;

    value = cf->args->elts;

    if (tlcf->headers == NULL) {

        tlcf->headers = ngx_array_create(cf->pool, 1,
                                        sizeof(ngx_http_tnt_header_val_t));
        if (tlcf->headers == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    hv = ngx_array_push(tlcf->headers);
    if (hv == NULL) {
        return NGX_CONF_ERROR;
    }

    hv->key = value[1];
    hv->handler = ngx_http_tnt_add_header_in;

    if (value[2].len == 0) {
        ngx_memzero(&hv->value, sizeof(ngx_http_complex_value_t));

    } else {
        ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

        ccv.cf = cf;
        ccv.value = &value[2];
        ccv.complex_value = &hv->value;

        if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

    if (cf->args->nelts == 3) {
        return NGX_CONF_OK;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_tnt_insert_add(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_tnt_loc_conf_t *tlcf = conf;

    ngx_str_t *value;
    ngx_int_t tmp;

    if (tlcf->req_type != 0 && tlcf->req_type != NGX_CONF_UNSET_SIZE) {
        return "is duplicate";
    }

    tlcf->req_type = (ngx_uint_t) TP_INSERT;

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "off") == 0) {
        tlcf->space_id = 0;
    }
    else {

        tmp = ngx_atoi(value[1].data, value[1].len);
        if (tmp < 0) {
            return "space id should be non negative number";
        }
        tlcf->space_id = (ngx_uint_t) tmp;
    }
    return ngx_http_tnt_format_compile(cf, tlcf, &value[2]);
}


static ngx_int_t
ngx_http_tnt_set_iter_type(ngx_str_t *v, ngx_uint_t *iter_type)
{
    if (ngx_strncmp(v->data, "eq", sizeof("eq") - 1) == 0) {
        *iter_type = (ngx_uint_t) TP_ITERATOR_EQ;
    } else if (ngx_strncmp(v->data, "req", sizeof("req") - 1) == 0) {
        *iter_type = (ngx_uint_t) TP_ITERATOR_REQ;
    } else if (ngx_strncmp(v->data, "all", sizeof("all") - 1) == 0) {
        *iter_type = (ngx_uint_t) TP_ITERATOR_ALL;
    } else if (ngx_strncmp(v->data, "lt", sizeof("lt") - 1) == 0) {
        *iter_type = (ngx_uint_t) TP_ITERATOR_LT;
    } else if (ngx_strncmp(v->data, "le", sizeof("le") - 1) == 0) {
        *iter_type = (ngx_uint_t) TP_ITERATOR_LE;
    } else if (ngx_strncmp(v->data, "ge", sizeof("ge") - 1) == 0) {
        *iter_type = (ngx_uint_t) TP_ITERATOR_GE;
    } else if (ngx_strncmp(v->data, "gt", sizeof("gt") - 1) == 0) {
        *iter_type = (ngx_uint_t) TP_ITERATOR_GT;
    } else if (ngx_strncmp(v->data, "all_set", sizeof("all_set") - 1) == 0) {
        *iter_type = (ngx_uint_t) TP_ITERATOR_BITS_ALL_SET;
    } else if (ngx_strncmp(v->data, "any_set", sizeof("any_set") - 1) == 0) {
        *iter_type = (ngx_uint_t) TP_ITERATOR_BITS_ANY_SET;
    } else if (ngx_strncmp(v->data, "all_non_set",
                    sizeof("all_non_set") - 1) == 0) {
        *iter_type = (ngx_uint_t) TP_ITERATOR_BITS_ALL_NON_SET;
    } else if (ngx_strncmp(v->data, "overlaps", sizeof("overlaps") - 1) == 0) {
        *iter_type = (ngx_uint_t) TP_ITERATOR_OVERLAPS;
    } else if (ngx_strncmp(v->data, "neighbor", sizeof("neighbor") - 1) == 0) {
        *iter_type = (ngx_uint_t) TP_ITERATOR_NEIGHBOR;
    } else {
        return NGX_ERROR;
    }
    return NGX_OK;
}


static char *
ngx_http_tnt_select_add(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_tnt_loc_conf_t *tlcf = conf;

    ngx_int_t tmp;
    ngx_str_t *value;

    if (tlcf->req_type != 0 && tlcf->req_type != NGX_CONF_UNSET_SIZE) {
        return "is duplicate";
    }

    tlcf->req_type = (ngx_uint_t) TP_SELECT;

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "off") == 0) {
        tlcf->space_id = 0;
    } else {

        tmp = ngx_atoi(value[1].data, value[1].len);
        if (tmp < 0) {
            return "space id should be non negative number";
        }
        tlcf->space_id = (ngx_uint_t) tmp;
    }

    if (ngx_strcmp(value[2].data, "off") == 0) {
        tlcf->index_id = 0;
    } else {

        tmp = ngx_atoi(value[2].data, value[2].len);
        if (tmp < 0) {
            return "index id should be non negative number";
        }
        tlcf->index_id = (ngx_uint_t) tmp;
    }

    tmp = ngx_atoi(value[3].data, value[3].len);
    if (tmp < 0) {
        return "select offset should be non negative number";
    }
    tlcf->select_offset = (ngx_uint_t) tmp;


    tmp = ngx_atoi(value[4].data, value[4].len);
    if (tmp < 0) {
        return "select limit should be non negative number";
    }
    tlcf->select_limit = (ngx_uint_t) tmp;

    if (ngx_http_tnt_set_iter_type(&value[5], &tlcf->iter_type) != NGX_OK) {
        return "unknown iterator type, allowed "
                "eq,req,all,lt,le,ge,gt,all_set,any_set,"
                "all_non_set,overlaps,neighbor";
    }

    return ngx_http_tnt_format_compile(cf, tlcf, &value[6]);

}


static char *
ngx_http_tnt_replace_add(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_tnt_loc_conf_t *tlcf = conf;

    ngx_int_t tmp;
    ngx_str_t *value;

    if (tlcf->req_type != 0 && tlcf->req_type != NGX_CONF_UNSET_SIZE) {
        return "is duplicate";
    }

    tlcf->req_type = (ngx_uint_t) TP_REPLACE;

    value = cf->args->elts;
    if (ngx_strcmp(value[1].data, "off") == 0) {
        tlcf->space_id = 0;
    } else {

        tmp = ngx_atoi(value[1].data, value[1].len);
        if (tmp < 0) {
            return "space id should be non negative number";
        }
        tlcf->space_id = (ngx_uint_t) tmp;
    }

    return ngx_http_tnt_format_compile(cf, tlcf, &value[2]);
}


static char *
ngx_http_tnt_delete_add(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_tnt_loc_conf_t *tlcf = conf;

    ngx_int_t tmp;
    ngx_str_t *value;

    if (tlcf->req_type != 0 && tlcf->req_type != NGX_CONF_UNSET_SIZE) {
        return "is duplicate";
    }

    tlcf->req_type = (ngx_uint_t) TP_DELETE;

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "off") == 0) {
        tlcf->space_id = 0;
    } else {

        tmp = ngx_atoi(value[1].data, value[1].len);
        if (tmp < 0) {
            return "space id should be non negative number";
        }
        tlcf->space_id = (ngx_uint_t) tmp;
    }

    if (ngx_strcmp(value[2].data, "off") == 0) {
        tlcf->index_id = 0;
    } else {

        tmp = ngx_atoi(value[2].data, value[2].len);
        if (tmp < 0) {
            return "index id should be non negative number";
        }
        tlcf->index_id = (ngx_uint_t) tmp;
    }

    return ngx_http_tnt_format_compile(cf, tlcf, &value[3]);
}


static char *
ngx_http_tnt_update_add(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_tnt_loc_conf_t *tlcf = conf;

    ngx_int_t tmp;
    ngx_str_t *value;
    char      *ok;

    if (tlcf->req_type != 0 && tlcf->req_type != NGX_CONF_UNSET_SIZE) {
        return "is duplicate";
    }

    tlcf->req_type = (ngx_uint_t) TP_UPDATE;

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "off") == 0) {
        tlcf->space_id = 0;
    } else {

        tmp = ngx_atoi(value[1].data, value[1].len);
        if (tmp < 0) {
            return "space id should be non negative number";
        }
        tlcf->space_id = (ngx_uint_t) tmp;
    }

    /** Keys  */
    ok = ngx_http_tnt_format_compile(cf, tlcf, &value[2]);
    if (ok != NGX_CONF_OK) {
        return ok;
    }

    /** Tuples */
    return ngx_http_tnt_format_compile(cf, tlcf, &value[3]);
}


static char *
ngx_http_tnt_upsert_add(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_tnt_loc_conf_t *tlcf = conf;

    ngx_int_t tmp;
    ngx_str_t *value;
    char      *ok;

    if (tlcf->req_type != 0 && tlcf->req_type != NGX_CONF_UNSET_SIZE) {
        return "is duplicate";
    }

    tlcf->req_type = (ngx_uint_t) TP_UPSERT;

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "off") == 0) {
        tlcf->space_id = 0;
    } else {

        tmp = ngx_atoi(value[1].data, value[1].len);
        if (tmp < 0) {
            return "space id should be non negative number";
        }
        tlcf->space_id = (ngx_uint_t) tmp;
    }

    /** Tuples */
    ok = ngx_http_tnt_format_compile(cf, tlcf, &value[2]);
    if (ok != NGX_CONF_OK) {
        return ok;
    }

    /** Operations */
    return ngx_http_tnt_format_compile(cf, tlcf, &value[3]);
}


static char *
ngx_http_tnt_read_array_of_uint(ngx_pool_t *pool, ngx_array_t *arr,
        ngx_str_t *str, u_char sep)
{
    ngx_uint_t  *val;
    u_char      *p, *e, *it;

    p = str->data;
    e = str->data + str->len;

    for (it = p; p < e; ++p) {

        if (*p == ',') {
            val = ngx_array_push(arr);
            if (val == NULL) {
                return NGX_CONF_ERROR;
            }
            *val = (ngx_uint_t) atoi((const char *) it);
            it = p + 1;
        }
    }

    if (it && it > e) {
        val = ngx_array_push(arr);
        if (val == NULL) {
            return NGX_CONF_ERROR;
        }
        *val = (ngx_uint_t) atoi((const char *) it);
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_tnt_test_allowed(ngx_array_t *arr, ngx_uint_t val)
{
    ngx_uint_t  i;
    ngx_uint_t  *arr_val;

    if (arr == NULL) {
        return NGX_OK;
    }

    arr_val = arr->elts;

    for (i = 0; i < arr->nelts; i++) {

        if (arr_val[i] == val) {
            return NGX_OK;
        }
    }

    return NGX_HTTP_NOT_ALLOWED;
}


static char *
ngx_http_tnt_allowed_spaces_add(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_tnt_loc_conf_t *tlcf = conf;

    ngx_str_t  *value;

    value = cf->args->elts;

    if (tlcf->allowed_spaces == NULL) {
        tlcf->allowed_spaces = ngx_array_create(cf->pool, 1, sizeof(ngx_uint_t));
        if (tlcf->allowed_spaces == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    return ngx_http_tnt_read_array_of_uint(cf->pool, tlcf->allowed_spaces,
            &value[1], ',');
}


static char *
ngx_http_tnt_allowed_indexes_add(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf)
{
    ngx_http_tnt_loc_conf_t *tlcf = conf;

    ngx_str_t  *value;

    value = cf->args->elts;

    if (tlcf->allowed_indexes == NULL) {
        tlcf->allowed_indexes = ngx_array_create(cf->pool, 1,
                sizeof(ngx_uint_t));
        if (tlcf->allowed_indexes == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    return ngx_http_tnt_read_array_of_uint(cf->pool, tlcf->allowed_indexes,
            &value[1], ',');
}


static ngx_int_t
ngx_http_tnt_format_read_input(ngx_http_request_t *r, ngx_str_t *dst)
{
    ngx_int_t           rc;
    ngx_buf_t           *b;
    ngx_chain_t         *body;
    ngx_buf_t           unparsed_body;
    ngx_str_t           tmp;

    tmp = r->args;

    if (r->headers_in.content_length_n > 0) {

        unparsed_body.pos = ngx_pnalloc(r->pool,
            sizeof(u_char) * r->headers_in.content_length_n + 1);

        if (unparsed_body.pos == NULL) {
            return NGX_ERROR;
        }

        unparsed_body.last = unparsed_body.pos;
        unparsed_body.start = unparsed_body.pos;
        unparsed_body.end = unparsed_body.pos + r->headers_in.content_length_n + 1;

        for (body = r->upstream->request_bufs; body; body = body->next) {

            b = body->buf;

                if (b->in_file) {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                        "in-file buffer found. aborted. "
                        "consider increasing your 'client_body_buffer_size' "
                        "setting");
                    return NGX_ERROR;
                }

            unparsed_body.last = ngx_copy(unparsed_body.last,
                b->pos, b->last - b->pos);
        }

        tmp.data = unparsed_body.start;
        tmp.len = unparsed_body.last - unparsed_body.start;
    }

    rc = ngx_http_tnt_unescape_uri(r, dst, &tmp);
    if (rc != NGX_OK) {
        return rc;
    }

    return NGX_OK;
}


static char *
ngx_http_tnt_format_compile(ngx_conf_t *cf, ngx_http_tnt_loc_conf_t *conf,
        ngx_str_t *format)
{
    u_char                      *name, *type;
    ngx_uint_t                  i;
    ngx_http_tnt_format_value_t fmt_val, *val;

    if (conf->format_values == NULL) {

        conf->format_values = ngx_array_create(cf->pool, 1,
                sizeof(ngx_http_tnt_format_value_t));

        if (conf->format_values == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    name = NULL;
    type = NULL;

    for (i = 0; i < format->len; ++i) {

        if (name == NULL) {
            name = format->data + i;
        }

        if (format->data[i] == '=' && type == NULL) {
            type = format->data + i + 1;
        }

        if (format->data[i] == '&' || format->data[i] == ','
                || i == format->len - 1)
        {
            fmt_val.name.data = name;
            fmt_val.name.len = (ngx_uint_t) (type - name);

            name = NULL;

            /** Spec. values */
            if (ngx_strncmp(type, "%%lim", sizeof("%%lim") - 1) == 0) {
                conf->limit_name = fmt_val.name;
            } else if (
                    ngx_strncmp(type, "%%off", sizeof("%%off") - 1) == 0) {

                conf->offset_name = fmt_val.name;
            } else if (ngx_strncmp(type, "%%it", sizeof("%%it") - 1) == 0) {

                conf->iter_type_name = fmt_val.name;
            } else if (
                ngx_strncmp(type, "%%space_id", sizeof("%%space_id") - 1) == 0) {
                conf->space_id_name = fmt_val.name;
            } else if (
                ngx_strncmp(type, "%%idx_id", sizeof("%%idx_id") - 1) == 0) {

                conf->index_id_name = fmt_val.name;
            /** Tuple */
            } else {

                val = ngx_array_push(conf->format_values);
                if (val == NULL) {
                    return NGX_CONF_ERROR;
                }

                val->name = fmt_val.name;
                val->value.len = 0;

                if (ngx_strncmp(type, "%n", sizeof("%n") - 1) == 0) {
                    val->type = TP_INT;
                } else if (ngx_strncmp(type, "%d", sizeof("%d") - 1) == 0) {
                    val->type = TP_DOUBLE;
                } else if (ngx_strncmp(type, "%f", sizeof("%f") - 1) == 0) {
                    val->type = TP_FLOAT;
                } else if (ngx_strncmp(type, "%s", sizeof("%s") - 1) == 0) {
                    val->type = TP_STR;
                } else if (ngx_strncmp(type, "%b", sizeof("%b") - 1) == 0) {
                    val->type = TP_BOOL;

                /* If a type has a prefix '%k' then it is a key for TP_UPDATE
                 */
                } else if (conf->req_type == TP_UPDATE) {

                    if (ngx_strncmp(type, "%kn", sizeof("%kn") - 1) == 0) {
                        val->type = TP_INT;
                        val->update_key = 1;
                    } else if (ngx_strncmp(type, "%kd", sizeof("%kd") - 1) == 0) {
                        val->type = TP_DOUBLE;
                        val->update_key = 1;
                    } else if (ngx_strncmp(type, "%kf", sizeof("%kf") - 1) == 0) {
                        val->type = TP_FLOAT;
                        val->update_key = 1;
                    } else if (ngx_strncmp(type, "%ks", sizeof("%ks") - 1) == 0) {
                        val->type = TP_STR;
                        val->update_key = 1;
                    } else if (ngx_strncmp(type, "%kb", sizeof("%kb") - 1) == 0) {
                        val->type = TP_BOOL;
                        val->update_key = 1;
                    } else {
                        goto unknown_format_error;
                    }
                }
                /* If a type has a prefix '%o' then it is an operation for
                 * TP_UPDATE
                 */
                else if (conf->req_type == TP_UPSERT) {

                    if (ngx_strncmp(type, "%on", sizeof("%on") - 1) == 0) {
                        val->type = TP_INT;
                        val->upsert_op = 1;
                    } else if (ngx_strncmp(type, "%od", sizeof("%od") - 1) == 0) {
                        val->type = TP_DOUBLE;
                        val->upsert_op = 1;
                    } else if (ngx_strncmp(type, "%of", sizeof("%of") - 1) == 0) {
                        val->type = TP_FLOAT;
                        val->upsert_op = 1;
                    } else if (ngx_strncmp(type, "%os", sizeof("%os") - 1) == 0) {
                        val->type = TP_STR;
                        val->upsert_op = 1;
                    } else if (ngx_strncmp(type, "%ob", sizeof("%ob") - 1) == 0) {
                        val->type = TP_BOOL;
                        val->upsert_op = 1;
                    } else {
                        goto unknown_format_error;
                    }

                } else {
                    goto unknown_format_error;
                }
            }

            type = NULL;
        }
    }

    return NGX_CONF_OK;

unknown_format_error:
    return "unknown format has been found, "
        "allowed %n,%d,%f,%s,%b,%lim,%off,%it,"
        "%space_id,%idx_id,%kn,%kd,%kf,%ks";
}


static ngx_int_t
ngx_http_tnt_tolower(int c)
{
    if (c >= 'A' && c <= 'Z') {
        c ^= 0x20;
    }
    return c;
}


static ngx_str_t /* dst */
ngx_http_tnt_urldecode(ngx_http_request_t *r, ngx_str_t *src)
{
    ngx_uint_t s;
    u_char     c;
    ngx_str_t  dst;

    dst.len = 0;
    dst.data = ngx_pnalloc(r->pool, src->len);
    if (dst.data == NULL) {
        return dst;
    }

    s = 0;
    while (s < src->len) {

        c = src->data[s++];

        if (c == '%' && (ngx_uint_t) (s + 2) <= src->len) {

            u_char c2 = src->data[s++];
            u_char c3 = src->data[s++];

            if (isxdigit(c2) && isxdigit(c3)) {

                c2 = ngx_http_tnt_tolower(c2);
                c3 = ngx_http_tnt_tolower(c3);

                if (c2 <= '9') {
                    c2 = c2 - '0';
                } else {
                    c2 = c2 - 'a' + 10;
                }

                if (c3 <= '9') {
                    c3 = c3 - '0';
                } else {
                    c3 = c3 - 'a' + 10;
                }

                dst.data[dst.len++] = 16 * c2 + c3;

            } else { /* %zz or something other invalid */
                dst.data[dst.len++] = c;
                dst.data[dst.len++] = c2;
                dst.data[dst.len++] = c3;
            }

        } else if (c == '+') {
            dst.data[dst.len++] = ' ';
        } else {
            dst.data[dst.len++] = c;
        }
    }

    return dst;
}


static ngx_int_t
ngx_http_tnt_unescape_uri(ngx_http_request_t *r, ngx_str_t *dst,
        ngx_str_t *src)
{
    dst->data = NULL;
    dst->len = 0;

    *dst = ngx_http_tnt_urldecode(r, src);

    if (dst->data == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_format_init(ngx_http_tnt_loc_conf_t *conf,
        ngx_http_request_t *r, ngx_http_tnt_prepared_result_t *prepared_result)
{
    ngx_int_t                   rc;
    ngx_uint_t                  i;
    ngx_http_tnt_ctx_t          *ctx;
    ngx_http_tnt_format_value_t *fmt_val, *conf_fmt_val;

    ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);

    memset(prepared_result, 0, sizeof(ngx_http_tnt_prepared_result_t));

    prepared_result->space_id = conf->space_id;
    prepared_result->index_id = conf->index_id;
    prepared_result->limit = conf->select_limit;
    prepared_result->offset = conf->select_offset;
    prepared_result->iter_type = conf->iter_type;

    if (conf->format_values == NULL) {
        ctx->format_values = NULL;
        return NGX_OK;
    }

    ctx->format_values = ngx_array_create(r->connection->pool, 1,
                        sizeof(ngx_http_tnt_format_value_t));
    if (ctx->format_values == NULL) {
        return NGX_ERROR;
    }

    rc = ngx_http_tnt_format_read_input(r, &prepared_result->in);
    if (rc != NGX_OK) {
        return rc;
    }

    conf_fmt_val = conf->format_values->elts;

    for (i = 0; i < conf->format_values->nelts; ++i) {

        fmt_val = ngx_array_push(ctx->format_values);
        if (fmt_val == NULL) {
            return NGX_ERROR;
        }

        fmt_val->name = conf_fmt_val[i].name;
        fmt_val->type = conf_fmt_val[i].type;
        fmt_val->value.data = NULL;
        fmt_val->value.len = 0;
        fmt_val->update_key = conf_fmt_val[i].update_key;
        fmt_val->upsert_op = conf_fmt_val[i].upsert_op;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_format_prepare(ngx_http_tnt_loc_conf_t *conf,
        ngx_http_request_t *r, ngx_http_tnt_prepared_result_t *prepared_result)
{
    enum {
        NOTHING = 0,
        EXPECTS_LIMIT = 2,
        EXPECTS_OFFSET = 4,
        EXPECTS_ITER_TYPE = 8,
        EXPECTS_SPACE_ID = 16,
        EXPECTS_INDEX_ID = 32,
    };

    u_char                     *arg_begin, *end;
    ngx_http_tnt_next_arg_t    arg;
    ngx_str_t                  key, value;
    ngx_int_t                  tmp;
    ngx_int_t                  expects;
    ngx_int_t                  rc;
    ngx_http_tnt_loc_conf_t    *tlcf;

    expects = NOTHING;
    arg.it = prepared_result->in.data;
    arg_begin = arg.it;
    end = arg.it + prepared_result->in.len;
    arg.value = NULL;

    if (conf->limit_name.len != 0) {
        expects |= EXPECTS_LIMIT;
    }

    if (conf->offset_name.len != 0) {
        expects |= EXPECTS_OFFSET;
    }

    if (conf->iter_type_name.len != 0) {
        expects |= EXPECTS_ITER_TYPE;
    }

    if (conf->space_id_name.len != 0) {
        expects |= EXPECTS_SPACE_ID;
    }

    if (conf->index_id_name.len != 0) {
        expects |= EXPECTS_INDEX_ID;
    }

    for (; arg.it < end; ) {


        arg = ngx_http_tnt_get_next_arg(arg.it, end);

        if (arg.value != NULL) {

            key.data = arg_begin;
            key.len = arg.value - arg_begin;

            value.data = arg.value;
            value.len = arg.it - arg.value;

            if (expects & EXPECTS_LIMIT &&
                    key.len == conf->limit_name.len &&
                    ngx_strncmp(key.data, conf->limit_name.data,
                                conf->limit_name.len) == 0)
            {
                prepared_result->limit = conf->select_limit_max;
                tmp = ngx_atoi(value.data, value.len);

                if (tmp >= 0) {

                    if ((ngx_uint_t) tmp > conf->select_limit_max) {
                        goto not_allowed;
                    }

                    prepared_result->limit = (ngx_uint_t) tmp;

                    expects &= ~EXPECTS_LIMIT;
                }
            }
            else if (expects & EXPECTS_OFFSET &&
                    key.len == conf->offset_name.len &&
                    ngx_strncmp(key.data, conf->offset_name.data,
                                conf->offset_name.len) == 0)
            {
                tmp = ngx_atoi(value.data, value.len);
                if (tmp >= 0) {
                    prepared_result->offset = (ngx_uint_t) tmp;
                    expects &= ~EXPECTS_OFFSET;
                }
            }
            else if (expects & EXPECTS_ITER_TYPE &&
                    key.len == conf->iter_type_name.len &&
                    ngx_strncmp(key.data, conf->iter_type_name.data,
                                conf->iter_type_name.len) == 0)
            {
                if (ngx_http_tnt_set_iter_type(&value, (ngx_uint_t *) &tmp) == NGX_OK) {
                    prepared_result->iter_type = (ngx_uint_t) tmp;
                    expects &= ~EXPECTS_ITER_TYPE;
                }
            }
            else if (expects & EXPECTS_SPACE_ID &&
                    key.len == conf->space_id_name.len &&
                    ngx_strncmp(key.data, conf->space_id_name.data,
                                conf->space_id_name.len) == 0)
            {
                tmp = ngx_atoi(value.data, value.len);
                if (tmp >= 0) {
                    prepared_result->space_id = (ngx_uint_t) tmp;
                    expects &= ~EXPECTS_SPACE_ID;
                }
            }
            else if (expects & EXPECTS_INDEX_ID &&
                    key.len == conf->index_id_name.len &&
                    ngx_strncmp(key.data, conf->index_id_name.data,
                                conf->index_id_name.len) == 0)
            {
                tmp = ngx_atoi(value.data, value.len);
                if (tmp >= 0) {
                    prepared_result->index_id = (ngx_uint_t) tmp;
                    expects &= ~EXPECTS_INDEX_ID;
                }
            }
            else {

                rc = ngx_http_tnt_format_prepare_kv(r, prepared_result, &key,
                        &value);

                if (rc != NGX_OK) {
                    return rc;
                }
            }

        }

        arg_begin = ++arg.it;
    }

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_tnt_module);

    rc = ngx_http_tnt_test_allowed(tlcf->allowed_spaces,
            prepared_result->space_id);
    if (rc != NGX_OK) {
        goto not_allowed;
    }

    rc = ngx_http_tnt_test_allowed(tlcf->allowed_indexes,
            prepared_result->index_id);
    if (rc != NGX_OK) {
        goto not_allowed;
    }

    if (expects == NOTHING) {
        return NGX_OK;
    }

    rc = ngx_http_tnt_set_err_str(r, NGX_HTTP_BAD_REQUEST,
                ngx_http_tnt_get_error_text(DML_HANDLER_FMT_ERROR)->msg);
    if (rc != NGX_OK) {
        return rc;
    }

    return NGX_HTTP_BAD_REQUEST;

not_allowed:

    rc = ngx_http_tnt_set_err_str(r, NGX_HTTP_NOT_ALLOWED,
                ngx_http_tnt_get_error_text(
                        DML_HANDLER_FMT_LIMIT_ERROR)->msg);
    if (rc != NGX_OK) {
        return rc;
    }

    return NGX_HTTP_NOT_ALLOWED;
}


static ngx_int_t
ngx_http_tnt_format_prepare_kv(ngx_http_request_t *r,
        ngx_http_tnt_prepared_result_t *prepared_result,
        ngx_str_t *key, ngx_str_t *value)
{
    ngx_uint_t                   i;
    ngx_http_tnt_format_value_t  *fmt_val;
    ngx_http_tnt_ctx_t           *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);

    if (ctx->format_values == NULL) {
        return NGX_OK;
    }
    if (value->len == 0 && value->data == NULL) {
        return NGX_OK;
    }

    fmt_val = ctx->format_values->elts;

    for (i = 0; i < ctx->format_values->nelts; i++) {

        if (fmt_val[i].value.len != 0 || key->len != fmt_val[i].name.len) {
            continue;
        }

        if (ngx_strncmp(key->data, fmt_val[i].name.data,
                    fmt_val[i].name.len) == 0) {

            fmt_val[i].value = *value;

            if (fmt_val[i].update_key) {
                ++prepared_result->update_keys_count;

            } else if (fmt_val[i].upsert_op) {
                ++prepared_result->upsert_tuples_count;

            } else {

                ++prepared_result->tuples_count;
            }

            break;
        }
    }

    return NGX_OK;
}


static u_char *
ngx_http_tnt_read_next(ngx_str_t *str, u_char sep)
{
    u_char *beg = str->data;
    u_char *end = str->data + str->len;

    for (; str->data < end; ++str->data, --str->len) {

        if (*str->data == sep) {
            ++str->data;
            --str->len;
            return beg;
        }
    }

    return NULL;
}

static ngx_int_t
ngx_http_tnt_format_bind_bad_request(ngx_http_request_t *r, ngx_str_t *name,
        const char *msg)
{
    ngx_int_t  rc;
    u_char     err_msg[64];

    if (msg == NULL) {
        snprintf((char *) err_msg, sizeof(err_msg), "'%.*s' is invalid",
                (int) name->len, (char *) name->data);
    } else {
         snprintf((char *) err_msg, sizeof(err_msg), "'%.*s' is invalid. %s",
                (int) name->len, (char *) name->data, msg);
    }

    rc = ngx_http_tnt_set_err(r, NGX_HTTP_BAD_REQUEST, err_msg,
            ngx_strlen(err_msg));
    if (rc != NGX_OK) {
        return rc;
    }

    return NGX_HTTP_BAD_REQUEST;
}


static ngx_int_t
ngx_http_tnt_format_bind_operation(ngx_http_request_t *r, struct tp *tp,
        ngx_str_t *name, ngx_str_t *val)
{
    u_char      *update_operation, *filedno_pt, *filedno_pt_end;
    ngx_int_t   filedno;

    update_operation = ngx_http_tnt_read_next(val, ',');

    if (update_operation == NULL) {
        goto no_operation_type;
    }

    switch (*update_operation) {
    case '=':
    case '!':
    case '+':
    case '-':
    case '&':
    case '^':
    case '|':
    case '#':
        break;
    default:
        goto no_operation_type;
    }

    filedno_pt = ngx_http_tnt_read_next(val, ',');
    filedno_pt_end = (val->data - 1);

    if (filedno_pt == NULL || (size_t) (filedno_pt_end - filedno_pt) == 0) {
        goto no_fieldno;
    }

    filedno = ngx_atoi(filedno_pt, (size_t) (filedno_pt_end - filedno_pt));
    if (filedno < 0 || filedno - 1 < 0) {
        goto no_fieldno;
    }
    filedno -= 1;

    if (tp_op(tp, *update_operation, (uint32_t) filedno) == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;

no_operation_type:
    return ngx_http_tnt_format_bind_bad_request( r, name,
            "Operation type is expecting");

no_fieldno:
    return ngx_http_tnt_format_bind_bad_request( r, name,
            "Fieldno is expecting");
}


static ngx_int_t
ngx_http_tnt_format_bind(ngx_http_request_t *r,
        ngx_http_tnt_prepared_result_t *prepared_result, struct tp *tp)
{
    ngx_int_t                    rc;
    ngx_str_t                    *value;
    ngx_uint_t                   i;
    ngx_http_tnt_format_value_t  *fmt_val;
    ngx_http_tnt_ctx_t           *ctx;
    ngx_http_tnt_loc_conf_t      *tlcf;
    ngx_int_t                    update_started;
    ngx_int_t                    upsert_add_ops_started;
    char                         num_buf[16];

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_tnt_module);

    ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);

    if (ctx->format_values == NULL) {
        return NGX_OK;
    }

    update_started = 0;
    upsert_add_ops_started = 0;

    fmt_val = ctx->format_values->elts;

    for (i = 0; i < ctx->format_values->nelts; i++) {

        value = &fmt_val[i].value;

        if (value->len == 0 && value->data == NULL) {
            continue;
        }

        /** Update {{{ */
        if (tlcf->req_type == TP_UPDATE) {

            if (!update_started && !fmt_val[i].update_key) {

                if (tp_updatebegin(tp,
                        (uint32_t) prepared_result->tuples_count)
                            == NULL)
                {
                    goto oom;
                }

                update_started = 1;
            }

            if (update_started) {

                rc = ngx_http_tnt_format_bind_operation(r, tp,
                        &fmt_val[i].name, value);
                if (rc == NGX_ERROR) {
                    goto oom;
                } else if (rc != NGX_OK) {
                    return rc;
                }
            }
        }
        /* }}} */

        /** Upsert {{{ */
        if (tlcf->req_type == TP_UPSERT && fmt_val[i].upsert_op) {

            if (!upsert_add_ops_started) {

                if (tp_upsertbegin_add_ops(tp,
                        (uint32_t) prepared_result->upsert_tuples_count)
                            == NULL)
                {
                    goto oom;
                }

                upsert_add_ops_started = 1;
            }

                rc = ngx_http_tnt_format_bind_operation(r, tp,
                    &fmt_val[i].name, value);
            if (rc == NGX_ERROR) {
                goto oom;
            } else if (rc != NGX_OK) {
                return rc;
            }
        }
        /* }}} */

        switch (fmt_val[i].type) {
        case TP_BOOL:
            if (value->len == sizeof("true") - 1 &&
                    ngx_strncasecmp(value->data,
                        (u_char *) "true", sizeof("true") - 1) == 0)
            {
                if (tp_encode_bool(tp, true) == NULL) {
                    goto oom;
                }
            } else if (value->len == sizeof("false") - 1 &&
                ngx_strncasecmp(value->data,
                            (u_char *) "false", sizeof("false") - 1) == 0)
            {
                if (tp_encode_bool(tp, false) == NULL) {
                    goto oom;
                }
            }
            else {
                return ngx_http_tnt_format_bind_bad_request(
                        r, &fmt_val[i].name, "True/False is expecting.");
            }

            break;
        case TP_INT:

            snprintf(num_buf, sizeof(num_buf), "%.*s",
                    (int) value->len, (char *) value->data);

            if (*value->data == '-') {

                if (tp_encode_int(tp, (int64_t) atoll(num_buf)) == NULL) {
                    goto oom;
                }

            } else if (tp_encode_uint(tp, (int64_t) atoll(num_buf)) == NULL) {
                goto oom;
            }

            break;
        case TP_DOUBLE:

            snprintf((char *) num_buf, sizeof(num_buf), "%.*s",
                    (int) value->len, (char *) value->data);

            if (tp_encode_double(tp, atof(num_buf)) == NULL) {
                goto oom;
            }

            break;
        case TP_FLOAT:

            snprintf((char *) num_buf, sizeof(num_buf), "%.*s",
                    (int) value->len, (char *) value->data);

            if (tp_encode_double(tp, (float) atof(num_buf)) == NULL) {
                goto oom;
            }

            break;
        case TP_STR:
            if (tp_encode_str(tp, (const char *) value->data,
                        value->len) == NULL)
            {
                goto oom;
            }
            break;
        default:
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "Can't issue the query, error = 'unknown type %d'",
                (int) fmt_val[i].type);
            return NGX_ERROR;
        }
    }

    return NGX_OK;

oom:
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
            "Can't issue the query, error = '%V'",
            &ngx_http_tnt_get_error_text(HTTP_REQUEST_TOO_LARGE)->msg);

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_tnt_add_header_in(ngx_http_request_t *r,
        ngx_http_tnt_header_val_t *hv, ngx_str_t *value)
{
    ngx_table_elt_t  *h;

    if (value->len) {

        h = ngx_list_push(&r->headers_in.headers);
        if (h == NULL) {
            return NGX_ERROR;
        }

        h->hash = 1;
        h->key = hv->key;
        h->value = *value;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_process_headers(ngx_http_request_t *r,
        ngx_http_tnt_loc_conf_t *tlcf)
{
    ngx_uint_t                i;
    ngx_str_t                 value;
    ngx_http_tnt_header_val_t *h;

    if (tlcf->headers == NULL) {
        return NGX_OK;
    }

    h = tlcf->headers->elts;

    for (i = 0; i < tlcf->headers->nelts; i++) {

        if (ngx_http_complex_value(r, &h[i].value, &value) != NGX_OK) {
            return NGX_ERROR;
        }

        if (h[i].handler(r, &h[i], &value) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static char *
ngx_http_tnt_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_tnt_loc_conf_t *tlcf = conf;

    ngx_str_t                 *value;
    ngx_url_t                 u;
    ngx_http_core_loc_conf_t  *clcf;

    if (tlcf->upstream.upstream) {
        return "is duplicate";
    }

    value = cf->args->elts;

    ngx_memzero(&u, sizeof(ngx_url_t));

    u.url = value[1];
    u.no_resolve = 1;

    tlcf->upstream.upstream = ngx_http_upstream_add(cf, &u, 0);
    if (tlcf->upstream.upstream == NULL) {
        return NGX_CONF_ERROR;
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    clcf->handler = ngx_http_tnt_handler;

    if (clcf->name.data[clcf->name.len - 1] == '/') {
        clcf->auto_redirect = 1;
    }

    return NGX_CONF_OK;
}
/** }}}
 */


/** Filters {{{
 */
static ngx_int_t
ngx_http_tnt_filter_init(void *data)
{
    dd("init filter");

    ngx_http_request_t  *r = data;
    ngx_http_upstream_t *u = r->upstream;
    ngx_http_tnt_ctx_t  *ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);

    ctx->state = READ_PAYLOAD;
    ctx->payload_size = ctx->rest = 0;

    if (u->headers_in.status_n != 200) {
        u->length = 0;
    } else {
        u->length = -1;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_send_reply(ngx_http_request_t *r, ngx_http_upstream_t *u,
        ngx_http_tnt_ctx_t *ctx)
{
    tp_transcode_t          tc;
    ngx_int_t               rc;
    ngx_http_tnt_loc_conf_t *tlcf;
    ngx_buf_t               *output;
    size_t                  output_size;

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_tnt_module);

    output_size =
        (ctx->tp_cache->end - ctx->tp_cache->start + ngx_http_tnt_overhead())
        * tlcf->out_multiplier;
    output = ngx_http_tnt_create_mem_buf(r, u, output_size);
    if (output == NULL) {
        return NGX_ERROR;
    }

    if (ctx->batch_size > 0
        && ctx->rest_batch_size == ctx->batch_size)
    {
        *output->pos = '[';
        ++output->pos;
    }

    tp_transcode_init_args_t args = {
        .output = (char *) output->pos,
        .output_size = output->end - output->pos,
        .method = NULL, .method_len = 0,
        .codec = TP_REPLY_TO_JSON,
        .mf = NULL
    };

    rc = tp_transcode_init(&tc, &args);
    if (rc == TP_TRANSCODE_ERROR) {
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,
                "[BUG] failed to call tp_transcode_init(output)");
        return NGX_ERROR;
    }

    tp_reply_to_json_set_options(&tc, tlcf->pure_result == NGX_TNT_CONF_ON,
            tlcf->multireturn_skip_count);

    rc = tp_transcode(&tc, (char *)ctx->tp_cache->start,
                      ctx->tp_cache->end - ctx->tp_cache->start);
    if (rc != TP_TRANSCODE_ERROR) {

        /* Finishing */
        size_t complete_msg_size = 0;
        rc = tp_transcode_complete(&tc, &complete_msg_size);
        if (rc == TP_TRANSCODE_ERROR) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "[BUG] failed to complete output transcoding. "
                    "UNKNOWN ERROR");
            goto error_exit;
        }

        /* Send an error to the client */
        if (rc == TP_TNT_ERROR) {
            ngx_pfree(r->pool, output);

            if (ngx_http_tnt_set_err(r, tc.errcode,
                    (u_char *) tc.errmsg, ngx_strlen(tc.errmsg)) != NGX_OK)
            {
                goto error_exit;
            }
        } else {
            output->last = output->pos + complete_msg_size;
        }
    }
    /** Transcoder down */
    else {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
            "[BUG] failed to transcode output. errcode: '%d', errmsg: '%s'",
            tc.errcode, get_str_safe((const u_char *) tc.errmsg));
        goto error_exit;
    }

    /** Transcoding - OK */
    tp_transcode_free(&tc);

    if (ctx->batch_size > 0) {

        if (ctx->rest_batch_size == 1)
        {
            *output->last = ']';
            ++output->last;
        }
        else if (ctx->rest_batch_size <= ctx->batch_size)
        {
            *output->last = ',';
            ++output->last;
        }
    }

    /** Reply was created in the ngx_http_tnt_output_err.
     *  That means we don't need the 'output' buffer.
     */
    if (ctx->in_err != NULL) {
        return NGX_OK;
    }

    return ngx_http_tnt_output(r, u, output);

error_exit:

    if (output) {
        ngx_pfree(r->pool, output);
    }

    tp_transcode_free(&tc);

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_tnt_filter_reply(ngx_http_request_t *r, ngx_http_upstream_t *u,
        ngx_buf_t *b)
{
    ngx_int_t          rc;

    ngx_http_tnt_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);
    ssize_t            bytes = b->last - b->pos;

    dd("filter_reply -> recv bytes: %i, rest: %i",
            (int) bytes, (int) ctx->rest);

    if (ctx->state == READ_PAYLOAD) {

        ssize_t payload_rest = ngx_min(ctx->payload.e - ctx->payload.p, bytes);

        if (payload_rest > 0) {

            ctx->payload.p = ngx_copy(ctx->payload.p, b->pos, payload_rest);
            bytes -= payload_rest;
            b->pos += payload_rest;
            payload_rest = ctx->payload.e - ctx->payload.p;

            dd("filter_reply -> payload rest:%i", (int) payload_rest);
        }

        if (payload_rest == 0) {

            ctx->payload_size = tp_read_payload((char *) &ctx->payload.mem[0],
                                                (char *) ctx->payload.e);
            if (ctx->payload_size <= 0) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                        "[BUG] tp_read_payload failed, ret:%i",
                        (int) ctx->payload_size);
                return NGX_ERROR;
            }

            ctx->rest = ctx->payload_size - 5 /* - header size */;

            dd("filter_reply -> got header payload:%i, rest:%i",
                    (int) ctx->payload_size,
                    (int) ctx->rest);

            ctx->tp_cache = ngx_create_temp_buf(r->pool, ctx->payload_size);
            if (ctx->tp_cache == NULL) {
                return NGX_ERROR;
            }

            ctx->tp_cache->pos = ctx->tp_cache->start;
            ctx->tp_cache->memory = 1;


            ctx->tp_cache->pos = ngx_copy(ctx->tp_cache->pos,
                                          &ctx->payload.mem[0],
                                          sizeof(ctx->payload.mem) - 1);

            ctx->payload.p = &ctx->payload.mem[0];

            ctx->state = READ_BODY;

        } else {
            return NGX_OK;
        }
    }

    rc = NGX_OK;

    if (ctx->state == READ_BODY) {

        ssize_t rest = ctx->rest - bytes, read_on = bytes;
        if (rest < 0) {
            rest *= -1;
            read_on = bytes - rest;
            ctx->rest = 0;
            ctx->state = SEND_REPLY;
            rc = NGX_AGAIN;
        } else if (rest == 0) {
            ctx->state = SEND_REPLY;
            ctx->rest = 0;
        } else {
            ctx->rest -= bytes;
        }

        ctx->tp_cache->pos = ngx_copy(ctx->tp_cache->pos, b->pos, read_on);
        b->pos += read_on;

        dd("filter_reply -> read_on:%i, rest:%i, cache rest:%i, buf size:%i",
                (int) read_on,
                (int) ctx->rest,
                (int) (ctx->tp_cache->end - ctx->tp_cache->pos),
                (int) (b->last - b->pos));
    }

    if (ctx->state == SEND_REPLY) {

        rc = ngx_http_tnt_send_reply(r, u, ctx);

        ctx->state = READ_PAYLOAD;
        ctx->rest = ctx->payload_size = 0;

        --ctx->rest_batch_size;

        if (ctx->rest_batch_size <= 0) {
            u->length = 0;
            ctx->rest_batch_size = 0;
            ctx->batch_size = 0;
        }

        ngx_pfree(r->pool, ctx->tp_cache);
        ctx->tp_cache = NULL;

        if (b->last - b->pos > 0) {
            rc = NGX_AGAIN;
        }
    }

    return rc;
}

static ngx_int_t
ngx_http_tnt_filter(void *data, ssize_t bytes)
{
    dd("filter");

    ngx_http_request_t   *r = data;
    ngx_http_upstream_t  *u = r->upstream;
    ngx_buf_t            *b = &u->buffer;

    b->last = b->last + bytes;

    ngx_int_t rc = NGX_OK;
    for (;;) {
        rc = ngx_http_tnt_filter_reply(r, u, b);
        if (rc != NGX_AGAIN)
            break;
        dd("Next message in same input buffer -- merge");
    }

    if (rc != NGX_ERROR) {
      u->keepalive = 1;
    }

    return rc;
}
/** }}}
 */


/** Other functions and utils {{{
 */
static ngx_buf_t *
ngx_http_tnt_create_mem_buf(ngx_http_request_t *r, ngx_http_upstream_t *u,
        size_t size)
{
    ngx_buf_t *b = ngx_create_temp_buf(r->pool, size);
    if (b == NULL) {
        return NULL;
    }

    b->pos = b->start;

    b->memory = 1;
    b->flush = 1;
    b->tag = u->output.tag;
    b->last = b->end;

    return b;
}


static ngx_int_t
ngx_http_tnt_output(ngx_http_request_t *r, ngx_http_upstream_t *u,
        ngx_buf_t *b)
{
    ngx_chain_t *cl, **ll;

    for (cl = u->out_bufs, ll = &u->out_bufs; cl; cl = cl->next) {
        ll = &cl->next;
    }

    cl = ngx_chain_get_free_buf(r->pool, &u->free_bufs);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    b->pos = b->start;
    b->flush = 1;
    b->last_in_chain = 1;
    b->tag = u->output.tag;

    cl->buf = b;
    cl->next = NULL;

    *ll = cl;

    return NGX_OK;
}
/** }}}
 */


static ngx_int_t
ngx_http_tnt_copy_headers(struct tp *tp, ngx_list_t *headers,
        size_t *map_items)
{
    size_t          i = 0;
    ngx_table_elt_t *h;
    ngx_list_part_t *part;

    if (headers->size > 0) {

        part = &headers->part;
        h = part->elts;

        for (;; i++) {

            if (i >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }
                part = part->next;
                h = part->elts;
                i = 0;
            }

            if (!tp_encode_str_map_item(tp,
                                        (const char *) h[i].key.data,
                                        h[i].key.len,
                                        (const char *) h[i].value.data,
                                        h[i].value.len) )
            {
                return NGX_ERROR;
            }

            ++(*map_items);
        }
    }

    return NGX_OK;
}


static size_t
ngx_http_tnt_get_output_size(ngx_http_request_t *r, ngx_http_tnt_ctx_t *ctx,
        ngx_http_tnt_loc_conf_t *tlcf, ngx_buf_t *request_b)
{
    (void) ctx;

    size_t output_size = ngx_http_tnt_overhead();

    if (r->headers_in.content_length_n > 0) {
      output_size += r->headers_in.content_length_n;
    }

    if (tlcf->method.len) {
      output_size += tlcf->method.len;
    }

    output_size *= tlcf->in_multiplier + 20 /* header overhead */;

    if (request_b != NULL) {
        output_size += request_b->last - request_b->start;
    }

    return output_size;
}


static ngx_int_t
ngx_http_tnt_output_err(ngx_http_request_t *r, ngx_http_tnt_ctx_t *ctx,
        ngx_int_t code)
{
    ngx_http_upstream_t  *u;
    ngx_chain_t          *cl, **ll;

    u = r->upstream;

    if (ctx->in_err == NULL) {
        u->headers_in.status_n = 500;
        u->state->status = 500;
        u->headers_in.content_length_n = 0;
        return NGX_OK;
    }

    u->headers_in.status_n = code;
    u->state->status = code;
    u->headers_in.content_length_n = ctx->in_err->last - ctx->in_err->pos;
    u->length = 0;

    for (cl = u->out_bufs, ll = &u->out_bufs; cl; cl = cl->next) {
        ll = &cl->next;
    }

    *ll = cl = ngx_chain_get_free_buf(r->pool, &u->free_bufs);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = ctx->in_err;
    cl->next = NULL;

    cl->buf->pos = cl->buf->start;
    cl->buf->end = cl->buf->last;

    cl->buf->flush = 1;
    cl->buf->memory = 1;
    cl->buf->tag = u->output.tag;
    cl->buf->last_in_chain = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_read_greeting(ngx_http_request_t *r, ngx_http_tnt_ctx_t *ctx,
        ngx_buf_t *b)
{
    if (b->last - b->pos >= (ptrdiff_t) sizeof("Tarantool") - 1
        && b->pos[0] == 'T'
        && b->pos[1] == 'a'
        && b->pos[2] == 'r'
        && b->pos[3] == 'a'
        && b->pos[4] == 'n'
        && b->pos[5] == 't'
        && b->pos[6] == 'o'
        && b->pos[7] == 'o'
        && b->pos[8] == 'l')
    {
        b->pos = b->pos + 128;
        /**
         *  Nginx should read only "greeting" (128 bytes).
         *  If tarantool sent the only one message (i.e. greeting),
         *  then we have to tell to nginx, that it needs read more bytes.
         */
        if (b->pos == b->last) {
            return NGX_AGAIN;
        }
    }

    ctx->greeting = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_wakeup_dying_upstream(ngx_http_request_t *r,
        ngx_chain_t *out_chain)
{
    static const u_char fd_event[] =
              "{\"method\":\"__nginx_tnt_event\",\"params\":[]}";

    ngx_http_tnt_ctx_t          *ctx;
    ngx_http_tnt_loc_conf_t     *tlcf;

    ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_tnt_module);

    out_chain->buf = ngx_create_temp_buf(r->pool,
                                sizeof(fd_event) * tlcf->in_multiplier);
    if (out_chain->buf == NULL) {
        return NGX_ERROR;
    }

    out_chain->next = NULL;
    out_chain->buf->memory = 1;
    out_chain->buf->flush = 1;

    out_chain->buf->pos = out_chain->buf->start;
    out_chain->buf->last = out_chain->buf->pos;
    out_chain->buf->last_in_chain = 1;

    /** Write some data to the upstream socket.
     *  This is need for runing handlers in right way.
     */
    if (ngx_http_tnt_send_once(r, ctx, out_chain, fd_event,
                ngx_strlen(fd_event)) != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_send_once(ngx_http_request_t *r, ngx_http_tnt_ctx_t *ctx,
    ngx_chain_t *out_chain, const u_char *buf, size_t len)
{
    tp_transcode_t           tc;
    size_t                   complete_msg_size;

    tp_transcode_init_args_t args = {
        .output = (char *) out_chain->buf->start,
        .output_size = out_chain->buf->end - out_chain->buf->start,
        .method = NULL,
        .method_len = 0,
        .codec = YAJL_JSON_TO_TP,
        .mf = NULL
    };

    if (tp_transcode_init(&tc, &args) == TP_TRANSCODE_ERROR) {
        goto error_exit;
    }

    if (tp_transcode(&tc, (char *) buf, len) == TP_TRANSCODE_ERROR) {
        dd("ngx_http_tnt_send:tp_transcode error: %s, code:%d",
                tc.errmsg, tc.errcode);
        goto error_exit;
    }

    if (tp_transcode_complete(&tc, &complete_msg_size) == TP_TRANSCODE_OK) {

        out_chain->buf->last = out_chain->buf->start + complete_msg_size;

        if (tc.batch_size > 1) {
            ctx->rest_batch_size = ctx->batch_size = tc.batch_size;
        }
    } else {
        goto error_exit;
    }

    tp_transcode_free(&tc);
    return NGX_OK;

error_exit:
    tp_transcode_free(&tc);
    return NGX_ERROR;
}


static void
ngx_http_tnt_cleanup(ngx_http_request_t *r, ngx_http_tnt_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->tp_cache != NULL) {
        ngx_pfree(r->pool, ctx->tp_cache);
        ctx->tp_cache = NULL;
    }
}


static ngx_int_t
ngx_http_tnt_set_method(ngx_http_tnt_ctx_t *ctx,
        ngx_http_request_t *r, ngx_http_tnt_loc_conf_t *tlcf)
{
    u_char *start, *pos, *end;

    if (tlcf->method.data && tlcf->method.len) {

        ctx->preset_method_len = ngx_min(tlcf->method.len,
                                         sizeof(ctx->preset_method)-1);
        ngx_memcpy(ctx->preset_method,
                   tlcf->method.data,
                   ctx->preset_method_len);

    } else if (tlcf->http_rest_methods & r->method) {

        if (r->uri.data == NULL || !r->uri.len) {
            goto error;
        }

        start = pos = (*r->uri.data == '/' ? r->uri.data + 1 : r->uri.data);
        end = r->uri.data + r->uri.len;

        for (;pos != end; ++pos) {
            if (*pos == '/') {
                ctx->preset_method_len = ngx_min(sizeof(ctx->preset_method)-1,
                                                 (size_t)(pos - start));
                ngx_memcpy(ctx->preset_method, start, ctx->preset_method_len);
                break;
            }
        }

        if (!ctx->preset_method[0]) {

            if (start == end) {
                goto error;
            }

            ctx->preset_method_len = ngx_min(sizeof(ctx->preset_method)-1,
                                            (size_t)(end - start));
            ngx_memcpy(ctx->preset_method, start, ctx->preset_method_len);
        }
    }
    /* Else -- expect the method in the body */

    return NGX_OK;

error:
    ctx->preset_method[0] = 0;
    ctx->preset_method_len = 0;
    return NGX_ERROR;
}


static ngx_http_tnt_next_arg_t
ngx_http_tnt_get_next_arg(u_char *it, u_char *end)
{
    ngx_http_tnt_next_arg_t next_arg = { .it = end, .value = NULL };

    for ( ; it != end; ++it) {

        if (next_arg.value == NULL /* CASE: ARG==.. */ &&
                *it == '=')
        {
            next_arg.value = it + 1;
            continue;
        } else if (*it == '&') {
            next_arg.it = it;
            break;
        }

    }

    return next_arg;
}


static ngx_int_t
ngx_http_tnt_encode_str_map_item(ngx_http_request_t *r,
                                 ngx_http_tnt_loc_conf_t *tlcf,
                                 struct tp *tp,
                                 u_char *key, size_t key_len,
                                 u_char *value, size_t value_len)
{
    ngx_int_t rc;
    ngx_str_t unescaped_value;
    ngx_str_t value_str;

    value_str.data = value;
    value_str.len = value_len;

    if (tlcf->pass_http_request & NGX_TNT_CONF_UNESCAPE) {

        rc = ngx_http_tnt_unescape_uri(r, &unescaped_value, &value_str);
        if (rc != NGX_OK) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "ngx_http_tnt_encode_str_map_item: unescape failed, "
                " it looks like OOM happened");
            return rc;
        }
        value_str = unescaped_value;
    }

    if (tp_encode_str_map_item(tp, (const char *) key, key_len,
                (const char *) value_str.data, value_str.len) == NULL)
    {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
            "ngx_http_tnt_encode_str_map_item: tp_encode failed, "
            " it looks like OOM happened");
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_encode_query_args(ngx_http_request_t *r,
    ngx_http_tnt_loc_conf_t *tlcf, struct tp *tp, ngx_uint_t *args_items)
{
    u_char                  *arg_begin, *end;
    ngx_http_tnt_next_arg_t arg;
    ngx_str_t               *args;

    args = &r->args;

    if (args->len == 0) {
        return NGX_OK;
    }

    arg_begin = args->data;
    end = arg_begin + args->len;

    arg.it = arg_begin;
    arg.value = NULL;

    for (; arg.it < end; ) {

        arg = ngx_http_tnt_get_next_arg(arg.it, end);

        const size_t value_len = arg.it - arg.value;
        if (arg.value && value_len > 0) {

            ++(*args_items);

            if (ngx_http_tnt_encode_str_map_item(r, tlcf, tp,
                                                 arg_begin,
                                                 arg.value - arg_begin - 1,
                                                 arg.value,
                                                 value_len) != NGX_OK)
            {
                return NGX_ERROR;
            }

        }
        arg_begin = ++arg.it;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_encode_urlencoded_body(ngx_http_request_t *r,
                                    ngx_http_tnt_loc_conf_t *tlcf,
                                    struct tp *tp,
                                    ngx_buf_t *b,
                                    ngx_uint_t *args_items)
{
    u_char                  *arg_begin, *end;
    ngx_http_tnt_next_arg_t arg;
    size_t                  value_len;

    if (b->start == b->last) {
        return NGX_OK;
    }

    arg_begin = b->start;
    end = b->last;

    arg.it = arg_begin;
    arg.value = NULL;

    for (; arg.it < end; ) {

        arg = ngx_http_tnt_get_next_arg(arg.it, end);

        value_len = (size_t) (arg.it - arg.value);

        if (arg.value && value_len > 0) {

            if (tp_encode_map(tp, 1) &&
                ngx_http_tnt_encode_str_map_item(r, tlcf, tp,
                                                 arg_begin,
                                                 arg.value - arg_begin - 1,
                                                 arg.value,
                                                 value_len) != NGX_OK)
            {
                return NGX_ERROR;
            }

            ++(*args_items);
        }
        arg_begin = ++arg.it;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_get_request_data(ngx_http_request_t *r,
                              ngx_http_tnt_loc_conf_t *tlcf,
                              struct tp *tp)
{
    /** TODO:
     *      This function should be a part of tp_transcode.{c,h}. It's very
     *      strange have this function here, so the best way is piece by piece
     *      move this functionality ...
     *
     *      Also it would be nice to have tied nginx structures
     *      with tp_transcode
     */
    char                *root_map_place;
    char                *map_place;
    size_t              root_items;
    size_t              map_items;
    ngx_buf_t           *b;
    ngx_chain_t         *body;
    char                *p;
    ngx_buf_t           unparsed_body;
    ngx_int_t           rc;

    root_items = 0;
    root_map_place = tp->p;

    if (tp_add(tp, 1 + sizeof(uint32_t)) == NULL) {
        goto oom_cant_encode;
    }

    /** Encode protocol */
    ++root_items;

    if (tp_encode_str_map_item(tp,
                                "proto", sizeof("proto") - 1,
                                (const char*) r->http_protocol.data,
                                r->http_protocol.len) == NULL)
    {
        goto oom_cant_encode;
    }

    /** Encode method */
    ++root_items;

    if (tp_encode_str_map_item(tp,
                                "method", sizeof("method") - 1,
                                (const char *) r->method_name.data,
                                r->method_name.len) == NULL)
    {
        goto oom_cant_encode;
    }

    /** Encode uri:
     *      whether NGX_TNT_CONF_PASS_SUBREQUEST_URI is not set then raw uri
     *      will be chosen, otherwise preprocessed value will be used
     */
    ++root_items;

    ngx_str_t uri = tlcf->pass_http_request & NGX_TNT_CONF_PASS_SUBREQUEST_URI
        ? r->uri : r->unparsed_uri;

    if (ngx_http_tnt_encode_str_map_item(r, tlcf, tp,
                                         (u_char *) "uri", sizeof("uri") - 1,
                                         uri.data, uri.len) == NGX_ERROR)
    {
        goto oom_cant_encode;
    }

    /** Encode query args */
    if (tlcf->pass_http_request & NGX_TNT_CONF_PARSE_ARGS) {

        ++root_items;

        if (tp_encode_str(tp, "args", sizeof("args") - 1) == NULL) {
            goto oom_cant_encode;
        }

        map_place = tp->p;
        if (tp_add(tp, 1 + sizeof(uint32_t)) == NULL) {
            goto oom_cant_encode;
        }

        map_items = 0;

        rc = ngx_http_tnt_encode_query_args(r, tlcf, tp, &map_items);
        if (rc == NGX_ERROR) {
            goto oom_cant_encode;
        }

        *(map_place++) = 0xdf;
        *(uint32_t *) map_place = mp_bswap_u32(map_items);
    }

    /** Encode http headers */
    ++root_items;

    if (tp_encode_str(tp, "headers", sizeof("headers") - 1) == NULL) {
        goto oom_cant_encode_headers;
    }

    map_items = 0;
    map_place = tp->p;

    if (tp_add(tp, 1 + sizeof(uint32_t)) == NULL) {
        goto oom_cant_encode_headers;
    }

    if (ngx_http_tnt_copy_headers(tp, &r->headers_in.headers, &map_items) ==
            NGX_ERROR)
    {
        goto oom_cant_encode_headers;
    }

    if ((tlcf->pass_http_request & NGX_TNT_CONF_PASS_HEADERS_OUT) &&
        (ngx_http_tnt_copy_headers(tp, &r->headers_out.headers, &map_items) ==
            NGX_ERROR) )
    {
        goto oom_cant_encode_headers;
    }

    *(map_place++) = 0xdf;
    *(uint32_t *) map_place = mp_bswap_u32(map_items);

    /** Encode body */
    if ((tlcf->pass_http_request & NGX_TNT_CONF_PARSE_URLENCODED) &&
            r->headers_in.content_length_n > 0 &&
            r->upstream->request_bufs)
    {
        ++root_items;

        if (tp_encode_str(tp, "args_urlencoded", sizeof("args_urlencoded") - 1) == NULL) {
            goto oom_cant_encode_body;
        }

        /** Encode urlencoded body as map - body = { K = V, .. } */
        map_place = tp->p;

        if (tp_add(tp, 1 + sizeof(uint32_t)) == NULL) {
            goto oom_cant_encode_body;
        }

        ngx_memset(&unparsed_body, 0, sizeof(ngx_buf_t));

        unparsed_body.pos = ngx_pnalloc(r->pool,
                        sizeof(u_char) * r->headers_in.content_length_n + 1);
        if (unparsed_body.pos == NULL) {
            return NGX_ERROR;
        }
        unparsed_body.last = unparsed_body.pos;
        unparsed_body.start = unparsed_body.pos;
        unparsed_body.end = unparsed_body.pos +
                r->headers_in.content_length_n + 1;

        for (body = r->upstream->request_bufs; body; body = body->next) {

            b = body->buf;

            if (b->in_file) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "in-file buffer found. aborted. "
                    "consider increasing your 'client_body_buffer_size' "
                    "setting");
                return NGX_ERROR;
            }

            unparsed_body.last = ngx_copy(unparsed_body.last,
                        b->pos, b->last - b->pos);
        }

        /** Actually this is an array not a map, I used this variable
         * since it's avaliable in this scope
         */
        map_items = 0;

        if (ngx_http_tnt_encode_urlencoded_body(r, tlcf, tp,
                    &unparsed_body, &map_items) != NGX_OK)
        {
            goto oom_cant_encode_body;
        }

        *(map_place++) = 0xdd;
        *(uint32_t *) map_place = mp_bswap_u32(map_items);
    }
    
    /** Unknown body type - encode as mp string
     */
    if ((tlcf->pass_http_request & NGX_TNT_CONF_PASS_BODY) &&
            r->headers_in.content_length_n > 0 &&
            r->upstream->request_bufs)
    {
        ++root_items;

        if (tp_encode_str(tp, "body", sizeof("body") - 1) == NULL) {
            goto oom_cant_encode_body;
        }


        int sz = mp_sizeof_str(r->headers_in.content_length_n);
        if (tp_ensure(tp, sz) == -1) {
            goto oom_cant_encode_body;
        }

        p = mp_encode_strl(tp->p, r->headers_in.content_length_n);

        for (body = r->upstream->request_bufs; body; body = body->next) {

            b = body->buf;

            if (b->in_file) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "in-file buffer found. aborted. "
                    "consider increasing your 'client_body_buffer_size' "
                    "setting");
                return NGX_ERROR;
            }

            p = (char *) ngx_copy(p, b->pos, b->last - b->pos);
        }

        if (tp_add(tp, sz) == NULL) {
            goto oom_cant_encode_body;
        }
    }

    *(root_map_place++) = 0xdf;
    *(uint32_t *) root_map_place = mp_bswap_u32(root_items);

    return NGX_OK;

oom_cant_encode:
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
            "can't encode uri, schema etc. "
            "aborted. consider increasing your "
            "'tnt_pass_http_request_buffer_size' setting");
    return NGX_ERROR;

oom_cant_encode_headers:
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
            "can't encode HTTP headers. "
            "aborted. consider increasing your "
            "'tnt_pass_http_request_buffer_size' setting");
    return NGX_ERROR;

oom_cant_encode_body:

    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
            "can't encode body. aborted. consider increasing your "
            "'tnt_pass_http_request_buffer_size' setting");
    return NGX_ERROR;
}


static ngx_buf_t *
ngx_http_tnt_get_request_data_map(ngx_http_request_t *r,
        ngx_http_tnt_loc_conf_t *tlcf)
{
    ngx_int_t rc;
    struct tp tp;
    ngx_buf_t *b;

    b = ngx_create_temp_buf(r->pool, tlcf->pass_http_request_buffer_size);
    if (b == NULL) {
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,
            "ngx_http_tnt_get_request_data_map can't allocate "
            "output buffer with size = %d",
            (int) tlcf->pass_http_request_buffer_size);
        return NULL;
    }

    b->memory = 1;
    b->flush = 1;

    b->pos = b->start;

    tp_init(&tp, (char *) b->start, b->end - b->start, NULL, NULL);
    tp.size = tp.p;

    rc = ngx_http_tnt_get_request_data(r, tlcf, &tp);
    if (rc != NGX_OK) {
        return NULL;
    }

    b->last = (u_char *) tp.p;

    return b;
}


static ngx_http_tnt_ctx_t *
ngx_http_tnt_create_ctx(ngx_http_request_t *r)
{
    ngx_http_tnt_ctx_t      *ctx;

    ctx = ngx_palloc(r->pool, sizeof(ngx_http_tnt_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }

    ngx_http_tnt_reset_ctx(ctx);

    ngx_http_set_ctx(r, ctx, ngx_http_tnt_module);

    ctx->state = OK;

    return ctx;
}


static void
ngx_http_tnt_reset_ctx(ngx_http_tnt_ctx_t *ctx)
{
    ctx->payload.p = &ctx->payload.mem[0];
    ctx->payload.e = &ctx->payload.mem[sizeof(ctx->payload.mem) - 1];

    ctx->state = OK;

    ctx->in_err = ctx->tp_cache = NULL;

    ctx->rest = 0;
    ctx->payload_size = 0;

    ctx->rest_batch_size = 0;
    ctx->batch_size = 0;

    ctx->greeting = 0;

    ctx->preset_method[0] = 0;
    ctx->preset_method_len = 0;
}


static ngx_int_t
ngx_http_tnt_init_handlers(ngx_http_request_t *r, ngx_http_upstream_t *u,
        ngx_http_tnt_loc_conf_t *tlcf)
{
    ngx_http_tnt_ctx_t  *ctx;

    ctx = ngx_http_tnt_create_ctx(r);
    if (ctx == NULL) {
        return NGX_ERROR;
    }

    if (ngx_http_tnt_set_method(ctx, r, tlcf) == NGX_ERROR) {
        return NGX_HTTP_BAD_REQUEST;
    }

    u->reinit_request = ngx_http_tnt_reinit_request;
    u->process_header = ngx_http_tnt_process_header;
    u->abort_request = ngx_http_tnt_abort_request;
    u->finalize_request = ngx_http_tnt_finalize_request;

    u->create_request = ngx_http_tnt_query_handler;

    if (tlcf->req_type > 0) {

        if (r->headers_in.content_type != NULL &&
            r->headers_in.content_type->value.len > 0)
        {

            if (!ngx_http_tnt_str_match(&r->headers_in.content_type->value,
                        "application/x-www-form-urlencoded",
                        sizeof("application/x-www-form-urlencoded") - 1) ||
                !ngx_http_tnt_str_match(&r->headers_in.content_type->value,
                        "application/x-www-form-urlencoded",
                        sizeof("application/x-www-form-urlencoded") - 1))
            {
                return NGX_HTTP_NOT_ALLOWED;
            }
        }

        u->create_request = ngx_http_tnt_dml_handler;
        return NGX_OK;
    }

    if (tlcf->pass_http_request & NGX_TNT_CONF_PASS_BODY) {
        /* This is default and already set */
        return NGX_OK;
    }

    if (r->headers_in.content_length_n > 0) {
        u->create_request = ngx_http_tnt_body_handler;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_body_handler(ngx_http_request_t *r)
{
    ngx_buf_t                   *b, *request_b = NULL;
    ngx_chain_t                 *body;
    size_t                      complete_msg_size;
    tp_transcode_t              tc;
    ngx_http_tnt_ctx_t          *ctx;
    ngx_chain_t                 *out_chain;
    ngx_http_tnt_loc_conf_t     *tlcf;
    const ngx_http_tnt_error_t  *e;

    ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_tnt_module);

    out_chain = ngx_alloc_chain_link(r->pool);

    if (out_chain == NULL) {
        return NGX_ERROR;
    }

    if (tlcf->pass_http_request & NGX_TNT_CONF_ON) {

        request_b = ngx_http_tnt_get_request_data_map(r, tlcf);
        if (request_b == NULL) {
            return NGX_ERROR;
        }
    }

    out_chain->buf = ngx_create_temp_buf(r->pool,
                        ngx_http_tnt_get_output_size(r, ctx, tlcf, request_b));

    if (out_chain->buf == NULL) {

        ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,
                "ngx_http_tnt_body_handler "
                "failed to allocate output buffer, size %ui",
                (r->headers_in.content_length_n + 1) * tlcf->in_multiplier);
        return NGX_ERROR;
    }

    out_chain->next = NULL;
    out_chain->buf->memory = 1;
    out_chain->buf->flush = 1;

    out_chain->buf->pos = out_chain->buf->start;
    out_chain->buf->last = out_chain->buf->pos;
    out_chain->buf->last_in_chain = 1;

    /**  Conv. input (json, x-url-encoded) into upstream format message {{{
     */
    tp_transcode_init_args_t args = {
        .output = (char *) out_chain->buf->start,
        .output_size = out_chain->buf->end - out_chain->buf->start,
        .method = (char *) ctx->preset_method,
        .method_len = ctx->preset_method_len,
        .codec = YAJL_JSON_TO_TP,
        .mf = NULL
    };

    if (tp_transcode_init(&tc, &args) == TP_TRANSCODE_ERROR) {
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,
                "[BUG] failed to call tp_transcode_init(input)");
        return NGX_ERROR;
    }

    /** Bind extra data e.g. http headers, uri etc */
    if (request_b != NULL) {
        tp_transcode_bind_data(&tc, (const char *) request_b->start,
                (const char *) request_b->last);
    }

    /** Parse url-encoded.
     *
     * urlencoded data saved into the first argument. The following code is
     * making a trancoder happy :)
     */
    if (tlcf->pass_http_request & NGX_TNT_CONF_PARSE_URLENCODED) {

        if (tp_transcode(&tc, "{\"params\":[]}", sizeof("{\"params\":[]}") - 1)
                == TP_TRANSCODE_ERROR)
        {
            ctx->state = INPUT_JSON_PARSE_FAILED;
            goto read_input_done;
        }
    }

    /** Parse JSON*/
    else {

        for (body = r->upstream->request_bufs; body; body = body->next) {


            if (body->buf->in_file) {

                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "in-file buffer found. aborted. "
                    "consider increasing your 'client_body_buffer_size' "
                    "setting");

                e = ngx_http_tnt_get_error_text(REQUEST_TOO_LARGE);
                if (ngx_http_tnt_set_err_str(r, e->code, e->msg) != NGX_OK) {
                    goto error_exit;
                }

                ctx->state = INPUT_TO_LARGE;
                goto read_input_done;

            } else {
                b = body->buf;
            }

            if (tp_transcode(&tc, (char *) b->pos, b->last - b->pos)
                    == TP_TRANSCODE_ERROR)
            {
                ctx->state = INPUT_JSON_PARSE_FAILED;
                goto read_input_done;
            }
        }
    }

    if (tp_transcode_complete(&tc, &complete_msg_size) == TP_TRANSCODE_OK) {

        out_chain->buf->last = out_chain->buf->start + complete_msg_size;

        if (tc.batch_size > 1) {
            ctx->rest_batch_size = ctx->batch_size = tc.batch_size;
        }

        dd("ctx->batch_size:%i, tc.batch_size:%i, complete_msg_size:%i",
            ctx->batch_size, tc.batch_size, (int) complete_msg_size);

    } else {
        ctx->state = INPUT_JSON_PARSE_FAILED;
        goto read_input_done;
    }
    /** }}} */
read_input_done:

    if (ctx->state != OK) {

        if (ctx->in_err == NULL &&
                ngx_http_tnt_set_err(r, tc.errcode, (u_char *) tc.errmsg,
                        ngx_strlen(tc.errmsg)) != NGX_OK)
        {
            goto error_exit;
        }

        if (ngx_http_tnt_wakeup_dying_upstream(r, out_chain) != NGX_OK) {
            goto error_exit;
        }
    }

    /** Hooking output chain*/
    r->upstream->request_bufs = out_chain;

    tp_transcode_free(&tc);

    return NGX_OK;

error_exit:

    tp_transcode_free(&tc);

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_tnt_query_handler(ngx_http_request_t *r)
{
    ngx_int_t                  rc;
    ngx_buf_t                  *buf;
    ngx_http_tnt_ctx_t         *ctx;
    ngx_chain_t                *out_chain;
    ngx_http_tnt_loc_conf_t    *tlcf;
    struct tp                  tp;
    const ngx_http_tnt_error_t *err = NULL;

    ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_tnt_module);

    out_chain = ngx_alloc_chain_link(r->pool);
    if (out_chain == NULL) {
        return NGX_ERROR;
    }

    out_chain->buf = ngx_create_temp_buf(r->pool,
                                         tlcf->pass_http_request_buffer_size);
    if (out_chain->buf == NULL) {
        return NGX_ERROR;
    }

    out_chain->next = NULL;
    out_chain->buf->memory = 1;
    out_chain->buf->flush = 1;

    out_chain->buf->pos = out_chain->buf->start;
    out_chain->buf->last = out_chain->buf->pos;
    out_chain->buf->last_in_chain = 1;

    /**
     *  Conv. GET/PUT/PATCH/DELETE to Tarantool message [
     */
    buf = out_chain->buf;
    tp_init(&tp, (char *) buf->start, buf->end - buf->start, NULL, NULL);

    if (!tp_call_nargs(&tp, (const char *) ctx->preset_method,
                            (size_t) ctx->preset_method_len, 1))
    {
        err = ngx_http_tnt_get_error_text(HTTP_REQUEST_TOO_LARGE);
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "ngx_http_tnt_query_handler - %s",
                get_str_safe(err->msg.data));
        return NGX_ERROR;
    }

    rc = ngx_http_tnt_get_request_data(r, tlcf, &tp);
    if (rc != NGX_OK) {
        err = ngx_http_tnt_get_error_text(HTTP_REQUEST_TOO_LARGE);
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "ngx_http_tnt_query_handler - %s",
                get_str_safe(err->msg.data));
        return rc;
    }

    out_chain->buf->last = (u_char *) tp.p;
    /** ]
     */

    /**
     * Hooking output chain
     */
    r->upstream->request_bufs = out_chain;

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_dml_handler(ngx_http_request_t *r)
{
    ngx_int_t                       rc;
    ngx_buf_t                       *buf;
    ngx_http_tnt_ctx_t              *ctx;
    ngx_chain_t                     *out_chain;
    ngx_http_tnt_loc_conf_t         *tlcf;
    struct tp                       tp;
    ngx_http_tnt_prepared_result_t   prepared_result;

    ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_tnt_module);

    rc = ngx_http_tnt_format_init(tlcf, r, &prepared_result);
    if (rc != NGX_OK) {
        return rc;
    }

    out_chain = ngx_alloc_chain_link(r->pool);
    if (out_chain == NULL) {
        return NGX_ERROR;
    }

    /** Preapre */
    rc = ngx_http_tnt_format_prepare(tlcf, r, &prepared_result);

    if (rc != NGX_OK) {

        if (rc == NGX_HTTP_BAD_REQUEST || rc == NGX_HTTP_NOT_ALLOWED) {

            rc = ngx_http_tnt_wakeup_dying_upstream(r, out_chain);
            if (rc != NGX_OK) {
                return rc;
            }

            ctx->state = INPUT_FMT_CANT_READ_INPUT;

            /** Hooking output chain */
            r->upstream->request_bufs = out_chain;

            return NGX_OK;
        }

        return rc;
    }

    /** Init output chain */
    out_chain->buf = ngx_create_temp_buf(r->pool,
                                         tlcf->pass_http_request_buffer_size);
    if (out_chain->buf == NULL) {
        return NGX_ERROR;
    }

    out_chain->next = NULL;
    out_chain->buf->memory = 1;
    out_chain->buf->flush = 1;

    out_chain->buf->pos = out_chain->buf->start;
    out_chain->buf->last = out_chain->buf->pos;
    out_chain->buf->last_in_chain = 1;

    buf = out_chain->buf;

    /** Here is starting a convertation from an HTTP request
     *  to a Tarantool request
     */
    tp_init(&tp, (char *) buf->start, buf->end - buf->start, NULL, NULL);

    /** Handle request type */
    switch (tlcf->req_type) {
    case TP_INSERT:
        if (tp_insert(&tp, (uint32_t) prepared_result.space_id) == NULL ||
            tp_tuple(&tp, prepared_result.tuples_count) == NULL)
        {
            goto cant_issue_request;
        }
        break;
    case TP_DELETE:

        if (tp_delete(&tp, (uint32_t) prepared_result.space_id,
                    (uint32_t) prepared_result.index_id) == NULL ||
            tp_key(&tp, prepared_result.tuples_count) == NULL)
        {
            goto cant_issue_request;
        }
        break;
    case TP_REPLACE:
        if (tp_replace(&tp, (uint32_t) prepared_result.space_id) == NULL ||
                tp_tuple(&tp, prepared_result.tuples_count) == NULL)
        {
            goto cant_issue_request;
        }
        break;
    case TP_SELECT:
        if (tp_select(&tp, (uint32_t) prepared_result.space_id,
                    (uint32_t) prepared_result.index_id,
                    prepared_result.offset, prepared_result.iter_type,
                    prepared_result.limit) == NULL ||
                tp_key(&tp, prepared_result.tuples_count) == NULL)
        {
            goto cant_issue_request;
        }
        break;
    case TP_UPDATE:
        if (tp_update(&tp, (uint32_t) prepared_result.space_id,
                    (uint32_t) prepared_result.index_id) == NULL ||
            tp_key(&tp, (uint32_t) prepared_result.update_keys_count) == NULL)
        {
            goto cant_issue_request;
        }
        break;
    case TP_UPSERT:
        if (tp_upsert(&tp, (uint32_t) prepared_result.space_id) == NULL ||
                tp_tuple(&tp, (uint32_t) prepared_result.tuples_count)
                    == NULL)
        {
            goto cant_issue_request;
        }
        break;
    default:
        goto cant_issue_request;
    }

    /** Bind values */
    rc = ngx_http_tnt_format_bind(r, &prepared_result, &tp);

    if (rc != NGX_OK) {

        if (rc == NGX_HTTP_BAD_REQUEST) {

            rc = ngx_http_tnt_wakeup_dying_upstream(r, out_chain);
            if (rc != NGX_OK) {
                return rc;
            }

            ctx->state = INPUT_FMT_CANT_READ_INPUT;

            /** Hooking output chain */
            r->upstream->request_bufs = out_chain;

            return NGX_OK;
        }

        return rc;
    }

    out_chain->buf->last = (u_char *) tp.p;

    /** Hooking output chain */
    r->upstream->request_bufs = out_chain;

    return NGX_OK;

cant_issue_request:
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
            "Can't issue the query; It could be one of the following errors: "
            "1) %V; "
            "2) unknown request type.",
            &ngx_http_tnt_get_error_text(HTTP_REQUEST_TOO_LARGE)->msg);
    return NGX_ERROR;
}


static ngx_int_t
ngx_http_tnt_reinit_request(ngx_http_request_t *r)
{
    dd("reinit request");

    ngx_http_tnt_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);
    if (ctx == NULL) {
        return NGX_OK;
    }

    ngx_http_tnt_cleanup(r, ctx);
    ngx_http_tnt_reset_ctx(ctx);

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_process_header(ngx_http_request_t *r)
{
    ngx_http_upstream_t *u = r->upstream;
    ngx_buf_t           *b = &r->upstream->buffer;
    ngx_http_tnt_ctx_t  *ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);

    ngx_int_t rc;

    dd("process_header-> greeting:%s, recv:%i",
            ctx->greeting ? "yes" : "no", (int) (b->last - b->pos));

    if (!ctx->greeting) {

        rc = ngx_http_tnt_read_greeting(r, ctx, b);
        if (rc == NGX_ERROR) {
            return rc;

        /** If ctx->state is not OK, then we did'nt sent a request,
         *  and we should stop work with this upstream.
         *
         *  So I found that the most good what we can is stop work.
         */
        } else if (rc == NGX_AGAIN && ctx->state == OK) {
            return rc;
        }
    }

    switch (ctx->state) {
    case OK:
        break;
    case INPUT_TO_LARGE:
    case INPUT_JSON_PARSE_FAILED:
    case INPUT_EMPTY:
    case INPUT_FMT_CANT_READ_INPUT:
        return ngx_http_tnt_output_err(r, ctx, NGX_HTTP_BAD_REQUEST);
    default:
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "[BUG] unexpected ctx->stage(%i)", ctx->state);
        return NGX_ERROR;
    }

   /*
    *   At this stage we can't get full upstream size,
    *   since Tarantool could send to us 1 upto N messages
    *   where each of messages could have X size.
    *
    *   As fix -- just set each upstream mode to chunked.
    */
    u->headers_in.chunked = 1;
    u->headers_in.status_n = 200;
    u->state->status = 200;

    return NGX_OK;
}


static void
ngx_http_tnt_abort_request(ngx_http_request_t *r)
{
    dd("abort request");
    ngx_http_tnt_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);
    ngx_http_tnt_cleanup(r, ctx);
}


static void
ngx_http_tnt_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    dd("finalize request");
    ngx_http_tnt_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);
    ngx_http_tnt_cleanup(r, ctx);
}


static ngx_int_t
ngx_http_tnt_str_match(ngx_str_t *a, const char *b,
        size_t len)
{
    if (a->len != len) {
        return 0;
    }
    if (ngx_strncmp(a->data, (const u_char *) b, len) == 0) {
        return 1;
    }
    return 0;
}


static ngx_int_t
ngx_http_tnt_set_err(ngx_http_request_t *r, int errcode, const u_char *msg,
        size_t len)
{
    const size_t msglen = len + sizeof("{"
                        "'error':{"
                            "'message':'',"
                            "'code':-XXXXX"
                        "}"
                    "}");

    u_char              escaped_msg[len * 2];
    u_char              *p;
    ngx_http_tnt_ctx_t  *ctx;
    ngx_buf_t           *b;

    b = ngx_create_temp_buf(r->pool, msglen);

    if (b == NULL) {
        return NGX_ERROR;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);

    b->memory = 1;
    b->pos = b->start;

    p = &escaped_msg[0];
    memset(escaped_msg, 0, sizeof(escaped_msg));
    const char *x;
    if ((x = json_encode_string((char **) &p, sizeof(escaped_msg) - 1,
                (const char *) msg, (size_t) len, true))
                    != NULL)
    {
        escaped_msg[0] = 0;
    }

    b->last = ngx_snprintf(b->start, msglen, "{"
                        "\"error\":{"
                            "\"code\":%d,"
                            "\"message\":%s"
                        "}"
                    "}",
                     errcode,
                     (escaped_msg[0] == 0 ? (u_char *) "\"\"" : escaped_msg));

    ctx->in_err = b;

    return NGX_OK;
}


static const ngx_http_tnt_error_t *
ngx_http_tnt_get_error_text(ngx_uint_t type)
{
    static const ngx_http_tnt_error_t errors[] = {

        {   ngx_string(
                "Request is too large, consider increasing your "
                "server's setting 'client_body_buffer_size'"),
            -32001
        },

        {   ngx_string("Unknown parse error"),
            -32002
        },

        {   ngx_string(
                "Request is too large, consider increasing your "
                "server's setting 'tnt_pass_http_request_buffer_size'"),
            -32001
        },

        {   ngx_string("The input has been mismatched by the format"),
            400
        },

        {   ngx_string(
                "The input has been reached limits, consider increasing "
                "server's settings 'tnt_select_limit_max', "
                "'tnt_allowed_spaces', 'tnt_allowed_indexes'"),
            405
        }

    };

    return &errors[type];
}

static size_t
ngx_http_tnt_overhead(void)
{
    return sizeof("[{"
        "'error': {"
            "'code':-XXXXX,"
            "'message':''"
        "},"
        "{ 'result': [[]],"
        "'id': 1867996680 }"
    "}");
}

