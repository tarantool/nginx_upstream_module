
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
 * Copyright (C) 2016-2019 Tarantool AUTHORS:
 * please see AUTHORS file.
 */

#ifndef JSON_ENCODER_H_INCLUDED
#define JSON_ENCODER_H_INCLUDED 1

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
#endif

/* {{{ API declaration */

/** Utils [
 */
static inline bool
append_str(char **buf, size_t *buf_len, const char *str, size_t str_size)
{
    if (*buf_len < str_size)
        return false;
    memcpy(*buf, str, str_size);
    *buf_len -= str_size;
    *buf += str_size;
    return true;
}

static inline bool
append_ch(char **buf, size_t *buf_len, char ch)
{
    return append_str(buf, buf_len, (const char *)&ch, 1);
}
/* ]
 */

/**
 * Encode input string to JSON string.
 * Plus, this function does "JSON escape" the input string
 *
 * Returns NULL - if ok, error message - if error occurred
 */
const char * /*error message*/
json_encode_string(
    char **buf, size_t buf_len,
    const char *str, size_t str_len,
    bool escape_solidus);

/* escape_solidus = false
 */
#define json_encode_string_ns(a,b,c,d) \
  json_encode_string((a), (b), (c), (d), false)

/* escape_solidus = true
 */
#define json_encode_string_s(a,b,c,d) \
  json_encode_string((a), (b), (c), (d), true)

#ifdef __cplusplus
} /* extern "C" */
#endif

/* }}} */

#endif /* JSON_ENCODER_H_INCLUDED */
