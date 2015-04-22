#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>
#include <stdio.h>

#define MP_SOURCE 1
#include "tp.h"
#include "tp_transcode.h"

#define DEBUG
#if defined DEBUG
#define dd(...) fprintf(stderr, __VA_ARGS__)
#else
#define dd(...)
#endif

#define say_error_r(ctx, ...) do { \
    snprintf((ctx)->tc->errmsg, sizeof((ctx)->tc->errmsg) - 1, __VA_ARGS__); \
    return TP_TRANSCODE_ERROR; \
} while (0)

#define say_error(ctx, ...) do \
    snprintf((ctx)->tc->errmsg, sizeof((ctx)->tc->errmsg) - 1, __VA_ARGS__); \
while (0)

#define say_overflow_r(c) \
    say_error_r((c), "line:%d, 'output' buffer overflow", __LINE__)

#define ALLOC(ctx_, size) \
    (ctx_)->tc->mf.alloc((ctx_)->tc->mf.ctx, (size))
#define REALLOC(ctx_, mem, size) \
    (ctx_)->tc->mf.realloc((ctx_)->tc->mf.ctx, (mem), (size))
#define FREE(ctx_, mem) \
    (ctx_)->tc->mf.free((ctx_)->tc->mf.ctx, (mem))

/*
 * CODEC - YAJL_JSON_RPC
 */
#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>

enum { MAX_STACK_SIZE = 254 - 1 };
enum type { TYPE_MAP = 1, TYPE_ARRAY = 2 };
enum stage {
    INIT        = 0,
    BATCH       = 1,
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

    int been_stages;

    uint32_t id;

    tp_transcode_t *tc;
    char *call_end;
} yajl_ctx_t;

static inline bool
spush(yajl_ctx_t *s,  char *ptr, int mask)
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
#define say_overflow_r_2(c) do { \
    say_error((c), "'output' buffer overflow"); \
    return 0; \
} while (0)

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
            say_error(s_ctx, "'id' _must_ be less than UINT32_t");
            return 0;
        }

        s_ctx->id = v;
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

        dd("method: %.*s\n", (int)len, str);

        int rc = 0;

        if (s_ctx->been_stages & PARAMS) {

            const size_t call_pos = (s_ctx->call_end - s_ctx->tp.s);
            const size_t m_size = len + call_pos + 5;

            char *m = ALLOC(s_ctx, m_size);
            if (mp_unlikely(!m)) {
                say_error(s_ctx, "'output' buffer overflow");
                goto hooking_call_done;
            }

            struct tp call;
            tp_init(&call, m, m_size, NULL, NULL);
            if (mp_unlikely(!tp_call(&call, (char *)str, len))) {
                say_error(s_ctx, "'output' buffer overflow");
                goto hooking_call_done;
            }

            const ssize_t pos_diff = (call.p - call.s) - call_pos;
            if (mp_unlikely(pos_diff < 0)) {
                say_error(s_ctx,
                            "If you see this message know BIG shit happend"
                            "at line %d", __LINE__);
                goto hooking_call_done;
            }

            if (mp_unlikely( (s_ctx->tp.p - s_ctx->tp.s + pos_diff)
                        > s_ctx->tp.e - s_ctx->tp.s
                        ) )
            {
                say_error(s_ctx, "'output' buffer overflow");
                goto hooking_call_done;
            }

            memmove(s_ctx->tp.s + (call.p - call.s) /* already + 5 */,
                    s_ctx->tp.s + call_pos,
                    (s_ctx->tp.p - s_ctx->tp.s));

            memcpy(s_ctx->tp.s, m, call.p - call.s);

            /* Shift 'pos' on diff & store new pkt size
             */
            s_ctx->tp.p += pos_diff;
            *s_ctx->tp.size = 0xce;
            *(uint32_t*)(s_ctx->tp.size + 1)
                        = mp_bswap_u32(s_ctx->tp.p - s_ctx->tp.size - 5);

            rc = 1;

hooking_call_done:
            if (mp_likely(m != NULL))
                FREE(s_ctx, m);

        } else /* 'method' before 'params'.
                *  Just reset tp's positions
                */
        {
            s_ctx->tp.p = s_ctx->tp.s;
            s_ctx->tp.sync = 0;
            s_ctx->tp.size = NULL;

            if (mp_unlikely(!tp_call(&s_ctx->tp, (char *)str, len))) {
                say_error(s_ctx, "'output' buffer overflow");
                return 0;
            }

            rc = 1;
        }

        dd("METHOD END\n");

        s_ctx->stage = WAIT_NEXT;
        s_ctx->been_stages |= METHOD;

        return rc;
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
            s_ctx->stage = PARAMS;
            s_ctx->been_stages |= PARAMS;
        }
        else if (len == sizeof("id") - 1
            && key[0] == 'i'
            && key[1] == 'd')
        {
            dd("ID STAGE\n");
            s_ctx->stage = ID;
            s_ctx->been_stages |= ID;
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
            dd("SKIPING: %.*s\n", (int)len, key);
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
        say_error(s_ctx, "'stack' overflow");
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
    if (mp_unlikely(s_ctx->stage != PARAMS))
        return 1;

    stack_item_t *item = spop(s_ctx);
    if (mp_likely(item != NULL)) {

        dd("map close, count %d '}'\n", (int)item->count);

        *(item->ptr++) = 0xdf;
        *(uint32_t *) item->ptr = mp_bswap_u32(item->count);

        if (mp_unlikely(item->type & PARAMS)) {
            dd("PARAMS END\n");
            s_ctx->stage = WAIT_NEXT;
        }
    } else
        s_ctx->stage = WAIT_NEXT;

    return 1;
}

static int
yajl_start_array(void *ctx)
{
    yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

    if (mp_unlikely(s_ctx->stage == INIT)) {
        say_error(s_ctx, "Batch not suported yet");
        return 0;
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
        say_error(s_ctx, "'stack' overflow");
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
        }
    } else
        s_ctx->stage = WAIT_NEXT;

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

    tp_call(&ctx->tp, "t", 1);
    ctx->call_end = ctx->tp.s + (ctx->tp.p - ctx->tp.s);

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

        if (s_ctx->tc->errmsg[0] == 0) {
            s_ctx->tc->errcode = -32700;
            stat = yajl_complete_parse(s_ctx->hand);
            unsigned char *err = yajl_get_error(s_ctx->hand, 0,
                                                input_, input_size);
            const int l = strlen((char *)err);
            if (l > 0)
                say_error(s_ctx, "%.*s", l - 1 /* skip \n */, (char *)err);
            yajl_free_error(s_ctx->hand, err);
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

    char *p = &s_ctx->tc->errmsg[0];
    char *e = &s_ctx->tc->errmsg[0] + sizeof(s_ctx->tc->errmsg) - 1;

    const yajl_status stat = yajl_complete_parse(s_ctx->hand);
    if (mp_likely(stat == yajl_status_ok)) {
        tp_reqid(&s_ctx->tp, s_ctx->id);
        *complete_msg_size = tp_used(&s_ctx->tp);
        return TP_TRANSCODE_OK;
    } else if (s_ctx->tc->errmsg[0] != 0) {
        p += snprintf(p, e - p, "%s", s_ctx->tc->errmsg);
        s_ctx->tc->errcode = -32000;
        return TP_TRANSCODE_ERROR;
    }

    if (mp_unlikely(!(s_ctx->been_stages & METHOD))) {
        p += snprintf(p, e - p, "Method not found");
    } else if (mp_unlikely(!(s_ctx->been_stages & PARAMS))) {
        p += snprintf(p, e - p, "Params not found");
    } else if (mp_unlikely(!(s_ctx->been_stages & ID))) {
        p += snprintf(p, e - p, "Id not found");
    } else {
        p += snprintf(p, e - p,
                "call _must_ contains 'method':'tnt_call',"
                "'params':object ");
    }

    s_ctx->tc->errcode = -32600;
    return TP_TRANSCODE_ERROR;
}

/**
 * CODEC - Tarantool message to JSON RPC
 */

typedef struct tp2json {
    char *output;
    char *pos;
    char *end;

    struct tpresponse r;
    bool tp_reply_stage;

    tp_transcode_t *tc;
} tp2json_t;

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

        ctx->pos += snprintf(ctx->pos, len, "%" PRIu64, mp_decode_uint(beg));
        break;
    case MP_INT:
        if (mp_unlikely(len < sizeof("18446744073709551615") - 1))
            say_overflow_r(ctx);

        ctx->pos += snprintf(ctx->pos, len, "%" PRId64, mp_decode_int(beg));
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
                snprintf(ctx->pos, len, "%c%c", hex[c >> 4], hex[c & 0xF]);
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

        PUT_CHAR('{')
        uint32_t i = 0;
        for (i = 0; i < size; i++) {
            if (i)
                PUT_CHAR(',')
            rc = tp2json_transcode_internal(ctx, beg, end);
            if (rc != TP_TRANSCODE_OK)
                return rc;

            PUT_CHAR(':')
            rc = tp2json_transcode_internal(ctx, beg, end);
            if (rc != TP_TRANSCODE_OK)
                return rc;
        }
        PUT_CHAR('}')
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

        rc = tp_reply(&ctx->r, in, in_size);
        if (rc <= 0) {
            say_error(ctx, "tarantool message parse error");
            goto error_exit;
        }

        ctx->pos += snprintf(ctx->output, ctx->end - ctx->output,
                "{\"id\":%zu,\"result\":", (size_t)tp_getreqid(&ctx->r));

        ctx->tp_reply_stage = false;
    }

    if (ctx->r.error) {

        const int elen = ctx->r.error_end - ctx->r.error;
        ctx->pos += snprintf(ctx->pos, ctx->end - ctx->pos,
                "{\"error\":{\"msg\":\"%.*s\",\"code\":%d}}",
                elen, ctx->r.error,
                ctx->r.code);

    } else {

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

static void *def_alloc(void *ctx, size_t s) {
    (void)ctx;
    return malloc(s);
}

static void *def_realloc(void *ctx, void *m, size_t s) {
    (void)ctx;
    return realloc(m, s);
}

static void def_free(void *ctx, void *m) {
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

ssize_t
tp_read_payload(const char * const buf, const char * const end)
{
    const size_t size = end - buf;
    if (size == 0 || size < 5)
        return 0;
    const char *p = buf, *test = buf;
    if (mp_check(&test, buf + size))
        return -1;
    if (mp_typeof(*p) != MP_UINT)
        return -1;
    return mp_decode_uint(&p) + 5;
}

