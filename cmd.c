/* $OpenBSD$ */

/*
 * Copyright (c) 2007 Nicholas Marriott <nicm@users.sourceforge.net>
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
#include <sys/time.h>

#include <fnmatch.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tmux.h"

const struct cmd_entry *cmd_table[] = {
	&cmd_attach_session_entry,
	&cmd_bind_key_entry,
	&cmd_break_pane_entry,
	&cmd_capture_pane_entry,
	&cmd_choose_buffer_entry,
	&cmd_choose_client_entry,
	&cmd_choose_session_entry,
	&cmd_choose_tree_entry,
	&cmd_choose_window_entry,
	&cmd_clear_history_entry,
	&cmd_clock_mode_entry,
	&cmd_command_prompt_entry,
	&cmd_confirm_before_entry,
	&cmd_copy_mode_entry,
	&cmd_delete_buffer_entry,
	&cmd_detach_client_entry,
	&cmd_display_message_entry,
	&cmd_display_panes_entry,
	&cmd_find_window_entry,
	&cmd_has_session_entry,
	&cmd_if_shell_entry,
	&cmd_join_pane_entry,
	&cmd_kill_pane_entry,
	&cmd_kill_server_entry,
	&cmd_kill_session_entry,
	&cmd_kill_window_entry,
	&cmd_last_pane_entry,
	&cmd_last_window_entry,
	&cmd_link_window_entry,
	&cmd_list_buffers_entry,
	&cmd_list_clients_entry,
	&cmd_list_commands_entry,
	&cmd_list_keys_entry,
	&cmd_list_panes_entry,
	&cmd_list_sessions_entry,
	&cmd_list_windows_entry,
	&cmd_load_buffer_entry,
	&cmd_lock_client_entry,
	&cmd_lock_server_entry,
	&cmd_lock_session_entry,
	&cmd_move_pane_entry,
	&cmd_move_window_entry,
	&cmd_new_session_entry,
	&cmd_new_window_entry,
	&cmd_next_layout_entry,
	&cmd_next_window_entry,
	&cmd_paste_buffer_entry,
	&cmd_pipe_pane_entry,
	&cmd_previous_layout_entry,
	&cmd_previous_window_entry,
	&cmd_refresh_client_entry,
	&cmd_rename_session_entry,
	&cmd_rename_window_entry,
	&cmd_resize_pane_entry,
	&cmd_respawn_pane_entry,
	&cmd_respawn_window_entry,
	&cmd_rotate_window_entry,
	&cmd_run_shell_entry,
	&cmd_save_buffer_entry,
	&cmd_select_layout_entry,
	&cmd_select_pane_entry,
	&cmd_select_window_entry,
	&cmd_send_keys_entry,
	&cmd_send_prefix_entry,
	&cmd_server_info_entry,
	&cmd_set_buffer_entry,
	&cmd_set_environment_entry,
	&cmd_set_option_entry,
	&cmd_set_window_option_entry,
	&cmd_show_buffer_entry,
	&cmd_show_environment_entry,
	&cmd_show_messages_entry,
	&cmd_show_options_entry,
	&cmd_show_window_options_entry,
	&cmd_source_file_entry,
	&cmd_split_window_entry,
	&cmd_start_server_entry,
	&cmd_suspend_client_entry,
	&cmd_swap_pane_entry,
	&cmd_swap_window_entry,
	&cmd_switch_client_entry,
	&cmd_unbind_key_entry,
	&cmd_unlink_window_entry,
	&cmd_wait_for_entry,
	NULL
};

void		 cmd_clear_state(struct cmd_state *);
struct client	*cmd_get_state_client(struct cmd_q *, int);
int		 cmd_set_state_tflag(struct cmd *, struct cmd_q *);
int		 cmd_set_state_sflag(struct cmd *, struct cmd_q *);
int
cmd_pack_argv(int argc, char **argv, char *buf, size_t len)
{
	size_t	arglen;
	int	i;

	if (argc == 0)
		return (0);

	*buf = '\0';
	for (i = 0; i < argc; i++) {
		if (strlcpy(buf, argv[i], len) >= len)
			return (-1);
		arglen = strlen(argv[i]) + 1;
		buf += arglen;
		len -= arglen;
	}

	return (0);
}

int
cmd_unpack_argv(char *buf, size_t len, int argc, char ***argv)
{
	int	i;
	size_t	arglen;

	if (argc == 0)
		return (0);
	*argv = xcalloc(argc, sizeof **argv);

	buf[len - 1] = '\0';
	for (i = 0; i < argc; i++) {
		if (len == 0) {
			cmd_free_argv(argc, *argv);
			return (-1);
		}

		arglen = strlen(buf) + 1;
		(*argv)[i] = xstrdup(buf);
		buf += arglen;
		len -= arglen;
	}

	return (0);
}

char **
cmd_copy_argv(int argc, char **argv)
{
	char	**new_argv;
	int	  i;

	if (argc == 0)
		return (NULL);
	new_argv = xcalloc(argc + 1, sizeof *new_argv);
	for (i = 0; i < argc; i++) {
		if (argv[i] != NULL)
			new_argv[i] = xstrdup(argv[i]);
	}
	return (new_argv);
}

void
cmd_free_argv(int argc, char **argv)
{
	int	i;

	if (argc == 0)
		return;
	for (i = 0; i < argc; i++)
		free(argv[i]);
	free(argv);
}

char *
cmd_stringify_argv(int argc, char **argv)
{
	char	*buf;
	int	 i;
	size_t	 len;

	if (argc == 0)
		return (xstrdup(""));

	len = 0;
	buf = NULL;

	for (i = 0; i < argc; i++) {
		len += strlen(argv[i]) + 1;
		buf = xrealloc(buf, len);

		if (i == 0)
			*buf = '\0';
		else
			strlcat(buf, " ", len);
		strlcat(buf, argv[i], len);
	}
	return (buf);
}

struct cmd *
cmd_parse(int argc, char **argv, const char *file, u_int line, char **cause)
{
	const struct cmd_entry **entryp, *entry;
	struct cmd		*cmd;
	struct args		*args;
	char			 s[BUFSIZ];
	int			 ambiguous = 0;

	*cause = NULL;
	if (argc == 0) {
		xasprintf(cause, "no command");
		return (NULL);
	}

	entry = NULL;
	for (entryp = cmd_table; *entryp != NULL; entryp++) {
		if ((*entryp)->alias != NULL &&
		    strcmp((*entryp)->alias, argv[0]) == 0) {
			ambiguous = 0;
			entry = *entryp;
			break;
		}

		if (strncmp((*entryp)->name, argv[0], strlen(argv[0])) != 0)
			continue;
		if (entry != NULL)
			ambiguous = 1;
		entry = *entryp;

		/* Bail now if an exact match. */
		if (strcmp(entry->name, argv[0]) == 0)
			break;
	}
	if (ambiguous)
		goto ambiguous;
	if (entry == NULL) {
		xasprintf(cause, "unknown command: %s", argv[0]);
		return (NULL);
	}

	args = args_parse(entry->args_template, argc, argv);
	if (args == NULL)
		goto usage;
	if (entry->args_lower != -1 && args->argc < entry->args_lower)
		goto usage;
	if (entry->args_upper != -1 && args->argc > entry->args_upper)
		goto usage;

	cmd = xcalloc(1, sizeof *cmd);
	cmd->entry = entry;
	cmd->args = args;

	if (file != NULL)
		cmd->file = xstrdup(file);
	cmd->line = line;

	return (cmd);

ambiguous:
	*s = '\0';
	for (entryp = cmd_table; *entryp != NULL; entryp++) {
		if (strncmp((*entryp)->name, argv[0], strlen(argv[0])) != 0)
			continue;
		if (strlcat(s, (*entryp)->name, sizeof s) >= sizeof s)
			break;
		if (strlcat(s, ", ", sizeof s) >= sizeof s)
			break;
	}
	s[strlen(s) - 2] = '\0';
	xasprintf(cause, "ambiguous command: %s, could be: %s", argv[0], s);
	return (NULL);

usage:
	if (args != NULL)
		args_free(args);
	xasprintf(cause, "usage: %s %s", entry->name, entry->usage);
	return (NULL);
}

void
cmd_clear_state(struct cmd_state *state)
{
	state->c = NULL;

	state->tflag.s = NULL;
	state->tflag.wl = NULL;
	state->tflag.wp = NULL;
	state->tflag.idx = -1;

	state->sflag.s = NULL;
	state->sflag.wl = NULL;
	state->sflag.wp = NULL;
	state->sflag.idx = -1;
}

struct client *
cmd_get_state_client(struct cmd_q *cmdq, int quiet)
{
	struct cmd	*cmd = cmdq->cmd;
	struct args	*args = cmd->args;

	switch (cmd->entry->flags & (CMD_PREP_CLIENT_C|CMD_PREP_CLIENT_T)) {
	case 0:
		return (cmd_find_client(cmdq, NULL, 1));
	case CMD_PREP_CLIENT_C:
		return (cmd_find_client(cmdq, args_get(args, 'c'), quiet));
	case CMD_PREP_CLIENT_T:
		return (cmd_find_client(cmdq, args_get(args, 't'), quiet));
	default:
		log_fatalx("both -t and -c for %s", cmd->entry->name);
	}
}

int
cmd_set_state_tflag(struct cmd *cmd, struct cmd_q *cmdq)
{
	struct cmd_state	*state = &cmdq->state;
	const char		*tflag;
	int			 flags = cmd->entry->flags, everything = 0;
	int			 prefer = !!(flags & CMD_PREP_PREFERUNATTACHED);
	struct session		*s;
	struct window		*w;
	struct winlink		*wl;
	struct window_pane	*wp;

	/*
	 * If the command wants something for -t and no -t argument is present,
	 * use the base command's -t instead.
	 */
	tflag = args_get(cmd->args, 't');
	if (tflag == NULL) {
		if ((flags & CMD_PREP_ALL_T) == 0)
			return (0); /* doesn't care about -t */
		cmd = cmdq->cmd;
		everything = 1;
		tflag = args_get(cmd->args, 't');
	}

	/*
	 * If no -t and the current command is allowed to fail, just skip to
	 * fill in as much we can. Otherwise continue and let cmd_find_* fail.
	 */
	if (tflag == NULL && (flags & CMD_PREP_CANFAIL))
		goto complete_everything;

	/* Fill in state using command (current or base) flags. */
	switch (cmd->entry->flags & CMD_PREP_ALL_T) {
	case 0:
		break;
	case CMD_PREP_SESSION_T|CMD_PREP_PANE_T:
		if (tflag != NULL && tflag[strcspn(tflag, ":.")] != '\0') {
			state->tflag.wl = cmd_find_pane(cmdq, tflag,
			    &state->tflag.s, &state->tflag.wp);
			if (state->tflag.wl == NULL)
				return (-1);
		} else {
			state->tflag.s = cmd_find_session(cmdq, tflag, prefer);
			if (state->tflag.s == NULL)
				return (-1);

			s = state->tflag.s;
			if ((w = window_find_by_id_str(tflag)) != NULL)
				wp = w->active;
			else if ((wp = window_pane_find_by_id_str(tflag)) != NULL)
				w = wp->window;
			wl = winlink_find_by_window(&s->windows, w);
			if (wl != NULL) {
				state->tflag.wl = wl;
				state->tflag.wp = wp;
			}
		}
		break;
	case CMD_PREP_SESSION_RENUM_T|CMD_PREP_INDEX_T:
		state->tflag.s = cmd_find_session(cmdq, tflag, prefer);
		if (state->tflag.s == NULL) {
			state->tflag.idx = cmd_find_index(cmdq, tflag, &state->tflag.s);
			if (state->tflag.idx == -2)
				return (-1);
		}
		break;
	case CMD_PREP_SESSION_T:
		state->tflag.s = cmd_find_session(cmdq, tflag, prefer);
		if (state->tflag.s == NULL)
			return (-1);
		break;
	case CMD_PREP_WINDOW_T:
		state->tflag.wl = cmd_find_window(cmdq, tflag, &state->tflag.s);
		if (state->tflag.wl == NULL)
			return (-1);
		break;
	case CMD_PREP_PANE_T:
		state->tflag.wl = cmd_find_pane(cmdq, tflag, &state->tflag.s,
		    &state->tflag.wp);
		if (state->tflag.wl == NULL)
			return (-1);
		break;
	case CMD_PREP_INDEX_T:
		state->tflag.idx = cmd_find_index(cmdq, tflag, &state->tflag.s);
		if (state->tflag.idx == -2)
			return (-1);
		break;
	default:
		log_fatalx("too many -t for %s", cmd->entry->name);
	}

	/*
	 * If this is still the current command, it wants what it asked for and
	 * nothing more. If it's the base command, fill in as much as possible
	 * because the current command may have different flags.
	 */
	if (!everything)
		return (0);

complete_everything:
	if (state->tflag.s == NULL) {
		if (state->c != NULL)
			state->tflag.s = state->c->session;
		if (state->tflag.s == NULL)
			state->tflag.s = cmd_find_current(cmdq);
		if (state->tflag.s == NULL) {
			if (flags & CMD_PREP_CANFAIL)
				return (0);

			cmdq_error(cmdq, "no current session");
			return (-1);
		}
	}
	if (state->tflag.wl == NULL)
		state->tflag.wl = cmd_find_window(cmdq, tflag, &state->tflag.s);
	if (state->tflag.wp == NULL) {
		state->tflag.wl = cmd_find_pane(cmdq, tflag, &state->tflag.s,
		    &state->tflag.wp);
	}

	return (0);
}

int
cmd_set_state_sflag(struct cmd *cmd, struct cmd_q *cmdq)
{
	struct cmd_state	*state = &cmdq->state;
	const char		*sflag;
	int			 flags = cmd->entry->flags, everything = 0;
	int			 prefer = !!(flags & CMD_PREP_PREFERUNATTACHED);
	struct session		*s;
	struct window		*w;
	struct winlink		*wl;
	struct window_pane	*wp;

	/*
	 * If the command wants something for -s and no -s argument is present,
	 * use the base command's -s instead.
	 */
	sflag = args_get(cmd->args, 's');
	if (sflag == NULL) {
		if ((flags & CMD_PREP_ALL_S) == 0)
			return (0); /* doesn't care about -s */
		cmd = cmdq->cmd;
		everything = 1;
		sflag = args_get(cmd->args, 's');
	}

	/*
	 * If no -s and the current command is allowed to fail, just skip to
	 * fill in as much we can. Otherwise continue and let cmd_find_* fail.
	 */
	if (sflag == NULL && (flags & CMD_PREP_CANFAIL))
		goto complete_everything;

	/* Fill in state using command (current or base) flags. */
	switch (cmd->entry->flags & CMD_PREP_ALL_S) {
	case 0:
		break;
	case CMD_PREP_SESSION_S|CMD_PREP_PANE_S:
		if (sflag != NULL && sflag[strcspn(sflag, ":.")] != '\0') {
			state->sflag.wl = cmd_find_pane(cmdq, sflag,
			    &state->sflag.s, &state->sflag.wp);
			if (state->sflag.wl == NULL)
				return (-1);
		} else {
			state->sflag.s = cmd_find_session(cmdq, sflag, prefer);
			if (state->sflag.s == NULL)
				return (-1);

			s = state->sflag.s;
			if ((w = window_find_by_id_str(sflag)) != NULL)
				wp = w->active;
			else if ((wp = window_pane_find_by_id_str(sflag)) != NULL)
				w = wp->window;
			wl = winlink_find_by_window(&s->windows, w);
			if (wl != NULL) {
				state->sflag.wl = wl;
				state->sflag.wp = wp;
			}
		}
		break;
	case CMD_PREP_SESSION_S:
		state->sflag.s = cmd_find_session(cmdq, sflag, prefer);
		if (state->sflag.s == NULL)
			return (-1);
		break;
	case CMD_PREP_WINDOW_S:
		state->sflag.wl = cmd_find_window(cmdq, sflag, &state->sflag.s);
		if (state->sflag.wl == NULL)
			return (-1);
		break;
	case CMD_PREP_PANE_S:
		state->sflag.wl = cmd_find_pane(cmdq, sflag, &state->sflag.s,
		    &state->sflag.wp);
		if (state->sflag.wl == NULL)
			return (-1);
		break;
	case CMD_PREP_INDEX_S:
		state->sflag.idx = cmd_find_index(cmdq, sflag, &state->sflag.s);
		if (state->sflag.idx == -2)
			return (-1);
		break;
	default:
		log_fatalx("too many -s for %s", cmd->entry->name);
	}

	/*
	 * If this is still the current command, it wants what it asked for and
	 * nothing more. If it's the base command, fill in as much as possible
	 * because the current command may have different flags.
	 */
	if (!everything)
		return (0);

complete_everything:
	if (state->sflag.s == NULL) {
		if (state->c != NULL)
			state->sflag.s = state->c->session;

		if (state->sflag.s == NULL)
			state->sflag.s = cmd_find_current(cmdq);

		if (state->sflag.s == NULL) {
			if (flags & CMD_PREP_CANFAIL)
				return (0);
			cmdq_error(cmdq, "no current session");
			return (-1);
		}
	}
	if (state->sflag.wl == NULL) {
		state->sflag.wl = cmd_find_pane(cmdq, sflag, &state->sflag.s,
		    &state->sflag.wp);
	}
	if (state->sflag.wp == NULL) {
		state->sflag.wl = cmd_find_pane(cmdq, sflag, &state->sflag.s,
		    &state->sflag.wp);
	}
	return (0);
}

int
cmd_prepare_state(struct cmd *cmd, struct cmd_q *cmdq)
{
	struct cmd_state	*state = &cmdq->state;
	struct args		*args = cmd->args;
	const char		*cflag;
	const char		*tflag;
	char                     tmp[BUFSIZ];
	int			 error;

	cmd_print(cmd, tmp, sizeof tmp);
	log_debug("preparing state for: %s (client %d)", tmp,
	    cmdq->client != NULL ? cmdq->client->ibuf.fd : -1);

	/* Start with an empty state. */
	cmd_clear_state(state);

	/*
	 * If the command wants a client and provides -c or -t, use it. If not,
	 * try the base command instead via cmd_get_state_client. No client is
	 * allowed if no flags, otherwise it must be available.
	 */
	switch (cmd->entry->flags & (CMD_PREP_CLIENT_C|CMD_PREP_CLIENT_T)) {
	case 0:
		state->c = cmd_get_state_client(cmdq, 1);
		break;
	case CMD_PREP_CLIENT_C:
		cflag = args_get(args, 'c');
		if (cflag == NULL)
			state->c = cmd_get_state_client(cmdq, 0);
		else
			state->c = cmd_find_client(cmdq, cflag, 0);
		if (state->c == NULL)
			return (-1);
		break;
	case CMD_PREP_CLIENT_T:
		tflag = args_get(args, 't');
		if (tflag == NULL)
			state->c = cmd_get_state_client(cmdq, 0);
		else
			state->c = cmd_find_client(cmdq, tflag, 0);
		if (state->c == NULL)
			return (-1);
		break;
	default:
		log_fatalx("both -c and -t for %s", cmd->entry->name);
	}

	error = cmd_set_state_tflag(cmd, cmdq);
	if (error == 0)
		error = cmd_set_state_sflag(cmd, cmdq);
	return (error);
}

size_t
cmd_print(struct cmd *cmd, char *buf, size_t len)
{
	size_t	off, used;

	off = xsnprintf(buf, len, "%s ", cmd->entry->name);
	if (off + 1 < len) {
		used = args_print(cmd->args, buf + off, len - off - 1);
		if (used == 0)
			off--;
		else
			off += used;
		buf[off] = '\0';
	}
	return (off);
}

/* Adjust current mouse position for a pane. */
int
cmd_mouse_at(struct window_pane *wp, struct mouse_event *m, u_int *xp,
    u_int *yp, int last)
{
	u_int	x, y;

	if (last) {
		x = m->lx;
		y = m->ly;
	} else {
		x = m->x;
		y = m->y;
	}

	if (m->statusat == 0 && y > 0)
		y--;
	else if (m->statusat > 0 && y >= (u_int)m->statusat)
		y = m->statusat - 1;

	if (x < wp->xoff || x >= wp->xoff + wp->sx)
		return (-1);
	if (y < wp->yoff || y >= wp->yoff + wp->sy)
		return (-1);

	*xp = x - wp->xoff;
	*yp = y - wp->yoff;
	return (0);
}

/* Get current mouse window if any. */
struct winlink *
cmd_mouse_window(struct mouse_event *m, struct session **sp)
{
	struct session	*s;
	struct window	*w;

	if (!m->valid || m->s == -1 || m->w == -1)
		return (NULL);
	if ((s = session_find_by_id(m->s)) == NULL)
		return (NULL);
	if ((w = window_find_by_id(m->w)) == NULL)
		return (NULL);

	if (sp != NULL)
		*sp = s;
	return (winlink_find_by_window(&s->windows, w));
}

/* Get current mouse pane if any. */
struct window_pane *
cmd_mouse_pane(struct mouse_event *m, struct session **sp,
    struct winlink **wlp)
{
	struct winlink		*wl;
	struct window_pane     	*wp;

	if ((wl = cmd_mouse_window(m, sp)) == NULL)
		return (NULL);
	if ((wp = window_pane_find_by_id(m->wp)) == NULL)
		return (NULL);
	if (!window_has_pane(wl->window, wp))
		return (NULL);

	if (wlp != NULL)
		*wlp = wl;
	return (wp);
}

/* Replace the first %% or %idx in template by s. */
char *
cmd_template_replace(const char *template, const char *s, int idx)
{
	char		 ch, *buf;
	const char	*ptr;
	int		 replaced;
	size_t		 len;

	if (strchr(template, '%') == NULL)
		return (xstrdup(template));

	buf = xmalloc(1);
	*buf = '\0';
	len = 0;
	replaced = 0;

	ptr = template;
	while (*ptr != '\0') {
		switch (ch = *ptr++) {
		case '%':
			if (*ptr < '1' || *ptr > '9' || *ptr - '0' != idx) {
				if (*ptr != '%' || replaced)
					break;
				replaced = 1;
			}
			ptr++;

			len += strlen(s);
			buf = xrealloc(buf, len + 1);
			strlcat(buf, s, len + 1);
			continue;
		}
		buf = xrealloc(buf, len + 2);
		buf[len++] = ch;
		buf[len] = '\0';
	}

	return (buf);
}
