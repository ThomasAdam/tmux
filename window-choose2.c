/* $OpenBSD$ */

/*
 * Copyright (c) 2016 Nicholas Marriott <nicholas.marriott@gmail.com>
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
#include <vis.h>

#include "tmux.h"

//
// XXX TODO: shortcut keys in all new modes
//           fix last detection with sorting
//           reference counting for s,wl,wp
//           change sort order messes up expands
//           key to turn off preview?
//           problem when width == 1

static struct screen	*window_choose2_init(struct window_pane *,
			    struct args *);
static void		 window_choose2_free(struct window_pane *);
static void		 window_choose2_resize(struct window_pane *, u_int,
			    u_int);
static void		 window_choose2_key(struct window_pane *,
			    struct client *, struct session *, key_code,
			    struct mouse_event *);

#define WINDOW_CHOOSE2_DEFAULT_COMMAND "detach-client -t '%%'"

const struct window_mode window_choose2_mode = {
	.init = window_choose2_init,
	.free = window_choose2_free,
	.resize = window_choose2_resize,
	.key = window_choose2_key,
};

enum window_choose2_type {
	WINDOW_CHOOSE2_SESSION,
	WINDOW_CHOOSE2_WINDOW,
	WINDOW_CHOOSE2_PANE
};

enum window_choose2_order {
	WINDOW_CHOOSE2_BY_NAME_INDEX_NUMBER,
	WINDOW_CHOOSE2_BY_NAME_NAME_NUMBER,
	WINDOW_CHOOSE2_BY_TIME_INDEX_NUMBER,
	WINDOW_CHOOSE2_BY_TIME_NAME_NUMBER,
};

struct window_choose2_data;
struct window_choose2_item {
	struct window_choose2_data	*data;
	enum window_choose2_order	 order;

	struct window_choose2_item	*parent;
	enum window_choose2_type	 type;

	u_int				 number;
	struct session			*s;
	struct winlink			*wl;
	struct window_pane		*wp;
	//XXX references

	int				 tagged;
	int				 expanded;
	int                              last;

	RB_ENTRY (window_choose2_item)	 entry;
};

RB_HEAD(window_choose2_tree, window_choose2_item);
static int window_choose2_cmp(const struct window_choose2_item *,
    const struct window_choose2_item *);
RB_GENERATE_STATIC(window_choose2_tree, window_choose2_item, entry,
    window_choose2_cmp);

struct window_choose2_data {
	char				*command;
	struct screen			 screen;
	u_int				 offset;
	struct window_choose2_item	*current;

	u_int				 width;
	u_int				 height;

	struct window_choose2_tree	 tree;
	u_int				 number;

	enum window_choose2_order	 order;
};

static void	window_choose2_up(struct window_choose2_data *);
static void	window_choose2_down(struct window_choose2_data *);
static void	window_choose2_run_command(struct client *, const char *,
		    const char *);
static void	window_choose2_free_tree(struct window_choose2_tree *);
static void	window_choose2_build_tree(struct window_choose2_data *);
static void	window_choose2_draw_screen(struct window_pane *);

static int
window_choose2_cmp(const struct window_choose2_item *a,
    const struct window_choose2_item *b)
{
	u_int	aidx, bidx;
	int	retval;

	if (a->s != b->s) {
		switch (a->order) {
		case WINDOW_CHOOSE2_BY_NAME_INDEX_NUMBER:
		case WINDOW_CHOOSE2_BY_NAME_NAME_NUMBER:
			return (strcmp(a->s->name, b->s->name));
		case WINDOW_CHOOSE2_BY_TIME_INDEX_NUMBER:
		case WINDOW_CHOOSE2_BY_TIME_NAME_NUMBER:
			if (timercmp(&a->s->activity_time,
			    &b->s->activity_time, >))
				return (-1);
			if (timercmp(&a->s->activity_time,
			    &b->s->activity_time, <))
				return (1);
		}
		fatalx("unknown order");
	}
	if (a->wl != b->wl) {
		if (a->wl == NULL && b->wl != NULL)
			return (-1);
		if (a->wl != NULL && b->wl == NULL)
			return (1);
		switch (a->order) {
		case WINDOW_CHOOSE2_BY_NAME_NAME_NUMBER:
		case WINDOW_CHOOSE2_BY_TIME_NAME_NUMBER:
			retval = strcmp(a->wl->window->name,
			    b->wl->window->name);
			if (retval != 0)
				return (retval);
			/* FALLTHROUGH */
		case WINDOW_CHOOSE2_BY_NAME_INDEX_NUMBER:
		case WINDOW_CHOOSE2_BY_TIME_INDEX_NUMBER:
			if (a->wl->idx < b->wl->idx)
				return (-1);
			if (a->wl->idx > b->wl->idx)
				return (1);
			return (0);
		}
		fatalx("unknown order");
	}
	if (a->wp != b->wp) {
		if (a->wp == NULL && b->wp != NULL)
			return (-1);
		if (a->wp != NULL && b->wp == NULL)
			return (1);
		switch (a->order) {
		case WINDOW_CHOOSE2_BY_NAME_INDEX_NUMBER:
		case WINDOW_CHOOSE2_BY_NAME_NAME_NUMBER:
		case WINDOW_CHOOSE2_BY_TIME_INDEX_NUMBER:
		case WINDOW_CHOOSE2_BY_TIME_NAME_NUMBER:
			window_pane_index(a->wp, &aidx);
			window_pane_index(b->wp, &bidx);
			if (aidx < bidx)
				return (-1);
			if (aidx > bidx)
				return (1);
			return (0);
		}
		fatalx("unknown order");
	}
	return (0);
}

static struct screen *
window_choose2_init(struct window_pane *wp, struct args *args)
{
	struct window_choose2_data	*data;
	struct screen			*s;

	wp->modedata = data = xcalloc(1, sizeof *data);

	if (args == NULL || args->argc == 0)
		data->command = xstrdup(WINDOW_CHOOSE2_DEFAULT_COMMAND);
	else
		data->command = xstrdup(args->argv[0]);

	s = &data->screen;
	screen_init(s, screen_size_x(&wp->base), screen_size_y(&wp->base), 0);
	s->mode &= ~MODE_CURSOR;

	data->order = WINDOW_CHOOSE2_BY_NAME_INDEX_NUMBER;
	RB_INIT(&data->tree);
	window_choose2_build_tree(data);

	window_choose2_draw_screen(wp);

	return (s);
}

static void
window_choose2_free(struct window_pane *wp)

{
	struct window_choose2_data	*data = wp->modedata;

	if (data == NULL)
		return;

	window_choose2_free_tree(&data->tree);

	screen_free(&data->screen);

	free(data->command);
	free(data);
}

static void
window_choose2_resize(struct window_pane *wp, u_int sx, u_int sy)
{
	struct window_choose2_data	*data = wp->modedata;
	struct screen			*s = &data->screen;

	screen_resize(s, sx, sy, 0);
	window_choose2_build_tree(data);
	window_choose2_draw_screen(wp);
	wp->flags |= PANE_REDRAW;
}

static void
window_choose2_key(struct window_pane *wp, struct client *c,
    __unused struct session *s, key_code key, struct mouse_event *m)
{
	struct window_choose2_data	*data = wp->modedata;
	struct window_choose2_item	*item;
	u_int				 i, x, y;
	int				 finished, found;

	/*
	 * t = toggle tag
	 * T = tag nothing
	 * C-t = tag all
	 * XXX
	 * q = exit
	 * O = change sort order
	 * ENTER = detach client
	 */

	if (key == KEYC_MOUSEDOWN1_PANE) {
		if (cmd_mouse_at(wp, m, &x, &y, 0) != 0)
			return;
		if (x > data->width || y > data->height)
			return;
		found = 0;
		RB_FOREACH(item, window_choose2_tree, &data->tree) {
			if (item->number == data->offset + y) {
				found = 1;
				data->current = item;
			}
		}
		if (found && key == KEYC_MOUSEDOWN1_PANE)
			key = '\r';
	}

	finished = 0;
	switch (key) {
	case KEYC_UP:
	case 'k':
	case KEYC_WHEELUP_PANE:
		window_choose2_up(data);
		break;
	case KEYC_DOWN:
	case 'j':
	case KEYC_WHEELDOWN_PANE:
		window_choose2_down(data);
		break;
	case KEYC_PPAGE:
	case '\002': /* C-b */
		for (i = 0; i < data->height; i++) {
			if (data->current->number == 0)
				break;
			window_choose2_up(data);
		}
		break;
	case KEYC_NPAGE:
	case '\006': /* C-f */
		for (i = 0; i < data->height; i++) {
			if (data->current->number == data->number - 1)
				break;
			window_choose2_down(data);
		}
		break;
	case KEYC_HOME:
		data->current = RB_MIN(window_choose2_tree, &data->tree);
		data->offset = 0;
		break;
	case KEYC_END:
		data->current = RB_MAX(window_choose2_tree, &data->tree);
		if (data->current->number > data->height - 1)
			data->offset = data->number - data->height;
		else
			data->offset = 0;
		break;
	case KEYC_LEFT:
	case '-':
		item = data->current;
		if (item->type == WINDOW_CHOOSE2_SESSION && !item->expanded) {
			if (data->current->number != 0)
				window_choose2_up(data);
			break;
		}
		if (item->type == WINDOW_CHOOSE2_WINDOW && !item->expanded)
			item = item->parent;
		else if (item->type == WINDOW_CHOOSE2_PANE)
			item = item->parent;
		item->expanded = 0;
		data->current = item;
		window_choose2_build_tree(data);
		break;
	case KEYC_RIGHT:
	case '+':
		item = data->current;
		if (item->type == WINDOW_CHOOSE2_PANE)
			item = item->parent;
		item->expanded = 1;
		window_choose2_build_tree(data);
		if (data->current->number != data->number - 1)
			window_choose2_down(data);
		break;
	case 't':
		data->current->tagged = !data->current->tagged;
		window_choose2_down(data);
		break;
	case 'T':
		RB_FOREACH(item, window_choose2_tree, &data->tree)
		    item->tagged = 0;
		break;
	case '\024': /* C-t */
		RB_FOREACH(item, window_choose2_tree, &data->tree)
		    item->tagged = 1;
		break;
	case 'O':
		data->order++;
		if (data->order > WINDOW_CHOOSE2_BY_TIME_NAME_NUMBER)
			data->order = WINDOW_CHOOSE2_BY_NAME_INDEX_NUMBER;
		window_choose2_build_tree(data);
		break;
	case '\r':
#if 0
		command = xstrdup(data->command);
		name = xstrdup(data->current->c->ttyname);
		window_pane_reset_mode(wp);
		window_choose2_run_command(c, command, name);
		free(name);
		free(command);
#endif
		return;
	case 'q':
	case '\033': /* Escape */
		finished = 1;
		break;
	}
	if (finished || server_client_how_many() == 0)
		window_pane_reset_mode(wp);
	else {
		window_choose2_draw_screen(wp);
		wp->flags |= PANE_REDRAW;
	}
}

static void
window_choose2_up(struct window_choose2_data *data)
{
	data->current = RB_PREV(window_choose2_tree, &data->tree, data->current);
	if (data->current == NULL) {
		data->current = RB_MAX(window_choose2_tree, &data->tree);
		if (data->current->number > data->height - 1)
			data->offset = data->number - data->height;
	} else if (data->current->number < data->offset)
		data->offset--;
}

static void
window_choose2_down(struct window_choose2_data *data)
{
	data->current = RB_NEXT(window_choose2_tree, &data->tree, data->current);
	if (data->current == NULL) {
		data->current = RB_MIN(window_choose2_tree, &data->tree);
		data->offset = 0;
	} else if (data->current->number > data->offset + data->height - 1)
		data->offset++;
}

#if 0
static void
window_choose2_run_command(struct client *c, const char *template,
    const char *name)
{
	struct cmdq_item	*new_item;
	struct cmd_list		*cmdlist;
	char			*command, *cause;

	command = cmd_template_replace(template, name, 1);
	if (command == NULL || *command == '\0')
		return;

	if (cmd_string_parse(command, &cmdlist, NULL, 0, &cause) != 0) {
		if (cause != NULL && c != NULL) {
			*cause = toupper((u_char)*cause);
			status_message_set(c, "%s", cause);
		}
		free(cause);
	} else {
		new_item = cmdq_get_command(cmdlist, NULL, NULL, 0);
		cmdq_append(c, new_item);
		cmd_list_free(cmdlist);
	}

	free (command);
}
#endif

static void
window_choose2_free_tree(struct window_choose2_tree *tree)
{
	struct window_choose2_item	*item, *item1;

	RB_FOREACH_SAFE(item, window_choose2_tree, tree, item1) {
		//XXX references

		RB_REMOVE(window_choose2_tree, tree, item);
		free(item);
	}
}

static struct window_choose2_item *
window_choose2_add_item(struct window_choose2_data *data,
    struct window_choose2_item *parent, enum window_choose2_type type,
    struct session *s, struct winlink *wl, struct window_pane *wp,
    struct window_choose2_item *was, struct window_choose2_item **current)
{
	struct window_choose2_item	*item;

	item = xcalloc(1, sizeof *item);
	item->data = data;
	item->order = data->order;

	item->parent = parent;
	item->type = type;

	item->s = s;
	item->wl = wl;
	item->wp = wp;
	//XXX references

	if (was != NULL &&
	    (item->s == was->s &&
	    item->wl == was->wl &&
	    item->wp == was->wp))
		*current = item;

	RB_INSERT(window_choose2_tree, &data->tree, item);
	return (item);
}

static void
window_choose2_add_panes(struct window_choose2_data *data,
    struct window_choose2_item *parent, struct session *s, struct winlink *wl,
    struct window_choose2_item *was, struct window_choose2_item **current)
{
	struct window_choose2_item	*item;
	struct window_pane		*wp;

	TAILQ_FOREACH(wp, &wl->window->panes, entry) {
		item = window_choose2_add_item(data, parent,
		    WINDOW_CHOOSE2_PANE, s, wl, wp, was, current);

		if (TAILQ_NEXT(wp, entry) == NULL)
			item->last = 1;
	}
}

static void
window_choose2_add_windows(struct window_choose2_data *data,
    struct window_choose2_item *parent, struct session *s,
    struct window_choose2_item *was, struct window_choose2_item **current,
    struct window_choose2_tree *old)
{
	struct window_choose2_item	*item, *old_item;
	struct winlink			*wl;

	RB_FOREACH(wl, winlinks, &s->windows) {
		item = window_choose2_add_item(data, parent,
		    WINDOW_CHOOSE2_WINDOW, s, wl, NULL, was, current);

		old_item = RB_FIND(window_choose2_tree, old, item);
		if (old_item != NULL)
			item->expanded = old_item->expanded;

		if (RB_NEXT(winlinks, &s->windows, wl) == NULL)
			item->last = 1;

		if (item->expanded) {
			window_choose2_add_panes(data, item, s, wl, was,
			    current);
		}
	}
}

static void
window_choose2_build_tree(struct window_choose2_data *data)
{
	struct session			*s;
	struct window_choose2_item	*item, *old_item, *current, *was;
	struct window_choose2_tree	 old;

	was = data->current;
	current = NULL;

	memcpy(&old, &data->tree, sizeof old);
	RB_INIT(&data->tree);

	RB_FOREACH(s, sessions, &sessions) {
		item = window_choose2_add_item(data, NULL,
		    WINDOW_CHOOSE2_SESSION, s, NULL, NULL, was, &current);

		old_item = RB_FIND(window_choose2_tree, &old, item);
		if (old_item != NULL)
			item->expanded = old_item->expanded;

		if (RB_NEXT(sessions, &sessions, s) == NULL)
			item->last = 1;

		if (item->expanded) {
			window_choose2_add_windows(data, item, s, was, &current,
			    &old);
		}
	}

	data->number = 0;
	RB_FOREACH(item, window_choose2_tree, &data->tree)
	    item->number = data->number++;

	if (current != NULL)
		data->current = current;
	else
		data->current = RB_MIN(window_choose2_tree, &data->tree);

	data->width = screen_size_x(&data->screen);
	data->height = (screen_size_y(&data->screen) / 3) * 2;
	if (data->height > data->number)
		data->height = screen_size_y(&data->screen) / 2;
	if (data->height < 10)
		data->height = screen_size_y(&data->screen);
	if (screen_size_y(&data->screen) - data->height < 2)
		data->height = screen_size_y(&data->screen);

	if (data->current == NULL)
		return;
	current = data->current;
	if (current->number < data->offset) {
		if (current->number > data->height - 1)
			data->offset = current->number - (data->height - 1);
		else
			data->offset = 0;
	}
	if (current->number > data->offset + data->height - 1) {
		if (current->number > data->height - 1)
			data->offset = current->number - (data->height - 1);
		else
			data->offset = 0;
	}

	window_choose2_free_tree(&old);
}

static void
window_choose2_draw_screen(struct window_pane *wp)
{
	struct window_choose2_data	*data = wp->modedata;
	struct screen			*s = &data->screen, *box = NULL;
	struct window_choose2_item	*item;
	struct session			*sp;
	struct winlink			*wlp;
	struct window_pane		*wpp;
	struct options			*oo = wp->window->options;
	struct screen_write_ctx	 	 ctx;
	struct grid_cell		 gc0;
	struct grid_cell		 gc;
	u_int				 width, height, i, needed, x, n, sy;
	u_int				 wl_size = 0, wl_count = 0;
	int				 last_wl = 0;
	char				 line[1024];
	const char			*tag, *prefix, *marker, *label = NULL;
	const char			*description;

	memcpy(&gc0, &grid_default_cell, sizeof gc0);
	memcpy(&gc, &grid_default_cell, sizeof gc);
	style_apply(&gc, oo, "mode-style");

	width = data->width;
	if (width >= sizeof line)
		width = (sizeof line) - 1;
	height = data->height;

	screen_write_start(&ctx, NULL, s);
	screen_write_clearscreen(&ctx, 8);

	RB_FOREACH(item, window_choose2_tree, &data->tree) {
		if (item->type == WINDOW_CHOOSE2_WINDOW)
			wl_size++;
	}

	i = 0;
	RB_FOREACH(item, window_choose2_tree, &data->tree) {
		sp = item->s;
		wlp = item->wl;
		wpp = item->wp;
		i++;

		if (i <= data->offset)
			continue;
		if (i - 1 > data->offset + height - 1)
			break;

		screen_write_cursormove(&ctx, 0, i - 1 - data->offset);

		marker = " ";
		marker = item->expanded ? "-" : "+";
		prefix = item->last ? "\001mq\001>" : "\001tq\001>";
		tag = item->tagged ? "*" : "";

		x = width;
		switch (item->type) {
		case WINDOW_CHOOSE2_SESSION:
			if (sp->flags & SESSION_UNATTACHED)
				description = "";
			else
				description = " (attached)";
			snprintf(line, sizeof line, "%s %s%s: %u windows%s",
			    marker, sp->name, tag, winlink_count(&sp->windows),
			    description);
			break;
		case WINDOW_CHOOSE2_WINDOW:
			wl_count++;
			if (wl_size == wl_count)
				last_wl = 1;
			snprintf(line, sizeof line, "%s %s %u%s: %s%s",
			    prefix, marker, wlp->idx, tag, wlp->window->name,
			    window_printable_flags(wlp));
			x = width + 2;
			break;
		case WINDOW_CHOOSE2_PANE:
			window_pane_index(wpp, &n);
			prefix = "\001x   tq\001>";
			if (item->last && !last_wl)
				prefix = "\001x   mq\001>";

			if (last_wl)
				prefix = "\001    tq\001>";

			if (item->last && last_wl)
				prefix = "\001    mq\001>";

			snprintf(line, sizeof line, "%s %u%s: \"%s\"%s",
			    prefix, n, tag, wp->base.title,
			    window_pane_printable_flags(wpp));
			x = width + 2;
			break;
		}

		if (item != data->current) {
			screen_write_puts(&ctx, &gc0, "%.*s", x, line);
			screen_write_clearendofline(&ctx, 8);
			continue;
		}
		screen_write_puts(&ctx, &gc, "%-*.*s", x, x, line);
	}

	if (height == screen_size_y(s) && width > 4) {
		screen_write_stop(&ctx);
		return;
	}

	sy = screen_size_y(s);
	item = data->current;
	sp = item->s;
	wlp = item->wl;
	wpp = item->wp;

	screen_write_cursormove(&ctx, 0, height);
	screen_write_box(&ctx, width, sy - height);

	switch (data->order) {
	case WINDOW_CHOOSE2_BY_NAME_INDEX_NUMBER:
		label = "sort: name-index-number";
		break;
	case WINDOW_CHOOSE2_BY_NAME_NAME_NUMBER:
		label = "sort: name-name-number";
		break;
	case WINDOW_CHOOSE2_BY_TIME_INDEX_NUMBER:
		label = "sort: time-index-number";
		break;
	case WINDOW_CHOOSE2_BY_TIME_NAME_NUMBER:
		label = "sort: time-name-number";
		break;
	}
	switch (item->type) {
	case WINDOW_CHOOSE2_SESSION:
		snprintf(line, sizeof line, "%s", sp->name);
		box = &sp->curw->window->active->base;
		break;
	case WINDOW_CHOOSE2_WINDOW:
		snprintf(line, sizeof line, "%s:%d", sp->name, wlp->idx);
		box = &wlp->window->active->base;
		break;
	case WINDOW_CHOOSE2_PANE:
		window_pane_index(wpp, &n);
		snprintf(line, sizeof line, "%s:%d.%u", sp->name, wlp->idx, n);
		box = &wpp->base;
		break;
	}
	needed = strlen(line) + strlen (label) + 5;
	if (width - 2 >= needed) {
		screen_write_cursormove(&ctx, 1, height);
		screen_write_puts(&ctx, &gc0, " %s (%s) ", line, label);
	}

	screen_write_cursormove(&ctx, 2, height + 1);
	screen_write_preview(&ctx, box, data->width - 4, sy - data->height - 2);

	screen_write_stop(&ctx);
}
