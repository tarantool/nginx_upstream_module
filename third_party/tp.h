#ifndef TP_H_INCLUDED
#define TP_H_INCLUDED

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <unistd.h>

#include <msgpuck.h>

#if !(defined TP_H_AUTH_OFF)
#  include <sha1.h>
#  include <base64.h>
#endif // TP_H_AUTH_OFF

#ifdef __cplusplus
extern "C" {
#endif

#define tpfunction_unused __attribute__((unused))

#if !defined __GNUC_MINOR__ || defined __INTEL_COMPILER || \
	defined __SUNPRO_C || defined __SUNPRO_CC
#define TP_GCC_VERSION(major, minor) 0
#else
#define TP_GCC_VERSION(major, minor) (__GNUC__ > (major) || \
	(__GNUC__ == (major) && __GNUC_MINOR__ >= (minor)))
#endif

#if !defined(__has_builtin)
#define __has_builtin(x) 0 /* clang */
#endif

#if TP_GCC_VERSION(2, 9) || __has_builtin(__builtin_expect)
#define tplikely(x) __builtin_expect(!!(x), 1)
#define tpunlikely(x) __builtin_expect(!!(x), 0)
#else
#define tplikely(x) (x)
#define tpunlikely(x) (x)
#endif

/* {{{ API declaration */
struct tp;

/* available types */
enum tp_type {
	TP_NIL = MP_NIL,
	TP_UINT = MP_UINT,
	TP_INT = MP_INT,
	TP_STR = MP_STR,
	TP_BIN = MP_BIN,
	TP_ARRAY = MP_ARRAY,
	TP_MAP = MP_MAP,
	TP_BOOL = MP_BOOL,
	TP_FLOAT = MP_FLOAT,
	TP_DOUBLE = MP_DOUBLE,
	TP_EXT = MP_EXT
};

/* header */
enum tp_header_key_t {
  TP_CODE      = 0x00,
	TP_SYNC      = 0x01,
	TP_SERVER_ID = 0x02,
	TP_LSN       = 0x03,
	TP_TIMESTAMP = 0x04,
	TP_SCHEMA_ID = 0x05
};

/* request body */
enum tp_body_key_t {
	TP_SPACE = 0x10,
	TP_INDEX = 0x11,
	TP_LIMIT = 0x12,
	TP_OFFSET = 0x13,
	TP_ITERATOR = 0x14,
	TP_INDEX_BASE = 0x15,
	TP_KEY = 0x20,
	TP_TUPLE = 0x21,
	TP_FUNCTION = 0x22,
	TP_USERNAME = 0x23,
	TP_SERVER_UUID = 0x24,
	TP_CLUSTER_UUID = 0x25,
	TP_VCLOCK = 0x26,
	TP_EXPRESSION = 0x27,
	TP_OPS = 0x28
};

/* response body */
enum tp_response_key_t {
	TP_DATA = 0x30,
	TP_ERROR = 0x31
};

/* request types */
enum tp_request_type {
	TP_SELECT = 1,
	TP_INSERT = 2,
	TP_REPLACE = 3,
	TP_UPDATE = 4,
	TP_DELETE = 5,
  TP_UPSERT = 9,
	TP_CALL = 10,
	TP_AUTH = 7,
	TP_EVAL = 8,
	TP_PING = 64,
	TP_JOIN = 65,
	TP_SUBSCRIBE = 66
};

enum tp_iterator_type {
	TP_ITERATOR_EQ = 0,
	TP_ITERATOR_REQ,
	TP_ITERATOR_ALL,
	TP_ITERATOR_LT,
	TP_ITERATOR_LE,
	TP_ITERATOR_GE,
	TP_ITERATOR_GT,
	TP_ITERATOR_BITS_ALL_SET,
	TP_ITERATOR_BITS_ANY_SET,
	TP_ITERATOR_BITS_ALL_NON_SET,
	TP_ITERATOR_OVERLAPS,
	TP_ITERATOR_NEIGHBOR
};

static const uint32_t SCRAMBLE_SIZE = 20;
typedef char *(*tp_reserve)(struct tp *p, size_t req, size_t *size);

/**
 * tp greetings object - points to parts of a greetings buffer.
 */
struct tpgreeting {
	const char *description; /* Points to a text containg tarantool name and version */
	const char *salt_base64; /* Points to connection salt in base64 encoding */
};

/*
 * Main tp request object - points to a request buffer.
 *
 * All fields except tp->p should not be accessed directly.
 * Appropriate accessors should be used instead.
*/
struct tp {
	char *s, *p, *e;       /* start, pos, end */
	char *size;            /* pointer to length field of current request */
	char *sync;            /* pointer to sync field of current request */
	tp_reserve reserve;    /* realloc function pointer */
	void *obj;             /* realloc function pointer */
};

/**
 * Struct for iterating msgpack array
 *
 * struct tp_array_itr itr;
 * if (tp_array_itr_init(&itr, buf, buf_size))
 * 	panic("It's not a valid array!.");
 * while (tp_array_itr_next(&itr)) {
 * 	mp_print(itr.elem, itr.elem_end);
 * 	// or do smth else with itr.elem
 * }
 */
struct tp_array_itr {
	const char *data; /* pointer to the beginning of array */
	const char *first_elem; /* pointer to the first element of array */
	const char *elem; /* pointer to current element of array */
	const char *elem_end; /* pointer to current element end of array */
	uint32_t elem_count; /* number of elements in array */
	int cur_index; /* index of current element */
};

/**
 * Struct for iterating msgpack map
 *
 * struct tp_map_itr itr;
 * if (tp_map_itr_init(&itr, buf, buf_size))
 * 	panic("It's not a valid map!.");
 * while(tp_map_itr_next(&itr)) {
 * 	mp_print(itr.key, itr.key_end);
 * 	mp_print(itr.value, itr.value_end);
 * 	// or do smth else with itr.key and itr.value
 * }
 */
struct tp_map_itr {
	const char *data; /* pointer to the beginning of map */
	const char *first_key; /* pointer to the first key of map */
	const char *key; /* pointer to current key of map */
	const char *key_end; /* pointer to current key end */
	const char *value; /* pointer to current value of map */
	const char *value_end; /* pointer to current value end */
	uint32_t pair_count; /* number of key-values pairs in array */
	int cur_index; /* index of current pair */
};

/**
 * tp response object - points to parts of a response buffer
 */
struct tpresponse {
	uint64_t bitmap;               /* bitmap of field IDs that was read from response */
	const char *buf;               /* points to beginning of buffer */
	uint32_t code;                 /* response code (0 is success, or errno if not) */
	uint32_t sync;                 /* synchronization id */
	uint32_t schema_id;            /* schema id */
	const char *error;             /* error message (NULL if not present) */
	const char *error_end;         /* end of error message (NULL if not present) */
	const char *data;              /* tuple data (NULL if not present) */
	const char *data_end;          /* end if tuple data (NULL if not present) */
	struct tp_array_itr tuple_itr; /* internal iterator over tuples */
	struct tp_array_itr field_itr; /* internal iterator over tuple fields */
};

/**
 * Receive greetings from the server.
 * Note, the input buffer is not copied,
 * so freeing or reusing the input buffer will invalidate tpgreeting struct
 * For example salt is used for authorization (tp_auth).
 * Returns 0 if buffer is too small or greetings size (128) on success
 */
static inline ssize_t
tp_greeting(struct tpgreeting *g, char *buf, size_t size);

/**
 * Main request initialization function.
 *
 * buf     - current buffer, may be NULL
 * size    - current buffer size
 * reserve - reallocation function, may be NULL
 * obj     - pointer to be passed to the reallocation function as
 *           context
 *
 * Either a buffer pointer or a reserve function must be
 * provided.
 * If reserve function is provided, data must be manually freed
 * when the buffer is no longer needed.
 *  (e.g. free(p->s) or tp_free(p) );
 */
static inline void
tp_init(struct tp *p, char *buf, size_t size,
        tp_reserve reserve, void *obj);

/**
 * A common reallocation function, can be used
 * for 'reserve' param in tp_init().
 * Resizes the buffer twice the previous size using realloc().
 *
 * struct tp req;
 * tp_init(&req, NULL, tp_realloc, NULL);
 * tp_ping(&req); // will call the reallocator
 *
 * data must be manually freed when the buffer is no longer
 * needed.
 * (e.g. free(p->s) or tp_free(p) );
 * if realloc will return NULL, then you must destroy previous memory.
 * (e.g.
 * if (tp_realloc(p, ..) == NULL) {
 * 	tp_free(p)
 * 	return NULL;
 * }
*/
tpfunction_unused static inline char *
tp_realloc(struct tp *p, size_t required, size_t *size);

/**
 * Free function for use in a pair with tp_realloc.
 * Don't use it when tp inited with static buffer!
 */
static inline void
tp_free(struct tp *p);

/**
 * Get currently allocated buffer pointer
 */
static inline char *
tp_buf(struct tp *p);

/**
 * Get currently allocated buffer size
 */
static inline size_t
tp_size(struct tp *p);

/**
 * Get the size of data in the buffer
 */
static inline size_t
tp_used(struct tp *p);

/**
 * Get the size available for write
 */
static inline size_t
tp_unused(struct tp *p);

/**
 * Create a ping request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_ping(&req);
 */
static inline char *
tp_ping(struct tp *p);

/**
 * Create an auth request.
 *
 * salt_base64 must be gathered from tpgreeting struct,
 * that is initialized during tp_greeting call.
 *
 * tp_auth(p, greet.salt_base64, "admin", 5, "pass", 4);
 */
static inline char *
tp_auth(struct tp *p, const char *salt_base64, const char *user, int ulen, const char *pass, int plen);

/**
 * Append a select request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_select(&req, 0, 0, 0, 100);
 * tp_key(&req, 1);
 * tp_sz(&req, "key");
 */
static inline char *
tp_select(struct tp *p, uint32_t space, uint32_t index,
	  uint32_t offset, enum tp_iterator_type iterator, uint32_t limit);

/**
 * Create an insert request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_insert(&req, 0);
 * tp_tuple(&req, 2);
 * tp_sz(&req, "key");
 * tp_sz(&req, "value");
 */
static inline char *
tp_insert(struct tp *p, uint32_t space);

/**
 * Create a replace request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_insert(&req, 0);
 * tp_tuple(&req, 2);
 * tp_sz(&req, "key");
 * tp_sz(&req, "value");
 */
static inline char *
tp_replace(struct tp *p, uint32_t space);

/**
 * Create a delete request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_delete(&req, 0);
 * tp_key(&req, 1);
 * tp_sz(&req, "key");
 */
static inline char *
tp_delete(struct tp *p, uint32_t space, uint32_t index);

/**
 * Create an update request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_update(&req, 0); // update of space 0
 * tp_key(&req, 1); // key with one part
 * tp_sz(&req, "key"); // one and only part of the key
 * tp_updatebegin(&req, 2); // update with two operations
 * tp_op(&req, "+", 2); // add to field 2 ..
 * tp_encode_uint(&req, 1); // .. a value 1
 * tp_op(&req, "=", 3); // set a field 3 ..
 * tp_sz(&req, "value"); // .. a value "value"
 */
static inline char *
tp_update(struct tp *p, uint32_t space, uint32_t index);

/**
 * Begin update operations.
 * See tp_update description for details.
 */
static inline char *
tp_updatebegin(struct tp *p, uint32_t op_count);

/**
 * Create an upsert request.
 */
static inline char *
tp_upsert(struct tp *p, uint32_t space);

/**
 * Upsert. Begin add operations.
 */
static inline char *
tp_upsertbegin_add_ops(struct tp *p, uint32_t op_count);

/**
 * Add an update operation.
 * See tp_update description.
 * Operation op could be:
 * "=" - assign operation argument to field <field_no>;
 *  will extend the tuple if <field_no> == <max_field_no> + 1
 * "#" - delete <argument> fields starting from <field_no>
 * "!" - insert <argument> before <field_no>
 * The following operation(s) are only defined for integer
 * types:
 * "+" - add argument to field <field_no>, argument
 * are integer
 * "-" - subtract argument from the field <field_no>
 * "&" - bitwise AND of argument and field <field_no>
 * "^" - bitwise XOR of argument and field <field_no>
 * "|" - bitwise OR of argument and field <field_no>
 */
static inline char *
tp_op(struct tp *p, char op, uint32_t field_no);

/**
 * Create a call request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 *
 * char proc[] = "hello_proc";
 * tp_call(&req, proc, sizeof(proc) - 1);
 * tp_encode_array(&req, 2);
 * tp_sz(&req, "arg1");
 * tp_sz(&req, "arg2");
 */
static inline char *
tp_call(struct tp *p, const char *function, int len);

/**
 *
 */
static inline char *
tp_format(struct tp *p, const char *format, ...);

/**
 * Write a tuple header
 * Same as tp_encode_array, added for compatibility.
 */
static inline char *
tp_tuple(struct tp *p, uint32_t field_count);

/**
 * Write a key header
 * Same as tp_encode_array, added for compatibility.
 */
static inline char *
tp_key(struct tp *p, uint32_t part_count);

/**
 * Add an uint value to the request
 */
static inline char *
tp_encode_uint(struct tp *p, uint64_t num);

/**
 * Add an int value to the request
 * the value must be less than zero
 */
static inline char *
tp_encode_int(struct tp *p, int64_t num);

/**
 * Add a string value to the request, with length provided.
 */
static inline char *
tp_encode_str(struct tp *p, const char *str, uint32_t len);

/**
 * Add a zero-end string value to the request.
 */
static inline char *
tp_encode_sz(struct tp *p, const char *str);

/**
 * Add a zero-end string value to the request.
 * (added for compatibility with tarantool 1.5 connector)
 */
static inline char *
tp_sz(struct tp *p, const char *str);

/**
 * Add a nil value to the request
 */
static inline char *
tp_encode_nil(struct tp *p);

/**
 * Add binary data to the request.
 */
static inline char *
tp_encode_bin(struct tp *p, const char *str, uint32_t len);

/**
 * Add an array to the request with a given size
 *
 * tp_encode_array(p, 3);
 * tp_encode_uint(p, 1);
 * tp_encode_uint(p, 2);
 * tp_encode_uint(p, 3);
 */
static inline char *
tp_encode_array(struct tp *p, uint32_t size);

/**
 * Add a map to the request with a given size
 *
 * tp_encode_array(p, 2);
 * tp_encode_sz(p, "name");
 * tp_encode_sz(p, "Alan");
 * tp_encode_sz(p, "birth");
 * tp_encode_uint(p, 1912);
 */
static inline char *
tp_encode_map(struct tp *p, uint32_t size);

/**
 * Add a bool value to the request.
 */
static inline char *
tp_encode_bool(struct tp *p, bool val);

/**
 * Add a float value to the request.
 */
static inline char *
tp_encode_float(struct tp *p, float num);

/**
 * Add a double float value to the request.
 */
static inline char *
tp_encode_double(struct tp *p, double num);

/**
 * Set the current request id.
 */
static inline void
tp_reqid(struct tp *p, uint32_t reqid);

/**
 * Ensure that buffer has enough space to fill size bytes, resize
 * buffer if needed. Returns -1 on error, and new allocated size
 * of success.
 */
static inline ssize_t
tp_ensure(struct tp *p, size_t size);

/**
 * Initialize struct tpresponse with a data buffer.
 * Returns -1 if an error occured
 * Returns 0 if buffer contains only part of the response
 * Return size in bytes of the response in buffer on success
 */
static inline ssize_t
tp_reply(struct tpresponse *r, const char * const buf, size_t size);

/**
 * Return the current request id
 */
static inline uint32_t
tp_getreqid(struct tpresponse *r);

/**
 * Check if the response has a tuple.
 * Automatically checked during tp_next() iteration.
 */
static inline int
tp_hasdata(struct tpresponse *r);

/**
 * Get tuple count in response
 */
static inline uint32_t
tp_tuplecount(const struct tpresponse *r);

/**
 * Skip to the next tuple or to the first tuple after rewind
 */
static inline int
tp_next(struct tpresponse *r);

/**
 * Check if there is one more tuple.
 */
static inline int
tp_hasnext(struct tpresponse *r);

/**
 * Rewind iteration to the first tuple.
 * Note that initialization of tpresponse via tp_reply
 * rewinds tuple iteration automatically
 */
static inline void
tp_rewind(struct tpresponse *r);

/**
 * Get the current tuple data, all fields.
 */
static inline const char *
tp_gettuple(struct tpresponse *r);

/**
 * Get the current tuple size in bytes.
 */
static inline uint32_t
tp_tuplesize(struct tpresponse *r);

/**
 *  Get a pointer to the end of the current tuple.
 */
static inline const char *
tp_tupleend(struct tpresponse *r);

/**
 * Skip to the next field.
 */
static inline int
tp_nextfield(struct tpresponse *r);

/**
 * Get the current field.
 */
static inline const char *
tp_getfield(struct tpresponse *r);

/*
 * Rewind iteration to the first tuple field of the current tuple.
 * Note that iterating tuples of the response
 * rewinds field iteration automatically
 */
static inline void
tp_rewindfield(struct tpresponse *r);

/*
 * Check if the current tuple has one more field.
 */
static inline int
tp_hasnextfield(struct tpresponse *r);


/**
 * Get the current field size in bytes.
 */
static inline uint32_t
tp_getfieldsize(struct tpresponse *r);

/*
 * Determine MsgPack type by first byte of encoded data.
 */
static inline enum tp_type
tp_typeof(const char c);

/**
 * Read unsigned integer value
 */
static inline uint64_t
tp_get_uint(const char *field);

/**
 * Read signed integer value
 */
static inline int64_t
tp_get_int(const char *field);

/**
 * Read float value
 */
static inline float
tp_get_float(const char *field);

/**
 * Read double value
 */
static inline double
tp_get_double(const char *field);

/**
 * Read bool value
 */
static inline bool
tp_get_bool(const char *field);

/**
 * Read string value
 */
static inline const char *
tp_get_str(const char *field, uint32_t *size);

/**
 * Read binary data value
 */
static inline const char *
tp_get_bin(const char *field, uint32_t *size);


/**
 * Init msgpack iterator by a pointer to msgpack array beginning.
 * First element will be accessible after tp_array_itr_next call.
 * Returns -1 on error
 */
static inline int
tp_array_itr_init(struct tp_array_itr *itr, const char *data, size_t size);

/**
 * Iterate to next position.
 * return true if success, or false if there are no elements left
 */
static inline bool
tp_array_itr_next(struct tp_array_itr *itr);

/**
 * Reset iterator to the beginning. First element will be
 * accessible after tp_array_itr_next call.
 * return true if success, or false if there are no elements left
 */
static inline void
tp_array_itr_reset(struct tp_array_itr *itr);

/**
 * Init msgpack map iterator by a pointer to msgpack map beginning.
 * First element will be accessible after tp_map_itr_next call.
 * Returns -1 on error
 */
static inline int
tp_map_itr_init(struct tp_map_itr *itr, const char *data, size_t size);

/**
 * Iterate to next position.
 * return true if success, or false if there are no pairs left
 */
static inline bool
tp_map_itr_next(struct tp_map_itr *itr);

/**
 * Reset iterator to the beginning. First pair will be
 * accessible after tp_map_itr_next call.
 * return true if success, or false if there are no pairs left
 */
static inline void
tp_map_itr_reset(struct tp_map_itr *itr);

/* }}} */


/* {{{ Implementation */

/**
 * Receive greetings from the server.
 * Note, the input buffer is not copied,
 * so freeing or reusing the input buffer will invalidate tpgreeting struct
 * For example salt is used for authorization (tp_auth).
 * Returns 0 if buffer is too small or greetings size (128) on success
 */
static inline ssize_t
tp_greeting(struct tpgreeting *g, char *buf, size_t size)
{
	if (size < 128)
		return 0;
	g->description = buf;
	g->salt_base64 = buf + 64;
	return 128;
}


/**
 * Get currently allocated buffer pointer
 */
static inline char *
tp_buf(struct tp *p)
{
	return p->s;
}

/**
 * Get currently allocated buffer size
 */
static inline size_t
tp_size(struct tp *p)
{
	return p->e - p->s;
}

/**
 * Get the size of data in the buffer
 */
static inline size_t
tp_used(struct tp *p)
{
	return p->p - p->s;
}

/**
 * Get the size available for write
 */
static inline size_t
tp_unused(struct tp *p)
{
	return p->e - p->p;
}

/**
 * A common reallocation function, can be used
 * for 'reserve' param in tp_init().
 * Resizes the buffer twice the previous size using realloc().
 *
 * struct tp req;
 * tp_init(&req, NULL, tp_realloc, NULL);
 * tp_ping(&req); // will call the reallocator
 *
 * data must be manually freed when the buffer is no longer
 * needed.
 * (e.g. free(p->s) or tp_free(p) );
 * if realloc will return NULL, then you must destroy previous memory.
 * (e.g.
 * if (tp_realloc(p, ..) == NULL) {
 * 	tp_free(p)
 * 	return NULL;
 * }
*/
tpfunction_unused static char *
tp_realloc(struct tp *p, size_t required, size_t *size)
{
	size_t toalloc = tp_size(p) * 2;
	if (tpunlikely(toalloc < required))
		toalloc = tp_size(p) + required;
	*size = toalloc;
	return (char *) realloc(p->s, toalloc);
}

/**
 * Free function for use in a pair with tp_realloc.
 * Don't use it when tp inited with static buffer!
 */
static inline void
tp_free(struct tp *p)
{
	free(p->s);
}

/**
 * Main initialization function.
 *
 * buf     - current buffer, may be NULL
 * size    - current buffer size
 * reserve - reallocation function, may be NULL
 * obj     - pointer to be passed to the reallocation function as
 *           context
 *
 * Either a buffer pointer or a reserve function must be
 * provided.
 * If reserve function is provided, data must be manually freed
 * when the buffer is no longer needed.
 *  (e.g. free(p->s) or tp_free(p) );
 */
static inline void
tp_init(struct tp *p, char *buf, size_t size,
        tp_reserve reserve, void *obj)
{
	p->s = buf;
	p->p = p->s;
	p->e = p->s + size;
	p->size = NULL;
	p->sync = NULL;
	p->reserve = reserve;
	p->obj = obj;
}

/**
 * Ensure that buffer has enough space to fill size bytes, resize
 * buffer if needed. Returns -1 on error, and new allocated size
 * on success.
 */
static inline ssize_t
tp_ensure(struct tp *p, size_t size)
{
	if (tplikely(tp_unused(p) >= size))
		return 0;
	if (tpunlikely(p->reserve == NULL))
		return -1;
	size_t sz;
	char *np = p->reserve(p, size, &sz);
	if (tpunlikely(np == NULL))
		return -1;
	if (tplikely(p->size))
		p->size = (np + (((char *)p->size) - p->s));
	if (tplikely(p->sync))
		p->sync = (np + (((char *)p->sync) - p->s));
	p->p = np + (p->p - p->s);
	p->s = np;
	p->e = np + sz;
	return sz;
}

/**
 * Accept data of specified size.
 * This is a function for internal use, and is not part of an API
 */
static inline char *
tp_add(struct tp *p, size_t size)
{
	void *ptr = p->p;
	p->p += size;
	assert(p->size);
	*p->size = 0xce;
	*(uint32_t*)(p->size + 1) = mp_bswap_u32(p->p - p->size - 5);
	return (char *) ptr;
}

/**
 * Internal
 * Function for getting size of header
 */
static inline uint32_t
tpi_sizeof_header(uint32_t code)
{
	return 5 + mp_sizeof_map(2) +
		mp_sizeof_uint(TP_CODE) +
		mp_sizeof_uint(code)	+
		mp_sizeof_uint(TP_SYNC) +
		5;
}

/**
 * Internal
 * Function for encoding header
 */
static inline char *
tpi_encode_header(struct tp* p, uint32_t code)
{
	p->size = p->p;
	char *h = mp_encode_map(p->p + 5, 2);
	h = mp_encode_uint(h, TP_CODE);
	h = mp_encode_uint(h, code);
	h = mp_encode_uint(h, TP_SYNC);
	p->sync = h;
	*h = 0xce;
	*(uint32_t*)(h + 1) = 0;
	h += 5;
	return h;
}

/**
 * Create a ping request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_ping(&req);
 */
static inline char *
tp_ping(struct tp *p)
{
	int hsz = tpi_sizeof_header(TP_PING);
	int  sz = mp_sizeof_map(0);
	if (tpunlikely(tp_ensure(p, sz + hsz) == -1))
		return NULL;
	char *h = tpi_encode_header(p, TP_PING);
	h = mp_encode_map(h, 0);
	return tp_add(p, sz + hsz);
}

/**
 * Append a select request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_select(&req, 0, 0, 0, 100);
 * tp_key(&req, 1);
 * tp_sz(&req, "key");
 */
static inline char *
tp_select(struct tp *p, uint32_t space, uint32_t index,
	  uint32_t offset, enum tp_iterator_type iterator, uint32_t limit)
{
	int hsz = tpi_sizeof_header(TP_SELECT);
	int  sz = mp_sizeof_map(6) +
		mp_sizeof_uint(TP_SPACE) +
		mp_sizeof_uint(space) +
		mp_sizeof_uint(TP_INDEX) +
		mp_sizeof_uint(index) +
		mp_sizeof_uint(TP_OFFSET) +
		mp_sizeof_uint(offset) +
		mp_sizeof_uint(TP_LIMIT) +
		mp_sizeof_uint(limit) +
		mp_sizeof_uint(TP_ITERATOR) +
		mp_sizeof_uint(iterator) +
		mp_sizeof_uint(TP_KEY);
	if (tpunlikely(tp_ensure(p, sz + hsz) == -1))
		return NULL;
	char *h = tpi_encode_header(p, TP_SELECT);
	h = mp_encode_map(h, 6);
	h = mp_encode_uint(h, TP_SPACE);
	h = mp_encode_uint(h, space);
	h = mp_encode_uint(h, TP_INDEX);
	h = mp_encode_uint(h, index);
	h = mp_encode_uint(h, TP_OFFSET);
	h = mp_encode_uint(h, offset);
	h = mp_encode_uint(h, TP_LIMIT);
	h = mp_encode_uint(h, limit);
	h = mp_encode_uint(h, TP_ITERATOR);
	h = mp_encode_uint(h, iterator);
	h = mp_encode_uint(h, TP_KEY);
	return tp_add(p, sz + hsz);
}

/**
 * Internal
 * Function for encoding insert or replace request
 */
static inline char *
tpi_encode_store(struct tp *p, enum tp_request_type type, uint32_t space)
{
	int hsz = tpi_sizeof_header(type);
	int sz = mp_sizeof_map(2) +
		mp_sizeof_uint(TP_SPACE) +
		mp_sizeof_uint(space) +
		mp_sizeof_uint(TP_TUPLE);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	char *h = tpi_encode_header(p, type);
	h = mp_encode_map(h, 2);
	h = mp_encode_uint(h, TP_SPACE);
	h = mp_encode_uint(h, space);
	h = mp_encode_uint(h, TP_TUPLE);
	return tp_add(p, sz + hsz);
}

/**
 * Create an insert request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_insert(&req, 0);
 * tp_tuple(&req, 2);
 * tp_sz(&req, "key");
 * tp_sz(&req, "value");
 */
static inline char *
tp_insert(struct tp *p, uint32_t space)
{
	return tpi_encode_store(p, TP_INSERT, space);
}

/**
 * Create a replace request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_insert(&req, 0);
 * tp_tuple(&req, 2);
 * tp_sz(&req, "key");
 * tp_sz(&req, "value");
 */
static inline char *
tp_replace(struct tp *p, uint32_t space)
{
	return tpi_encode_store(p, TP_REPLACE, space);
}

/**
 * Create a delete request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_delete(&req, 0);
 * tp_key(&req, 1);
 * tp_sz(&req, "key");
 */
static inline char *
tp_delete(struct tp *p, uint32_t space, uint32_t index)
{
	int hsz = tpi_sizeof_header(TP_DELETE);
	int  sz = mp_sizeof_map(3) +
		mp_sizeof_uint(TP_SPACE) +
		mp_sizeof_uint(space) +
		mp_sizeof_uint(TP_INDEX) +
		mp_sizeof_uint(index) +
		mp_sizeof_uint(TP_KEY);
	if (tpunlikely(tp_ensure(p, hsz + sz) == -1))
		return NULL;
	char *h = tpi_encode_header(p, TP_DELETE);
	h = mp_encode_map(h, 3);
	h = mp_encode_uint(h, TP_SPACE);
	h = mp_encode_uint(h, space);
	h = mp_encode_uint(h, TP_INDEX);
	h = mp_encode_uint(h, index);
	h = mp_encode_uint(h, TP_KEY);
	return tp_add(p, hsz + sz);
}

/**
 * Create a call request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 *
 * char proc[] = "hello_proc";
 * tp_call(&req, proc, sizeof(proc) - 1);
 * tp_encode_array(&req, 2);
 * tp_sz(&req, "arg1");
 * tp_sz(&req, "arg2");
 */
static inline char *
tp_call(struct tp *p, const char *function, int len)
{
	int hsz = tpi_sizeof_header(TP_CALL);
	int  sz = mp_sizeof_map(2) +
		mp_sizeof_uint(TP_FUNCTION) +
		mp_sizeof_str(len) +
		mp_sizeof_uint(TP_TUPLE);
	if (tpunlikely(tp_ensure(p, hsz + sz) == -1))
		return NULL;
	char *h = tpi_encode_header(p, TP_CALL);
	h = mp_encode_map(h, 2);
	h = mp_encode_uint(h, TP_FUNCTION);
	h = mp_encode_str(h, function, len);
	h = mp_encode_uint(h, TP_TUPLE);
	return tp_add(p, sz + hsz);
}

/**
 * Create an eval request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 *
 * char proc[] = "hello_proc";
 * tp_eval(&req, proc, sizeof(proc) - 1);
 * tp_encode_array(&req, 2);
 * tp_sz(&req, "arg1");
 * tp_sz(&req, "arg2");
 */
static inline char *
tp_eval(struct tp *p, const char *expr, int len)
{
	int hsz = tpi_sizeof_header(TP_EVAL);
	int  sz = mp_sizeof_map(2) +
		mp_sizeof_uint(TP_FUNCTION) +
		mp_sizeof_str(len) +
		mp_sizeof_uint(TP_TUPLE);
	if (tpunlikely(tp_ensure(p, hsz + sz) == -1))
		return NULL;
	char *h = tpi_encode_header(p, TP_EVAL);
	h = mp_encode_map(h, 2);
	h = mp_encode_uint(h, TP_EXPRESSION);
	h = mp_encode_str(h, expr, len);
	h = mp_encode_uint(h, TP_TUPLE);
	return tp_add(p, sz + hsz);
}

/**
 * Create an update request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_update(&req, 0); // update of space 0
 * tp_key(&req, 1); // key with one part
 * tp_sz(&req, "key"); // one and only part of the key
 * tp_updatebegin(&req, 2); // update with two operations
 * tp_op(&req, "+", 2); // add to field 2 ..
 * tp_encode_uint(&req, 1); // .. a value 1
 * tp_op(&req, "=", 3); // set a field 3 ..
 * tp_sz(&req, "value"); // .. a value "value"
 */
static inline char *
tp_update(struct tp *p, uint32_t space, uint32_t index)
{
	int hsz = tpi_sizeof_header(TP_UPDATE);
	int  sz = mp_sizeof_map(4) +
		mp_sizeof_uint(TP_SPACE) +
		mp_sizeof_uint(space) +
    mp_sizeof_uint(TP_INDEX) +
    mp_sizeof_uint(index) +
		mp_sizeof_uint(TP_KEY);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	char *h = tpi_encode_header(p, TP_UPDATE);
	h = mp_encode_map(h, 4);
	h = mp_encode_uint(h, TP_SPACE);
	h = mp_encode_uint(h, space);
	h = mp_encode_uint(h, TP_INDEX);
	h = mp_encode_uint(h, index);
	h = mp_encode_uint(h, TP_KEY);
	return tp_add(p, sz + hsz);
}


/**
 * Create an upsert request.
 */
static inline char *
tp_upsert(struct tp *p, uint32_t space)
{
	int hsz = tpi_sizeof_header(TP_UPSERT);
	int  sz = mp_sizeof_map(3) +
		mp_sizeof_uint(TP_SPACE) +
		mp_sizeof_uint(space) +
		mp_sizeof_uint(TP_TUPLE);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	char *h = tpi_encode_header(p, TP_UPSERT);
	h = mp_encode_map(h, 3);
	h = mp_encode_uint(h, TP_SPACE);
	h = mp_encode_uint(h, space);
	h = mp_encode_uint(h, TP_TUPLE);
	return tp_add(p, sz + hsz);

}

/**
 * Upsert. Begin add operations.
 */
static inline char *
tp_upsertbegin_add_ops(struct tp *p, uint32_t op_count)
{
	int sz = mp_sizeof_uint(TP_OPS) + mp_sizeof_array(op_count);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	char *h = mp_encode_uint(p->p, TP_OPS);
	mp_encode_array(h, op_count);
	return tp_add(p, sz);
}

/**
 * Begin update operations.
 * See tp_update description for details.
 */
static inline char *
tp_updatebegin(struct tp *p, uint32_t op_count)
{
	int sz = mp_sizeof_uint(TP_TUPLE) + mp_sizeof_array(op_count);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	char *h = mp_encode_uint(p->p, TP_TUPLE);
	mp_encode_array(h, op_count);
	return tp_add(p, sz);
}

/**
 * Add an update operation.
 * See tp_update description.
 * Operation op could be:
 * "=" - assign operation argument to field <field_no>;
 *  will extend the tuple if <field_no> == <max_field_no> + 1
 * "#" - delete <argument> fields starting from <field_no>
 * "!" - insert <argument> before <field_no>
 * The following operation(s) are only defined for integer
 * types:
 * "+" - add argument to field <field_no>, argument
 * are integer
 * "-" - subtract argument from the field <field_no>
 * "&" - bitwise AND of argument and field <field_no>
 * "^" - bitwise XOR of argument and field <field_no>
 * "|" - bitwise OR of argument and field <field_no>
 */
static inline char *
tp_op(struct tp *p, char op, uint32_t field_no)
{
	int sz = mp_sizeof_array(3) +
		mp_sizeof_str(1) +
		mp_sizeof_uint(field_no);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	char *h = mp_encode_array(p->p, 3);
	h = mp_encode_str(h, &op, 1);
	h = mp_encode_uint(h, field_no);
	return tp_add(p, sz);
}

/**
 * Add an slice operation
 * See tp_update description.
 */
static inline char *
tp_op_splice(struct tp *p, uint32_t field_no,
	     uint32_t offset, uint32_t cut_limit,
	     const char *paste, uint32_t paste_len)
{
	int sz = mp_sizeof_array(5) +
		mp_sizeof_str(1) +
		mp_sizeof_uint(field_no) +
		mp_sizeof_uint(field_no) +
		mp_sizeof_uint(field_no) +
		mp_sizeof_str(paste_len);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	char *h = mp_encode_array(p->p, 5);
	h = mp_encode_str(h, ":", 1);
	h = mp_encode_uint(h, field_no);
	h = mp_encode_uint(h, offset);
	h = mp_encode_uint(h, cut_limit);
	h = mp_encode_str(h, paste, paste_len);
	return tp_add(p, sz);
}


/**
 * Internal
 * tpi_xor
 * The function is for internal use, not part of the API
 */
static inline void
tpi_xor(unsigned char *to, const unsigned char *left,
       const unsigned char *right, uint32_t len)
{
	const uint8_t *end = to + len;
	while (to < end)
		*to++= *left++ ^ *right++;
}

/**
 * Internal
 * tpi_scramble_prepare
 * The function is for internal use, not part of the API
 */
static inline void
tpi_scramble_prepare(void *out, const void *salt, const void *password,
		 int password_len)
{
#if !(defined TP_H_AUTH_OFF)
	unsigned char hash1[SCRAMBLE_SIZE];
	unsigned char hash2[SCRAMBLE_SIZE];
	SHA1_CTX ctx;

	SHA1Init(&ctx);
	SHA1Update(&ctx, (const unsigned char *) password, password_len);
	SHA1Final(hash1, &ctx);

	SHA1Init(&ctx);
	SHA1Update(&ctx, hash1, SCRAMBLE_SIZE);
	SHA1Final(hash2, &ctx);

	SHA1Init(&ctx);
	SHA1Update(&ctx, (const unsigned char *) salt, SCRAMBLE_SIZE);
	SHA1Update(&ctx, hash2, SCRAMBLE_SIZE);
	SHA1Final((unsigned char *) out, &ctx);

	tpi_xor((unsigned char *) out, hash1, (const unsigned char *) out,
	       SCRAMBLE_SIZE);
#endif
}

/**
 * Create an auth request.
 *
 * salt_base64 must be gathered from tpgreeting struct,
 * that is initialized during tp_greeting call.
 *
 * tp_auth(p, greet.salt_base64, "admin", 5, "pass", 4);
 */
static inline char *
tp_auth(struct tp *p, const char *salt_base64, const char *user,
	int ulen, const char *pass, int plen)
{
#if !(defined TP_H_AUTH_OFF)
	int hsz = tpi_sizeof_header(TP_AUTH);
	int  sz = mp_sizeof_array(2) +
		      mp_sizeof_str(0) +
		      mp_sizeof_str(SCRAMBLE_SIZE);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	char *h = tpi_encode_header(p, TP_AUTH);
	h = mp_encode_map(h, 2);
	h = mp_encode_uint(h, TP_USERNAME);
	h = mp_encode_str(h, user, ulen);
	h = mp_encode_uint(h, TP_TUPLE);
	h = mp_encode_array(h, 2);
	h = mp_encode_str(h, "chap-sha1", 9);

	char salt[64];
	base64_decode(salt_base64, 44, salt, 64);
	char scramble[SCRAMBLE_SIZE];
	tpi_scramble_prepare(scramble, salt, pass, plen);
	h = mp_encode_str(h, scramble, SCRAMBLE_SIZE);

	return tp_add(p, sz + hsz);
#else
  assert("not implemented");
  return NULL;
#endif // TP_H_AUTH_OFF
}

/**
 * Create an deauth (auth as a guest) request.
 *
 * tp_deauth(p);
 */
static inline char *
tp_deauth(struct tp *p)
{
	int hsz = tpi_sizeof_header(TP_AUTH);
	int  sz = mp_sizeof_array(0);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	p->size = p->p;
	char *h = tpi_encode_header(p, TP_AUTH);
	h = mp_encode_map(h, 2);
	h = mp_encode_uint(h, TP_USERNAME);
	h = mp_encode_str(h, "guest", 5);
	h = mp_encode_uint(h, TP_TUPLE);
	h = mp_encode_array(h, 0);

	return tp_add(p, sz + hsz);
}

/**
 * Set the current request id.
 */
static inline void
tp_reqid(struct tp *p, uint32_t reqid)
{
	assert(p->sync != NULL);
	char *h = p->sync;
	*h = 0xce;
	*(uint32_t*)(h + 1) = mp_bswap_u32(reqid);
}

/**
 * Add a nil value to the request
 */
static inline char *
tp_encode_nil(struct tp *p)
{
	int sz = mp_sizeof_nil();
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	mp_encode_nil(p->p);
	return tp_add(p, sz);
}

/**
 * Add an uint value to the request
 */
static inline char *
tp_encode_uint(struct tp *p, uint64_t num)
{
	int sz = mp_sizeof_uint(num);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	mp_encode_uint(p->p, num);
	return tp_add(p, sz);
}

/**
 * Add an int value to the request
 * the value must be less than zero
 */
static inline char *
tp_encode_int(struct tp *p, int64_t num)
{
	int sz = mp_sizeof_int(num);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	mp_encode_int(p->p, num);
	return tp_add(p, sz);
}

/**
 * Add a string value to the request, with length provided.
 */
static inline char *
tp_encode_str(struct tp *p, const char *str, uint32_t len)
{
	int sz = mp_sizeof_str(len);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	mp_encode_str(p->p, str, len);
	return tp_add(p, sz);
}

/**
 * Add a zero-end string value to the request.
 */
static inline char *
tp_encode_sz(struct tp *p, const char *str)
{
	uint32_t len = (uint32_t)strlen(str);
	int sz = mp_sizeof_str(len);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	mp_encode_str(p->p, str, len);
	return tp_add(p, sz);
}

/**
 * Add a zero-end string value to the request.
 * (added for compatibility with tarantool 1.5 connector)
 */
static inline char *
tp_sz(struct tp *p, const char *str)
{
	return tp_encode_sz(p, str);
}

/**
 * Add binary data to the request.
 */
static inline char *
tp_encode_bin(struct tp *p, const char *str, uint32_t len)
{
	int sz = mp_sizeof_bin(len);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	mp_encode_bin(p->p, str, len);
	return tp_add(p, sz);
}

/**
 * Add an array to the request with a given size
 *
 * tp_encode_array(p, 3);
 * tp_encode_uint(p, 1);
 * tp_encode_uint(p, 2);
 * tp_encode_uint(p, 3);
 */
static inline char *
tp_encode_array(struct tp *p, uint32_t size)
{
	int sz = mp_sizeof_array(size);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	mp_encode_array(p->p, size);
	return tp_add(p, sz);
}

/**
 * Add a map to the request with a given size
 *
 * tp_encode_array(p, 2);
 * tp_encode_sz(p, "name");
 * tp_encode_sz(p, "Alan");
 * tp_encode_sz(p, "birth");
 * tp_encode_uint(p, 1912);
 */
static inline char *
tp_encode_map(struct tp *p, uint32_t size)
{
	int sz = mp_sizeof_map(size);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	mp_encode_map(p->p, size);
	return tp_add(p, sz);
}

/**
 * Add a bool value to the request.
 */
static inline char *
tp_encode_bool(struct tp *p, bool val)
{
	int sz = mp_sizeof_bool(val);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	mp_encode_bool(p->p, val);
	return tp_add(p, sz);
}

/**
 * Add a float value to the request.
 */
static inline char *
tp_encode_float(struct tp *p, float num)
{
	int sz = mp_sizeof_float(num);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	mp_encode_float(p->p, num);
	return tp_add(p, sz);
}

/**
 * Add a double float value to the request.
 */
static inline char *
tp_encode_double(struct tp *p, double num)
{
	int sz = mp_sizeof_double(num);
	if (tpunlikely(tp_ensure(p, sz) == -1))
		return NULL;
	mp_encode_double(p->p, num);
	return tp_add(p, sz);
}

/**
 *
 */
static inline char *
tp_format(struct tp *p, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	size_t unused = tp_unused(p);
	size_t sz = mp_vformat(p->p, unused, format, args);
	if (sz > unused) {
		if (tpunlikely(tp_ensure(p, sz) == -1)) {
			va_end(args);
			return NULL;
		}
		mp_vformat(p->p, unused, format, args);
	}
	va_end(args);
	return tp_add(p, sz);
}


/**
 * Write a tuple header
 * Same as tp_encode_array, added for compatibility.
 */
static inline char *
tp_tuple(struct tp *p, uint32_t field_count)
{
	return tp_encode_array(p, field_count);
}

/**
 * Write a key header
 * Same as tp_encode_array, added for compatibility.
 */
static inline char *
tp_key(struct tp *p, uint32_t part_count)
{
	return tp_encode_array(p, part_count);
}

/**
 * Init msgpack iterator by a pointer to msgpack array begin.
 * First element will be accessible after tp_array_itr_next call.
 * Returns -1 on error
 */
static inline int
tp_array_itr_init(struct tp_array_itr *itr, const char *data, size_t size)
{
	memset(itr, 0, sizeof(*itr));
	if (size == 0 || mp_typeof(*data) != MP_ARRAY)
		return -1;
	const char *e = data;
	if (mp_check(&e, data + size))
		return -1;
	itr->data = data;
	itr->first_elem = data;
	itr->elem_count = mp_decode_array(&itr->first_elem);
	itr->cur_index = -1;
	return 0;
}

/**
 * Iterate to next position.
 * return true if success, or false if there are no elements left
 */
static inline bool
tp_array_itr_next(struct tp_array_itr *itr)
{
	itr->cur_index++;
	if ((uint32_t)itr->cur_index >= itr->elem_count)
		return false;
	if (itr->cur_index == 0)
		itr->elem = itr->first_elem;
	else
		itr->elem = itr->elem_end;
	itr->elem_end = itr->elem;
	mp_next(&itr->elem_end);
	return true;
}

/**
 * Reset iterator to the beginning. First element will be
 * accessible after tp_array_itr_next call.
 * return true if success, or false if there are no elements left
 */
static inline void
tp_array_itr_reset(struct tp_array_itr *itr)
{
	itr->cur_index = -1;
	itr->elem = 0;
	itr->elem_end = 0;
}

/**
 * Init msgpack map iterator by a pointer to msgpack map begin.
 * First element will be accessible after tp_map_itr_next call.
 * Returns -1 on error
 */
static inline int
tp_map_itr_init(struct tp_map_itr *itr, const char *data, size_t size)
{
	memset(itr, 0, sizeof(*itr));
	if (size == 0 || mp_typeof(*data) != MP_MAP)
		return -1;
	const char *e = data;
	if (mp_check(&e, data + size))
		return -1;
	itr->data = data;
	itr->first_key = data;
	itr->pair_count = mp_decode_map(&itr->first_key);
	itr->cur_index = -1;
	return 0;
}

/**
 * Iterate to next position.
 * return true if success, or false if there are no pairs left
 */
static inline bool
tp_map_itr_next(struct tp_map_itr *itr)
{
	itr->cur_index++;
	if ((uint32_t)itr->cur_index >= itr->pair_count)
		return false;
	if (itr->cur_index == 0)
		itr->key = itr->first_key;
	else
		itr->key = itr->value_end;
	itr->key_end = itr->key;
	mp_next(&itr->key_end);
	itr->value = itr->key_end;
	itr->value_end = itr->value;
	mp_next(&itr->value_end);
	return true;
}

/**
 * Reset iterator to the beginning. First pair will be
 * accessible after tp_map_itr_next call.
 * return true if success, or false if there are no pairs left
 */
static inline void
tp_map_itr_reset(struct tp_map_itr *itr)
{
	itr->cur_index = -1;
	itr->key = 0;
	itr->key_end = 0;
	itr->value = 0;
	itr->value_end = 0;
}

/**
 * Initialize struct tpresponse with a data buffer.
 * Returns -1 if an error occured
 * Returns 0 if buffer contains only part of the response
 * Return size in bytes of the response in buffer on success
 */
static inline ssize_t
tp_reply(struct tpresponse *r, const char * const buf, size_t size)
{
	memset(r, 0, sizeof(*r));
	if (size == 0)
		return 0;
	const char *p = buf;
	/* len */
	const char *test = p;
	if (mp_check(&test, buf + size))
		return -1;
	if (mp_typeof(*p) != MP_UINT)
		return -1;
	uint32_t len = mp_decode_uint(&p);
	if (size < len + (uint32_t)(p - buf))
		return 0;
	/* header */
	test = p;
	if (mp_check(&test, buf + size))
		return -1;
	if (mp_typeof(*p) != MP_MAP)
		return -1;
	uint32_t n = mp_decode_map(&p);
	while (n-- > 0) {
		if (mp_typeof(*p) != MP_UINT)
			return -1;
		uint32_t key = mp_decode_uint(&p);
		if (mp_typeof(*p) != MP_UINT)
			return -1;
		switch (key) {
		case TP_SYNC:
			if (mp_typeof(*p) != MP_UINT)
				return -1;
			r->sync = mp_decode_uint(&p);
			break;
		case TP_CODE:
			if (mp_typeof(*p) != MP_UINT)
				return -1;
			r->code = mp_decode_uint(&p);
			break;
		case TP_SCHEMA_ID:
			if (mp_typeof(*p) != MP_UINT)
				return -1;
			r->schema_id = mp_decode_uint(&p);
			break;
		default:
			mp_next(&p);
			break;
		}
		r->bitmap |= (1ULL << key);
	}

	/* body */
	if (p == buf + len + 5)
		return len + 5; /* no body */
	test = p;
	if (mp_check(&test, buf + size))
		return -1;
	if (mp_typeof(*p) != MP_MAP)
		return -1;
	n = mp_decode_map(&p);
	while (n-- > 0) {
		uint32_t key = mp_decode_uint(&p);
		switch (key) {
		case TP_ERROR: {
			if (mp_typeof(*p) != MP_STR)
				return -1;
			uint32_t elen = 0;
			r->error = mp_decode_str(&p, &elen);
			r->error_end = r->error + elen;
			break;
		}
		case TP_DATA: {
			if (mp_typeof(*p) != MP_ARRAY)
				return -1;
			r->data = p;
			mp_next(&p);
			r->data_end = p;
			break;
		}
		}
		r->bitmap |= (1ULL << key);
	}
	if (r->data) {
		if (tp_array_itr_init(&r->tuple_itr, r->data, r->data_end - r->data))
			return -1;
	}
	return p - buf;
}

/**
 * Return the current response id
 */
static inline uint32_t
tp_getreqid(struct tpresponse *r)
{
	return r->sync;
}

/**
 * Check if the response has a tuple.
 * Automatically checked during tp_next() iteration.
 */
static inline int
tp_hasdata(struct tpresponse *r)
{
	return r->tuple_itr.elem_count > 0;
}

/**
 * Get tuple count in response
 */
static inline uint32_t
tp_tuplecount(const struct tpresponse *r)
{
	return r->tuple_itr.elem_count;
}

/**
 * Rewind iteration to the first tuple.
 * Note that initialization of tpresponse via tp_reply
 * rewinds tuple iteration automatically
 */
static inline void
tp_rewind(struct tpresponse *r)
{
	tp_array_itr_reset(&r->tuple_itr);
	memset(&r->field_itr, 0, sizeof(r->field_itr));
}

/**
 * Skip to the next tuple or to the first tuple after rewind
 */
static inline int
tp_next(struct tpresponse *r)
{
	if (!tp_array_itr_next(&r->tuple_itr)) {
		memset(&r->field_itr, 0, sizeof(r->field_itr));
		return 0;
	}
	tp_array_itr_init(&r->field_itr, r->tuple_itr.elem, r->tuple_itr.elem_end - r->tuple_itr.elem);
	return 1;

}

/**
 * Check if there is one more tuple.
 */
static inline int
tp_hasnext(struct tpresponse *r)
{
	return (uint32_t)(r->tuple_itr.cur_index + 1) < r->tuple_itr.elem_count;
}

/**
 * Get the current tuple data, all fields.
 */
static inline const char *
tp_gettuple(struct tpresponse *r)
{
	return r->tuple_itr.elem;
}

/**
 * Get the current tuple size in bytes.
 */
static inline uint32_t
tp_tuplesize(struct tpresponse *r)
{
	return (uint32_t)(r->tuple_itr.elem_end - r->tuple_itr.elem);
}

/**
 *  Get a pointer to the end of the current tuple.
 */
static inline const char *
tp_tupleend(struct tpresponse *r)
{
	return r->tuple_itr.elem_end;
}

/*
 * Rewind iteration to the first tuple field of the current tuple.
 * Note that iterating tuples of the response
 * rewinds field iteration automatically
 */
static inline void
tp_rewindfield(struct tpresponse *r)
{
	tp_array_itr_reset(&r->field_itr);
}

/**
 * Skip to the next field.
 */
static inline int
tp_nextfield(struct tpresponse *r)
{
	return tp_array_itr_next(&r->field_itr);
}

/*
 * Check if the current tuple has one more field.
 */
static inline int
tp_hasnextfield(struct tpresponse *r)
{
	return (uint32_t)(r->field_itr.cur_index + 1) < r->field_itr.elem_count;
}


/**
 * Get the current field.
 */
static inline const char *
tp_getfield(struct tpresponse *r)
{
	return r->field_itr.elem;
}

/**
 * Get the current field size in bytes.
 */
static inline uint32_t
tp_getfieldsize(struct tpresponse *r)
{
	return (uint32_t)(r->field_itr.elem_end - r->field_itr.elem);
}

/*
 * Determine MsgPack type by first byte of encoded data.
 */
static inline enum tp_type
tp_typeof(const char c)
{
	return (enum tp_type) mp_typeof(c);
}

/**
 * Read unsigned integer value
 */
static inline uint64_t
tp_get_uint(const char *field)
{
	return mp_decode_uint(&field);
}

/**
 * Read signed integer value
 */
static inline int64_t
tp_get_int(const char *field)
{
	return mp_decode_int(&field);
}

/**
 * Read float value
 */
static inline float
tp_get_float(const char *field)
{
	return mp_decode_float(&field);
}

/**
 * Read double value
 */
static inline double
tp_get_double(const char *field)
{
	return mp_decode_double(&field);
}

/**
 * Read bool value
 */
static inline bool
tp_get_bool(const char *field)
{
	return mp_decode_bool(&field);
}

/**
 * Read string value
 */
static inline const char *
tp_get_str(const char *field, uint32_t *size)
{
	return mp_decode_str(&field, size);
}

/**
 * Read binary data value
 */
static inline const char *
tp_get_bin(const char *field, uint32_t *size)
{
	return mp_decode_bin(&field, size);
}

/* }}} */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
