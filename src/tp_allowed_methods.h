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
 * Copyright (C) 2015 Tarantool AUTHORS:
 * please see AUTHORS file.
 */

#ifndef TP_ALLOWED_METHODS_H_INCLUDED
#define TP_ALLOWED_METHODS_H_INCLUDED 1

#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* {{{ API declaration */
struct link__ {
  unsigned char* name;
  size_t len;
  struct link__ *next;
};

typedef struct tp_allowed_methods {
  struct link__ *head, *tail;
} tp_allowed_methods_t;

void tp_allowed_methods_init(tp_allowed_methods_t *tam);
void tp_allowed_methods_free(tp_allowed_methods_t *tam);

bool tp_allowed_methods_add(tp_allowed_methods_t *tam, const unsigned char *name, size_t len);
bool tp_allowed_methods_is_allow(const tp_allowed_methods_t *tam, const unsigned char *name, size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* }}} */

#endif /* TP_ALLOWED_METHODS_H_INCLUDED */
