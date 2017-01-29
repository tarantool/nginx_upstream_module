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

#include <tp_ext.h>
#include <ngx_http_tnt_handlers.h>
#include <debug.h>


extern ngx_module_t ngx_http_tnt_module;


static inline ngx_int_t
ngx_http_tnt_copy_headers(struct tp *tp,
                          ngx_list_t *headers,
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


static inline size_t
ngx_http_tnt_get_output_size(
    ngx_http_request_t *r,
    ngx_http_tnt_ctx_t *ctx,
    ngx_http_tnt_loc_conf_t *tlcf,
    ngx_buf_t *request_b)
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


static inline ngx_int_t
ngx_http_tnt_read_greeting(ngx_http_request_t *r,
                           ngx_http_tnt_ctx_t *ctx,
                           ngx_buf_t *b)
{
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
        .output = (char *)out_chain->buf->start,
        .output_size = out_chain->buf->end - out_chain->buf->start,
        .method = NULL, .method_len = 0,
        .codec = YAJL_JSON_TO_TP,
        .mf = NULL
    };

    if (tp_transcode_init(&tc, &args) == TP_TRANSCODE_ERROR) {
        goto error_exit;
    }

    if (tp_transcode(&tc, (char *)buf, len) == TP_TRANSCODE_ERROR) {
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


static inline void
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


static inline ngx_int_t
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

static inline ngx_http_tnt_next_arg_t
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


static inline ngx_int_t
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

    if (!tp_encode_str_map_item(
            tp,
            (const char *)dkey, key_len,
            (const char *)dvalue, value_len))
    {
        dd("ngx_http_tnt_encode_str_map_item: tp_encode_str_map_item failed");
        return NGX_ERROR;
    }

    return NGX_OK;
}

static inline ngx_int_t
ngx_http_tnt_encode_query_args(
        ngx_http_request_t *r,
        ngx_http_tnt_loc_conf_t *tlcf,
        struct tp *tp,
        ngx_uint_t *args_items)
{
    u_char *arg_begin, *end;

    if (r->args.len == 0) {
        return NGX_OK;
    }

    arg_begin = r->args.data;
    end = arg_begin + r->args.len;

    ngx_http_tnt_next_arg_t arg = { .it = arg_begin, .value = NULL };

    for (; arg.it < end; ) {

        arg = ngx_http_tnt_get_next_arg(arg.it, end);

        const size_t value_len = arg.it - arg.value;
        if (arg.value && value_len > 0) {

            ++(*args_items);
            if (!tp_encode_str_map_item(tp,
                                        (const char *)arg_begin,
                                        arg.value - arg_begin - 1,
                                        (const char *)arg.value, value_len))
            {
                dd("parse args: tp_encode_str_map_item failed");
                return NGX_ERROR;
            }
        }
        arg_begin = ++arg.it;
    }

    return NGX_OK;
}


static inline ngx_int_t
ngx_http_tnt_get_request_data(ngx_http_request_t *r,
                              ngx_http_tnt_loc_conf_t *tlcf,
                              struct tp *tp)
{
    char            *root_map_place;
    char            *map_place;
    size_t          root_items;
    size_t          map_items;
    ngx_buf_t       *b;
    ngx_chain_t     *body;
    char            *p;

    root_items = 0;
    root_map_place = tp->p;
    tp_add(tp, 1 + sizeof(uint32_t));

    /** Encode protocol
     */
    ++root_items;

    if (!tp_encode_str_map_item(tp,
                                "proto", sizeof("proto")-1,
                                (const char*)r->http_protocol.data,
                                r->http_protocol.len))
    {
        return NGX_ERROR;
    }

    /** Encode method
     */
    ++root_items;

    if (!tp_encode_str_map_item(tp,
                                "method", sizeof("method")-1,
                                (const char*)r->method_name.data,
                                r->method_name.len))
    {
        return NGX_ERROR;
    }

    /** Encode raw uri
     */
    ++root_items;

    if (ngx_http_tnt_encode_str_map_item(r, tlcf, tp,
                                         (u_char *)"uri", sizeof("uri") - 1,
                                         r->unparsed_uri.data,
                                         r->unparsed_uri.len) == NGX_ERROR)
    {
        return NGX_ERROR;
    }

    /** Encode query args
     */
    if (tlcf->pass_http_request & NGX_TNT_CONF_PARSE_ARGS) {

        ++root_items;

        if (!tp_encode_str(tp, "args", sizeof("args")-1)) {
            return NGX_ERROR;
        }

        map_place = tp->p;
        if (!tp_add(tp, 1 + sizeof(uint32_t))) {
            return NGX_ERROR;
        }

        map_items = 0;

        if (ngx_http_tnt_encode_query_args(
                    r, tlcf, tp, &map_items) == NGX_ERROR)
        {
            return NGX_ERROR;
        }

        *(map_place++) = 0xdf;
        *(uint32_t *) map_place = mp_bswap_u32(map_items);
    }

    /** Encode http headers
     */
    ++root_items;

    if (!tp_encode_str(tp, "headers", sizeof("headers")-1)) {
        return NGX_ERROR;
    }

    map_items = 0;
    map_place = tp->p;

    if (!tp_add(tp, 1 + sizeof(uint32_t))) {
        return NGX_ERROR;
    }

    if (ngx_http_tnt_copy_headers(tp, &r->headers_in.headers, &map_items) ==
            NGX_ERROR)
    {
        return NGX_ERROR;
    }

    if ((tlcf->pass_http_request & NGX_TNT_CONF_PASS_HEADERS_OUT) &&
        (ngx_http_tnt_copy_headers(tp, &r->headers_out.headers, &map_items) ==
            NGX_ERROR) )
    {
        return NGX_ERROR;
    }

    *(map_place++) = 0xdf;
    *(uint32_t *) map_place = mp_bswap_u32(map_items);

    /* Encode body
     */
    if ((tlcf->pass_http_request & NGX_TNT_CONF_PASS_BODY) &&
            r->headers_in.content_length_n > 0 &&
            r->upstream->request_bufs )
    {
        ++root_items;

        if (!tp_encode_str(tp, "body", sizeof("body") - 1)) {
            return NGX_ERROR;
        }

        int sz = mp_sizeof_str(r->headers_in.content_length_n);
        if (tp_ensure(tp, sz) == -1) {
            return NGX_ERROR;
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

        if (!tp_add(tp, sz)) {
            return NGX_ERROR;
       }
    }

    *(root_map_place++) = 0xdf;
    *(uint32_t *) root_map_place = mp_bswap_u32(root_items);

    return NGX_OK;
}


static inline ngx_buf_t *
ngx_http_tnt_get_request_data_map(
    ngx_http_request_t *r,
    ngx_http_tnt_loc_conf_t *tlcf)
{
    ngx_int_t rc;
    struct tp tp;
    ngx_buf_t *b;

    b = ngx_create_temp_buf(r->pool, tlcf->pass_http_request_buffer_size);
    if (b == NULL) {
        crit("[BUG?] ngx_http_tnt_get_request_data_map - "
             "failed to allocate output buffer, size");
        return NULL;
    }

    b->memory = 1;
    b->flush = 1;

    b->pos = b->start;

    tp_init(&tp, (char *)b->start, b->end - b->start, NULL, NULL);
    tp.size = tp.p;

    rc = ngx_http_tnt_get_request_data(r, tlcf, &tp);
    if (rc != NGX_OK) {
        return NULL;
    }

    b->last = (u_char *)tp.p;

    return b;
}


ngx_http_tnt_ctx_t *
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


void
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


ngx_int_t ngx_http_tnt_init_handlers(ngx_http_request_t *r,
                                     ngx_http_upstream_t *u,
                                     ngx_http_tnt_loc_conf_t *tlcf)
{
    ngx_http_tnt_ctx_t *ctx;

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

    if (tlcf->pass_http_request & NGX_TNT_CONF_PASS_BODY) {
        dd("NGX_TNT_CONF_PASS_BODY");
        u->create_request = ngx_http_tnt_query_handler;
    } else {
        if (r->headers_in.content_length_n > 0) {
            dd("ngx_http_tnt_body_json_handler");
            u->create_request = ngx_http_tnt_body_json_handler;
        } else {
            dd("ngx_http_tnt_query_handler");
            u->create_request = ngx_http_tnt_query_handler;
        }
    }

    return NGX_OK;
}


ngx_int_t
ngx_http_tnt_body_json_handler(ngx_http_request_t *r)
{
    ngx_buf_t               *b, *request_b = NULL;
    ngx_chain_t             *body;
    size_t                  complete_msg_size;
    tp_transcode_t          tc;
    ngx_http_tnt_ctx_t      *ctx;
    ngx_chain_t             *out_chain;
    ngx_http_tnt_loc_conf_t *tlcf;

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
        crit("[BUG?] ngx_http_tnt_body_json_handler -- "
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
     *  Conv. input json to Tarantool message [
     */
    tp_transcode_init_args_t args = {
        .output = (char *)out_chain->buf->start,
        .output_size = out_chain->buf->end - out_chain->buf->start,
        .method = (char *)ctx->preset_method,
        .method_len = ctx->preset_method_len,
        .codec = YAJL_JSON_TO_TP,
        .mf = NULL
    };

    if (tp_transcode_init(&tc, &args) == TP_TRANSCODE_ERROR) {
        crit("[BUG] failed to call tp_transcode_init(input)");
        return NGX_ERROR;
    }

    /**
     * Bind extra data e.g. http headers, uri ...
     */
    if (request_b != NULL) {
        tp_transcode_bind_data(&tc,
                               (const char *)request_b->start,
                               (const char *)request_b->last);
    }

    for (body = r->upstream->request_bufs; body; body = body->next) {

        if (body->buf->in_file) {

            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "tnt: in-file buffer found. aborted. "
                "consider increasing your 'client_body_buffer_size' "
                "setting");

            const ngx_http_tnt_error_t *e = get_error_text(REQUEST_TOO_LARGE);
            ctx->in_err = ngx_http_tnt_set_err(r,
                                               e->code,
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
                                              (u_char *)tc.errmsg,
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


ngx_int_t
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
     *  Conv. GET/PUT/DELETE to Tarantool message [
     */
    buf = out_chain->buf;
    tp_init(&tp, (char *)buf->start, buf->end - buf->start, NULL, NULL);

    if (!tp_call_nargs(&tp,
                       (const char *)ctx->preset_method,
                       (size_t)ctx->preset_method_len, 1))
    {
        err = get_error_text(HTTP_REQUEST_TOO_LARGE);
        crit("ngx_http_tnt_query_handler - %s", get_str_safe(err->msg.data));
        return NGX_ERROR;
    }

    rc = ngx_http_tnt_get_request_data(r, tlcf, &tp);
    if (rc != NGX_OK) {
        err = get_error_text(HTTP_REQUEST_TOO_LARGE);
        crit("ngx_http_tnt_query_handler - %s", get_str_safe(err->msg.data));
        return rc;
    }

    out_chain->buf->last = (u_char *)tp.p;
    /** ]
     */

    /**
     * Hooking output chain
     */
    r->upstream->request_bufs = out_chain;

    return NGX_OK;
}


ngx_int_t
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


ngx_int_t
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


void
ngx_http_tnt_abort_request(ngx_http_request_t *r)
{
    dd("abort http tnt request");
    ngx_http_tnt_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);
    ngx_http_tnt_cleanup(r, ctx);
}


void
ngx_http_tnt_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    dd("finalize http tnt request");
    ngx_http_tnt_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_tnt_module);
    ngx_http_tnt_cleanup(r, ctx);
}


ngx_buf_t*
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

/**
 */
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

const ngx_http_tnt_error_t *
get_error_text(int type)
{
    return &errors[type];
}

