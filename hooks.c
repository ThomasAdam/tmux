/* $Id$ */

/*
 * Copyright (c) 2012 Thomas Adam <thomas@xteddy.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <ctype.h>
#include <stdlib.h>

#include <string.h>

#include "tmux.h"

RB_GENERATE(hooks, hook, entry, hooks_cmp);

int
hooks_cmp(struct hook *hook1, struct hook *hook2)
{
	return (strcmp(hook1->name, hook2->name));
}

void
hooks_init(struct hooks *hooks, struct hooks *copy)
{
	RB_INIT(hooks);

	if (copy != NULL)
		hooks_copy(copy, hooks);
}

void
hooks_copy(struct hooks *src, struct hooks *dst)
{
	struct hook	*h;

	RB_FOREACH(h, hooks, src)
		hooks_add(dst, h->name, h->cmdlist);
}

void
hooks_free(struct hooks *hooks)
{
	struct hook	*h, *h2;

	RB_FOREACH_SAFE(h, hooks, hooks, h2)
		hooks_remove(hooks, h);
}

void
hooks_add(struct hooks *hooks, const char *name, struct cmd_list *cmdlist)
{
	struct hook	*h;

	if ((h = hooks_find(hooks, (char *)name)) != NULL)
		hooks_remove(hooks, h);

	h = xcalloc(1, sizeof *h);
	h->name = xstrdup(name);
	h->cmdlist = cmdlist;
	h->cmdlist->references++;

	RB_INSERT(hooks, hooks, h);
}

void
hooks_remove(struct hooks *hooks, struct hook *h)
{
	if (h == NULL)
		return;

	RB_REMOVE(hooks, hooks, h);
	cmd_list_free(h->cmdlist);
	free(h->name);
	free(h);
}

struct hook *
hooks_find(struct hooks *hooks, const char *name)
{
	struct hook	 h;

	if (name == NULL)
		return (NULL);

	h.name = (char *)name;

	return (RB_FIND(hooks, hooks, &h));
}
