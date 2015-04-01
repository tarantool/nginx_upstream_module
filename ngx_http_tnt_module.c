
/*
 * Copyright (C)
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <ngx_http_tnt_codec.h>


typedef struct {
    ngx_http_upstream_conf_t   upstream;
    ngx_int_t                  index;
} ngx_http_tnt_loc_conf_t;


typedef struct {
    ngx_http_request_t        *request;
    ngx_uint_t                method_type;
} ngx_http_tnt_ctx_t;


static ngx_int_t ngx_http_tnt_create_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_tnt_reinit_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_tnt_process_header(ngx_http_request_t *r);
static ngx_int_t ngx_http_tnt_filter_init(void *data);
static ngx_int_t ngx_http_tnt_filter(void *data, ssize_t bytes);
static void ngx_http_tnt_abort_request(ngx_http_request_t *r);
static void ngx_http_tnt_finalize_request(ngx_http_request_t *r,
    ngx_int_t rc);

static void *ngx_http_tnt_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_tnt_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);

static char *ngx_http_tnt_pass(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static ngx_command_t  ngx_http_tnt_commands[] = {

    { ngx_string("tnt_pass"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_http_tnt_pass,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_tnt_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_tnt_create_loc_conf,          /* create location configuration */
    ngx_http_tnt_merge_loc_conf            /* merge location configuration */
};


ngx_module_t  ngx_http_tnt_module = {
    NGX_MODULE_V1,
    &ngx_http_tnt_module_ctx,              /* module context */
    ngx_http_tnt_commands,                 /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_http_tnt_handler(ngx_http_request_t *r)
{
    ngx_int_t                   rc;
    ngx_http_upstream_t         *u;
    ngx_http_tnt_ctx_t          *ctx;
    ngx_http_tnt_loc_conf_t     *mlcf;

    if (!(r->method & NGX_HTTP_POST)) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    if (ngx_http_set_content_type(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ngx_http_upstream_create(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    u = r->upstream;

    ngx_str_set(&u->schema, "tnt://");
    u->output.tag = (ngx_buf_tag_t) &ngx_http_tnt_module;

    mlcf = ngx_http_get_module_loc_conf(r, ngx_http_tnt_module);

    u->conf = &mlcf->upstream;

    u->create_request = ngx_http_tnt_create_request;
    u->reinit_request = ngx_http_tnt_reinit_request;
    u->process_header = ngx_http_tnt_process_header;
    u->abort_request = ngx_http_tnt_abort_request;
    u->finalize_request = ngx_http_tnt_finalize_request;

    ctx = ngx_palloc(r->pool, sizeof(ngx_http_tnt_ctx_t));
    if (ctx == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    u->input_filter_init = ngx_http_tnt_filter_init;
    u->input_filter = ngx_http_tnt_filter;
    u->input_filter_ctx = ctx;

    ctx->request = r;

    ngx_http_set_ctx(r, ctx, ngx_http_tnt_module);

    rc = ngx_http_read_client_request_body(r, ngx_http_upstream_init);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    return NGX_DONE;
}


static ngx_int_t
ngx_http_tnt_create_request(ngx_http_request_t *r)
{
    ngx_buf_t *input = r->request_body->bufs->buf;

    ngx_int_t       rc;
    ngx_buf_t       *output;
    ngx_chain_t     *cl;

    if (!input) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                "invalid request_body->bufs->buf");
        return NGX_ERROR;
    }

    transcode_t *tc = tc_create(r->connection->pool,
            (const u_char *)"json RPC 2.0", (const u_char *)"msgpack");
    if (tc == NULL) {
        return NGX_ERROR;
    }

    output = NULL;
    rc = tc_decode(tc, input, output);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    output = ngx_create_temp_buf(r->pool, sizeof("a") - 1);
    if (output == NULL) {
        return NGX_ERROR;
    }

    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = output;
    r->upstream->request_bufs = cl;

    output->pos = (u_char*)"a";
    output->last = output->pos + sizeof("a") - 1;

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_reinit_request(ngx_http_request_t *r)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "reinit http tnt request");
    return NGX_ERROR;
}


static ngx_int_t
ngx_http_tnt_process_header(ngx_http_request_t *r)
{
    ngx_http_upstream_t *u = r->upstream;
    (void)u;
    return NGX_ERROR;
}


static ngx_int_t
ngx_http_tnt_filter_init(void *data)
{
    (void)data;
    return NGX_ERROR;
}


static ngx_int_t
ngx_http_tnt_filter(void *data, ssize_t bytes)
{
    (void)data, (void)bytes;
    return NGX_ERROR;
}


static void
ngx_http_tnt_abort_request(ngx_http_request_t *r)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "abort http tnt request");
    return;
}


static void
ngx_http_tnt_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "finalize http tnt request");
    return;
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

    conf->upstream.local                 = NGX_CONF_UNSET_PTR;
    conf->upstream.next_upstream_tries   = NGX_CONF_UNSET_UINT;
    conf->upstream.connect_timeout       =
    conf->upstream.send_timeout          =
    conf->upstream.read_timeout          =
    conf->upstream.next_upstream_timeout = NGX_CONF_UNSET_MSEC;

    conf->upstream.buffer_size           = NGX_CONF_UNSET_SIZE;

    /* the hardcoded values */
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

    ngx_conf_merge_uint_value(conf->upstream.next_upstream_tries,
                              prev->upstream.next_upstream_tries, 0);

    ngx_conf_merge_msec_value(conf->upstream.connect_timeout,
                              prev->upstream.connect_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.send_timeout,
                              prev->upstream.send_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.read_timeout,
                              prev->upstream.read_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.next_upstream_timeout,
                              prev->upstream.next_upstream_timeout, 0);

    ngx_conf_merge_size_value(conf->upstream.buffer_size,
                              prev->upstream.buffer_size,
                              (size_t) ngx_pagesize);

    if (conf->upstream.upstream == NULL) {
        conf->upstream.upstream = prev->upstream.upstream;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_tnt_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_tnt_loc_conf_t *mlcf = conf;

    ngx_str_t                 *value;
    ngx_url_t                  u;
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
