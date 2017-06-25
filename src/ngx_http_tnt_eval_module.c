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
 * Copyright (C) 2017 Tarantool AUTHORS:
 * please see AUTHORS file.
 */

#include "debug.h"

#include <yajl/yajl_tree.h>

#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_http_variable_t        *variable;
    ngx_uint_t                  index;
} ngx_http_tnt_eval_variable_t;


typedef struct {
    ngx_array_t                *variables;
    ngx_str_t                   eval_location;
    size_t                      buffer_size;
} ngx_http_tnt_eval_loc_conf_t;


typedef struct {
    ngx_http_tnt_eval_loc_conf_t   *base_conf;
    ngx_http_variable_value_t      **values;
    ngx_int_t                      status;
    ngx_int_t                      done:1;
    ngx_int_t                      in_progress:1;

    /* A reference to the main request */
    ngx_http_request_t             *r;

    /* A body passed from the Tarantool */
    ngx_buf_t                      buffer;

} ngx_http_tnt_eval_ctx_t;


static ngx_int_t
ngx_http_tnt_eval_init_variables(ngx_http_request_t *r,
    ngx_http_tnt_eval_ctx_t *ctx, ngx_http_tnt_eval_loc_conf_t *ecf);

static ngx_int_t ngx_http_tnt_eval_post_subrequest_handler(ngx_http_request_t *r,
    void *data, ngx_int_t rc);

static void *ngx_http_tnt_eval_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_tnt_eval_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);

static char *ngx_http_tnt_eval_block(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static ngx_int_t ngx_http_tnt_eval_header_filter(ngx_http_request_t *r);
static ngx_int_t ngx_http_tnt_eval_body_filter(ngx_http_request_t *r,
    ngx_chain_t *in);

static ngx_int_t ngx_http_tnt_eval_discard_bufs(ngx_pool_t *pool, ngx_chain_t *in);

static ngx_int_t ngx_http_tnt_eval_init(ngx_conf_t *cf);

static ngx_int_t ngx_http_tnt_eval_parse_meta(ngx_http_request_t *r,
    ngx_http_tnt_eval_ctx_t *ctx, ngx_http_variable_value_t *v);
static ngx_int_t ngx_http_tnt_eval_output(ngx_http_request_t *r,
    ngx_http_tnt_eval_ctx_t *ctx);
static ngx_int_t ngx_http_tnt_subrequest(ngx_http_request_t *r,
    ngx_str_t *uri, ngx_str_t *args, ngx_http_request_t **psr,
    ngx_http_post_subrequest_t *ps, ngx_uint_t flags);
static void ngx_http_tnt_eval_finalize_request(ngx_http_request_t *r);


static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;

/** if tarantool wants return some headers, status and so on, then
 *  nginx expectes that tarantool will output:
 *      [{__nginx = [STATUS, {HEADERS}]}, {H1:V1 ..}]
 */
static const char *tarantool_ngx_path[] = { "__ngx", (const char *) 0 };


static ngx_command_t  ngx_http_tnt_eval_commands[] = {

    { ngx_string("tnt_eval"),
      NGX_HTTP_LOC_CONF|NGX_CONF_2MORE|NGX_CONF_BLOCK,
      ngx_http_tnt_eval_block,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("tnt_eval_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_tnt_eval_loc_conf_t, buffer_size),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_tnt_eval_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_tnt_eval_init,            /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_tnt_eval_create_loc_conf, /* create location configuration */
    ngx_http_tnt_eval_merge_loc_conf   /* merge location configuration */
};


ngx_module_t  ngx_http_tnt_eval_module = {
    NGX_MODULE_V1,
    &ngx_http_tnt_eval_module_ctx,     /* module context */
    ngx_http_tnt_eval_commands,        /* module directives */
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
ngx_http_tnt_eval_handler(ngx_http_request_t *r)
{
    ngx_str_t                          args;
    ngx_str_t                          subrequest_uri;
    ngx_uint_t                         flags;
    ngx_http_tnt_eval_loc_conf_t       *ecf;
    ngx_http_tnt_eval_ctx_t            *ctx;
    ngx_http_tnt_eval_ctx_t            *sr_ctx;
    ngx_http_request_t                 *sr;
    ngx_int_t                          rc;
    ngx_http_post_subrequest_t         *psr;
    u_char                             *p;

    ecf = ngx_http_get_module_loc_conf(r, ngx_http_tnt_eval_module);

    /* Modules is not on */
    if (ecf->variables == NULL || !ecf->variables->nelts) {
        return NGX_DECLINED;
    }

    rc = ngx_http_read_client_request_body(r,
                ngx_http_tnt_eval_finalize_request);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_eval_module);
    if (ctx == NULL) {

        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_tnt_eval_ctx_t));
        if (ctx == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        dd("A new context has been created, r = %p, ctx = %p", r, ctx);

        ctx->base_conf = ecf;
        ctx->r = r;

        ngx_http_set_ctx(r, ctx, ngx_http_tnt_eval_module);
    }

    if (ctx->done) {
        dd("subrequest done, r = %p, ctx = %p", r, ctx);

        /** If tnt_pass is down, then status should be passed to the client.
         * 0 meas that upstream works
         */
        if (ctx->status != 0) {
            return ctx->status;
        }

        return NGX_DECLINED;
    }

    if (ctx->in_progress) {
        dd("still in progress, r = %p, ctx = %p", r, ctx);
        return NGX_DONE;
    }

    psr = ngx_palloc(r->pool, sizeof(ngx_http_post_subrequest_t));
    if (psr == NULL) {
        return NGX_ERROR;
    }

    if (ngx_http_tnt_eval_init_variables(r, ctx, ecf) != NGX_OK) {
        return NGX_ERROR;
    }

    args = r->args;
    flags = 0;

    subrequest_uri.len = ecf->eval_location.len + r->uri.len;

    p = ngx_palloc(r->pool, subrequest_uri.len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    subrequest_uri.data = p;

    p = ngx_copy(p, ecf->eval_location.data, ecf->eval_location.len);
    ngx_memcpy(p, r->uri.data, r->uri.len);

    if (ngx_http_parse_unsafe_uri(r, &subrequest_uri, &args, &flags)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    psr->handler = ngx_http_tnt_eval_post_subrequest_handler;
    psr->data = ctx;

    dd("issue subrequest");

    flags |= NGX_HTTP_SUBREQUEST_WAITED;

    rc = ngx_http_tnt_subrequest(r, &subrequest_uri, &args, &sr, psr, flags);
    if (rc == NGX_ERROR || rc == NGX_DONE) {
        return rc;
    }

    ctx->in_progress = 1;

    /** We don't allow eval in subrequests
     */
    sr_ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_tnt_eval_ctx_t));
    if (sr_ctx == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ngx_http_set_ctx(sr, sr_ctx, ngx_http_tnt_eval_module);

    dd("wait for subrequest to complete");

    return NGX_DONE;
}


static ngx_int_t
ngx_http_tnt_eval_init_variables(ngx_http_request_t *r,
    ngx_http_tnt_eval_ctx_t *ctx, ngx_http_tnt_eval_loc_conf_t *ecf)
{
    ngx_uint_t                       i;
    ngx_http_tnt_eval_variable_t *variable;

    ctx->values = ngx_pcalloc(r->pool,
                              ecf->variables->nelts
                              * sizeof(ngx_http_variable_value_t *));

    if (ctx->values == NULL) {
        return NGX_ERROR;
    }

    variable = ecf->variables->elts;

    for (i = 0; i < ecf->variables->nelts; i++) {
        ctx->values[i] = r->variables + variable[i].index;
        ctx->values[i]->valid = 0;
        ctx->values[i]->not_found = 1;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_eval_post_subrequest_handler(ngx_http_request_t *r, void *data,
    ngx_int_t rc)
{
    ngx_http_tnt_eval_ctx_t *ctx = data;

    ngx_http_tnt_eval_output(r, ctx);

    ctx->done = 1;

    ctx->status = rc;

#if 0
    /* work-around a bug in the nginx core (ngx_http_named_location)
     */
    r->parent->write_event_handler = ngx_http_core_run_phases;
#endif

    return NGX_OK;
}


/** Parse meta data iff it was sent by Tarantool
 */
static ngx_int_t
ngx_http_tnt_eval_parse_meta(ngx_http_request_t *r,
    ngx_http_tnt_eval_ctx_t *ctx, ngx_http_variable_value_t *v /* body */)
{
    char                             errbuf[1024];
    const char                       *y_header;
    const char                       *tmp;
    yajl_val                         y_root;
    yajl_val                         y_node;
    yajl_val                         y_headers;
    yajl_val                         y_header_value;
    ngx_uint_t                       i;
    ngx_table_elt_t                  *h;
    ngx_http_variable_value_t        *status;
    ngx_int_t                        len;

    /* A conf var was'nt setted, so just call next filter */
    if (v->data == NULL ||  v->len <= 0) {
        return NGX_DECLINED;
    }

    /* TODO This should be fixed! {{{ */
    u_char *buf = ngx_pcalloc(ctx->r->pool, sizeof(u_char) * v->len + 1);
    if (buf == NULL) {
        return NGX_ERROR;
    }
    memcpy(buf, v->data, v->len);
    buf[v->len] = 0;
    /* }}} */
    y_root = yajl_tree_parse((const char *) buf,
                errbuf, sizeof(errbuf));
    if (y_root == NULL || !(YAJL_IS_ARRAY(y_root) &&
                y_root->u.array.len > 0)) {

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "parse reply failed, message = \"%s\", body = \"%s\"",
                errbuf, v->data);
        return NGX_ERROR;
    }

    y_node = yajl_tree_get(y_root->u.array.values[0], tarantool_ngx_path,
                yajl_t_array);
    if (y_node != NULL) {

        /** Set HTTP status
         */
        if (y_node->u.array.len >= 1 &&
            YAJL_GET_INTEGER(y_node->u.array.values[0]))
        {
            status = ctx->values[0];

            len = sizeof(u_char) + sizeof("2147483647") /* int32_t max */;

            status->data = (u_char *) ngx_palloc(ctx->r->pool, len);
            if (status->data == NULL) {
                return NGX_ERROR;
            }

            status->len = snprintf((char *) status->data, len, "%i",
                    (int) YAJL_GET_INTEGER(y_node->u.array.values[0]));
            status->valid = 1;
            status->not_found = 0;

            dd("read a new HTTP status, r = %p, ctx = %p, ctx.status = %s",
                    r, ctx, status->data);
        }

        /** Set HTTP headers
         */
        if (ctx->r &&
            y_node->u.array.len >= 2 &&
            YAJL_IS_OBJECT(y_node->u.array.values[1]))
        {
            y_headers = y_node->u.array.values[1];

            for (i = 0; i < y_headers->u.object.len; ++i) {

                y_header = y_headers->u.object.keys[i];
                y_header_value = y_headers->u.object.values[i];

                if (!YAJL_IS_STRING(y_header_value)) {
                    continue;
                }

                h = ngx_list_push(&ctx->r->headers_out.headers);
                if (h == NULL) {
                    goto ngx_error;
                }

                /** Here is insertion of headers passed from the Tarantool {{{
                 */
                h->hash = 1;

                h->key.len = ngx_strlen(y_header);
                h->key.data = ngx_pnalloc(r->pool, h->key.len * sizeof(u_char));
                if (h->key.data == NULL) {
                    goto ngx_error;
                }
                memcpy(h->key.data, y_header, h->key.len);

                tmp = YAJL_GET_STRING(y_header_value);
                h->value.len = strlen(tmp);
                h->value.data = ngx_pnalloc(r->pool,
                            h->value.len * sizeof(u_char));
                if (h->value.data == NULL) {
                    goto ngx_error;
                }
                tmp = YAJL_GET_STRING(y_header_value);
                memcpy(h->value.data, tmp, h->value.len);
                /** }}}
                 */
            }
        }
    }

    if (y_node != NULL) {
        yajl_tree_free(y_node);
        y_node = NULL;
    }

    return NGX_OK;

ngx_error:
    if (y_node != NULL) {
        yajl_tree_free(y_node);
        y_node = NULL;
    }

    return NGX_ERROR;
}


/*
 * Evaluate tarantool output into the variable.
 *
 * This evaluation method assume, that we have at least one varible.
 * ngx_http_tnt_eval_handler must guarantee this. *
 */
static ngx_int_t
ngx_http_tnt_eval_output(ngx_http_request_t *r,
        ngx_http_tnt_eval_ctx_t *ctx)
{
    ngx_http_variable_value_t     *body_value;
    ngx_http_tnt_eval_ctx_t   *sr_ctx;

    dd("Output stream for the tarantool into the variable");

    body_value = ctx->values[1];
    sr_ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_eval_module);

    if (sr_ctx && sr_ctx->buffer.start) {

        body_value->len = sr_ctx->buffer.last - sr_ctx->buffer.pos;
        body_value->data = sr_ctx->buffer.pos;
        body_value->valid = 1;
        body_value->not_found = 0;

    } else if (r->upstream) {

        body_value->len = r->upstream->buffer.last - r->upstream->buffer.pos;
        body_value->data = r->upstream->buffer.pos;
        body_value->valid = 1;
        body_value->not_found = 0;

        dd("found upstream buffer %d: %.*s, no cacheable = %d",
                (int) body_value->len, (int) body_value->len,
                body_value->data, (int) body_value->no_cacheable);
    }

    if (ngx_http_tnt_eval_parse_meta(r, ctx, body_value) == NGX_ERROR) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_subrequest(ngx_http_request_t *r,
    ngx_str_t *uri, ngx_str_t *args, ngx_http_request_t **psr,
    ngx_http_post_subrequest_t *ps, ngx_uint_t flags)
{
    ngx_time_t                    *tp;
    ngx_connection_t              *c;
    ngx_http_request_t            *sr;
    ngx_http_core_srv_conf_t      *cscf;
    ngx_http_postponed_request_t  *pr, *p;

    if (r->subrequests == 0) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "subrequests cycle while processing \"%V\"", uri);
        return NGX_ERROR;
    }

    /*
     * 1000 is reserved for other purposes.
     */
    if (r->main->count >= 65535 - 1000) {
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,
                      "request reference counter overflow "
                      "while processing \"%V\"", uri);
        return NGX_ERROR;
    }

    sr = ngx_pcalloc(r->pool, sizeof(ngx_http_request_t));
    if (sr == NULL) {
        return NGX_ERROR;
    }

    sr->signature = NGX_HTTP_MODULE;

    c = r->connection;
    sr->connection = c;

    sr->ctx = ngx_pcalloc(r->pool, sizeof(void *) * ngx_http_max_module);
    if (sr->ctx == NULL) {
        return NGX_ERROR;
    }

    if (ngx_list_init(&sr->headers_out.headers, r->pool, 20,
                      sizeof(ngx_table_elt_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);
    sr->main_conf = cscf->ctx->main_conf;
    sr->srv_conf = cscf->ctx->srv_conf;
    sr->loc_conf = cscf->ctx->loc_conf;

    sr->pool = r->pool;

    sr->method = r->method;
    sr->method_name = r->method_name;
    sr->http_version = r->http_version;

    sr->request_line = r->request_line;
    sr->uri = *uri;

    sr->headers_in = r->headers_in;
    sr->request_body = r->request_body;

    if (args) {
        sr->args = *args;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http tnt subrequest \"%V?%V\"", uri, &sr->args);

    sr->subrequest_in_memory = (flags & NGX_HTTP_SUBREQUEST_IN_MEMORY) != 0;
    sr->waited = (flags & NGX_HTTP_SUBREQUEST_WAITED) != 0;

    sr->unparsed_uri = r->unparsed_uri;
    sr->http_protocol = r->http_protocol;

    ngx_http_set_exten(sr);

    sr->main = r->main;
    sr->parent = r;
    sr->post_subrequest = ps;
    sr->read_event_handler = ngx_http_request_empty_handler;
    sr->write_event_handler = ngx_http_handler;

    if (c->data == r && r->postponed == NULL) {
        c->data = sr;
    }

    sr->variables = r->variables;

    sr->log_handler = r->log_handler;

    pr = ngx_palloc(r->pool, sizeof(ngx_http_postponed_request_t));
    if (pr == NULL) {
        return NGX_ERROR;
    }

    pr->request = sr;
    pr->out = NULL;
    pr->next = NULL;

    if (r->postponed) {
        for (p = r->postponed; p->next; p = p->next) { /* void */ }
        p->next = pr;

    } else {
        r->postponed = pr;
    }

    sr->internal = 1;
    sr->discard_body = r->discard_body;
    sr->expect_tested = 1;
    sr->main_filter_need_in_memory = r->main_filter_need_in_memory;

    sr->uri_changes = NGX_HTTP_MAX_URI_CHANGES + 1;
    sr->subrequests = r->subrequests - 1;

    tp = ngx_timeofday();
    sr->start_sec = tp->sec;
    sr->start_msec = tp->msec;

    r->main->count++;
    *psr = sr;

    return ngx_http_post_request(sr, NULL);
}


static void
ngx_http_tnt_eval_finalize_request(ngx_http_request_t *r)
{
    dd("finalize request");
    ngx_http_finalize_request(r, NGX_DONE);
}


static void *
ngx_http_tnt_eval_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_tnt_eval_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_tnt_eval_loc_conf_t));

    if (conf == NULL) {
        return NULL;
    }

    conf->buffer_size = NGX_CONF_UNSET_SIZE;

    return conf;
}


static char *
ngx_http_tnt_eval_merge_loc_conf(ngx_conf_t *cf,
        void *parent, void *child)
{
    ngx_http_tnt_eval_loc_conf_t *prev = parent;
    ngx_http_tnt_eval_loc_conf_t *conf = child;

    ngx_conf_merge_size_value(conf->buffer_size, prev->buffer_size,
                              (size_t) ngx_pagesize * 16);

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_tnt_eval_variable(ngx_http_request_t *r,
        ngx_http_variable_value_t *v, uintptr_t data)
{
    (void) r;
    (void) data;

    dd("XXX variable get_handle");

    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    v->len = 0;
    v->data = (u_char*) "";

    return NGX_OK;
}


static char *
ngx_http_tnt_eval_add_variables(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf)
{
    ngx_http_tnt_eval_loc_conf_t         *ecf = conf;

    ngx_uint_t                           i;
    ngx_int_t                            index;
    ngx_str_t                            *value;
    ngx_http_variable_t                  *v;
    ngx_http_tnt_eval_variable_t         *variable;

    value = cf->args->elts;

    ecf->variables = ngx_array_create(cf->pool,
        cf->args->nelts, sizeof(ngx_http_tnt_eval_variable_t));

    if (ecf->variables == NULL) {
        return NGX_CONF_ERROR;
    }

    if (cf->args->nelts != 3) {

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "invalid number of args, it should be tnt_eval "
                "$http_stat $http_body { BLOCK }");
        return NGX_CONF_ERROR;
    }

    /* Skip the first one, which is tnt_eval
     */
    for (i = 1; i < cf->args->nelts; i++) {

        if (value[i].data[0] != '$') {

            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                    "invalid variable name \"%V\", it should be \"$VAR_NAME\"",
                    &value[i]);
            return NGX_CONF_ERROR;
        }

        variable = ngx_array_push(ecf->variables);
        if (variable == NULL) {
            return NGX_CONF_ERROR;
        }

        value[i].len--;
        value[i].data++;

        v = ngx_http_add_variable(cf, &value[i], NGX_HTTP_VAR_CHANGEABLE);
        if (v == NULL) {
            return NGX_CONF_ERROR;
        }

        index = ngx_http_get_variable_index(cf, &value[i]);
        if (index == NGX_ERROR) {
            return NGX_CONF_ERROR;
        }

        if (v->get_handler == NULL) {
            v->get_handler = ngx_http_tnt_eval_variable;
            v->data = index;
        }

        variable->variable = v;
        variable->index = index;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_tnt_eval_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_tnt_eval_loc_conf_t  *pecf = conf;

    char                      *rv;
    void                      *mconf;
    ngx_str_t                 name;
    ngx_uint_t                i;
    ngx_conf_t                save;
    ngx_module_t              **modules;
    ngx_http_module_t         *module;
    ngx_http_conf_ctx_t       *ctx, *pctx;
    ngx_http_core_loc_conf_t  *clcf, *rclcf;
    ngx_http_core_srv_conf_t  *cscf;

#if defined(nginx_version) && nginx_version >= 8042 && nginx_version <= 8053
    return "does not work with " NGINX_VER;
#endif

    if (ngx_http_tnt_eval_add_variables(cf, cmd, conf) != NGX_CONF_OK) {
        return NGX_CONF_ERROR;
    }

    ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_conf_ctx_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    pctx = cf->ctx;
    ctx->main_conf = pctx->main_conf;
    ctx->srv_conf = pctx->srv_conf;

    ctx->loc_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->loc_conf == NULL) {
        return NGX_CONF_ERROR;
    }

#if defined(nginx_version) && nginx_version >= 1009011
    modules = cf->cycle->modules;
#else
    modules = ngx_modules;
#endif

    for (i = 0; modules[i]; i++) {

        if (modules[i]->type != NGX_HTTP_MODULE) {
            continue;
        }

        module = modules[i]->ctx;

        if (module->create_loc_conf) {

            mconf = module->create_loc_conf(cf);
            if (mconf == NULL) {
                 return NGX_CONF_ERROR;
            }

            ctx->loc_conf[modules[i]->ctx_index] = mconf;
        }
    }

    clcf = ctx->loc_conf[ngx_http_core_module.ctx_index];

    name.len = sizeof("/tnt_eval_") - 1 + NGX_OFF_T_LEN;

    name.data = ngx_palloc(cf->pool, name.len);

    if (name.data == NULL) {
        return NGX_CONF_ERROR;
    }

    name.len = ngx_sprintf(name.data, "/tnt_eval_%O", (off_t)(uintptr_t) clcf)
               - name.data;

    clcf->loc_conf = ctx->loc_conf;
    clcf->name = name;
    clcf->exact_match = 0;
    clcf->noname = 0;
    clcf->internal = 1;
    clcf->noregex = 1;

    cscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_core_module);
    if (cscf == NULL || cscf->ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    rclcf = cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];
    if (rclcf == NULL) {
        return NGX_CONF_ERROR;
    }

    if (ngx_http_add_location(cf, &rclcf->locations, clcf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    pecf->eval_location = clcf->name;

    save = *cf;
    cf->ctx = ctx;
    cf->cmd_type = NGX_HTTP_LOC_CONF;

    rv = ngx_conf_parse(cf, NULL);

    *cf = save;

    return rv;
}


static ngx_int_t
ngx_http_tnt_eval_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_tnt_eval_handler;

    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_tnt_eval_header_filter;

    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_tnt_eval_body_filter;

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_eval_header_filter(ngx_http_request_t *r)
{
    ngx_http_tnt_eval_ctx_t *ctx;

    dd("Processing in a header filter");

    /* If this module does not active, just goes to a next filter */
    ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_eval_module);
    if (ctx == NULL) {
        return ngx_http_next_header_filter(r);
    }

    /* Comeback to the main request */
    if (r == r->main) {

        dd("Comeback! Goes to a next header filter, r = %p, ctx = %p",
                r, ctx);
        return ngx_http_next_header_filter(r);
    }

    r->filter_need_in_memory = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_http_tnt_eval_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_http_tnt_eval_ctx_t          *ctx;
    ngx_chain_t                      *cl;
    ngx_buf_t                        *b;
    ngx_http_tnt_eval_loc_conf_t     *conf;
    size_t                           len;
    ssize_t                          rest;

    if (r == r->main) {
        return ngx_http_next_body_filter(r, in);
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_eval_module);
    if (ctx == NULL) {
        return ngx_http_next_body_filter(r, in);
    }

    dd("Processing in a body filter");

    conf = ngx_http_get_module_loc_conf(r->parent, ngx_http_tnt_eval_module);

    b = &ctx->buffer;

    if (b->start == NULL) {
        b->start = ngx_palloc(r->pool, conf->buffer_size);
        if (b->start == NULL) {
            return NGX_ERROR;
        }

        b->end = b->start + conf->buffer_size;
        b->pos = b->last = b->start;
    }

    for (cl = in; cl; cl = cl->next) {

        rest = b->end - b->last;
        if (rest == 0) {
            break;
        }

        if (!ngx_buf_in_memory(cl->buf)) {
            dd("buf not in memory!");
            continue;
        }

        len = cl->buf->last - cl->buf->pos;

        if (len == 0) {
            continue;
        }

        if (len > (size_t) rest) {
            /* Truncating the exceeding part of the response body */
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "tnt_eval: need more bytes. aborted. "
                    "consider increasing your 'tnt_eval_buffer_size' "
                    "setting");
            len = rest;
        }

        b->last = ngx_copy(b->last, cl->buf->pos, len);
    }

    return ngx_http_tnt_eval_discard_bufs(r->pool, in);
}


static ngx_int_t
ngx_http_tnt_eval_discard_bufs(ngx_pool_t *pool, ngx_chain_t *in)
{
    ngx_chain_t *cl;

    for (cl = in; cl; cl = cl->next) {
#if 0
        if (cl->buf->temporary && cl->buf->memory
                && ngx_buf_size(cl->buf) > 0) {
            ngx_pfree(pool, cl->buf->start);
        }
#endif

        cl->buf->pos = cl->buf->last;
    }

    return NGX_OK;
}
