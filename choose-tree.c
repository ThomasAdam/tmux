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

#include "tmux.h"

static struct screen	*choose_tree_init(struct window_pane *, struct args *);
static void		 choose_tree_free(struct window_pane *);
static void		 choose_tree_resize(struct window_pane *, u_int, u_int);
static void		 choose_tree_key(struct window_pane *,
			     struct client *, struct session *, key_code,
			     struct mouse_event *);

#define SESSION_DEFAULT_COMMAND " switch-client -t '%%'"
#define WINDOW_DEFAULT_COMMAND " select-window -t '%%'"
#define PANE_DEFAULT_COMMAND " select-pane -t '%%'"

#define CHOOSE_TREE_SESSION_TEMPLATE				\
	" #{session_name}: #{session_windows} windows"		\
	"#{?session_grouped, (group ,}"				\
	"#{session_group}#{?session_grouped,),}"		\
	"#{?session_attached, (attached),}"
#define CHOOSE_TREE_WINDOW_TEMPLATE				\
	"#{window_index}: #{window_name}#{window_flags} "	\
	"\"#{pane_title}\" (#{window_panes} panes)"
#define CHOOSE_TREE_PANE_TEMPLATE				\
	"#{pane_index}: #{pane_id}: - (#{pane_tty}) "

const struct window_mode choose_tree_mode = {
	.init = choose_tree_init,
	.free = choose_tree_free,
	.resize = choose_tree_resize,
	.key = choose_tree_key,
};

struct choose_tree_data;
struct choose_tree_item {
	struct choose_tree_data		*data;

	u_int				 number;
	u_int				 order;
	const char			*name;

	RB_ENTRY (choose_tree_item)	 entry;
};

RB_HEAD(choose_tree_tree, choose_tree_item);
static int choose_tree_cmp(const struct choose_tree_item *,
    const struct choose_tree_item *);
RB_GENERATE_STATIC(choose_tree_tree, choose_tree_item, entry,
    choose_tree_cmp);

enum choose_tree_order {
	CHOOSE_TREE_BUFFER_BY_NAME,
	CHOOSE_TREE_BUFFER_BY_TIME,
};

enum choose_tree_type {
	CHOOSE_TREE_SESSION,
	CHOOSE_TREE_WINDOW,
	CHOOSE_TREE_PANE,
};

struct choose_tree_data {
	struct session			*s;
	struct winlink			*wl;
	struct window_pane		*wp;
	char				*command;
	struct screen			 screen;
	u_int				 offset, wl_count, wp_count;
	struct choose_tree_item		*current;

	u_int				 width;
	u_int				 height;

	struct choose_tree_tree 	 tree;
	u_int				 number;
	enum choose_tree_order	 	 order;
	enum choose_tree_type		 type;
};

static struct choose_tree_item *choose_tree_add_item(struct choose_tree_data *,
			     struct format_tree *, enum choose_tree_type);

static void	choose_tree_up(struct choose_tree_data *);
static void	choose_tree_down(struct choose_tree_data *);
static void	choose_tree_run_command(struct client *, const char *,
		    const char *);
static void	choose_tree_free_tree(struct choose_tree_data *);
static void	choose_tree_build_tree(struct choose_tree_data *);
static void	choose_tree_draw_screen(struct window_pane *);

static u_int	global_tree_order = 0;

static int
choose_tree_cmp(const struct choose_tree_item *a,
    const struct choose_tree_item *b)
{
	if (a->order < b->order)
		return (-1);
	if (a->order > b->order)
		return (1);
	return (0);
}

static struct screen *
choose_tree_init(struct window_pane *wp, struct args *args)
{
	struct choose_tree_data		*data;
	struct screen			*s;

	wp->modedata = data = xcalloc(1, sizeof *data);

	s = &data->screen;
	screen_init(s, screen_size_x(&wp->base), screen_size_y(&wp->base), 0);
	s->mode &= ~MODE_CURSOR;

	data->order = CHOOSE_TREE_BUFFER_BY_NAME;
	RB_INIT(&data->tree);
	choose_tree_build_tree(data);

	choose_tree_draw_screen(wp);

	return (s);
}

static void
choose_tree_free(struct window_pane *wp)
{
	struct choose_tree_data	*data = wp->modedata;

	if (data == NULL)
		return;

	choose_tree_free_tree(data);

	screen_free(&data->screen);

	free(data->command);
	free(data);
}

static void
choose_tree_resize(struct window_pane *wp, u_int sx, u_int sy)
{
	struct choose_tree_data	*data = wp->modedata;
	struct screen			*s = &data->screen;

	screen_resize(s, sx, sy, 0);
	choose_tree_build_tree(data);
	choose_tree_draw_screen(wp);
	wp->flags |= PANE_REDRAW;
}

static void
choose_tree_key(struct window_pane *wp, struct client *c,
    __unused struct session *s, key_code key, struct mouse_event *m)
{
	struct choose_tree_data	*data = wp->modedata;
	struct choose_tree_item	*item;
	u_int			 i, x, y;
	int			 finished, found;
	char			*command, *name;

	/*
	 * q = exit
	 * O = change sort order
	 * ENTER = paste buffer
	 */

	if (key == KEYC_MOUSEDOWN1_PANE) {
		if (cmd_mouse_at(wp, m, &x, &y, 0) != 0)
			return;
		if (x > data->width || y > data->height)
			return;
		found = 0;
		RB_FOREACH(item, choose_tree_tree, &data->tree) {
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
		choose_tree_up(data);
		break;
	case KEYC_DOWN:
	case 'j':
	case KEYC_WHEELDOWN_PANE:
		choose_tree_down(data);
		break;
	case KEYC_PPAGE:
	case '\002': /* C-b */
		for (i = 0; i < data->height; i++) {
			if (data->current->number == 0)
				break;
			choose_tree_up(data);
		}
		break;
	case KEYC_NPAGE:
	case '\006': /* C-f */
		for (i = 0; i < data->height; i++) {
			if (data->current->number == data->number - 1)
				break;
			choose_tree_down(data);
		}
		break;
	case KEYC_HOME:
		data->current = RB_MIN(choose_tree_tree, &data->tree);
		data->offset = 0;
		break;
	case KEYC_END:
		data->current = RB_MAX(choose_tree_tree, &data->tree);
		if (data->current->number > data->height - 1)
			data->offset = data->number - data->height;
		else
			data->offset = 0;
		break;
#if 0
	case 'O':

		if (data->order == WINDOW_BUFFER_BY_NAME)
			data->order = WINDOW_BUFFER_BY_TIME;
		else if (data->order == WINDOW_BUFFER_BY_TIME)
			data->order = WINDOW_BUFFER_BY_NAME;
		choose_tree_build_tree(data);
		break;
#endif
	case '\r':
		command = xstrdup(data->command);
		name = xstrdup(data->current->name);
		window_pane_reset_mode(wp);
		choose_tree_run_command(c, command, name);
		free(name);
		free(command);
		return;
	case 'q':
	case '\033': /* Escape */
		finished = 1;
		break;
	}
	if (finished)
		window_pane_reset_mode(wp);
	else {
		choose_tree_draw_screen(wp);
		wp->flags |= PANE_REDRAW;
	}
}

static void
choose_tree_up(struct choose_tree_data *data)
{
	data->current = RB_PREV(choose_tree_tree, &data->tree, data->current);
	if (data->current == NULL) {
		data->current = RB_MAX(choose_tree_tree, &data->tree);
		if (data->current->number > data->height - 1)
			data->offset = data->number - data->height;
	} else if (data->current->number < data->offset)
		data->offset--;
}

static void
choose_tree_down(struct choose_tree_data *data)
{
	data->current = RB_NEXT(choose_tree_tree, &data->tree, data->current);
	if (data->current == NULL) {
		data->current = RB_MIN(choose_tree_tree, &data->tree);
		data->offset = 0;
	} else if (data->current->number > data->offset + data->height - 1)
		data->offset++;
}

static void
choose_tree_run_command(struct client *c, const char *template,
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
choose_tree_free_tree(struct choose_tree_data *data)
{
	struct choose_tree_item	*item, *item1;

	RB_FOREACH_SAFE(item, choose_tree_tree, &data->tree, item1) {
		free((void *)item->name);

		RB_REMOVE(choose_tree_tree, &data->tree, item);
		free(item);
	}
}

static struct choose_tree_item *
choose_tree_add_item(struct choose_tree_data *data, struct format_tree *ft,
    enum choose_tree_type ctt)
{
	struct choose_tree_item	*item;
	struct session		*s = data->s;
	struct winlink		*wl = data->wl;
	char			*fmt = NULL;
	u_int			 no_of_wl = 0, no_of_wp = 0;
	int			 has_panes = 0;

	item = xcalloc(1, sizeof *item);
	item->data = data;

	switch (ctt) {
	case CHOOSE_TREE_SESSION:
		/* XXX: What about expand/collapse of sessions? */
		item->name = format_expand(ft, CHOOSE_TREE_SESSION_TEMPLATE);
		data->type = CHOOSE_TREE_SESSION;
		break;
	case CHOOSE_TREE_WINDOW:
		no_of_wl = winlink_count(&s->windows);
		has_panes = window_count_panes(wl->window) > 1;
		if (data->wl_count < no_of_wl && !has_panes) {
			xasprintf(&fmt, " \001tq\001> %s",
			    CHOOSE_TREE_WINDOW_TEMPLATE);
		} else {
			if (data->wl_count == no_of_wl) {
				xasprintf(&fmt, " \001mq\001> %s",
				   CHOOSE_TREE_WINDOW_TEMPLATE);
			} else {
				xasprintf(&fmt, " \001tq\001> %s",
				    CHOOSE_TREE_WINDOW_TEMPLATE);
			}
		}
		item->name = format_expand(ft, fmt);
		data->type = CHOOSE_TREE_WINDOW;
		break;
	case CHOOSE_TREE_PANE:
		no_of_wp = window_count_panes(wl->window);
		if (data->wp_count < no_of_wp) {
			xasprintf(&fmt, " \001x   tq\001> %s",
			    CHOOSE_TREE_PANE_TEMPLATE);
		} else {
			if (data->wp_count == no_of_wp) {
				xasprintf(&fmt, " \001x   mq\001> %s",
				    CHOOSE_TREE_PANE_TEMPLATE);
			} else {
				xasprintf(&fmt, " \001x   tq\001> %s",
				    CHOOSE_TREE_PANE_TEMPLATE);
			}
		}
		item->name = format_expand(ft, fmt);
		data->type = CHOOSE_TREE_PANE;
		break;
	}

	item->order = global_tree_order++;
	RB_INSERT(choose_tree_tree, &data->tree, item);

	free(fmt);

	return (item);
}

static void
choose_tree_build_tree(struct choose_tree_data *data)
{
	struct screen			*s = &data->screen;
	struct session			*sess;
	struct winlink			*wl;
	struct window_pane		*wp;
	struct choose_tree_item		*item;
	struct format_tree		*ft;
	char				*name;
	struct choose_tree_item		*current;

	if (data->current != NULL)
		name = xstrdup(data->current->name);
	else
		name = NULL;
	current = NULL;

	choose_tree_free_tree(data);

	RB_FOREACH(sess, sessions, &sessions) {
		data->s = sess;
		data->wl_count = data->wp_count = 0;
		ft = format_create(NULL, 0);
		format_defaults(ft, NULL, sess, NULL, NULL);
		item = choose_tree_add_item(data, ft, CHOOSE_TREE_SESSION);
		format_free(ft);

		RB_FOREACH(wl, winlinks, &sess->windows) {
			data->wl = wl;
			data->wl_count++;
			ft = format_create(NULL, 0);
			format_defaults(ft, NULL, sess, wl, NULL);
			item = choose_tree_add_item(data, ft,
			    CHOOSE_TREE_WINDOW);
			format_free(ft);

			if (window_count_panes(wl->window) == 1)
				continue;

			TAILQ_FOREACH(wp, &wl->window->panes, entry) {
				data->wp = wp;
				data->wp_count++;
				ft = format_create(NULL, 0);
				format_defaults(ft, NULL, sess, wl, wp);
				item = choose_tree_add_item(data, ft,
				    CHOOSE_TREE_PANE);
				format_free(ft);
			}
		}
		if (name != NULL && strcmp(name, item->name) == 0)
			current = item;
	}

	data->number = 0;
	RB_FOREACH(item, choose_tree_tree, &data->tree) {
		item->number = data->number++;
	}

	if (current != NULL)
		data->current = current;
	else
		data->current = RB_MIN(choose_tree_tree, &data->tree);
	free(name);

	data->width = screen_size_x(s);
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
choose_tree_draw_screen(struct window_pane *wp)
{
	struct choose_tree_data	*data = wp->modedata;
	struct screen			*s = &data->screen;
	struct choose_tree_item		*item;
	struct options			*oo = wp->window->options;
	struct screen_write_ctx	 	 ctx;
	struct grid_cell		 gc0;
	struct grid_cell		 gc;
	u_int				 width, height, i, at, needed;
	char				 line[1024], *cp;
	const char			*pdata, *end, *label, *name;
	size_t				 psize = 0;

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
	RB_FOREACH(item, choose_tree_tree, &data->tree) {
		i++;

		if (i <= data->offset)
			continue;
		if (i - 1 > data->offset + height - 1)
			break;

		name = strdup(item->name);

		screen_write_cursormove(&ctx, 0, i - 1 - data->offset);

		snprintf(line, sizeof line, "%-16s ", name);
		free((void *)name);

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

	screen_write_cursormove(&ctx, 0, height);
	screen_write_box(&ctx, width, screen_size_y(s) - height);

	label = "sort: name";
	needed = strlen(data->current->name) + strlen (label) + 5;
	if (width - 2 >= needed) {
		screen_write_cursormove(&ctx, 1, height);
		screen_write_puts(&ctx, &gc0, " %s (%s) ", data->current->name,
		    label);
	}

	pdata = NULL;
	psize = 0;
	end = pdata;
	for (i = height + 1; i < screen_size_y(s) - 1; i++) {
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
		if (at > width - 2)
			at = width - 2;
		line[at] = '\0';

		if (*line != '\0')
			screen_write_puts(&ctx, &gc0, "%s", line);
		while (s->cx != width - 1)
			screen_write_putc(&ctx, &grid_default_cell, ' ');

		if (end == pdata + psize)
			break;
		end++;
	}

	screen_write_stop(&ctx);
}
