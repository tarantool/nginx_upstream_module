
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

#include "json_encoders.h"


static inline void
hex_buf_init(char *hex_buf)
{
    hex_buf[0] = '\\';
    hex_buf[1] = 'u';
    hex_buf[2] = '0';
    hex_buf[3] = '0';
    hex_buf[6] = 0;
}


static inline void
char_to_hex(char *hex_buf, unsigned char c)
{
    static const char * hexchar = "0123456789ABCDEF";
    hex_buf[0] = hexchar[c >> 4];
    hex_buf[1] = hexchar[c & 0x0F];
}


const char * /*error message*/
json_encode_string(
    char **buf, size_t buf_len,
    const char *str, size_t str_len,
    bool escape_solidus)
{
    if (!buf || !buf_len)
        return "json_encode_string: invalid arguments";

    char hex_buf[7];

    hex_buf_init(hex_buf);

    if (!append_ch(buf, &buf_len, '\"'))
        goto oom;

    if (str) {
        for (;str_len != 0; --str_len, ++str) {

            const char ch = *str;
            const char *escaped = NULL;

            switch (ch) {
            case '\r': escaped = "\\r"; break;
            case '\n': escaped = "\\n"; break;
            case '\\': escaped = "\\\\"; break;
            /* it is not required to escape a solidus in JSON:
             * read sec. 2.5: http://www.ietf.org/rfc/rfc4627.txt
             * specifically, this production from the grammar:
             *   unescaped = %x20-21 / %x23-5B / %x5D-10FFFF
             */
            case '/': if (escape_solidus) escaped = "\\/"; break;
            case '"': escaped = "\\\""; break;
            case '\f': escaped = "\\f"; break;
            case '\b': escaped = "\\b"; break;
            case '\t': escaped = "\\t"; break;
            default:
                if ((unsigned char) ch < 32) {
                    char_to_hex(hex_buf + 4, ch);
                    escaped = hex_buf;
                }
                break;
            } /* switch */

            if (escaped) {
                if (!append_str(buf, &buf_len, escaped, strlen(escaped)))
                    goto oom;
            } else {
                if (!append_ch(buf, &buf_len, ch))
                    goto oom;
            }
        } /* for */
    } /* if */

    if (!append_ch(buf, &buf_len, '\"'))
        goto oom;

    return NULL;
oom:
    return "json_encode_string: out of memory";
}
