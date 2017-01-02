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

static struct screen	*window_client_init(struct window_pane *,
			     struct args *);
static void		 window_client_free(struct window_pane *);
static void		 window_client_resize(struct window_pane *, u_int,
			     u_int);
static void		 window_client_key(struct window_pane *,
			     struct client *, struct session *, key_code,
			     struct mouse_event *);

#define WINDOW_CLIENT_DEFAULT_COMMAND "detach-client -t '%%'"

const struct window_mode window_client_mode = {
	.init = window_client_init,
	.free = window_client_free,
	.resize = window_client_resize,
	.key = window_client_key,
};

enum window_client_order {
	WINDOW_CLIENT_BY_TTY_NAME,
	WINDOW_CLIENT_BY_CREATION_TIME,
	WINDOW_CLIENT_BY_ACTIVITY_TIME,
};

struct window_client_data;
struct window_client_item {
	struct window_client_data	*data;

	u_int				 number;
	struct client			*c;

	int				 tagged;

	RB_ENTRY (window_client_item)	 entry;
};

RB_HEAD(window_client_tree, window_client_item);
static int window_client_cmp(const struct window_client_item *,
    const struct window_client_item *);
RB_GENERATE_STATIC(window_client_tree, window_client_item, entry,
    window_client_cmp);

struct window_client_data {
	char				*command;
	struct screen			 screen;
	u_int				 offset;
	struct window_client_item	*current;

	u_int				 width;
	u_int				 height;

	struct window_client_tree	 tree;
	u_int				 number;

	enum window_client_order	 order;
};

static void	window_client_up(struct window_client_data *);
static void	window_client_down(struct window_client_data *);
static void	window_client_run_command(struct client *, const char *,
		    const char *);
static void	window_client_free_tree(struct window_client_tree *);
static void	window_client_build_tree(struct window_client_data *);
static void	window_client_draw_screen(struct window_pane *);

static int
window_client_cmp(const struct window_client_item *a,
    const struct window_client_item *b)
{
	switch (a->data->order) {
	case WINDOW_CLIENT_BY_TTY_NAME:
		return (strcmp(a->c->ttyname, b->c->ttyname));
	case WINDOW_CLIENT_BY_CREATION_TIME:
		if (timercmp(&a->c->creation_time, &b->c->creation_time, >))
			return (-1);
		if (timercmp(&a->c->creation_time, &b->c->creation_time, <))
			return (1);
		return (0);
	case WINDOW_CLIENT_BY_ACTIVITY_TIME:
		if (timercmp(&a->c->activity_time, &b->c->activity_time, >))
			return (-1);
		if (timercmp(&a->c->activity_time, &b->c->activity_time, <))
			return (1);
		return (0);
	}
	fatalx("unknown order");
}

static struct screen *
window_client_init(struct window_pane *wp, struct args *args)
{
	struct window_client_data	*data;
	struct screen			*s;

	wp->modedata = data = xcalloc(1, sizeof *data);

	if (args == NULL || args->argc == 0)
		data->command = xstrdup(WINDOW_CLIENT_DEFAULT_COMMAND);
	else
		data->command = xstrdup(args->argv[0]);

	s = &data->screen;
	screen_init(s, screen_size_x(&wp->base), screen_size_y(&wp->base), 0);
	s->mode &= ~MODE_CURSOR;

	data->order = WINDOW_CLIENT_BY_ACTIVITY_TIME;
	RB_INIT(&data->tree);
	window_client_build_tree(data);

	window_client_draw_screen(wp);

	return (s);
}

static void
window_client_free(struct window_pane *wp)
{
	struct window_client_data	*data = wp->modedata;

	if (data == NULL)
		return;

	window_client_free_tree(&data->tree);

	screen_free(&data->screen);

	free(data->command);
	free(data);
}

static void
window_client_resize(struct window_pane *wp, u_int sx, u_int sy)
{
	struct window_client_data	*data = wp->modedata;
	struct screen			*s = &data->screen;

	screen_resize(s, sx, sy, 0);
	window_client_build_tree(data);
	window_client_draw_screen(wp);
	wp->flags |= PANE_REDRAW;
}

static void
window_client_key(struct window_pane *wp, struct client *c,
    __unused struct session *s, key_code key, struct mouse_event *m)
{
	struct window_client_data	*data = wp->modedata;
	struct window_client_item	*item;
	u_int				 i, x, y;
	int				 finished, found;
	char				*command, *name;

	/*
	 * t = toggle client tag
	 * T = tag no clients
	 * C-t = tag all clients
	 * d = detach client
	 * D = detach tagged clients
	 * x = detach and kill client
	 * X = detach and kill tagged clients
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
		RB_FOREACH(item, window_client_tree, &data->tree) {
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
		window_client_up(data);
		break;
	case KEYC_DOWN:
	case 'j':
	case KEYC_WHEELDOWN_PANE:
		window_client_down(data);
		break;
	case KEYC_PPAGE:
	case '\002': /* C-b */
		for (i = 0; i < data->height; i++) {
			if (data->current->number == 0)
				break;
			window_client_up(data);
		}
		break;
	case KEYC_NPAGE:
	case '\006': /* C-f */
		for (i = 0; i < data->height; i++) {
			if (data->current->number == data->number - 1)
				break;
			window_client_down(data);
		}
		break;
	case KEYC_HOME:
		data->current = RB_MIN(window_client_tree, &data->tree);
		data->offset = 0;
		break;
	case KEYC_END:
		data->current = RB_MAX(window_client_tree, &data->tree);
		if (data->current->number > data->height - 1)
			data->offset = data->number - data->height;
		else
			data->offset = 0;
		break;
	case 'd':
	case 'x':
	case 'z':
		item = data->current;
		window_client_down(data);
		if (key == 'd')
			server_client_detach(item->c, MSG_DETACH);
		else if (key == 'x')
			server_client_detach(item->c, MSG_DETACHKILL);
		else if (key == 'z')
			server_client_suspend(item->c);
		window_client_build_tree(data);
		break;
	case 'D':
	case 'X':
	case 'Z':
		RB_FOREACH(item, window_client_tree, &data->tree) {
			if (!item->tagged)
				continue;
			if (item == data->current)
				window_client_down(data);
			if (key == 'D')
				server_client_detach(item->c, MSG_DETACH);
			else if (key == 'X')
				server_client_detach(item->c, MSG_DETACHKILL);
			else if (key == 'Z')
				server_client_suspend(item->c);
		}
		window_client_build_tree(data);
		break;
	case 't':
		data->current->tagged = !data->current->tagged;
		window_client_down(data);
		break;
	case 'T':
		RB_FOREACH(item, window_client_tree, &data->tree)
			item->tagged = 0;
		break;
	case '\024': /* C-t */
		RB_FOREACH(item, window_client_tree, &data->tree)
			item->tagged = 1;
		break;
	case 'O':
		data->order++;
		if (data->order > WINDOW_CLIENT_BY_ACTIVITY_TIME)
			data->order = WINDOW_CLIENT_BY_TTY_NAME;
		window_client_build_tree(data);
		break;
	case '\r':
		command = xstrdup(data->command);
		name = xstrdup(data->current->c->ttyname);
		window_pane_reset_mode(wp);
		window_client_run_command(c, command, name);
		free(name);
		free(command);
		return;
	case 'q':
	case '\033': /* Escape */
		finished = 1;
		break;
	}
	if (finished || server_client_how_many() == 0)
		window_pane_reset_mode(wp);
	else {
		window_client_draw_screen(wp);
		wp->flags |= PANE_REDRAW;
	}
}

static void
window_client_up(struct window_client_data *data)
{
	data->current = RB_PREV(window_client_tree, &data->tree, data->current);
	if (data->current == NULL) {
		data->current = RB_MAX(window_client_tree, &data->tree);
		if (data->current->number > data->height - 1)
			data->offset = data->number - data->height;
	} else if (data->current->number < data->offset)
		data->offset--;
}

static void
window_client_down(struct window_client_data *data)
{
	data->current = RB_NEXT(window_client_tree, &data->tree, data->current);
	if (data->current == NULL) {
		data->current = RB_MIN(window_client_tree, &data->tree);
		data->offset = 0;
	} else if (data->current->number > data->offset + data->height - 1)
		data->offset++;
}

static void
window_client_run_command(struct client *c, const char *template,
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

static void
window_client_free_tree(struct window_client_tree *tree)
{
	struct window_client_item	*item, *item1;

	RB_FOREACH_SAFE(item, window_client_tree, tree, item1) {
		server_client_unref(item->c);

		RB_REMOVE(window_client_tree, tree, item);
		free(item);
	}
}

static void
window_client_build_tree(struct window_client_data *data)
{
	struct client			*c;
	struct window_client_item	*item, *current;
	char				*name;

	if (data->current != NULL)
		name = xstrdup(data->current->c->ttyname);
	else
		name = NULL;
	current = NULL;

	window_client_free_tree(&data->tree);

	TAILQ_FOREACH(c, &clients, entry) {
		if (c->session == NULL || (c->flags & CLIENT_DETACHING))
			continue;

		item = xcalloc(1, sizeof *item);
		item->data = data;

		item->c = c;
		c->references++;

		item->tagged = 0;

		RB_INSERT(window_client_tree, &data->tree, item);

		if (name != NULL && strcmp(name, item->c->ttyname) == 0)
			current = item;
	}

	data->number = 0;
	RB_FOREACH(item, window_client_tree, &data->tree)
		item->number = data->number++;

	if (current != NULL)
		data->current = current;
	else
		data->current = RB_MIN(window_client_tree, &data->tree);
	free(name);

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
}

static void
window_client_draw_screen(struct window_pane *wp)
{
	struct window_client_data	*data = wp->modedata;
	struct screen			*s = &data->screen, *box;
	struct window_client_item	*item;
	struct client			*c;
	struct options			*oo = wp->window->options;
	struct screen_write_ctx	 	 ctx;
	struct grid_cell		 gc0;
	struct grid_cell		 gc;
	u_int				 width, height, i, needed, sy;
	char				*tim, line[1024], *name;
	const char			*tag, *label;

	memcpy(&gc0, &grid_default_cell, sizeof gc0);
	memcpy(&gc, &grid_default_cell, sizeof gc);
	style_apply(&gc, oo, "mode-style");

	width = data->width;
	if (width >= sizeof line)
		width = (sizeof line) - 1;
	height = data->height;

	screen_write_start(&ctx, NULL, s);
	screen_write_clearscreen(&ctx, 8);

	i = 0;
	RB_FOREACH(item, window_client_tree, &data->tree) {
		c = item->c;
		i++;

		if (i <= data->offset)
			continue;
		if (i - 1 > data->offset + height - 1)
			break;

		screen_write_cursormove(&ctx, 0, i - 1 - data->offset);

		tim = ctime(&c->activity_time.tv_sec);
		*strchr(tim, '\n') = '\0';

		tag = "";
		if (item->tagged)
			tag = "*";

		xasprintf(&name, "%s%s:", c->ttyname, tag);
		snprintf(line, sizeof line, "%-16s session %s (%s)", name,
		    c->session->name, tim);
		free(name);

		if (item != data->current) {
			screen_write_puts(&ctx, &gc0, "%.*s", width, line);
			screen_write_clearendofline(&ctx, 8);
			continue;
		}
		screen_write_puts(&ctx, &gc, "%-*.*s", width, width, line);
	}

	if (height == screen_size_y(s) && width > 4) {
		screen_write_stop(&ctx);
		return;
	}

	sy = screen_size_y(s);
	c = data->current->c;
	box = &c->session->curw->window->active->base;

	screen_write_cursormove(&ctx, 0, height);
	screen_write_box(&ctx, width, sy - height);

	if (data->order == WINDOW_CLIENT_BY_TTY_NAME)
		label = "sort: tty";
	else if (data->order == WINDOW_CLIENT_BY_ACTIVITY_TIME)
		label = "sort: activity";
	else
		label = "sort: created";
	needed = strlen(c->ttyname) + strlen (label) + 5;
	if (width - 2 >= needed) {
		screen_write_cursormove(&ctx, 1, height);
		screen_write_puts(&ctx, &gc0, " %s (%s) ", c->ttyname, label);
	}

	screen_write_cursormove(&ctx, 2, height + 1);
	screen_write_preview(&ctx, box, data->width - 4, sy - data->height - 4);
	screen_write_cursormove(&ctx, 0, sy - 3);
	screen_write_line(&ctx, data->width, 1, 1);
	screen_write_cursormove(&ctx, 2, sy - 2);
	screen_write_copy(&ctx, &c->status, 0, 0, data->width - 4, 1);

	screen_write_stop(&ctx);
}
