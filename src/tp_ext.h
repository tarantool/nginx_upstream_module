
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
 * Copyright (C) 2015-2019 Tarantool AUTHORS:
 * please see AUTHORS file.
 */

#ifndef TP_EXT_H_INCLUDED
#define TP_EXT_H_INCLUDED 1

#if !defined(TP_H_AUTH_OFF)
#   define TP_H_AUTH_OFF 1
#endif // TP_H_AUTH_OFF

#include <tp.h> /* third_party/tp.h */

#define unlikely mp_unlikely
#define likely mp_likely

#ifdef __cplusplus
extern "C" {
#endif

/* {{{ API declaration */

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

static inline char *
tp_call_nargs(struct tp *p, const char *method, size_t method_len,
    size_t nargs)
{
    if (!tp_call(p, (const char *)method, (int)method_len))
      return NULL;
    return tp_encode_array(p, nargs);
}

static inline char *
tp_encode_str_map_item(struct tp *p, const char *key, size_t key_len,
    const char * value, size_t value_len)
{
  if (!tp_encode_str(p, key, key_len))
      return NULL;
  return tp_encode_str(p, value, value_len);
}

#ifdef __cplusplus
} /* extern "C" */
#endif

/* }}} */

#endif /* TP_EXT_H_INCLUDED */
