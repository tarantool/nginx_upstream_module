/*
 * Copyright (C)
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <tp_transcode.h>
#include <debug.h>

#if (NGX_HAVE_VARIADIC_MACROS)
# define log_crit(log, ...) \
     ngx_log_error_core(NGX_LOG_CRIT, (log), 0, __VA_ARGS__)
# define crit(...) log_crit(r->connection->log, __VA_ARGS__)
# else
/** TODO
 *  Warn. user here
 */
static inline void crit(...) {}
#endif // NGX_HAVE_VARIADIC_MACROS

enum ctx_state {
    OK = 0,
    INPUT_JSON_PARSE_FAILED,
    INPUT_TO_LARGE
};


typedef struct {
    ngx_http_upstream_conf_t upstream;

    size_t                   in_multiplier;
    size_t                   out_multiplier;

    ngx_int_t                index;
} ngx_http_tnt_loc_conf_t;


typedef struct {
    tp_transcode_t      in_t, out_t;

    ngx_buf_t           *in_cache;
    ngx_chain_t         *out_chain;

    ssize_t             payload;
    ngx_int_t           greetings:1;

    enum ctx_state      state;

    ngx_str_t errmsg;
} ngx_http_tnt_ctx_t;


static inline ngx_int_t ngx_http_tnt_read_greetings(ngx_http_request_t *r,
    ngx_http_tnt_ctx_t *ctx, ngx_buf_t *b);
static inline ngx_int_t ngx_http_tnt_say_error(ngx_http_request_t *r,
    ngx_http_tnt_ctx_t *ctx, ngx_int_t code);
static inline void ngx_http_tnt_cleanup(ngx_http_request_t *r);
static inline ngx_int_t ngx_http_set_input_parse_errmsg(ngx_http_request_t *r,
    ngx_http_tnt_ctx_t *ctx);

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


static size_t JSON_RPC_MAGIC = sizeof("{"
                        "'error': {"
                            "'code':-XXXXX,"
                            "'message':''"
                        "},"
                        "'result':{},"
                        "'id':4294967295"
                    "}") - 1;

static const u_char REQUEST_TOO_LARGE[] = "{"
                        "\"error\":{"
                            "\"code\":-32001,"
                            "\"message\":"
                                "\"Request too large, consider increasing your "
                                "server's setting 'client_body_buffer_size'\""
                        "}"
                    "}";

static const u_char UNKNOWN_PARSE_ERROR[] = "{"
                        "\"error\":{"
                            "\"code\":-32002,"
                            "\"message\":\"Unknown parse error\""
                        "}"
                    "}";

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
    ngx_http_tnt_ctx_t      *ctx;
    size_t                  complete_msg_size = 0;
    size_t                  output_size;
    ngx_http_tnt_loc_conf_t *tlcf;

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_tnt_module);

    if (r->headers_in.content_length_n == 0) {
        /** XXX
         *  Probably, this case we should handle like 'NOT ALLOWED'?
         */
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "empty body");
        return NGX_ERROR;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);
    if (ctx == NULL) {

        ctx = ngx_palloc(r->pool, sizeof(ngx_http_tnt_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        memset(ctx, 0, sizeof(ngx_http_tnt_ctx_t));

        ngx_http_set_ctx(r, ctx, ngx_http_tnt_module);

        ctx->out_chain = ngx_alloc_chain_link(r->pool);
        if (ctx->out_chain == NULL) {
            return NGX_ERROR;
        }

        ctx->out_chain->next = NULL;

        output_size = r->headers_in.content_length_n * tlcf->in_multiplier;
        ctx->out_chain->buf = ngx_create_temp_buf(r->pool, output_size);
        if (ctx->out_chain->buf == NULL) {
            crit("[BUG] failed to allocate output buffer, size %ui",
                    output_size);
            return NGX_ERROR;
        }
        ctx->out_chain->buf->last_in_chain = 1;

        dd("[output] out buffer size %i", (int)output_size);

        if (tp_transcode_init(&ctx->in_t, (char *)ctx->out_chain->buf->start,
                               output_size, YAJL_JSON_TO_TP, NULL)
                == TP_TRANSCODE_ERROR)
        {
            crit("[BUG] failed to call tp_transcode_init(input)");
            return NGX_ERROR;
        }

    } else /* return after NGX_AGAIN */ {

        if (ctx->in_t.codec.create == NULL) {
            crit("[BUG] ngx_http_tnt_create_request() invalid 'ctx'");
            goto error_exit;
        }

    }

    /** Input body transcoding
     */
    ctx->state = OK;

    for (body = r->upstream->request_bufs; body; body = body->next) {

        if (body->buf->in_file) {

            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "tnt: in-file buffer found. aborted. "
                "consider increasing your 'client_body_buffer_size' "
                "setting");

            ctx->errmsg.data = (u_char *)&REQUEST_TOO_LARGE[0];
            ctx->errmsg.len = sizeof(REQUEST_TOO_LARGE) - 1;

            ctx->state = INPUT_TO_LARGE;

            goto read_input_done;

        } else {
            b = body->buf;
        }

        if (tp_transcode(&ctx->in_t, (char *)b->pos, b->last - b->pos)
                == TP_TRANSCODE_ERROR)
        {
            dd("[input] failed: '%s'", ctx->in_t.errmsg);

            if (ngx_http_set_input_parse_errmsg(r, ctx) == NGX_ERROR) {
                return NGX_ERROR;
            }

            ctx->state = INPUT_JSON_PARSE_FAILED;

            goto read_input_done;
        }

    }

    if (tp_transcode_complete(&ctx->in_t, &complete_msg_size)
            == TP_TRANSCODE_OK)
    {
        ctx->out_chain->buf->last =
                ctx->out_chain->buf->start + complete_msg_size;
    }
    else
    {
        dd("[input] failed to complete");

        if (ngx_http_set_input_parse_errmsg(r, ctx) == NGX_ERROR) {
            return NGX_ERROR;
        }

        ctx->state = INPUT_JSON_PARSE_FAILED;

        goto read_input_done;
    }

read_input_done:
    if (ctx->state != OK && ctx->errmsg.data == NULL) {
        ctx->errmsg.data = (u_char *)&UNKNOWN_PARSE_ERROR[0];
        ctx->errmsg.len = sizeof(UNKNOWN_PARSE_ERROR) - 1;
    }

    tp_transcode_free(&ctx->in_t);

    /**
     * Hooking output chain
     */
    r->upstream->request_bufs = ctx->out_chain;

    return NGX_OK;

error_exit:
    tp_transcode_free(&ctx->in_t);
    return NGX_ERROR;
}


static ngx_int_t
ngx_http_tnt_reinit_request(ngx_http_request_t *r)
{
    dd("reinit connection with Tarantool...");

    ngx_http_tnt_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);
    if (ctx != NULL) {
        ngx_pfree(r->pool, ctx);
        ngx_http_set_ctx(r, NULL, ngx_http_tnt_module);
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_process_header(ngx_http_request_t *r)
{
    ngx_http_upstream_t *u = r->upstream;
    ngx_buf_t           *b = &r->upstream->buffer;

    ngx_int_t               rc;
    ngx_http_tnt_ctx_t      *ctx;
    ngx_http_tnt_loc_conf_t *tlcf;

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_tnt_module);

    ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);

    rc = ngx_http_tnt_read_greetings(r, ctx, b);
    if (rc != NGX_OK) {
        return rc;
    }

    switch (ctx->state) {
    case OK:
        break;
    case INPUT_TO_LARGE:
    case INPUT_JSON_PARSE_FAILED:
        return ngx_http_tnt_say_error(r, ctx, NGX_HTTP_BAD_REQUEST);
    default:
        assert(false);
        return NGX_ERROR;
    }

    if (b->last - b->pos == 0) {
        return NGX_AGAIN;
    }

    ctx->payload = tp_read_payload((char *)b->pos, (char *)b->last);
    switch (ctx->payload) {
    case 0:
        dd("tp_read_payload() need more bytes .. NGX_AGAIN");
        return NGX_AGAIN;
    case -1:
        crit("[BUG?] tarantool sent invalid 'payload' (i.e. message size)");
        return NGX_HTTP_UPSTREAM_INVALID_HEADER;
    default:

        u->headers_in.status_n = 200;
        u->state->status = 200;

        /** We can't get fair size of outgoing JSON message at this stage - so
         * just multiply msgpack payload in hope is more or enough
         * for the JSON message.
         */
        u->headers_in.content_length_n = JSON_RPC_MAGIC
                                        + ctx->payload * tlcf->out_multiplier;

        ctx->in_cache = ngx_create_temp_buf(r->pool, ctx->payload);
        if (ctx->in_cache == NULL) {
            return NGX_ERROR;
        }

        ctx->in_cache->pos = ctx->in_cache->start;
        ctx->in_cache->memory = 1;

        dd("got 'payload' input len:%i, _magic_ output len:%i",
            (int)ctx->payload,
            (int)u->headers_in.content_length_n);

        break;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_filter_init(void *data)
{
    ngx_http_request_t  *r = data;
    ngx_http_upstream_t *u = r->upstream;
    ngx_http_tnt_ctx_t  *ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);
    if (ctx == NULL) {
        crit("[BUG]  ngx_http_tnt_filter_init() invalid 'ctx'");
        u->length = 0;
        return NGX_ERROR;
    }

    u->length = ctx->payload;

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_filter(void *data, ssize_t bytes)
{
    ngx_http_request_t   *r = data;
    ngx_http_upstream_t  *u = r->upstream;
    ngx_buf_t            *b = &u->buffer;

    ngx_int_t            rc;
    ngx_http_tnt_ctx_t   *ctx;
    ngx_chain_t          *cl, **ll;

    /*
     * Transcoding tarantool message
     */
    ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);
    if (ctx == NULL) {
        crit("[BUG] ngx_http_tnt_filter() invalid 'ctx'");
        return NGX_ERROR;
    }

    if (ctx->state != OK) {
        crit("[BUG] ngx_http_tnt_filter() invalid 'ctx'");
        return NGX_ERROR;
    }

    dd("recv bytes:%i, ctx->payload:%i, u->length:%i",
            (int)bytes,
            (int)ctx->payload,
            (int)u->length);

    b->last = b->last + (bytes > ctx->payload ? bytes : ctx->payload);
    ctx->payload -= bytes;
    u->length -= bytes;

    if (ctx->in_cache->pos == ctx->in_cache->end) {
        crit("[BUG] ctx->in_cache overflow");
        return NGX_ERROR;
    }

    ctx->in_cache->pos = ngx_copy(ctx->in_cache->pos, b->pos, bytes);

    /* Done with tnt input
     */
    if (ctx->payload > 0) {
        return NGX_OK;
    }

    if (ctx->payload < 0) {
        /** Still have a chance to successfully parse incomming message ^_^
         */
        crit("[BUG] payload(%i) < 0", ctx->payload);
        u->length = 0;
    }

    for (cl = u->out_bufs, ll = &u->out_bufs; cl; cl = cl->next) {
        ll = &cl->next;
    }

    *ll = cl = ngx_chain_get_free_buf(r->pool, &u->free_bufs);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = ngx_create_temp_buf(r->pool, u->headers_in.content_length_n);
    if (cl->buf == NULL) {
        return NGX_ERROR;
    }

    cl->buf->memory = 1;
    cl->buf->flush = 1;
    cl->buf->tag = u->output.tag;
    cl->buf->last = cl->buf->end;

    rc = tp_transcode_init(&ctx->out_t, (char *)cl->buf->start,
                                        u->headers_in.content_length_n,
                                        TP_REPLY_TO_JSON,
                                        NULL);
    if (rc == TP_TRANSCODE_ERROR) {
        crit("[BUG] failed to call tp_transcode_init(output)");
        return NGX_ERROR;
    }

    ngx_int_t result = NGX_OK;
    rc = tp_transcode(&ctx->out_t, (char *)ctx->in_cache->start,
                      ctx->in_cache->end - ctx->in_cache->start);
    switch (rc) {
    case TP_TRANSCODE_OK: {

        size_t complete_msg_size = 0;
        rc = tp_transcode_complete(&ctx->out_t, &complete_msg_size);
        if (rc != TP_TRANSCODE_OK) {
            crit("[BUG] failed to complete output transcoding");
            result = NGX_ERROR;
        } else {

            /** 'erase' trailer
             */
            u_char *p = NULL;
            for (p = cl->buf->start + complete_msg_size;
                p < cl->buf->end;
                ++p)
            {
                *p = ' ';
            }

        }

        break;
    }

    case TP_TRANSCODE_ERROR:
    default:
        crit("[BUG] failed to transcode output, err: '%s'", ctx->out_t.errmsg);
        result = NGX_ERROR;
        break;
    }

    tp_transcode_free(&ctx->out_t);

    ctx->payload = 0;
    ngx_pfree(r->pool, ctx->in_cache);
    ctx->in_cache = NULL;
    ctx->state = OK;

    return result;
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


static inline ngx_int_t
ngx_http_tnt_read_greetings(ngx_http_request_t *r,
                            ngx_http_tnt_ctx_t *ctx,
                            ngx_buf_t *b)
{
    if (ctx->greetings) {
        return NGX_OK;
    }

    if (b->last - b->pos < 128) {
        crit("[BUG] Tarantool sent invalid greetings len:%i",
                b->last - b->pos);
        return NGX_ERROR;
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
        ctx->greetings = 1;
        return NGX_OK;
    }

    crit("[BUG] Tarantool sent strange greeting: '%.*s',"
        " expected 'Tarantool' with len. == 128",
        128, b->pos);

    return NGX_ERROR;
}


static inline ngx_int_t
ngx_http_set_input_parse_errmsg(ngx_http_request_t *r,
                                ngx_http_tnt_ctx_t *ctx)
{
    static const char ERR_RES_FMT[] = "{"
                        "\"error\":{"
                            "\"code\":%d,"
                            "\"message\":\"%s\""
                        "}"
                    "}";

    static const size_t ERR_RES_SIZE = sizeof("{"
                        "'error':{"
                            "'message':'',"
                            "'code':-XXXXX"
                        "}"
                    "}") - 1;

    if (ctx->in_t.errmsg[0] != 0) {
        ctx->errmsg.len = ngx_strlen(ctx->in_t.errmsg) + ERR_RES_SIZE;
        ctx->errmsg.data = ngx_palloc(r->pool, ctx->errmsg.len);
        if (ctx->errmsg.data == NULL) {
            return NGX_ERROR;
        }

        ngx_snprintf(ctx->errmsg.data, ctx->errmsg.len, ERR_RES_FMT,
                     ctx->in_t.errcode, ctx->in_t.errmsg);

        dd("parse error: %.*s", (int)ctx->errmsg.len, ctx->errmsg.data);

    }

    return NGX_OK;
}

static inline ngx_int_t ngx_http_tnt_say_error(ngx_http_request_t *r,
                                               ngx_http_tnt_ctx_t *ctx,
                                               ngx_int_t code)
{
    ngx_http_upstream_t  *u;
    ngx_chain_t          *cl, **ll;

    u = r->upstream;

    if (ctx->errmsg.data == NULL) {
        u->headers_in.status_n = 500;
        u->state->status = 500;
        u->headers_in.content_length_n = 0;
        return NGX_OK;
    }

    u->headers_in.status_n = code;
    u->state->status = code;
    u->headers_in.content_length_n = ctx->errmsg.len;
    u->length = ctx->errmsg.len;

    for (cl = u->out_bufs, ll = &u->out_bufs; cl; cl = cl->next) {
        ll = &cl->next;
    }

    *ll = cl = ngx_chain_get_free_buf(r->pool, &u->free_bufs);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = ngx_create_temp_buf(r->pool, ctx->errmsg.len);
    if (cl->buf == NULL) {
        return NGX_ERROR;
    }

    cl->buf->pos = cl->buf->start;
    cl->buf->last = ngx_snprintf(cl->buf->start, ctx->errmsg.len,
                                "%s", ctx->errmsg.data);
    cl->buf->end = cl->buf->last;

    cl->buf->flush = 1;
    cl->buf->memory = 1;
    cl->buf->tag = u->output.tag;

    ctx->state = OK;

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

    if (ctx->in_cache != NULL) {
        ngx_pfree(r->pool, ctx->in_cache);
    }

    ngx_http_set_ctx(r, NULL, ngx_http_tnt_module);
}

