/*
 * Copyright (c) 2015 Nicholas Marriott <nicm@users.sourceforge.net>
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
#include <time.h>

#include "tmux.h"

static struct screen	*window_tree_init(struct window_pane *);
static void		 window_tree_free(struct window_pane *);
static void		 window_tree_resize(struct window_pane *, u_int, u_int);
static void		 window_tree_key(struct window_pane *,
			     struct client *, struct session *, key_code,
			     struct mouse_event *);

/* XXX should be passed from command */
#define SESSION_TREE_COMMAND "switch-client -t '%%'"
#define WINDOW_TREE_COMMAND "select-window -t '%%'"
#define PANE_TREE_COMMAND "select-pane -t '%%'"

const struct window_mode window_tree_mode = {
	window_tree_init,
	window_tree_free,
	window_tree_resize,
	window_tree_key,
	NULL,
	NULL
};

struct window_tree_data;
struct window_tree_item {
	struct window_tree_data		*data;

	u_int				 number;
	u_int				 tagged;
	const char			*name;
	u_int				 order;

	time_t				 created;

	RB_ENTRY (window_tree_item)	 entry;
};

RB_HEAD(window_tree_tree, window_tree_item);
static int window_tree_cmp(const struct window_tree_item *,
    const struct window_tree_item *);
RB_GENERATE_STATIC(window_tree_tree, window_tree_item, entry,
    window_tree_cmp);

struct window_tree_data {
	struct screen			 screen;
	u_int				 offset;
	u_int				 height;
	struct window_tree_item		*current;

	struct window_tree_tree	 	 tree;
	u_int				 number;
	int				 by_name;
};

static void	window_tree_up(struct window_tree_data *);
static void	window_tree_down(struct window_tree_data *);
static void	window_tree_run_command(struct client *, const char *);
static void	window_tree_free_tree(struct window_tree_data *);
static void	window_tree_build_tree(struct window_tree_data *, int);
static void	window_tree_draw_screen(struct window_pane *);

static int
window_tree_cmp(const struct window_tree_item *a,
    const struct window_tree_item *b)
{
	if (a->data->by_name)
		return (strcmp(a->name, b->name));
	if (a->order > b->order)
		return (-1);
	if (a->order < b->order)
		return (1);
	return (0);
}

static struct screen *
window_tree_init(struct window_pane *wp)
{
	struct window_tree_data	*data;
	struct screen			*s;

	wp->modedata = data = xcalloc(1, sizeof *data);

	s = &data->screen;
	screen_init(s, screen_size_x(&wp->base), screen_size_y(&wp->base), 0);
	s->mode &= ~MODE_CURSOR;

	RB_INIT(&data->tree);
	window_tree_build_tree(data, 0);

	window_tree_draw_screen(wp);

	return (s);
}

static void
window_tree_free(struct window_pane *wp)
{
	struct window_tree_data	*data = wp->modedata;

	if (data == NULL)
		return;

	window_tree_free_tree(data);

	screen_free(&data->screen);
	free(data);
}

static void
window_tree_resize(struct window_pane *wp, u_int sx, u_int sy)
{
	struct window_tree_data	*data = wp->modedata;
	struct screen			*s = &data->screen;

	screen_resize(s, sx, sy, 0);
	window_tree_build_tree(data, data->by_name);
	window_tree_draw_screen(wp);
	wp->flags |= PANE_REDRAW;
}

static void
window_tree_key(struct window_pane *wp, struct client *c,
    __unused struct session *s, key_code key, struct mouse_event *m)
{
	struct window_tree_data	*data = wp->modedata;
	struct window_tree_item	*item;
	u_int				 i, y;
	int				 finished;
	char				*name;

	/*
	 * t = toggle tree tag
	 * T = tag no trees
	 * C-t = tag all trees
	 * d = delete tree
	 * D = delete tagged trees
	 * q = exit
	 * O = change sort order
	 * ENTER = paste tree
	 */

	if (key == KEYC_MOUSEDOWN1_PANE) {
		if (cmd_mouse_at(wp, m, NULL, &y, 0) != 0)
			return;
		if (y > data->height)
			return;
		RB_FOREACH(item, window_tree_tree, &data->tree) {
			if (item->number == data->offset + y)
				data->current = item;
		}
		if (key == KEYC_MOUSEDOWN1_PANE)
			key = '\r';
	}

	finished = 0;
	switch (key) {
	case KEYC_UP:
	case 'k':
	case KEYC_WHEELUP_PANE:
		window_tree_up(data);
		break;
	case KEYC_DOWN:
	case 'j':
	case KEYC_WHEELDOWN_PANE:
		window_tree_down(data);
		break;
	case KEYC_PPAGE:
	case '\002': /* C-b */
		for (i = 0; i < data->height; i++) {
			if (data->current->number == 0)
				break;
			window_tree_up(data);
		}
		break;
	case KEYC_NPAGE:
	case '\006': /* C-f */
		for (i = 0; i < data->height; i++) {
			if (data->current->number == data->number - 1)
				break;
			window_tree_down(data);
		}
		break;
	case KEYC_HOME:
		data->current = RB_MIN(window_tree_tree, &data->tree);
		data->offset = 0;
		break;
	case KEYC_END:
		data->current = RB_MAX(window_tree_tree, &data->tree);
		if (data->current->number > data->height - 1)
			data->offset = data->number - data->height;
		else
			data->offset = 0;
		break;
	case 'd':
		item = data->current;
		window_tree_down(data);
		window_tree_build_tree(data, data->by_name);
		break;
	case 'D':
		RB_FOREACH(item, window_tree_tree, &data->tree) {
			if (!item->tagged)
				continue;
			if (item == data->current)
				window_tree_down(data);
		}
		window_tree_build_tree(data, data->by_name);
		break;
	case 't':
		data->current->tagged = !data->current->tagged;
		window_tree_down(data);
		break;
	case 'T':
		RB_FOREACH(item, window_tree_tree, &data->tree)
			item->tagged = 0;
		break;
	case '\024': /* C-t */
		RB_FOREACH(item, window_tree_tree, &data->tree)
			item->tagged = 1;
		break;
	case 'O':
		window_tree_build_tree(data, !data->by_name);
		break;
	case '\r':
		name = xstrdup(data->current->name);
		window_pane_reset_mode(wp);
		window_tree_run_command(c, name);
		free(name);
		return;
	case 'q':
		finished = 1;
		break;
	}
	if (finished)
		window_pane_reset_mode(wp);
	else {
		window_tree_draw_screen(wp);
		wp->flags |= PANE_REDRAW;
	}
}

static void
window_tree_up(struct window_tree_data *data)
{
	data->current = RB_PREV(window_tree_tree, &data->tree, data->current);
	if (data->current == NULL) {
		data->current = RB_MAX(window_tree_tree, &data->tree);
		if (data->current->number > data->height - 1)
			data->offset = data->number - data->height;
	} else if (data->current->number < data->offset)
		data->offset--;
}

static void
window_tree_down(struct window_tree_data *data)
{
	data->current = RB_NEXT(window_tree_tree, &data->tree, data->current);
	if (data->current == NULL) {
		data->current = RB_MIN(window_tree_tree, &data->tree);
		data->offset = 0;
	} else if (data->current->number > data->offset + data->height - 1)
		data->offset++;
}

static void
window_tree_run_command(struct client *c, const char *name)
{
	struct cmd_list	*cmdlist;
	char		*command = NULL, *cause;

	//command = cmd_template_replace(WINDOW_BUFFER_COMMAND, name, 1);
	if (command == NULL || *command == '\0')
		return;

	if (cmd_string_parse(command, &cmdlist, NULL, 0, &cause) != 0) {
		if (cause != NULL && c != NULL) {
			*cause = toupper((u_char)*cause);
			status_message_set(c, "%s", cause);
		}
		free(cause);
	} else {
		cmdq_append(c, cmdq_get_command(cmdlist, NULL, NULL, 0));
		cmd_list_free(cmdlist);
	}

	free (command);
}

static void
window_tree_free_tree(struct window_tree_data *data)
{
	struct window_tree_item	*item, *item1;

	RB_FOREACH_SAFE(item, window_tree_tree, &data->tree, item1) {
		free((void*)item->name);

		RB_REMOVE(window_tree_tree, &data->tree, item);
		free(item);
	}
}

static void
window_tree_build_tree(struct window_tree_data *data, int by_name)
{
	struct screen			*s = &data->screen;
	struct window_tree_item	*item;
	char				*name;
	struct window_tree_item	*current;
	struct session		*ses;
	struct winlink		*wl;


	if (data->current != NULL)
		name = xstrdup(data->current->name);
	else
		name = NULL;
	current = NULL;

	window_tree_free_tree(data);
	data->by_name = by_name;

	RB_FOREACH(ses, sessions, &sessions) {
		item = xcalloc(1, sizeof(*item));
		item->data = data;
		xasprintf(&item->name," \001tq\001> %s", ses->name);
		RB_INSERT(window_tree_tree, &data->tree, item);

		RB_FOREACH(wl, winlinks, &ses->windows) {
			item = xcalloc(1, sizeof *item);
			item->data = data;

			xasprintf(&item->name, " \001mq\001> %s", wl->window->name);
			item->order = 1;

			item->tagged = 0;
			item->created = 0;

			RB_INSERT(window_tree_tree, &data->tree, item);

			if (name != NULL && strcmp(name, item->name) == 0)
				current = item;
		}
	}

	data->number = 0;
	RB_FOREACH(item, window_tree_tree, &data->tree)
		item->number = data->number++;

	if (current != NULL)
		data->current = current;
	else
		data->current = RB_MIN(window_tree_tree, &data->tree);
	free(name);

	data->height = (screen_size_y(s) / 3) * 2;
	if (data->height > data->number)
		data->height = screen_size_y(s) / 2;
	if (data->height < 10)
		data->height = screen_size_y(s);
	if (screen_size_y(s) - data->height < 2)
		data->height = screen_size_y(s);

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
}

static void
window_tree_draw_screen(struct window_pane *wp)
{
	struct window_tree_data	*data = wp->modedata;
	struct screen			*s = &data->screen;
	struct window_tree_item	*item;
	struct options			*oo = wp->window->options;
	struct screen_write_ctx	 	 ctx;
	struct grid_cell		 gc0;
	struct grid_cell		 gc;
	u_int				 width, height, i, at;
	char				*tim, line[1024], *name, *cp;
	const char			*tag, *pdata, *end, *label;
	size_t				 psize;

	memcpy(&gc0, &grid_default_cell, sizeof gc0);
	memcpy(&gc, &grid_default_cell, sizeof gc);
	style_apply(&gc, oo, "mode-style");

	height = data->height;
	width = screen_size_x(s);
	if (width >= sizeof line)
		width = (sizeof line) - 1;

	screen_write_start(&ctx, NULL, s);
	screen_write_clearscreen(&ctx, 8);

	i = 0;
	RB_FOREACH(item, window_tree_tree, &data->tree) {
		i++;

		if (i <= data->offset)
			continue;
		if (i - 1 > data->offset + height - 1)
			break;

		screen_write_cursormove(&ctx, 0, i - 1 - data->offset);

		tim = ctime(&item->created);
		*strchr(tim, '\n') = '\0';

		tag = "";
		if (item->tagged)
			tag = "*";

		xasprintf(&name, "%s%s:", item->name, tag);
		//snprintf(line, sizeof line, "%-16s %zu bytes (%s)", name,
		//    item->size, tim);
		free(name);

		if (item != data->current) {
			screen_write_puts(&ctx, &gc0, "%.*s", width, line);
			screen_write_clearendofline(&ctx, 8);
			continue;
		}
		screen_write_puts(&ctx, &gc, "%-*.*s", width, width, line);
	}

	if (height == screen_size_y(s)) {
		screen_write_stop(&ctx);
		return;
	}

	if (data->by_name)
		label = " sort: name";
	else
		label = " sort: time";
	if (width - 1 < strlen (label))
		label = "";

	gc0.attr |= GRID_ATTR_CHARSET;
	screen_write_cursormove(&ctx, 0, height);
	screen_write_putc(&ctx, &gc0, 'l');
	for (i = 1; i < width - strlen(label); i++)
		screen_write_putc(&ctx, &gc0, 'q');
	gc0.attr &= ~GRID_ATTR_CHARSET;
	screen_write_puts(&ctx, &gc0, "%s", label);

	//XXX
	wp = RB_MIN(window_pane_tree, &all_window_panes);
	screen_write_cursormove(&ctx, 0, height + 1);
	screen_write_preview(&ctx, &wp->base, 40, 20);
	return;
	//XXX
	end = pdata;
	for (i = height + 1; i < screen_size_y(s); i++) {
		gc0.attr |= GRID_ATTR_CHARSET;
		screen_write_cursormove(&ctx, 0, i);
		screen_write_putc(&ctx, &gc0, 'x');
		gc0.attr &= ~GRID_ATTR_CHARSET;

		at = 0;
		while (end != pdata + psize && *end != '\n') {
			if ((sizeof line) - at > 5) {
				cp = vis(line + at, *end, VIS_TAB|VIS_OCTAL, 0);
				at = cp - line;
			}
			end++;
		}
		if (at > width - 1)
			at = width - 1;
		line[at] = '\0';

		if (*line != '\0')
			screen_write_puts(&ctx, &gc0, "%s", line);
		screen_write_clearendofline(&ctx, 8);

		if (end == pdata + psize)
			break;
		end++;
	}

	screen_write_stop(&ctx);
}
