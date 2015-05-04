#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>
#include <stdio.h>

#define MP_SOURCE 1
#include "tp.h"
#include "tp_transcode.h"

#if defined DEBUG
#define dd(...) fprintf(stderr, __VA_ARGS__)
#else
#define dd(...)
#endif

#define ALLOC(ctx_, size) \
    (ctx_)->tc->mf.alloc((ctx_)->tc->mf.ctx, (size))
#define REALLOC(ctx_, mem, size) \
    (ctx_)->tc->mf.realloc((ctx_)->tc->mf.ctx, (mem), (size))
#define FREE(ctx_, mem) \
    (ctx_)->tc->mf.free((ctx_)->tc->mf.ctx, (mem))

enum type { TYPE_MAP = 1, TYPE_ARRAY = 2, TYPE_KEY = 4 };

static inline void
say_error_(tp_transcode_t *t, int code, const char *e, size_t len)
{
    if (mp_unlikely(t->errmsg != NULL))
        t->mf.free(t->mf.ctx, t->errmsg);
    t->errmsg = t->mf.alloc(t->mf.ctx, (len + 1) * sizeof(char));
    if (mp_unlikely(t->errmsg != NULL)) {
        memcpy(t->errmsg, e, len);
        *(t->errmsg + len) = '\0';
    }
    t->errcode  = code;
}

#define say_error(ctx, c, e) \
    say_error_((ctx)->tc, (c), (e), sizeof(e) - 1)

#define say_error_r(ctx, c, e) do { \
    say_error((ctx), (c), (e)); \
    return TP_TRANSCODE_ERROR; \
} while (0)

#define say_overflow_r(c) \
    say_error_r((c), -32603,  "[BUG?] 'output' buffer overflow")

#define say_overflow_r_2(c) do { \
    say_error((c), -32603, "[BUG?] 'output' buffer overflow"); \
    return 0; \
} while (0)

#define say_wrong_params(ctx) \
            say_error((ctx), -32700, \
                  "'params' _must_ be array, 'params' _may_ be empty array")


/*
 * CODEC - YAJL_JSON_RPC
 */
#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>

enum { MAX_STACK_SIZE = 254 - 1 };
enum stage {
    INIT        = 0,
    WAIT_NEXT   = 2,
    PARAMS      = 4,
    ID          = 8,
    METHOD      = 16
};

typedef struct {
    char *ptr;
    int16_t count;
    uint16_t type;
} stack_item_t;

typedef struct {
    yajl_handle hand;

    yajl_alloc_funcs *yaf;

    stack_item_t *stack;
    uint8_t size, allocated;

    char *b;
    size_t output_size;
    struct tp tp;

    enum stage stage;
    bool batch_mode_on;

    int been_stages;

    uint32_t id;

    tp_transcode_t *tc;
} yajl_ctx_t;

static inline char *
tp_call_wof(struct tp *p)
{
    int sz = 5 +
            mp_sizeof_map(2) +
            mp_sizeof_uint(TP_CODE) +
            mp_sizeof_uint(TP_CALL) +
            mp_sizeof_uint(TP_SYNC) +
            5 +
            mp_sizeof_map(2);
    if (tpunlikely(tp_ensure(p, sz) == -1))
        return NULL;
    p->size = p->p;
    char *h = mp_encode_map(p->p + 5, 2);
    h = mp_encode_uint(h, TP_CODE);
    h = mp_encode_uint(h, TP_CALL);
    h = mp_encode_uint(h, TP_SYNC);
    p->sync = h;
    *h = 0xce;
    *(uint32_t*)(h + 1) = 0;
    h += 5;
    h = mp_encode_map(h, 2);
    return tp_add(p, sz);
}

static inline char *
tp_call_wof_add_func(struct tp *p, const char *function, int len)
{
    int sz = mp_sizeof_uint(TP_FUNCTION) +
                mp_sizeof_str(len);
    if (tpunlikely(tp_ensure(p, sz) == -1))
        return NULL;
    char *h = mp_encode_uint(p->p, TP_FUNCTION);
    h = mp_encode_str(h, function, len);
    return tp_add(p, sz);
}

static inline char *
tp_call_wof_add_params(struct tp *p)
{
    int sz = mp_sizeof_uint(TP_TUPLE);
    if (tpunlikely(tp_ensure(p, sz) == -1))
        return NULL;
    mp_encode_uint(p->p, TP_TUPLE);
    return tp_add(p, sz);
}

static inline bool
spush(yajl_ctx_t *s, char *ptr, int mask)
{
    if (mp_likely(s->size < MAX_STACK_SIZE)) {

        if (mp_unlikely(s->allocated == s->size)) {
            s->allocated += 16;
            s->stack = REALLOC(s, s->stack,
                               sizeof(stack_item_t) * s->allocated);
            if (s->stack == NULL)
                return false;

            size_t i;
            for (i = s->size; i < s->allocated; ++i) {
                s->stack[i].ptr = 0;
                s->stack[i].count = 0;
                s->stack[i].type = 0;
            }
        }

        s->stack[s->size].ptr = ptr;
        s->stack[s->size].count = 0;
        s->stack[s->size].type = mask;

        ++s->size;

        return true;
    }

    return false;
}

static inline stack_item_t*
stop(yajl_ctx_t *s)
{
    if (mp_likely(s->size > 0))
        return &s->stack[s->size - 1];
    return NULL;
}

static inline stack_item_t*
spop(yajl_ctx_t *s)
{
    stack_item_t *ret = NULL;

    if (mp_likely(s->size > 0)) {
        --s->size;
        ret = &s->stack[s->size];
    }

    return ret;
}

static inline void
sinc_if(yajl_ctx_t *s, int cond)
{
    stack_item_t *item = stop(s);
    if (mp_likely(item && item->type & cond))
        ++item->count;
}
#define sinc_if_array(s) sinc_if((s), TYPE_ARRAY)
#define sinc_if_map(s) sinc_if((s), TYPE_MAP)

static int
yajl_null(void *ctx)
{
    yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

    if (mp_unlikely(s_ctx->stage != PARAMS))
        return 1;

    dd("null\n");

    sinc_if_array(s_ctx);
    if (mp_unlikely(!tp_encode_nil(&s_ctx->tp)))
        say_overflow_r_2(s_ctx);

    return 1;
}

static int
yajl_boolean(void * ctx, int v)
{
    yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

    if (mp_unlikely(s_ctx->stage != PARAMS))
        return 1;

    dd("bool: %s\n", v ? "true" : "false");

    sinc_if_array(s_ctx);
    if (mp_unlikely(!tp_encode_bool(&s_ctx->tp, v)))
        say_overflow_r_2(s_ctx);

    return 1;
}

static int
yajl_integer(void *ctx, long long v)
{
    yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

    if (mp_likely(s_ctx->stage == PARAMS)) {

        dd("integer: %lld\n", v);

        sinc_if_array(s_ctx);

        char *r = NULL;
        if (v < 0)
            r = tp_encode_int(&s_ctx->tp, (int64_t)v);
        else
            r = tp_encode_uint(&s_ctx->tp, (uint64_t)v);

        if (mp_unlikely(!r))
            say_overflow_r_2(s_ctx);

    } else if (s_ctx->stage == ID) {

        dd ("id: %lld\n", v);

        if (mp_unlikely(v > UINT32_MAX)) {
            say_error(s_ctx, -32600, "'id' _must_ be less than UINT32_t");
            return 0;
        }

        tp_reqid(&s_ctx->tp, v);
        s_ctx->been_stages |= ID;
        s_ctx->stage = WAIT_NEXT;
    }

    return 1;
}

static int
yajl_double(void *ctx, double v)
{
    yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

    if (mp_unlikely(s_ctx->stage != PARAMS))
        return 1;

    dd("double: %g\n", v);

    sinc_if_array(s_ctx);

    if (mp_unlikely(!tp_encode_double(&s_ctx->tp, v)))
        say_overflow_r_2(s_ctx);

    return 1;
}


static int
yajl_string(void *ctx, const unsigned char * str, size_t len)
{
    yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

    if (mp_likely(s_ctx->stage == PARAMS)) {

        dd("string: %.*s\n", (int)len, str);

        if (len > 0) {

            sinc_if_array(s_ctx);

            if (mp_unlikely(!tp_encode_str(&s_ctx->tp,
                            (const char *)str, len)))
                say_overflow_r_2(s_ctx);
        }

    } else if (s_ctx->stage == METHOD) {

        dd("METHOD: %.*s\n", (int)len, str);

        if (mp_unlikely(
                !tp_call_wof_add_func(&s_ctx->tp, (const char *)str, len)))
                say_overflow_r_2(s_ctx);

        dd("METHOD END\n");

        s_ctx->stage = WAIT_NEXT;
        s_ctx->been_stages |= METHOD;
    }

    return 1;
}

static int
yajl_map_key(void *ctx, const unsigned char * key, size_t len)
{
    yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

    if (mp_unlikely(s_ctx->stage == INIT)) {

        dd("MESSAGE START\n");

        s_ctx->stage = WAIT_NEXT;

        if (mp_unlikely(s_ctx->tc->batch_size == 255)) {
            say_error(s_ctx, -32600,
                     "too large batch, max allowed 254 calls per request");
            return 0;
        }

        ++s_ctx->tc->batch_size;

        if (mp_unlikely(!tp_call_wof(&s_ctx->tp)))
            say_overflow_r_2(s_ctx);
    }

    if (mp_likely(s_ctx->stage == PARAMS)) {

        dd("key: %.*s\n", (int)len, key);

        sinc_if_map(s_ctx);

        if (mp_unlikely(!tp_encode_str(&s_ctx->tp, (char *)key, len)))
            say_overflow_r_2(s_ctx);

    } else if (s_ctx->stage == WAIT_NEXT) {

        if (len == sizeof("params") - 1
            && key[0] == 'p'
            && key[1] == 'a'
            && key[2] == 'r'
            && key[3] == 'a'
            && key[4] == 'm'
            && key[5] == 's')
        {
            dd("PARAMS STAGE\n");

            if (mp_unlikely(!tp_call_wof_add_params(&s_ctx->tp)))
                say_overflow_r_2(s_ctx);

            s_ctx->stage = PARAMS;
        }
        else if (len == sizeof("id") - 1
            && key[0] == 'i'
            && key[1] == 'd')
        {
            dd("ID STAGE\n");
            s_ctx->stage = ID;
        }
        else if (len == sizeof("method") - 1
                && key[0] == 'm'
                && key[1] == 'e'
                && key[2] == 't'
                && key[3] == 'h'
                && key[4] == 'o'
                && key[5] == 'd')
        {
            dd("METHOD STAGE\n");
            s_ctx->stage = METHOD;
        }
        else
        {
            dd("SKIPING: %.*s\n", (int)len, key);
        }
    } else
        s_ctx->stage = WAIT_NEXT;

    return 1;
}

static int
yajl_start_map(void *ctx)
{
    yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

    if (mp_unlikely(s_ctx->stage != PARAMS))
        return 1;

    dd("map open '{'\n");

    sinc_if_array(s_ctx);

    bool r = false;
    if (mp_unlikely(s_ctx->size == 0))
        r = spush(s_ctx, s_ctx->tp.p, TYPE_MAP | PARAMS);
    else
        r = spush(s_ctx, s_ctx->tp.p, TYPE_MAP);
    if (mp_unlikely(!r)) {
        say_error(s_ctx, -32603, "[BUG?] 'stack' overflow");
        return 0;
    }

    if (mp_unlikely(s_ctx->tp.e < s_ctx->tp.p + 1 + sizeof(uint32_t)))
        say_overflow_r_2(s_ctx);

    tp_add(&s_ctx->tp, 1 + sizeof(uint32_t));

    return 1;
}


static int
yajl_end_map(void *ctx)
{
    yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

    if ((s_ctx->been_stages & (PARAMS | ID | METHOD))
            == (PARAMS | ID | METHOD))
    {
        dd("WAIT NEXT BATCH\n");

        s_ctx->stage = INIT;

        s_ctx->been_stages = 0;
    }

    if (mp_unlikely(s_ctx->stage != PARAMS))
        return 1;

    stack_item_t *item = spop(s_ctx);
    if (mp_likely(item != NULL)) {

        dd("map close, count %d '}'\n", (int)item->count);

        *(item->ptr++) = 0xdf;
        *(uint32_t *) item->ptr = mp_bswap_u32(item->count);

        if (mp_unlikely(item->type & PARAMS)) {
            say_wrong_params(s_ctx);
            return 0;
        }
    } else {
        say_wrong_params(s_ctx);
        return 0;
    }

    return 1;
}

static int
yajl_start_array(void *ctx)
{
    yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

    if (mp_unlikely(s_ctx->stage == INIT)) {
        s_ctx->batch_mode_on = true;
        return 1;
    }

    if (mp_unlikely(s_ctx->stage != PARAMS))
        return 1;

    dd("array open '['\n");

    sinc_if_array(s_ctx);

    bool r = false;
    if (mp_unlikely(s_ctx->size == 0))
        r = spush(s_ctx, s_ctx->tp.p, TYPE_ARRAY | PARAMS);
    else
        r = spush(s_ctx, s_ctx->tp.p, TYPE_ARRAY);
    if (mp_unlikely(!r)) {
        say_error(s_ctx, -32603, "[BUG?] 'stack' overflow");
        return 0;
    }

    if (mp_unlikely(s_ctx->tp.e < s_ctx->tp.p + 1 + sizeof(uint32_t)))
        say_overflow_r_2(s_ctx);

    tp_add(&s_ctx->tp, 1 + sizeof(uint32_t));

    return 1;
}

static int
yajl_end_array(void *ctx)
{
    yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

    if (mp_unlikely(s_ctx->stage != PARAMS))
        return 1;

    stack_item_t *item = spop(s_ctx);
    if (mp_likely(item != NULL)) {
        dd("array close, count %d ']'\n", item->count);

        *(item->ptr++) = 0xdd;
        *(uint32_t *) item->ptr = mp_bswap_u32(item->count);

        if (mp_unlikely(item->type & PARAMS)) {
            dd("PARAMS END\n");
            s_ctx->stage = WAIT_NEXT;
            s_ctx->been_stages |= PARAMS;
        }
    } else {
        say_wrong_params(s_ctx);
        return 0;
    }

    return 1;
}

static void *
yajl_json2tp_create(tp_transcode_t *tc, char *output, size_t output_size)
{
    static yajl_callbacks callbacks = { yajl_null, yajl_boolean, yajl_integer,
        yajl_double, NULL, yajl_string, yajl_start_map, yajl_map_key,
        yajl_end_map, yajl_start_array, yajl_end_array
    };

    yajl_ctx_t *ctx = tc->mf.alloc(tc->mf.ctx, sizeof(yajl_ctx_t));
    if (mp_unlikely(!ctx))
        goto error_exit;

    memset(ctx, 0 , sizeof(yajl_ctx_t));

    ctx->stage = INIT;

    ctx->output_size = output_size;
    tp_init(&ctx->tp, (char *)output, output_size, NULL, NULL);

    ctx->size = 0;
    ctx->allocated = 16;
    ctx->stack = tc->mf.alloc(tc->mf.ctx, sizeof(stack_item_t) * 16);
    if (mp_unlikely(!ctx->stack))
        goto error_exit;

    size_t i = 0;
    for (i = 0; i < ctx->allocated; ++i) {
        ctx->stack[i].ptr = NULL;
        ctx->stack[i].count = -1;
        ctx->stack[i].type = 0;
    }

    ctx->yaf = tc->mf.alloc(tc->mf.ctx, sizeof(yajl_alloc_funcs));
    if (mp_unlikely(!ctx->yaf))
        goto error_exit;

    *ctx->yaf = (yajl_alloc_funcs){tc->mf.alloc, tc->mf.realloc,
        tc->mf.free, tc->mf.ctx };

    ctx->hand = yajl_alloc(&callbacks, ctx->yaf, (void *)ctx);
    if (mp_unlikely(!ctx->hand))
        goto error_exit;

    ctx->tc = tc;

    return ctx;

error_exit:
    if (ctx) {
        if (ctx->hand)
            yajl_free(ctx->hand);

        if (ctx->stack)
            tc->mf.free(tc->mf.ctx, ctx->stack);

        if (ctx->yaf)
            tc->mf.free(tc->mf.ctx, ctx->yaf);

        tc->mf.free(tc->mf.ctx, ctx);
    }

    return NULL;
}


static void
yajl_json2tp_free(void *ctx)
{
    yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;
    if (mp_unlikely(!s_ctx))
        return;

    tp_transcode_t * tc = s_ctx->tc;

    if (mp_likely(s_ctx->stack != NULL))
        FREE(s_ctx, s_ctx->stack);

    if (mp_likely(s_ctx->yaf != NULL))
        FREE(s_ctx, s_ctx->yaf);

    if (mp_likely(s_ctx->hand != NULL))
        yajl_free(s_ctx->hand);

    tc->mf.free(tc->mf.ctx, s_ctx);
}

static enum tt_result
yajl_json2tp_transcode(void *ctx, const char *input, size_t input_size)
{
#if !defined MIN
# define MIN(a, b) ((a) > (b) ? (b) : (a))
#endif

    yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

    const unsigned char *input_ = (const unsigned char *)input;
    yajl_status stat = yajl_parse(s_ctx->hand, input_, input_size);
    if (mp_unlikely(stat != yajl_status_ok)) {

        if (s_ctx->tc->errmsg == NULL) {

            stat = yajl_complete_parse(s_ctx->hand);
            unsigned char *err = yajl_get_error(s_ctx->hand, 0,
                                                input_, input_size);
            const int l = strlen((char *)err) - 1 /* skip \n */;
            if (l > 0) {
                s_ctx->tc->errmsg = ALLOC(s_ctx, l);
                if (mp_likely(s_ctx->tc->errmsg != NULL))
                    say_error_(s_ctx->tc, 0, (char *)err, l);
            }
            yajl_free_error(s_ctx->hand, err);
            s_ctx->tc->errcode = -32700;
        }

        return TP_TRANSCODE_ERROR;
    }

    return TP_TRANSCODE_OK;

#undef MIN
}

static enum tt_result
yajl_json2tp_complete(void *ctx, size_t *complete_msg_size)
{
    yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

    const yajl_status stat = yajl_complete_parse(s_ctx->hand);
    if (mp_likely(stat == yajl_status_ok)) {
        *complete_msg_size = tp_used(&s_ctx->tp);
        return TP_TRANSCODE_OK;
    }

    return TP_TRANSCODE_ERROR;
}

/**
 * CODEC - Tarantool message to JSON RPC
 */

typedef struct tp2json {
    struct tpresponse r;

    char *output;
    char *pos;
    char *end;

    bool tp_reply_stage;

    tp_transcode_t *tc;

    int state;
} tp2json_t;

static inline int
code_conv(int code)
{
    switch (code) {
    case 0:
        break;
    case 32801:
        code = -32601;
        break;
    default:
        if (code > 0)
          code *= -1;
        break;
    }

    return code;
}

static void*
tp2json_create(tp_transcode_t *tc, char *output, size_t output_size)
{
    tp2json_t *ctx = tc->mf.alloc(tc->mf.ctx, sizeof(tp2json_t));
    if (mp_unlikely(!ctx))
        return NULL;

    ctx->pos = ctx->output = output;
    ctx->end = output + output_size;
    ctx->tc = tc;
    ctx->tp_reply_stage = true;
    ctx->state = 0;

    return ctx;
}

static void
tp2json_free(void *ctx_)
{
    tp2json_t *ctx = ctx_;
    if (mp_unlikely(!ctx))
        return;

    tp_transcode_t * tc = ctx->tc;
    tc->mf.free(tc->mf.ctx, ctx);
}

static enum tt_result
tp2json_transcode_internal(tp2json_t *ctx, const char **beg, const char *end)
{
#define PUT_CHAR(c) { *ctx->pos = (c); ++ctx->pos; }

    enum tt_result rc;
    const char *p = *beg;
    size_t len = ctx->end - ctx->pos;

    if (p == end)
        return TP_TRANSCODE_OK;

    if (mp_unlikely(ctx->pos == ctx->end))
        say_overflow_r(ctx);

    switch (mp_typeof(**beg)) {
    case MP_NIL:

        if (mp_unlikely(len < 4))
            say_overflow_r(ctx);

        mp_next(beg);
        PUT_CHAR('n')
        PUT_CHAR('u')
        PUT_CHAR('l')
        PUT_CHAR('l')
        break;
    case MP_UINT:
        if (mp_unlikely(len < sizeof("18446744073709551615") - 1))
            say_overflow_r(ctx);

        if ((ctx->state & (TYPE_MAP | TYPE_KEY)) == (TYPE_MAP | TYPE_KEY))
            ctx->pos += snprintf(ctx->pos, len, "\"%" PRIu64 "\"",
                                 mp_decode_uint(beg));
        else
            ctx->pos += snprintf(ctx->pos, len, "%" PRIu64,
                                 mp_decode_uint(beg));
        break;
    case MP_INT:
        if (mp_unlikely(len < sizeof("18446744073709551615") - 1))
            say_overflow_r(ctx);

        if ((ctx->state & (TYPE_MAP | TYPE_KEY)) == (TYPE_MAP | TYPE_KEY))
            ctx->pos += snprintf(ctx->pos, len, "\"%" PRId64 "\"",
                                 mp_decode_int(beg));
        else
            ctx->pos += snprintf(ctx->pos, len, "%" PRId64,
                                 mp_decode_int(beg));
        break;
    case MP_STR:
    {
        uint32_t strlen = 0;
        const char *str = mp_decode_str(beg, &strlen);

        if (mp_unlikely(len < strlen + 2/*""*/))
            say_overflow_r(ctx);

        ctx->pos += snprintf(ctx->pos, len, "\"%.*s\"", strlen, str);
        break;
    }
    case MP_BIN:
    {
        static const char *hex = "0123456789ABCDEF";

        uint32_t binlen = 0;
        const char *bin = mp_decode_bin(beg, &binlen);

        if (mp_unlikely(len < binlen))
            say_overflow_r(ctx);
        uint32_t i = 0;
        for (i = 0; i < binlen; i++) {
            unsigned char c = (unsigned char)bin[i];
            ctx->pos +=
                snprintf(ctx->pos, len, "\"%c%c\"", hex[c >> 4], hex[c & 0xF]);
        }
        break;
    }
    case MP_ARRAY:
    {
        const uint32_t size = mp_decode_array(beg);

        if (mp_unlikely(len < size + 2/*,[]*/))
            say_overflow_r(ctx);

        PUT_CHAR('[')
        uint32_t i = 0;
        for (i = 0; i < size; i++) {
            if (i)
                PUT_CHAR(',')
            rc = tp2json_transcode_internal(ctx, beg, end);
            if (rc != TP_TRANSCODE_OK)
                return rc;
        }
        PUT_CHAR(']')
        break;
    }
    case MP_MAP:
    {
        const uint32_t size = mp_decode_map(beg);

        if (mp_unlikely(len < size + 2/*,{}*/))
            say_overflow_r(ctx);

        ctx->state |= TYPE_MAP;

        PUT_CHAR('{')
        uint32_t i = 0;
        for (i = 0; i < size; i++) {

            if (i)
                PUT_CHAR(',')

            ctx->state |= TYPE_KEY;
            rc = tp2json_transcode_internal(ctx, beg, end);
            if (rc != TP_TRANSCODE_OK)
                return rc;

            ctx->state &= ~TYPE_KEY;

            PUT_CHAR(':')
            rc = tp2json_transcode_internal(ctx, beg, end);
            if (rc != TP_TRANSCODE_OK)
                return rc;
        }
        PUT_CHAR('}')

        ctx->state &= ~TYPE_MAP;

        break;
    }
    case MP_BOOL:
        if (mp_decode_bool(beg)) {

            if (mp_unlikely(len < sizeof("true") - 1))
                say_overflow_r(ctx);

            PUT_CHAR('t')
            PUT_CHAR('r')
            PUT_CHAR('u')
            PUT_CHAR('e')
        } else {

            if (mp_unlikely(len < sizeof("false") - 1))
                say_overflow_r(ctx);

            PUT_CHAR('f')
            PUT_CHAR('a')
            PUT_CHAR('l')
            PUT_CHAR('s')
            PUT_CHAR('e')
        }
        break;
    case MP_FLOAT:

        if (mp_unlikely(len < 7))
            say_overflow_r(ctx);

        ctx->pos += snprintf(ctx->pos, len, "%f", mp_decode_float(beg));
        break;
    case MP_DOUBLE:

        if (mp_unlikely(len < 15))
            say_overflow_r(ctx);

        ctx->pos += snprintf(ctx->pos, len, "%f", mp_decode_double(beg));
        break;
    case MP_EXT:
        /* TODO What we should do here? */
        mp_next(beg);
        break;
    default:
        return TP_TRANSCODE_ERROR;
    }

    return TP_TRANSCODE_OK;

#undef PUT_CHAR
}

static enum tt_result
tp_reply2json_transcode(void *ctx_, const char *in, size_t in_size)
{
    enum tt_result rc;

    tp2json_t *ctx = ctx_;

    if (ctx->tp_reply_stage) {

        if (tp_reply(&ctx->r, in, in_size) <= 0) {
            say_error(ctx, -32603, "[BUG] tp_reply() failed");
            goto error_exit;
        }

        ctx->tp_reply_stage = false;
    }

    if (ctx->r.error) {

        const int elen = ctx->r.error_end - ctx->r.error;
        ctx->pos += snprintf(ctx->pos, ctx->end - ctx->pos,
                "{\"id\":%zu,\"error\":{\"message\":\"%.*s\",\"code\":%d}",
                (size_t)tp_getreqid(&ctx->r),
                elen, ctx->r.error,
                code_conv(ctx->r.code));

    } else {

        ctx->pos += snprintf(ctx->output, ctx->end - ctx->output,
                "{\"id\":%zu,\"result\":", (size_t)tp_getreqid(&ctx->r));

        const char *it = ctx->r.data;
        rc = tp2json_transcode_internal(ctx, &it, ctx->r.data_end);
        if (mp_unlikely(rc == TP_TRANSCODE_ERROR))
            goto error_exit;

    }

    *ctx->pos = '}';
    ++ctx->pos;

    return TP_TRANSCODE_OK;

error_exit:
    ctx->pos = ctx->output;
    return TP_TRANSCODE_ERROR;
}

static enum tt_result
tp2json_transcode(void *ctx_, const char *in, size_t in_size)
{
    tp2json_t *ctx = ctx_;

    const char *it = in, *end = in + in_size;

    /* TODO
     * Need add tarantool message structure check like in tp_reply
     */

    /* Message len */
    enum tt_result rc = tp2json_transcode_internal(ctx, &it, end);
    if (mp_unlikely(rc == TP_TRANSCODE_ERROR))
        goto error_exit;

    /* Header */
    rc = tp2json_transcode_internal(ctx, &it, end);
    if (mp_unlikely(rc == TP_TRANSCODE_ERROR))
        goto error_exit;

    /* Body */
    rc = tp2json_transcode_internal(ctx, &it, end);
    if (mp_unlikely(rc == TP_TRANSCODE_ERROR))
        goto error_exit;

    return TP_TRANSCODE_OK;

error_exit:
    ctx->tc->errcode = -32700;
    ctx->pos = ctx->output;
    return TP_TRANSCODE_ERROR;
}

static enum tt_result
tp2json_complete(void *ctx_, size_t *complete_msg_size)
{
    tp2json_t *ctx = ctx_;

    if (mp_unlikely(ctx->pos == ctx->output)) {
        *complete_msg_size = 0;
        return TP_TRANSCODE_ERROR;
    }
    *complete_msg_size = ctx->pos - ctx->output;
    return TP_TRANSCODE_OK;
}

/**
 * Known codecs
 */
#define CODEC(create_, transcode_, complete_, free_) \
    (tp_codec_t) { \
        .create = (create_), \
        .transcode = (transcode_), \
        .complete = (complete_), \
        .free = (free_) \
}

tp_codec_t codecs[TP_CODEC_MAX] = {

    CODEC(&yajl_json2tp_create,
            &yajl_json2tp_transcode,
            &yajl_json2tp_complete,
            &yajl_json2tp_free),

    CODEC(&tp2json_create,
            &tp_reply2json_transcode,
            &tp2json_complete,
            &tp2json_free),

    CODEC(&tp2json_create,
            &tp2json_transcode,
            &tp2json_complete,
            &tp2json_free)

};
#undef CODEC

/*
 * Public API
 */

static void *
def_alloc(void *ctx, size_t s)
{
    (void)ctx;
    return malloc(s);
}

static void *
def_realloc(void *ctx, void *m, size_t s)
{
    (void)ctx;
    return realloc(m, s);
}

static void
def_free(void *ctx, void *m)
{
    (void)ctx;
    free(m);
}

enum tt_result
tp_transcode_init(tp_transcode_t *t, char *output, size_t output_size,
    enum tp_codec_type codec, mem_fun_t *mf)
{
    memset(t, 0, sizeof(tp_transcode_t));

    if (mp_unlikely(codec == TP_CODEC_MAX))
        return TP_TRANSCODE_ERROR;

    t->codec = codecs[codec];
    if (mp_unlikely(!t->codec.create))
        return TP_TRANSCODE_ERROR;

    if (mp_likely(mf != NULL)) {
        t->mf = *mf;
    } else {
        t->mf.alloc = &def_alloc;
        t->mf.realloc = &def_realloc;
        t->mf.free = &def_free;
    }

    t->codec.ctx = t->codec.create(t, output, output_size);
    if (mp_unlikely(!t->codec.ctx))
        return TP_TRANSCODE_ERROR;

    t->errcode = -32700;

    return TP_TRANSCODE_OK;
}

void
tp_transcode_free(tp_transcode_t *t)
{
  assert(t);
  assert(t->codec.ctx);

  if (mp_unlikely(t->errmsg != NULL)) {
    t->mf.free(t->mf.ctx, t->errmsg);
    t->errmsg = NULL;
  }

  t->codec.free(t->codec.ctx);
  t->codec.ctx = NULL;
}

enum tt_result
tp_transcode_complete(tp_transcode_t *t, size_t *complete_msg_size)
{
  assert(t);
  assert(t->codec.ctx);
  *complete_msg_size = 0;
  return t->codec.complete(t->codec.ctx, complete_msg_size);
}

enum tt_result
tp_transcode(tp_transcode_t *t, const char *b, size_t s)
{
  assert(t);
  assert(t->codec.ctx);
  return t->codec.transcode(t->codec.ctx, b, s);
}

bool
tp_dump(char *output, size_t output_size,
        const char *input, size_t input_size)
{
  tp_transcode_t t;
  if (tp_transcode_init(&t, output, output_size, TP_TO_JSON, NULL)
          == TP_TRANSCODE_ERROR)
    return false;

  if (tp_transcode(&t, input, input_size) == TP_TRANSCODE_ERROR) {
    tp_transcode_free(&t);
    return false;
  }

  size_t complete_msg_size = 0;
  tp_transcode_complete(&t, &complete_msg_size);
  output[complete_msg_size] = '0';

  tp_transcode_free(&t);

  return complete_msg_size > 0;
}

ssize_t
tp_read_payload(const char * const buf, const char * const end)
{
    const size_t size = end - buf;
    if (size == 0 || size < 4)
        return -1;
    const char *p = buf, *test = buf;
    if (mp_check(&test, buf + size))
        return -1;
    if (mp_typeof(*p) != MP_UINT)
        return -1;
    return mp_decode_uint(&p) + 5;
}

