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

#include "tp_allowed_methods.h"
#include "tp_ext.h"

#include <assert.h>
#include <string.h>


static inline void
free_link(struct link__ *link)
{
    if (!link)
        return;
    if (link->name)
        free(link->name);
}


static inline struct link__ *
new_link(const unsigned char *name, size_t len)
{
    if (!name)
        return NULL;
    struct link__ *link = (struct link__ *)malloc(sizeof(struct link__));
    if (!link)
        goto error;
    *link = (struct link__) { .next = NULL, .len = len };
    link->name = (unsigned char *)malloc(len  * sizeof(unsigned char));
    if (!link->name)
        goto error;
    memcpy(link->name, name, len);
    return link;
error:
    free_link(link);
    return NULL;
}


void
tp_allowed_methods_init(tp_allowed_methods_t *tam)
{
    assert(tam);
    tam->tail = tam->head = NULL;
}


bool
tp_allowed_methods_add(tp_allowed_methods_t *tam,
                       const unsigned char *name,
                       size_t len)
{
    assert(tam);
    if (tam->tail) {
        tam->tail->next = new_link(name, len);
        tam->tail = tam->tail->next;
    } else if (!tam->head) {
        tam->head = new_link(name, len);
        tam->tail = tam->head;
    } else
        assert(false);
    if (!tam->tail)
        return false;
    return true;
}


void
tp_allowed_methods_free(tp_allowed_methods_t *tam)
{
    assert(tam);
    struct link__ *it = tam->head;
    for (; it; it = it->next)
        free_link(it);
    tam->head = tam->tail = NULL;
}


bool
tp_allowed_methods_is_allow(const tp_allowed_methods_t *tam,
                            const unsigned char *name,
                            size_t len)
{
    assert(tam);
    if (!tam->head /* - OFF */)
        return true;
    struct link__ *it = tam->head;
    for (; it; it = it->next) {
      if (it->name && it->len == len &&
              strcmp((const char *)it->name, (const char *)name) == 0)
          return true;
    }
    return false;
}
