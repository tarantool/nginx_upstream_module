#ifndef NGX_HTTP_TNT_TRANSCODE_H
#define NGX_HTTP_TNT_TRANSCODE_H 1

#include <ngx_core.h>

#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>


typedef struct {
  ngx_pool_t    *pool;
  yajl_handle   yh;
  ngx_buf_t     *output;
} transcode_t;


static int
yajl_null(void *ctx)
{
  (void)ctx;
  return 1;
}

#define DD0(fmt) \
  ngx_log_debug0(NGX_LOG_DEBUG_HTTP, tc->pool->log, 0, (fmt));
#define DD1(fmt, a1) \
  ngx_log_debug1(NGX_LOG_DEBUG_HTTP, tc->pool->log, 0, (fmt), (a1));
#define DD2(fmt, a1, a2) \
  ngx_log_debug2(NGX_LOG_DEBUG_HTTP, tc->pool->log, 0, (fmt), (a1), (a2));

static int
yajl_boolean(void * ctx, int v)
{
  transcode_t *tc = (transcode_t*) ctx;
  DD1("bool: %s", (v ? "true" : "false"));
  return 1;
}


static int
yajl_integer(void *ctx, long long v)
{
  transcode_t *tc = (transcode_t*) ctx;
  DD1("integer: %lld", v);
  return 1;
}


static int
yajl_double(void *ctx, double v)
{
  transcode_t *tc = (transcode_t*) ctx;
  DD1("double: %g", v);
  return 1;
}


static int
yajl_string(void *ctx, const unsigned char * s, size_t sz)
{
  transcode_t *tc = (transcode_t*) ctx;
  (void)tc, (void)s, (void)sz;
 // DD2("string: %*s", sz, s);
  return 1;
}


static int
yajl_map_key(void *ctx, const unsigned char * v, size_t sz)
{
  transcode_t *tc = (transcode_t*) ctx;
  (void)tc, (void)v, (void)sz;



 // DD2("key: %*s", sz, s);
  return 1;
}


static int
yajl_start_map(void *ctx)
{
  transcode_t *tc = (transcode_t*) ctx;
  DD0("map open '{'");
  return 1;
}


static int
yajl_end_map(void *ctx)
{
  transcode_t *tc = (transcode_t*) ctx;
  DD0("map close '}'");
  return 1;
}


static int
yajl_start_array(void *ctx)
{
  transcode_t *tc = (transcode_t*) ctx;
  DD0("array open '['");
  return 1;
}


static int
yajl_end_array(void *ctx)
{
  transcode_t *tc = (transcode_t*) ctx;
  DD0("array close ']'");
  return 1;
}


static inline void*
tc_mem_alloc(void *ctx, size_t sz)
{
  transcode_t *tc = (transcode_t *)ctx;
  (void)tc;
  return malloc(sz);
}


static inline void*
tc_mem_realloc(void *ctx, void *ptr, size_t sz)
{
  transcode_t *tc = (transcode_t *)ctx;
  (void)tc;
  return realloc(ptr, sz);
}


static inline void
tc_mem_free(void *ctx, void *ptr)
{
  transcode_t *tc = (transcode_t *)ctx;
  (void)tc;
  if (ptr) {
    free(ptr);
  }
}


static inline transcode_t*
tc_create(ngx_pool_t *p, const u_char *input_codec_name,
    const u_char *output_codec_name)
{

  static yajl_callbacks callbacks = {
    yajl_null,
    yajl_boolean,
    yajl_integer,
    yajl_double,
    NULL,
    yajl_string,
    yajl_start_map,
    yajl_map_key,
    yajl_end_map,
    yajl_start_array,
    yajl_end_array
  };

  transcode_t *tc;

  yajl_alloc_funcs alloc_funcs = {
    tc_mem_alloc, tc_mem_realloc, tc_mem_free, (void *) NULL
  };

  tc = ngx_palloc(p, sizeof(transcode_t));
  if (!tc) {
    return NULL;
  }

  alloc_funcs.ctx = (void *)tc;

  tc->yh = yajl_alloc(&callbacks, &alloc_funcs, (void *)tc);
  tc->pool = p;

  return tc;
}


static inline ngx_int_t
tc_decode(transcode_t *tc, ngx_buf_t *input, ngx_buf_t *output)
{
  (void)output;

  yajl_status stat;
  u_char      *errmsg;
  size_t      len = input->last - input->pos;

  stat = yajl_parse(tc->yh, input->pos, len);

// TODO impl. partial parsing
#if 0
  if (stat != yajl_status_ok) {
    return NGX_ERROR;
  }
#endif

  stat = yajl_complete_parse(tc->yh);
  if (stat != yajl_status_ok) {
    errmsg = yajl_get_error(tc->yh, 0, input->pos, len);
    DD1("yajl error: '%s'", errmsg);
    yajl_free_error(tc->yh, errmsg);
    return NGX_ERROR;
  }

  yajl_free(tc->yh);

  return NGX_OK;
}


static inline ngx_int_t
tc_json_encode(transcode_t *tc, ngx_buf_t *input, ngx_buf_t *output)
{
  (void)input, (void)tc, (void)output;
  return NGX_OK;
}

static inline void
tc_free(transcode_t *tc)
{
  yajl_free(tc->yh);
}

#undef DD0
#undef DD1
#undef DD2

#endif

