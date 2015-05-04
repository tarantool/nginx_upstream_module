/*
 * Copyright (C)
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <tp_transcode.h>
#include <debug.h>

enum ctx_state {
    OK = 0,

    INPUT_JSON_PARSE_FAILED,
    INPUT_TO_LARGE,
    INPUT_EMPTY,

    READ_PAYLOAD,
    READ_BODY,
    SEND_REPLY
};


typedef struct ngx_http_tnt_error {
    const ngx_str_t msg;
    int code;
} ngx_http_tnt_error_t;


typedef struct {
    ngx_http_upstream_conf_t upstream;
    ngx_int_t                index;

    size_t                   in_multiplier;
    size_t                   out_multiplier;
} ngx_http_tnt_loc_conf_t;


typedef struct {

    struct {
        u_char mem[6];
        u_char *p, *e;
    } payload;

    enum ctx_state     state;
    ngx_buf_t          *in_err, *tp_cache;
    ssize_t            rest, payload_size;
    int8_t             rest_batch_size, batch_size;
    ngx_int_t          greeting:1;

} ngx_http_tnt_ctx_t;


/** Pre-filter functions
 */
static inline ngx_http_tnt_ctx_t * ngx_http_tnt_create_ctx(
    ngx_http_request_t *r);
static inline void ngx_http_tnt_reset_ctx(ngx_http_tnt_ctx_t *ctx);
static inline ngx_buf_t* ngx_http_set_err(ngx_http_request_t *r,
    int errcode, const u_char *msg, size_t msglen);
static inline ngx_int_t ngx_http_tnt_output_err(ngx_http_request_t *r,
    ngx_http_tnt_ctx_t *ctx, ngx_int_t code);

/** Filter functions
 */
static inline ngx_int_t ngx_http_tnt_read_greeting(ngx_http_request_t *r,
    ngx_http_tnt_ctx_t *ctx, ngx_buf_t *b);
static ngx_int_t ngx_http_tnt_send_reply(ngx_http_request_t *r,
    ngx_http_upstream_t *u, ngx_http_tnt_ctx_t *ctx);
static ngx_int_t ngx_http_tnt_filter_reply(ngx_http_request_t *r,
    ngx_http_upstream_t *u, ngx_buf_t *b);

/** Rest
 */
static inline void ngx_http_tnt_cleanup(ngx_http_request_t *r);
static inline ngx_buf_t * ngx_http_tnt_create_mem_buf(ngx_http_request_t *r,
                            ngx_http_upstream_t *u, size_t size);
static inline ngx_int_t ngx_http_tnt_output(ngx_http_request_t *r,
        ngx_http_upstream_t *u, ngx_buf_t *b);

/** Ngx handlers
 */
static ngx_int_t ngx_http_tnt_create_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_tnt_reinit_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_tnt_process_header(ngx_http_request_t *r);
static ngx_int_t ngx_http_tnt_filter_init(void *data);
static ngx_int_t ngx_http_tnt_filter(void *data, ssize_t bytes);
static void ngx_http_tnt_abort_request(ngx_http_request_t *r);
static void ngx_http_tnt_finalize_request(ngx_http_request_t *r, ngx_int_t rc);

static void *ngx_http_tnt_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_tnt_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);

static char *ngx_http_tnt_pass(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static const ngx_http_tnt_error_t errors[] = {

    {   ngx_string("Request too large, consider increasing your "
        "server's setting 'client_body_buffer_size'"),
        -32001
    },

    {   ngx_string("Unknown parse error"),
        -32002
    }
};

enum ngx_http_tnt_err_messages_idx {
    REQUEST_TOO_LARGE   = 0,
    UNKNOWN_PARSE_ERROR = 1
};

static size_t OVERHEAD = sizeof("{"
                        "'error': {"
                            "'code':-XXXXX,"
                            "'message':''"
                        "},"
                        "'result':{},"
                        "'id':4294967295"
                    "}") - 1;


static ngx_conf_bitmask_t  ngx_http_tnt_next_upstream_masks[] = {
    { ngx_string("error"), NGX_HTTP_UPSTREAM_FT_ERROR },
    { ngx_string("timeout"), NGX_HTTP_UPSTREAM_FT_TIMEOUT },
    { ngx_string("invalid_response"), NGX_HTTP_UPSTREAM_FT_INVALID_HEADER },
    { ngx_string("off"), NGX_HTTP_UPSTREAM_FT_OFF },
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

      ngx_null_command
};


static ngx_http_module_t  ngx_http_tnt_module_ctx = {
    NULL,                           /* preconfiguration */
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

/** Ngx handlers
 */
static ngx_int_t
ngx_http_tnt_handler(ngx_http_request_t *r)
{
    ngx_int_t               rc;
    ngx_http_upstream_t     *u;
    ngx_http_tnt_loc_conf_t *tlcf;

    if (!(r->method & NGX_HTTP_POST)) {
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

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_tnt_module);

    u->conf = &tlcf->upstream;

    u->create_request = ngx_http_tnt_create_request;
    u->reinit_request = ngx_http_tnt_reinit_request;
    u->process_header = ngx_http_tnt_process_header;
    u->abort_request = ngx_http_tnt_abort_request;
    u->finalize_request = ngx_http_tnt_finalize_request;

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


static ngx_int_t
ngx_http_tnt_create_request(ngx_http_request_t *r)
{
    ngx_buf_t               *b;
    ngx_chain_t             *body;
    size_t                  complete_msg_size;
    tp_transcode_t          tc;
    ngx_http_tnt_ctx_t      *ctx;
    ngx_chain_t             *out_chain;
    ngx_http_tnt_loc_conf_t *tlcf;

    if (r->headers_in.content_length_n == 0) {
        /** XXX
         *  Probably, this case we should handle like 'NOT ALLOWED'?
         */
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "empty body");
        return NGX_ERROR;
    }

    ctx = ngx_http_tnt_create_ctx(r);
    if (ctx == NULL) {
        return NGX_ERROR;
    }

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_tnt_module);

    out_chain = ngx_alloc_chain_link(r->pool);
    if (out_chain == NULL) {
        return NGX_ERROR;
    }

    out_chain->buf = ngx_create_temp_buf(r->pool,
                        r->headers_in.content_length_n * tlcf->in_multiplier);
    if (out_chain->buf == NULL) {
        crit("[BUG?] failed to allocate output buffer, size %ui",
              r->headers_in.content_length_n * tlcf->in_multiplier);
        return NGX_ERROR;
    }

    out_chain->next = NULL;
    out_chain->buf->memory = 1;
    out_chain->buf->flush = 1;

    out_chain->buf->pos = out_chain->buf->start;
    out_chain->buf->last = out_chain->buf->pos;
    out_chain->buf->last_in_chain = 1;

    /**
     *  Conv. input json to Tarantool message [
     */
    if (tp_transcode_init(&tc,
                          (char *)out_chain->buf->start,
                          out_chain->buf->end - out_chain->buf->start,
                          YAJL_JSON_TO_TP,
                          NULL)
            == TP_TRANSCODE_ERROR)
    {
        crit("[BUG] failed to call tp_transcode_init(input)");
        return NGX_ERROR;
    }

    for (body = r->upstream->request_bufs; body; body = body->next) {

        if (body->buf->in_file) {

            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "tnt: in-file buffer found. aborted. "
                "consider increasing your 'client_body_buffer_size' "
                "setting");

            const ngx_http_tnt_error_t *e = &errors[REQUEST_TOO_LARGE];
            ctx->in_err = ngx_http_set_err(r, e->code,
                                           e->msg.data, e->msg.len);
            if (ctx->in_err == NULL) {
                goto error_exit;
            }

            ctx->state = INPUT_TO_LARGE;

            goto read_input_done;

        } else {
            b = body->buf;
        }

        if (tp_transcode(&tc, (char *)b->pos, b->last - b->pos)
                == TP_TRANSCODE_ERROR)
        {
            ctx->in_err = ngx_http_set_err(r, tc.errcode,
                                           (u_char *)tc.errmsg,
                                           ngx_strlen(tc.errmsg));
            if (ctx->in_err == NULL) {
                goto error_exit;
            }

            ctx->state = INPUT_JSON_PARSE_FAILED;

            goto read_input_done;
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
        dd("[input] failed to complete");

        ctx->in_err = ngx_http_set_err(r, tc.errcode,
                                       (u_char *)tc.errmsg,
                                       ngx_strlen(tc.errmsg));
        if (ctx->in_err == NULL) {
            goto error_exit;
        }

        ctx->state = INPUT_JSON_PARSE_FAILED;

        goto read_input_done;
    }
    /** ]
     */

read_input_done:
    tp_transcode_free(&tc);

    /**
     * Hooking output chain
     */
    r->upstream->request_bufs = out_chain;

    return NGX_OK;

error_exit:
    tp_transcode_free(&tc);
    return NGX_ERROR;
}


static ngx_int_t
ngx_http_tnt_reinit_request(ngx_http_request_t *r)
{
    dd("reinit connection with Tarantool...");

    ngx_http_tnt_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);
    if (ctx == NULL) {
        return NGX_OK;
    }
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
        crit("[BUG] unexpected ctx->stage(%i)", ctx->state);
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
ngx_http_tnt_filter(void *data, ssize_t bytes)
{
    dd("filter");

    ngx_http_request_t   *r = data;
    ngx_http_upstream_t  *u = r->upstream;
    ngx_buf_t            *b = &u->buffer;

    b->last = b->last + bytes;

    /**
     *
     */
    ngx_int_t rc = NGX_OK;
    for (;;) {
        rc = ngx_http_tnt_filter_reply(r, u, b);
        if (rc != NGX_AGAIN) break;
        dd("Next message in same input buffer -- merge");
    }

    return rc;
}


static void
ngx_http_tnt_abort_request(ngx_http_request_t *r)
{
    dd("abort http tnt request");
    ngx_http_tnt_cleanup(r);
}


static void
ngx_http_tnt_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    dd("finalize http tnt request");
    ngx_http_tnt_cleanup(r);
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
     */

    conf->upstream.local = NGX_CONF_UNSET_PTR;

    conf->upstream.connect_timeout =
    conf->upstream.send_timeout =
    conf->upstream.read_timeout = NGX_CONF_UNSET_MSEC;

    conf->upstream.buffer_size =
    conf->in_multiplier =
    conf->out_multiplier = NGX_CONF_UNSET_SIZE;

    /*
     * The hardcoded values
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

    return NGX_CONF_OK;
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


/** Pre-filter functions
 */
static inline ngx_http_tnt_ctx_t *
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


static inline void
ngx_http_tnt_reset_ctx(ngx_http_tnt_ctx_t *ctx)
{
    ctx->payload.p = &ctx->payload.mem[0];
    ctx->payload.e = &ctx->payload.mem[sizeof(ctx->payload.mem) - 1];

    ctx->state = OK;

    ctx->in_err = ctx->tp_cache = NULL;

    ctx->rest =
    ctx->payload_size =
    ctx->rest_batch_size =
    ctx->batch_size =
    ctx->greeting = 0;
}


static inline ngx_buf_t*
ngx_http_set_err(ngx_http_request_t *r,
                int errcode,
                const u_char *msg, size_t len)
{
    const size_t msglen = len + sizeof("{"
                        "'error':{"
                            "'message':'',"
                            "'code':-XXXXX"
                        "}"
                    "}") - 1;

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

static inline ngx_int_t
ngx_http_tnt_output_err(ngx_http_request_t *r,
                        ngx_http_tnt_ctx_t *ctx,
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

/** Filter functions
 */
static inline ngx_int_t
ngx_http_tnt_read_greeting(ngx_http_request_t *r,
                            ngx_http_tnt_ctx_t *ctx,
                            ngx_buf_t *b)
{
    if (b->last - b->pos < 128) {
        crit("[BUG] Tarantool sent invalid greeting len:%i",
                b->last - b->pos);
        return NGX_AGAIN;
    }

    if (b->last - b->pos >= (ptrdiff_t)sizeof("Tarantool") - 1
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
        ctx->greeting = 1;

        /**
         *  Sometimes Nginx reads only 'greeting'(i.e. 128 bytes) -- to avoid
         *  side effects (inside 'init'/'filter') we must return to
         *  'process_header'
         */
        if (b->pos == b->last) {
            return NGX_AGAIN;
        }

        return NGX_OK;
    }

    crit("[BUG] Tarantool sent strange greeting: '%.*s',"
        " expected 'Tarantool' with len. == 128",
        128, b->pos);

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_tnt_send_reply(ngx_http_request_t *r,
                        ngx_http_upstream_t *u,
                        ngx_http_tnt_ctx_t *ctx)
{
    tp_transcode_t          tc;
    ngx_int_t               rc;
    ngx_http_tnt_loc_conf_t *tlcf;
    ngx_buf_t               *output;

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_tnt_module);

    output = ngx_http_tnt_create_mem_buf(r, u,
                    (ctx->tp_cache->end - ctx->tp_cache->start)
                        * tlcf->out_multiplier + OVERHEAD);
    if (output == NULL) {
        return NGX_ERROR;
    }

    if (ctx->batch_size > 0
        && ctx->rest_batch_size == ctx->batch_size)
    {
        *output->pos = '[';
        ++output->pos;
    }

    rc = tp_transcode_init(&tc,
                           (char *)output->pos, output->end - output->pos,
                           TP_REPLY_TO_JSON,
                           NULL);
    if (rc == TP_TRANSCODE_ERROR) {
        crit("[BUG] failed to call tp_transcode_init(output)");
        return NGX_ERROR;
    }

    rc = tp_transcode(&tc, (char *)ctx->tp_cache->start,
                      ctx->tp_cache->end - ctx->tp_cache->start);
    if (rc == TP_TRANSCODE_OK) {

        size_t complete_msg_size = 0;
        rc = tp_transcode_complete(&tc, &complete_msg_size);
        if (rc == TP_TRANSCODE_ERROR) {

            crit("[BUG] failed to complete output transcoding");

            ngx_pfree(r->pool, output);

            const ngx_http_tnt_error_t *e = &errors[UNKNOWN_PARSE_ERROR];
            output = ngx_http_set_err(r, e->code, e->msg.data, e->msg.len);
            if (output == NULL) {
                goto error_exit;
            }

            goto done;
        }

        output->last = output->pos + complete_msg_size;

    } else if (rc == TP_TRANSCODE_ERROR) {

        crit("[BUG] failed to transcode output, err: '%s'", tc.errmsg);

        ngx_pfree(r->pool, output);

        output = ngx_http_set_err(r,
                                  tc.errcode,
                                  (u_char *)tc.errmsg,
                                  ngx_strlen(tc.errmsg));
        if (output == NULL) {
            goto error_exit;
        }
    }

done:
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

    return ngx_http_tnt_output(r, u, output);

error_exit:
    tp_transcode_free(&tc);
    return NGX_ERROR;
}


static ngx_int_t
ngx_http_tnt_filter_reply(ngx_http_request_t *r,
                          ngx_http_upstream_t *u,
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
                crit("[BUG] tp_read_payload failed, ret:%i",
                        (int)ctx->payload_size);
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


/** Rest
 */

static inline ngx_buf_t *
ngx_http_tnt_create_mem_buf(ngx_http_request_t *r,
                            ngx_http_upstream_t *u,
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


static inline ngx_int_t
ngx_http_tnt_output(ngx_http_request_t *r,
                    ngx_http_upstream_t *u,
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


static inline void
ngx_http_tnt_cleanup(ngx_http_request_t *r)
{
    ngx_http_tnt_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);
    if (ctx == NULL) {
        crit("[BUG] nothing to cleanup");
        return;
    }

    ngx_pfree(r->pool, ctx);

    if (ctx->tp_cache != NULL) {
        ngx_pfree(r->pool, ctx->tp_cache);
    }

    ngx_http_set_ctx(r, NULL, ngx_http_tnt_module);
}

