
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


#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_config.h>

#include <debug.h>
#include <tp_ext.h>
#include <tp_transcode.h>
#include <ngx_http_tnt_version.h>


typedef enum ngx_tnt_conf_states {
    NGX_TNT_CONF_ON               = 1,
    NGX_TNT_CONF_OFF              = 2,
    NGX_TNT_CONF_PARSE_ARGS       = 4,
    NGX_TNT_CONF_UNESCAPE         = 8,
    NGX_TNT_CONF_PASS_BODY        = 16,
    NGX_TNT_CONF_PASS_HEADERS_OUT = 32,
    NGX_TNT_CONF_PARSE_URLENCODED = 64,
} ngx_tnt_conf_states_e;

typedef struct ngx_http_tnt_header_val_s ngx_http_tnt_header_val_t;

typedef ngx_int_t (*ngx_http_set_header_pt)(ngx_http_request_t *r,
    ngx_http_tnt_header_val_t *hv, ngx_str_t *value);

struct ngx_http_tnt_header_val_s {
    ngx_http_complex_value_t   value;
    ngx_str_t                  key;
    ngx_http_set_header_pt     handler;
};


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

    /** If it is set, then the client will recv. a pure result, e.g. {}
     * otherwise {"result":[], "id": NUM}id
     */
    ngx_uint_t               pure_result;

    /** Tarantool returns array of array as the result set,
     * this option will help avoid "array of array" behavior.
     * For instance.
     * If this option is set to 2, then the result will: result:{}.
     * If this option is set to 0, then the result will: result:[[{}]].
     */
    ngx_uint_t                multireturn_skip_count;

    ngx_array_t               *headers;

    /**
    enum tp_request_type      operation_type;
    int                       operation_space_id;
    ngx_str_t                 operation_format;
    */

} ngx_http_tnt_loc_conf_t;


/** Set of allowed REST methods
 */
static const ngx_uint_t ngx_http_tnt_allowed_methods =
    (NGX_HTTP_POST|NGX_HTTP_GET|NGX_HTTP_PUT|NGX_HTTP_PATCH|NGX_HTTP_DELETE);

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

    /**
     */
    ngx_int_t          url_encoded_body:1;

    /** The preset method and its length
     */
    u_char             preset_method[128];
    u_char             preset_method_len;
} ngx_http_tnt_ctx_t;

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

/** Filters */
static ngx_int_t ngx_http_tnt_filter_init(void *data);
static ngx_int_t ngx_http_tnt_send_reply(ngx_http_request_t *r,
    ngx_http_upstream_t *u, ngx_http_tnt_ctx_t *ctx);
static ngx_int_t ngx_http_tnt_filter_reply(ngx_http_request_t *r,
    ngx_http_upstream_t *u, ngx_buf_t *b);
static ngx_int_t ngx_http_tnt_filter(void *data, ssize_t bytes);

/** Other functions  */
static  ngx_buf_t * ngx_http_tnt_create_mem_buf(ngx_http_request_t *r,
    ngx_http_upstream_t *u, size_t size);
static  ngx_int_t ngx_http_tnt_output(ngx_http_request_t *r,
    ngx_http_upstream_t *u, ngx_buf_t *b);

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

/** Ctx */
static ngx_http_tnt_ctx_t *ngx_http_tnt_create_ctx(ngx_http_request_t *r);
static void ngx_http_tnt_reset_ctx(ngx_http_tnt_ctx_t *ctx);

/** Input handlers */
static ngx_int_t ngx_http_tnt_init_handlers(ngx_http_request_t *r,
        ngx_http_upstream_t *u, ngx_http_tnt_loc_conf_t *tlcf);
static ngx_int_t ngx_http_tnt_body_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_tnt_query_handler(ngx_http_request_t *r);

/** Upstream handlers */
static ngx_int_t ngx_http_tnt_reinit_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_tnt_process_header(ngx_http_request_t *r);
static void ngx_http_tnt_abort_request(ngx_http_request_t *r);
static void ngx_http_tnt_finalize_request(ngx_http_request_t *r, ngx_int_t rc);

static ngx_buf_t *ngx_http_tnt_set_err(ngx_http_request_t *r,
        int errcode, const u_char *msg, size_t msglen);
static const ngx_http_tnt_error_t *ngx_http_tnt_get_error_text(int type);

static size_t ngx_http_tnt_overhead(void);


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
#if 0
    { ngx_string("tnt_insert"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE3,
      ngx_http_tnt_insert_add,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },
#endif
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

/** Entry point [[
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

/** ]]
 */

/** Confs [[
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

    conf->upstream.connect_timeout =
    conf->upstream.send_timeout =
    conf->upstream.read_timeout =
    conf->upstream.next_upstream_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.next_upstream_tries = NGX_CONF_UNSET;

    conf->upstream.buffer_size =
    conf->in_multiplier =
    conf->out_multiplier =
    conf->multireturn_skip_count =
    conf->pass_http_request_buffer_size = NGX_CONF_UNSET_SIZE;

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


#if 0
static char *
ngx_http_tnt_insert_add(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_tnt_loc_conf_t *tlcf = conf;

    ngx_str_t                          *value;
    ngx_http_tnt_header_val_t          *hv;
    ngx_http_compile_complex_value_t   ccv;

    conf->operation_type = TP_INSERT;

    conf->operation_space_id = atoi((const char *) cf->args->elts[1]);
    if (conf->operation_space_id <= 0) {
        return "space id sould be integer value";
    }

    conf->operation_format = cf->args->elts[2];

    return NGX_CONF_OK;
}
#endif


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
    ngx_http_tnt_loc_conf_t *mlcf = conf;

    ngx_str_t                 *value;
    ngx_url_t                 u;
    ngx_http_core_loc_conf_t  *clcf;

    if (mlcf->upstream.upstream) {
        return "is duplicate";
    }

    value = cf->args->elts;

    ngx_memzero(&u, sizeof(ngx_url_t));

    u.url = value[1];
    u.no_resolve = 1;

    mlcf->upstream.upstream = ngx_http_upstream_add(cf, &u, 0);
    if (mlcf->upstream.upstream == NULL) {
        return NGX_CONF_ERROR;
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    clcf->handler = ngx_http_tnt_handler;

    if (clcf->name.data[clcf->name.len - 1] == '/') {
        clcf->auto_redirect = 1;
    }

    return NGX_CONF_OK;
}
/** ]]
 */

/** Filters [[
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

        /* Finishing
         */
        size_t complete_msg_size = 0;
        rc = tp_transcode_complete(&tc, &complete_msg_size);
        if (rc == TP_TRANSCODE_ERROR) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "[BUG] failed to complete output transcoding. "
                    "UNKNOWN ERROR");
            goto error_exit;
        }

        /* Ok. We send an error to the client from the Tarantool
         */
        if (rc == TP_TNT_ERROR) {
            ngx_pfree(r->pool, output);

            /* Swap output */
            output = ngx_http_tnt_set_err(r, tc.errcode,
                                          (u_char *)tc.errmsg,
                                          ngx_strlen(tc.errmsg));
            if (output == NULL) {
                goto error_exit;
            }
        } else {
            output->last = output->pos + complete_msg_size;
        }
    }
    /* Transcoder down
    */
    else {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
            "[BUG] failed to transcode output. errcode: '%d', errmsg: '%s'",
            tc.errcode, get_str_safe((const u_char *) tc.errmsg));
        goto error_exit;
    }

    /* Transcoding - OK
     */
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

    /** Reply was created in ngx_http_tnt_output_err. That means we don't need
     * output reply from the tarantool.
     *
     * if it will be outputed then the client will have two reply, which is
     * an error.
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
    ngx_http_tnt_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);
    ssize_t            bytes = b->last - b->pos;

    dd("filter_reply -> recv bytes: %i, rest: %i", (int)bytes, (int)ctx->rest);

    if (ctx->state == READ_PAYLOAD) {

        ssize_t payload_rest = ngx_min(ctx->payload.e - ctx->payload.p, bytes);
        if (payload_rest > 0) {
            ctx->payload.p = ngx_copy(ctx->payload.p, b->pos, payload_rest);
            bytes -= payload_rest;
            b->pos += payload_rest;
            payload_rest = ctx->payload.e - ctx->payload.p;

            dd("filter_reply -> payload rest:%i", (int)payload_rest);
        }

        if (payload_rest == 0) {
            ctx->payload_size = tp_read_payload((char *)&ctx->payload.mem[0],
                                                (char *)ctx->payload.e);
            if (ctx->payload_size <= 0) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                        "[BUG] tp_read_payload failed, ret:%i",
                        (int) ctx->payload_size);
                return NGX_ERROR;
            }

            ctx->rest = ctx->payload_size - 5 /* - header size */;

            dd("filter_reply -> got header payload:%i, rest:%i",
                    (int)ctx->payload_size,
                    (int)ctx->rest);

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

    ngx_int_t rc = NGX_OK;
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
                (int)read_on,
                (int)ctx->rest,
                (int)(ctx->tp_cache->end - ctx->tp_cache->pos),
                (int)(b->last - b->pos));
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
/** ]]
 */

/** Other functions and utils [[
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
/** ]]
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
    (void)ctx;
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
ngx_http_tnt_read_greeting(ngx_http_request_t *r,
                           ngx_http_tnt_ctx_t *ctx,
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
ngx_http_tnt_send_once(ngx_http_request_t *r,
                       ngx_http_tnt_ctx_t *ctx,
                       ngx_chain_t *out_chain,
                       const u_char *buf, size_t len)
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
                        ngx_http_request_t *r,
                        ngx_http_tnt_loc_conf_t *tlcf)
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


typedef struct ngx_http_tnt_next_arg {
  u_char *it, *value;
} ngx_http_tnt_next_arg_t;


static ngx_http_tnt_next_arg_t
ngx_http_tnt_get_next_arg(u_char *it, u_char *end)
{
    ngx_http_tnt_next_arg_t next_arg = { .it = end, .value = NULL };

    for ( ; it != end; ++it) {

        if (*it == '=') {
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
    u_char *dkey = key;
    u_char *dvalue = value;

    u_char *ekey = NULL, *ekey_end = NULL;
    u_char *evalue = NULL, *evalue_end = NULL;

    if (tlcf->pass_http_request & NGX_TNT_CONF_UNESCAPE) {

        ekey_end = ekey = ngx_pnalloc(r->pool, key_len);
        if (ekey == NULL) {
            return NGX_ERROR;
        }

        ngx_unescape_uri(&ekey_end, &key, key_len, NGX_UNESCAPE_URI);
        key_len = ekey_end - ekey;

        evalue_end = evalue = ngx_pnalloc(r->pool, value_len);
        if (evalue == NULL) {
            return NGX_ERROR;
        }

        ngx_unescape_uri(&evalue_end, &value, value_len, NGX_UNESCAPE_URI);
        value_len = evalue_end - evalue;

        dkey = ekey;
        dvalue = evalue;
    }

#if 0
    dd("ngx_http_tnt_encode_str_map_item, dkey = %.*s, dvalue = %.*s",
       (int)key_len, (char *)dkey, (int)value_len, (char *)dvalue);
#endif

    if (!tp_encode_str_map_item(tp,
                                (const char *) dkey, key_len,
                                (const char *) dvalue, value_len))
    {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
            "ngx_http_tnt_encode_str_map_item: tp_encode_str_map_item failed");
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_encode_query_args(ngx_http_request_t *r,
                               ngx_http_tnt_loc_conf_t *tlcf,
                               struct tp *tp,
                               ngx_str_t *args,
                               ngx_uint_t *args_items)
{
    u_char                  *arg_begin, *end;
    ngx_http_tnt_next_arg_t arg;

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

        value_len = arg.it - arg.value;

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
     *      This function should be part of tp_transcode.{c,h}. It's very
     *      strange have this function here, so the best way is piece by piece
     *      move a functionality ...
     *
     *      Also it would be nice to tie nginx structures with tp_transcode
     */
    char                *root_map_place;
    char                *map_place;
    size_t              root_items;
    size_t              map_items;
    ngx_buf_t           *b;
    ngx_chain_t         *body;
    char                *p;
    ngx_buf_t           unparsed_body;

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

    /** Encode raw uri */
    ++root_items;

    if (ngx_http_tnt_encode_str_map_item(r, tlcf, tp,
                                         (u_char *) "uri", sizeof("uri") - 1,
                                         r->unparsed_uri.data,
                                         r->unparsed_uri.len) == NGX_ERROR)
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

        if (ngx_http_tnt_encode_query_args(
                    r, tlcf, tp, &r->args, &map_items) == NGX_ERROR)
        {
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
    if ((tlcf->pass_http_request & NGX_TNT_CONF_PASS_BODY ||
                tlcf->pass_http_request & NGX_TNT_CONF_PARSE_URLENCODED) &&
            r->headers_in.content_length_n > 0 &&
            r->upstream->request_bufs)
    {
        ++root_items;

        if (tp_encode_str(tp, "body", sizeof("body") - 1) == NULL) {
            goto oom_cant_encode_body;
        }

        /** Encode urlencoded body as map - body = { K = V, .. } */
        if (tlcf->pass_http_request & NGX_TNT_CONF_PARSE_URLENCODED) {

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
                        "tnt: in-file buffer found. aborted. "
                        "consider increasing your 'client_body_buffer_size' "
                        "setting");
                    return NGX_ERROR;
                }

                unparsed_body.last = ngx_copy(unparsed_body.last,
                            b->pos, b->last - b->pos);
            }

            /** Actually this is array not map, I used this variable since it's
             * avaliable
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
        else {

            int sz = mp_sizeof_str(r->headers_in.content_length_n);
            if (tp_ensure(tp, sz) == -1) {
                goto oom_cant_encode_body;
            }

            p = mp_encode_strl(tp->p, r->headers_in.content_length_n);

            for (body = r->upstream->request_bufs; body; body = body->next) {

                b = body->buf;

                if (b->in_file) {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                        "tnt: in-file buffer found. aborted. "
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
    }

    *(root_map_place++) = 0xdf;
    *(uint32_t *) root_map_place = mp_bswap_u32(root_items);

    return NGX_OK;

oom_cant_encode:
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
            "tnt: can't encode uri, schema etc. "
            "aborted. consider increasing your "
            "'tnt_pass_http_request_buffer_size' setting");
    return NGX_ERROR;

oom_cant_encode_headers:
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
            "tnt: can't encode HTTP headers. "
            "aborted. consider increasing your "
            "'tnt_pass_http_request_buffer_size' setting");
    return NGX_ERROR;

oom_cant_encode_body:

    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
            "tnt: can't encode body. aborted. consider increasing your "
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

    /** Default */
    u->create_request = ngx_http_tnt_query_handler;

#if 0
    if (tlcf->operation_type != 0) {
        return NGX_OK;
    }
#endif

    if (tlcf->pass_http_request & NGX_TNT_CONF_PASS_BODY) {
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
                "ngx_http_tnt_body_handler -- "
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

    /**
     *  Conv. input (json, x-url-encoded) into Tarantool's message [
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

    /** Parse url-encoded*/
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
                    "tnt: in-file buffer found. aborted. "
                    "consider increasing your 'client_body_buffer_size' "
                    "setting");

                e = ngx_http_tnt_get_error_text(REQUEST_TOO_LARGE);
                ctx->in_err = ngx_http_tnt_set_err(r, e->code,
                        e->msg.data, e->msg.len);
                if (ctx->in_err == NULL) {
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
            ctx->batch_size,
            tc.batch_size,
            (int)complete_msg_size);

    } else {
        ctx->state = INPUT_JSON_PARSE_FAILED;
        goto read_input_done;
    }
    /** ]
     */
read_input_done:

    if (ctx->state != OK) {

        if (ctx->in_err == NULL) {
           ctx->in_err = ngx_http_tnt_set_err(r,
                                              tc.errcode,
                                              (u_char *) tc.errmsg,
                                              ngx_strlen(tc.errmsg));
            if (ctx->in_err == NULL) {
                goto error_exit;
            }
        }

        /* Rewrite output buffer since it may be less then needed
         */
        static const u_char fd_event[] =
              "{\"method\":\"__nginx_needs_fd_event\",\"params\":[]}";

        out_chain->buf = ngx_create_temp_buf(r->pool,
                                    sizeof(fd_event) * tlcf->in_multiplier);
        if (out_chain->buf == NULL) {
            goto error_exit;
        }

        out_chain->next = NULL;
        out_chain->buf->memory = 1;
        out_chain->buf->flush = 1;

        out_chain->buf->pos = out_chain->buf->start;
        out_chain->buf->last = out_chain->buf->pos;
        out_chain->buf->last_in_chain = 1;

        /** Fire event manualy on tarantool socket.
         *  This is need for run all parts of sequence
         */
        if (ngx_http_tnt_send_once(r,
                                   ctx,
                                   out_chain,
                                   fd_event, ngx_strlen(fd_event))
                  != NGX_OK)
        {
          dd("ngx_http_tnt_send_once (i.e. file fd event) failed");
          goto error_exit;
        }
    }

    /**
     * Hooking output chain
     */
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
ngx_http_tnt_reinit_request(ngx_http_request_t *r)
{
    dd("reinit connection with Tarantool...");

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

    dd("process_header-> greeting: '%s', recv: %i",
            ctx->greeting ? "yes" : "no",
            (int)(b->last - b->pos));

    if (!ctx->greeting) {

        rc = ngx_http_tnt_read_greeting(r, ctx, b);
        if (rc == NGX_ERROR) {
            return rc;

        /**
         *   If ctx->state is not OK we did not sent request to Tarantool
         *   backend but we still must handle ctx->state at this stage --
         *   so just ignore NGX_AGAIN and pass to next handler.
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
    dd("abort http tnt request");
    ngx_http_tnt_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);
    ngx_http_tnt_cleanup(r, ctx);
}


static void
ngx_http_tnt_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    dd("finalize http tnt request");
    ngx_http_tnt_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);
    ngx_http_tnt_cleanup(r, ctx);
}


static ngx_buf_t*
ngx_http_tnt_set_err(ngx_http_request_t *r,
                     int errcode,
                     const u_char *msg, size_t len)
{
    const size_t msglen = len + sizeof("{"
                        "'error':{"
                            "'message':'',"
                            "'code':-XXXXX"
                        "}"
                    "}");

    ngx_buf_t *b = ngx_create_temp_buf(r->pool, msglen);
    if (b == NULL) {
        return NULL;
    }

    b->memory = 1;
    b->pos = b->start;

    b->last = ngx_snprintf(b->start, msglen, "{"
                        "\"error\":{"
                            "\"code\":%d,"
                            "\"message\":\"%s\""
                        "}"
                    "}",
                     errcode,
                     msg);

    return b;
}


static const ngx_http_tnt_error_t *
ngx_http_tnt_get_error_text(int type)
{
    static const ngx_http_tnt_error_t errors[] = {

        {   ngx_string("Request too large, consider increasing your "
            "server's setting 'client_body_buffer_size'"),
            -32001
        },

        {   ngx_string("Unknown parse error"),
            -32002
        },

        {   ngx_string("Request too largs, consider increasing your "
            "server's setting 'tnt_pass_http_request_buffer_size'"),
            -32001
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

