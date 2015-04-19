#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>
#include <stdio.h>

#define MP_SOURCE 1
#include "tp.h"
#include "tp_transcode.h"

#if defined VERBOSE
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

/*
 * CODEC - YAJL_JSON_RPC
 */
#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>

enum { MAX_STACK_SIZE = 254 - 1 /* one for 'params'*/ };
enum type { TYPE_MAP = 1, TYPE_ARRAY = 2 };
enum stage { START = 0, PARAMS = 4, ID = 8, METHOD = 16 };
enum tp_stage { TP_START = 0, TP_METHOD = 64, TP_ARGS = 128 };

typedef struct {
	char *ptr;
	int16_t count;
	uint16_t type;
} stack_item_t;

typedef struct {
	yajl_handle hand;

	stack_item_t *stack;
	uint8_t size, allocated;

	char *b;
	size_t output_size;
	struct tp tp;

	enum stage stage;
	enum tp_stage tp_stage;

	int been_stages;

	uint32_t id;

	tp_transcode_t *tc;
} yajl_ctx_t;

static inline bool
spush(yajl_ctx_t *s,  char *ptr, int mask)
{
	if (mp_likely(s->size < MAX_STACK_SIZE)) {

		if (mp_unlikely(s->allocated < s->size)) {

			s->stack = (stack_item_t *)realloc(s->stack,
											   sizeof(stack_item_t) * 16);
			if (s->stack == NULL)
				return false;

			s->allocated += 16;
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

static const char bad_tnt_method[] =
	"tarantool method _must_ be first _string_ in 'params' array";
#define BAD_TNT_METHOD_R {\
	say_error(s_ctx, "%s", bad_tnt_method); \
	dd("BAD_TNT_METHOD_R: %d\n", __LINE__);\
	return 0; }

static const char bad_id[] = "'id' _must_ be positive integer";
#define BAD_ID_R {\
	say_error(s_ctx, "%s", bad_id); \
	dd("BAD_ID_R: %d\n", __LINE__);\
	return 0; }

static int
yajl_null(void *ctx)
{
	yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

	if (mp_likely(s_ctx->stage == PARAMS)) {

		if (mp_likely(s_ctx->tp_stage == TP_ARGS)) {

			dd("null\n");

			sinc_if_array(s_ctx);
			if (mp_unlikely(!tp_encode_nil(&s_ctx->tp)))
				say_overflow_r_2(s_ctx);

		} else
			BAD_TNT_METHOD_R

	} else if (s_ctx->stage == ID)
		BAD_ID_R

	return 1;
}

static int
yajl_boolean(void * ctx, int v)
{
	yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

	if (mp_likely(s_ctx->stage == PARAMS)) {

		if (mp_likely(s_ctx->tp_stage == TP_ARGS)) {

			dd("bool: %s\n", v ? "true" : "false");

			sinc_if_array(s_ctx);
			if (mp_unlikely(!tp_encode_bool(&s_ctx->tp, v)))
				say_overflow_r_2(s_ctx);

		} else
			BAD_TNT_METHOD_R

	} else if (s_ctx->stage == ID)
		BAD_ID_R

	return 1;
}

static int
yajl_integer(void *ctx, long long v)
{
	yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

	if (mp_likely(s_ctx->stage == PARAMS)) {

		if (mp_likely(s_ctx->tp_stage == TP_ARGS)) {

			dd("integer: %lld\n", v);

			sinc_if_array(s_ctx);

			char *r = NULL;
			if (v < 0)
				r = tp_encode_int(&s_ctx->tp, (int64_t)v);
			else
				r = tp_encode_uint(&s_ctx->tp, (uint64_t)v);

			if (mp_unlikely(!r))
				say_overflow_r_2(s_ctx);
		} else
			BAD_TNT_METHOD_R

	} else if (s_ctx->stage == ID) {

		dd ("id: %lld\n", v);

		if (mp_unlikely(v > UINT32_MAX)) {
			say_error(s_ctx, "'id' large then UINT32_t");
			return 0;
		}

		s_ctx->id = v;
		s_ctx->stage = START;
	}

	return 1;
}

static int
yajl_double(void *ctx, double v)
{
	yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

	if (mp_likely(s_ctx->stage == PARAMS)) {

		dd("double: %g\n", v);

		if (mp_likely(s_ctx->tp_stage == TP_ARGS)) {
			sinc_if_array(s_ctx);

			if (mp_unlikely(!tp_encode_double(&s_ctx->tp, v)))
				say_overflow_r_2(s_ctx);
		} else
			BAD_TNT_METHOD_R

	} else if (s_ctx->stage == ID)
		BAD_ID_R

	return 1;
}

static int
yajl_string(void *ctx, const unsigned char * str, size_t len)
{
	yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

	if (mp_likely(s_ctx->stage == PARAMS)) {

		if (mp_likely(s_ctx->tp_stage == TP_ARGS)) {

			dd("string: %.*s\n", (int)len, str);

			if (len > 0) {

				sinc_if_array(s_ctx);

				if (mp_unlikely(!tp_encode_str(&s_ctx->tp,
								(const char *)str, len)))
					say_overflow_r_2(s_ctx);
			}

		} else if (s_ctx->tp_stage == TP_METHOD) {

			dd("method: %.*s\n", (int)len, str);

			if (mp_unlikely(!tp_call(&s_ctx->tp, (const char *)str, len)))
				say_overflow_r_2(s_ctx);

			s_ctx->tp_stage = TP_ARGS;
			s_ctx->been_stages |= TP_METHOD;

			if (!spush(ctx, NULL, PARAMS | TP_METHOD)) {
				say_error(s_ctx, "'stack' overflow");
				return 0;
			}

		} else
			assert(false);

	} else if (s_ctx->stage == METHOD) {

		s_ctx->stage = START;

	} else if (s_ctx->stage == ID)
		BAD_ID_R
	else
		assert(false);

	return 1;
}

static int
yajl_map_key(void *ctx, const unsigned char * key, size_t len)
{
	yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

	if (mp_likely(s_ctx->stage == PARAMS)) {

		dd("key: %.*s\n", (int)len, key);

		if (mp_likely(s_ctx->tp_stage == TP_ARGS)) {
			sinc_if_map(s_ctx);

			if (mp_unlikely(!tp_encode_str(&s_ctx->tp, (const char *)key, len)))
				say_overflow_r_2(s_ctx);
		} else
			BAD_TNT_METHOD_R

	} else if (s_ctx->stage != ID && s_ctx->stage != METHOD) {

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
			s_ctx->tp_stage = TP_METHOD;
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
			s_ctx->been_stages |= METHOD;
		}
		else
		{
			const int l = len > 32 ? 32 : len;
			say_error(s_ctx,
				"unknown key '%.*s', allowed: 'method', 'params', 'id'",
				l, key);
			return 0;
		}
	}

	return 1;
}

static int
yajl_start_map(void *ctx)
{
	yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

	if (mp_likely(s_ctx->stage == PARAMS)) {

		if (mp_likely(s_ctx->tp_stage == TP_ARGS)) {

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
		} else
			BAD_TNT_METHOD_R

	} else if (s_ctx->stage == ID)
		BAD_ID_R

	return 1;
}


static int
yajl_end_map(void *ctx)
{
	yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

	if (mp_likely(s_ctx->stage == PARAMS)) {

		if (mp_likely(s_ctx->tp_stage == TP_ARGS)) {

			stack_item_t *item = spop(s_ctx);
			if (item && !(item->type & TP_METHOD)) {

				dd("map close, count %d '}'\n", (int)item->count);

				*(item->ptr++) = 0xdf;
				*(uint32_t *) item->ptr = mp_bswap_u32(item->count);

			 if (mp_unlikely(item->type & PARAMS)) {
					s_ctx->stage = START;
					s_ctx->tp_stage = TP_START;
				}
			}

		} else
			BAD_TNT_METHOD_R
	}

	return 1;
}

static int
yajl_start_array(void *ctx)
{
	yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

	if (mp_likely(s_ctx->stage != PARAMS))
		return 1;

	if (mp_unlikely(s_ctx->tp_stage == TP_METHOD)) {

		return 1;

	} else if (s_ctx->tp_stage == TP_ARGS) {

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

	} else
		BAD_TNT_METHOD_R

	return 1;
}

static int
yajl_end_array(void *ctx)
{
	yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

	if (mp_unlikely(s_ctx->stage != PARAMS))
		return 1;

	stack_item_t *item = stop(s_ctx);
	if (item && item->type & TP_METHOD)
		s_ctx->stage = START;

	if (mp_likely(s_ctx->tp_stage == TP_ARGS)) {

		stack_item_t *item = spop(s_ctx);
		if (item && !(item->type & TP_METHOD)) {
			dd("array close, count %d ']'\n", item->count);

			*(item->ptr++) = 0xdd;
			*(uint32_t *) item->ptr = mp_bswap_u32(item->count);

			if (item->type & PARAMS)
				s_ctx->tp_stage = TP_START;

			s_ctx->been_stages |= TP_ARGS;
		}

	} else
		BAD_TNT_METHOD_R

	return 1;
}

static void *
yajl_json2tp_create(tp_transcode_t *tc, char *output, size_t output_size)
{
	static yajl_callbacks callbacks = { yajl_null, yajl_boolean, yajl_integer,
		yajl_double, NULL, yajl_string, yajl_start_map, yajl_map_key,
		yajl_end_map, yajl_start_array, yajl_end_array
	};

	yajl_ctx_t *ctx = malloc(sizeof(yajl_ctx_t));
	if (mp_unlikely(ctx == NULL))
		goto error_exit;

	memset(ctx, 0 , sizeof(yajl_ctx_t));

	ctx->stage = START;
	ctx->tp_stage = TP_START;

	ctx->output_size = output_size;
	tp_init(&ctx->tp, (char *)output, output_size, NULL, NULL);

	ctx->size = 0;
	ctx->allocated = 16;
	ctx->stack = (stack_item_t *)malloc(sizeof(stack_item_t) * 16);
	if (ctx->stack == NULL)
		goto error_exit;

	size_t i = 0;
	for (i = 0; i < ctx->allocated; ++i) {
		ctx->stack[i].ptr = NULL;
		ctx->stack[i].count = -1;
		ctx->stack[i].type = 0;
	}

	ctx->hand = yajl_alloc(&callbacks, NULL, (void *)ctx);
	if (mp_unlikely(ctx->hand == NULL))
		goto error_exit;

	ctx->tc = tc;

	return ctx;

error_exit:
	if (ctx && ctx->hand)
		yajl_free(ctx->hand);

	if (ctx->stack)
		free(ctx->stack);

	if (ctx)
		free(ctx);

	return NULL;
}


static void
yajl_json2tp_free(void *ctx)
{
	yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

	if (s_ctx) {

		if (s_ctx->stack)
			free(s_ctx->stack);

		if (mp_likely(s_ctx->hand != NULL))
			yajl_free(s_ctx->hand);

		free(s_ctx);
	}
}

static int
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
			stat = yajl_complete_parse(s_ctx->hand);
			unsigned char *err = yajl_get_error(s_ctx->hand, 0,
						input_, input_size);
			int l = strlen((char *)err);
			if (l > 0) {
				l -= 1; /* skip \n */
				say_error(s_ctx, "%.*s", l, (char *)err);
			}
			yajl_free_error(s_ctx->hand, err);
		}

		return TP_TRANSCODE_ERROR;
	}

	return TP_TRANSCODE_OK;
#undef MIN
}

static int
yajl_json2tp_complete(void *ctx, size_t *complete_msg_size)
{
	yajl_ctx_t *s_ctx = (yajl_ctx_t *)ctx;

	char *p = &s_ctx->tc->errmsg[0];
	char *e = &s_ctx->tc->errmsg[0] + sizeof(s_ctx->tc->errmsg) - 1;

	const yajl_status stat = yajl_complete_parse(s_ctx->hand);
	if (mp_unlikely(stat != yajl_status_ok)) {

		if (s_ctx->tc->errmsg[0] == 0) {
			p += snprintf(p, e - p, "json _must_ contain 'method':str,"
						"'params': ['tnt_method', arg1, ... argN]");
		}

		return TP_TRANSCODE_ERROR;
	}

	if (mp_unlikely(
		!(s_ctx->been_stages & METHOD
			&& s_ctx->been_stages & PARAMS
			&& s_ctx->been_stages & TP_METHOD
			&& s_ctx->been_stages & TP_ARGS)
		|| !s_ctx->been_stages
	))
	{
		p += snprintf(p, e - p, "json _must_ contain 'method':str,"
						"'params': ['tnt_method', arg1, ... argN]");
		return TP_TRANSCODE_ERROR;
	}

	tp_reqid(&s_ctx->tp, s_ctx->id);

	*complete_msg_size = tp_used(&s_ctx->tp);

	return TP_TRANSCODE_OK;
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
	tp2json_t *ctx = malloc(sizeof(tp2json_t));
	if (mp_likely(ctx != NULL)) {
		ctx->pos = ctx->output = output;
		ctx->end = output + output_size;
		ctx->tc = tc;
		ctx->tp_reply_stage = true;
	}
	return ctx;
}

static void
tp2json_free(void *ctx_)
{
	tp2json_t *ctx = ctx_;
	free(ctx);
}

static int
tp2json_transcode_internal(tp2json_t *ctx, const char **beg, const char *end)
{
#define PUT_CHAR(c) { *ctx->pos = (c); ++ctx->pos; }

	int rc;
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

static int
tp_reply2json_transcode(void *ctx_, const char *in, size_t in_size)
{
	int rc;

	tp2json_t *ctx = ctx_;

	if (ctx->tp_reply_stage) {

		rc = tp_reply(&ctx->r, in, in_size);
		if (rc == 0)
			return TP_TRANSCODE_AGAIN;
		else if (rc < 0) {
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
		else if (rc == TP_TRANSCODE_AGAIN)
			return TP_TRANSCODE_AGAIN;

	}

	*ctx->pos = '}';
	++ctx->pos;

	return TP_TRANSCODE_OK;

error_exit:
	ctx->pos = ctx->output;
	return TP_TRANSCODE_ERROR;
}

static int
tp2json_transcode(void *ctx_, const char *in, size_t in_size)
{
	tp2json_t *ctx = ctx_;

	const char *it = in, *end = in + in_size;

	/* TODO
	 * Need add tarantool message structure check like in tp_reply
	 */

	/* Message len */
	int rc = tp2json_transcode_internal(ctx, &it, end);
	if (mp_unlikely(rc == TP_TRANSCODE_ERROR))
		goto error_exit;
	if (rc == TP_TRANSCODE_AGAIN)
		return rc;

	/* Header */
	rc = tp2json_transcode_internal(ctx, &it, end);
	if (mp_unlikely(rc == TP_TRANSCODE_ERROR))
		goto error_exit;
	if (rc == TP_TRANSCODE_AGAIN)
		return rc;

	/* Body */
	rc = tp2json_transcode_internal(ctx, &it, end);
	if (mp_unlikely(rc == TP_TRANSCODE_ERROR))
		goto error_exit;
	if (rc == TP_TRANSCODE_AGAIN)
		return rc;

	return TP_TRANSCODE_OK;

error_exit:
	ctx->pos = ctx->output;
	return TP_TRANSCODE_ERROR;
}

static int
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

int
tp_transcode_init(tp_transcode_t *t, char *output, size_t output_size,
	enum tp_codec_type codec)
{
	memset(t, 0, sizeof(tp_transcode_t));

	if (mp_unlikely(codec == TP_CODEC_MAX))
		return TP_TRANSCODE_ERROR;

	t->codec = codecs[codec];
	if (mp_unlikely(!t->codec.create))
		return TP_TRANSCODE_ERROR;

	t->codec.ctx = t->codec.create(t, output, output_size);
	if (mp_unlikely(!t->codec.ctx))
		return TP_TRANSCODE_ERROR;

	return TP_TRANSCODE_OK;
}

ssize_t
tp_read_payload(const char * const buf, const char * const end)
{
	const size_t size = end - buf;
	if (size == 0 || size < 6)
		return 0;
	const char *p = buf, *test = buf;
	if (mp_check(&test, buf + size))
		return -1;
	if (mp_typeof(*p) != MP_UINT)
		return -1;
	return mp_decode_uint(&p) + p - buf;
}

