/* $OpenBSD$ */

/*
 * Copyright (c) 2009 Nicholas Marriott <nicm@users.sourceforge.net>
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

#include <event.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "tmux.h"

struct alerts	 alerts;
RB_GENERATE(alerts, alert, entry, alert_cmp);

int	 server_window_check_bell(struct session *, struct winlink *);
int	 server_window_check_activity(struct session *, struct winlink *);
int	 server_window_check_silence(struct session *, struct winlink *);
void	 server_window_run_hooks(struct session *, struct winlink *);
void	 ring_bell(struct session *);

struct window_flag_hook {
	int		 flag;
	const char	*name;
};

const struct window_flag_hook	 window_flag_hook_names[] = {
	{WINLINK_BELL, "on-window-bell"},
	{WINLINK_ACTIVITY, "on-window-activity"},
	{WINLINK_SILENCE, "on-window-silence"},
};

int
alert_cmp(struct alert *a1, struct alert *a2)
{
	return strcmp(a1->name, a2->name);
}

struct alert *
alert_new(void)
{
	struct alert	*al;

	al = xcalloc(1, sizeof *al);
	return (al);
}

void
alert_free(struct alert *al)
{
	struct winlink	*wl,* wl1;

	RB_REMOVE(alerts, &alerts, al);
	RB_FOREACH_SAFE(wl, winlinks, &al->windows, wl1)
		winlink_remove(&al->windows, wl);
	free(al);
}

/* Window functions that need to happen every loop. */
void
server_window_loop(void)
{
	struct window	*w;
	struct winlink	*wl;
	struct session	*s;
	u_int		 i;

	for (i = 0; i < ARRAY_LENGTH(&windows); i++) {
		w = ARRAY_ITEM(&windows, i);
		if (w == NULL)
			continue;

		RB_FOREACH(s, sessions, &sessions) {
			wl = session_has(s, w);
			if (wl == NULL)
				continue;

			if (server_window_check_bell(s, wl) ||
			    server_window_check_activity(s, wl) ||
			    server_window_check_silence(s, wl)) {
				server_window_run_hooks(s, wl);
				server_status_session(s);
			}
		}
	}
}

/* Run any hooks associated with monitored events. */
void
server_window_run_hooks(struct session *s, struct winlink *wl)
{
	struct hooks	*hooks;
	struct alert	*al, al_find;
	struct winlink	*wl_new;
	const char	*hook_name = NULL;
	u_int		 i;
	int		 flag;

	for (i = 0; i < nitems(window_flag_hook_names); i++) {
		if (wl->flags & window_flag_hook_names[i].flag) {
			hook_name = window_flag_hook_names[i].name;
			flag = window_flag_hook_names[i].flag;
			break;
		}
	}

	if (hook_name == NULL)
		return;

	al_find.name = hook_name;
	if ((al = RB_FIND(alerts, &alerts, &al_find)) == NULL) {
		al = alert_new();
		al->name = hook_name;
		al->flag = flag;
		al->s = s;
		RB_INIT(&al->windows);
	}
	wl_new = winlink_add(&al->windows, wl->idx);
	winlink_set_window(wl_new, wl->window);
	wl_new->flags |= wl->flags & WINLINK_ALERTFLAGS;
	RB_INSERT(alerts, &alerts, al);

	hooks = &s->hooks;
	cmdq_hooks_run(hooks, NULL, hook_name, NULL);
	alert_free(al);
}


/* Check for bell in window. */
int
server_window_check_bell(struct session *s, struct winlink *wl)
{
	struct client	*c;
	struct window	*w = wl->window;
	u_int		 i;
	int		 action, visual;

	if (!(w->flags & WINDOW_BELL) || wl->flags & WINLINK_BELL)
		return (0);
	if (s->curw != wl)
		wl->flags |= WINLINK_BELL;
	if (s->curw->window == w)
		w->flags &= ~WINDOW_BELL;

	visual = options_get_number(&s->options, "visual-bell");
	action = options_get_number(&s->options, "bell-action");
	if (action == BELL_NONE)
		return (0);
	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);
		if (c == NULL || c->session != s || c->flags & CLIENT_CONTROL)
			continue;
		if (!visual) {
			if (c->session->curw->window == w || action == BELL_ANY)
				tty_bell(&c->tty);
			continue;
		}
		if (c->session->curw->window == w)
			status_message_set(c, "Bell in current window");
		else if (action == BELL_ANY)
			status_message_set(c, "Bell in window %u", wl->idx);
	}

	return (1);
}

/* Check for activity in window. */
int
server_window_check_activity(struct session *s, struct winlink *wl)
{
	struct client	*c;
	struct window	*w = wl->window;
	u_int		 i;

	if (s->curw->window == w)
		w->flags &= ~WINDOW_ACTIVITY;

	if (!(w->flags & WINDOW_ACTIVITY) || wl->flags & WINLINK_ACTIVITY)
		return (0);
	if (s->curw == wl)
		return (0);

	if (!options_get_number(&w->options, "monitor-activity"))
		return (0);

	if (options_get_number(&s->options, "bell-on-alert"))
		ring_bell(s);
	wl->flags |= WINLINK_ACTIVITY;

	if (options_get_number(&s->options, "visual-activity")) {
		for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
			c = ARRAY_ITEM(&clients, i);
			if (c == NULL || c->session != s)
				continue;
			status_message_set(c, "Activity in window %u", wl->idx);
		}
	}

	return (1);
}

/* Check for silence in window. */
int
server_window_check_silence(struct session *s, struct winlink *wl)
{
	struct client	*c;
	struct window	*w = wl->window;
	struct timeval	 timer;
	u_int		 i;
	int		 silence_interval, timer_difference;

	if (!(w->flags & WINDOW_SILENCE) || wl->flags & WINLINK_SILENCE)
		return (0);

	if (s->curw == wl) {
		/*
		 * Reset the timer for this window if we've focused it.  We
		 * don't want the timer tripping as soon as we've switched away
		 * from this window.
		 */
		if (gettimeofday(&w->silence_timer, NULL) != 0)
			fatal("gettimeofday failed.");

		return (0);
	}

	silence_interval = options_get_number(&w->options, "monitor-silence");
	if (silence_interval == 0)
		return (0);

	if (gettimeofday(&timer, NULL) != 0)
		fatal("gettimeofday");
	timer_difference = timer.tv_sec - w->silence_timer.tv_sec;
	if (timer_difference <= silence_interval)
		return (0);

	if (options_get_number(&s->options, "bell-on-alert"))
		ring_bell(s);
	wl->flags |= WINLINK_SILENCE;

	if (options_get_number(&s->options, "visual-silence")) {
		for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
			c = ARRAY_ITEM(&clients, i);
			if (c == NULL || c->session != s)
				continue;
			status_message_set(c, "Silence in window %u", wl->idx);
		}
	}

	return (1);
}

/* Ring terminal bell. */
void
ring_bell(struct session *s)
{
	struct client	*c;
	u_int		 i;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);
		if (c == NULL)
			continue;
		if (c->flags & CLIENT_CONTROL)
			continue;
		if (c->session == s)
			tty_bell(&c->tty);
	}
}
