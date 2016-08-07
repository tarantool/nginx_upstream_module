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

#include <ngx_config.h>

#include <debug.h>
#include <ngx_http_tnt_handlers.h>


/** Filter functions
 */
static ngx_int_t ngx_http_tnt_send_reply(
    ngx_http_request_t *r,
    ngx_http_upstream_t *u,
    ngx_http_tnt_ctx_t *ctx);

static ngx_int_t ngx_http_tnt_filter_reply(
    ngx_http_request_t *r,
    ngx_http_upstream_t *u,
    ngx_buf_t *b);

/** Rest
 */
static inline ngx_buf_t * ngx_http_tnt_create_mem_buf(
    ngx_http_request_t *r,
    ngx_http_upstream_t *u,
    size_t size);

static inline ngx_int_t ngx_http_tnt_output(
    ngx_http_request_t *r,
    ngx_http_upstream_t *u,
    ngx_buf_t *b);

/** Filters
 */
static ngx_int_t ngx_http_tnt_filter_init(void *data);
static ngx_int_t ngx_http_tnt_filter(void *data, ssize_t bytes);

/** Confs
 */
static void *ngx_http_tnt_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_tnt_merge_loc_conf(
    ngx_conf_t *cf,
    void *parent,
    void *child);

static char *ngx_http_tnt_pass(
    ngx_conf_t *cf,
    ngx_command_t *cmd,
    void *conf);

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
    { ngx_null_string, 0 }
};

static ngx_conf_bitmask_t  ngx_http_tnt_methods[] = {
    { ngx_string("get"), NGX_HTTP_GET },
    { ngx_string("post"), NGX_HTTP_POST },
    { ngx_string("put"), NGX_HTTP_PUT },
    { ngx_string("delete"), NGX_HTTP_DELETE },
    { ngx_string("all"), (NGX_CONF_BITMASK_SET
                          |NGX_HTTP_GET
                          |NGX_HTTP_POST
                          |NGX_HTTP_PUT
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
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, method),
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

    { ngx_string("tnt_http_methods"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, http_methods),
      &ngx_http_tnt_methods },

    /* Experimental feature:
     *  the feature allow to skip top part of result scheme [[
     */
    { ngx_string("tnt_pure_result"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF
          |NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, pure_result),
      &ngx_http_tnt_pass_http_request_masks },

    { ngx_string("tnt_multireturn_skip_count"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF
          |NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_loc_conf_t, multireturn_skip_count),
      NULL },
    /* ]]
     */

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

/** Handlers
 */

static ngx_int_t
ngx_http_tnt_handler(ngx_http_request_t *r)
{
    ngx_int_t               rc;
    ngx_http_upstream_t     *u;
    ngx_http_tnt_loc_conf_t *tlcf;

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_tnt_module);

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
    conf->pass_http_request_buffer_size =
    conf->multireturn_skip_count = NGX_CONF_UNSET_SIZE;

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

    ngx_conf_merge_str_value(conf->method, prev->method, "");

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

    /** Experimental, see conf commands description [[
     */
    ngx_conf_merge_bitmask_value(conf->pure_result,
                  prev->pure_result, NGX_TNT_CONF_OFF);

    ngx_conf_merge_size_value(conf->multireturn_skip_count,
                  prev->multireturn_skip_count, 0);

    /* ]]
     */

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

/** Filter functions
 */

static ngx_int_t
ngx_http_tnt_send_reply(ngx_http_request_t *r,
                        ngx_http_upstream_t *u,
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
        .output = (char *)output->pos,
        .output_size = output->end - output->pos,
        .method = NULL, .method_len = 0,
        .codec = TP_REPLY_TO_JSON,
        .mf = NULL
    };

    rc = tp_transcode_init(&tc, &args);
    if (rc == TP_TRANSCODE_ERROR) {
        crit("[BUG] failed to call tp_transcode_init(output)");
        return NGX_ERROR;
    }

    tp_reply_to_json_set_options(&tc,
                                 tlcf->pure_result == NGX_TNT_CONF_ON,
                                 tlcf->multireturn_skip_count);

    rc = tp_transcode(&tc, (char *)ctx->tp_cache->start,
                      ctx->tp_cache->end - ctx->tp_cache->start);
    if (rc == TP_TRANSCODE_OK) {

        size_t complete_msg_size = 0;
        rc = tp_transcode_complete(&tc, &complete_msg_size);
        if (rc == TP_TRANSCODE_ERROR) {

            crit("[BUG] failed to complete output transcoding");

            ngx_pfree(r->pool, output);

            const ngx_http_tnt_error_t *e = get_error_text(UNKNOWN_PARSE_ERROR);
            output = ngx_http_tnt_set_err(r, e->code, e->msg.data, e->msg.len);
            if (output == NULL) {
                goto error_exit;
            }

            goto done;
        }

        output->last = output->pos + complete_msg_size;

    } else if (rc == TP_TRANSCODE_ERROR) {

        crit("[BUG] failed to transcode output, err: '%s'", tc.errmsg);

        ngx_pfree(r->pool, output);

        output = ngx_http_tnt_set_err(r,
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

