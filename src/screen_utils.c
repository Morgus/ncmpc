/* ncmpc (Ncurses MPD Client)
 * (c) 2004-2009 The Music Player Daemon Project
 * Project homepage: http://musicpd.org
 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "screen_utils.h"
#include "screen.h"
#include "mpdclient.h"
#include "config.h"
#include "i18n.h"
#include "options.h"
#include "colors.h"
#include "wreadln.h"
#ifndef NCMPC_H
#include "ncmpc.h"
#endif /* NCMPC_H */

#include <mpd/client.h>

#include <stdlib.h>
#include <unistd.h>

#define FIND_PROMPT  _("Find")
#define RFIND_PROMPT _("Find backward")
#define JUMP_PROMPT _("Jump")

void
screen_bell(void)
{
	if (options.audible_bell)
		beep();
	if (options.visible_bell)
		flash();
}

int
screen_getch(const char *prompt)
{
	WINDOW *w = screen.status_bar.window.w;
	int key = -1;

	colors_use(w, COLOR_STATUS_ALERT);
	werase(w);
	wmove(w, 0, 0);
	waddstr(w, prompt);

	echo();
	curs_set(1);

	while ((key = wgetch(w)) == ERR)
		;

#ifdef HAVE_GETMOUSE
	/* ignore mouse events */
	if (key == KEY_MOUSE)
		return screen_getch(prompt);
#endif

	noecho();
	curs_set(0);

	return key;
}

char *
screen_readln(const char *prompt,
	      const char *value,
	      GList **history,
	      GCompletion *gcmp)
{
	struct window *window = &screen.status_bar.window;
	WINDOW *w = window->w;
	char *line = NULL;

	wmove(w, 0,0);
	curs_set(1);
	colors_use(w, COLOR_STATUS_ALERT);
	line = wreadln(w, prompt, value, window->cols, history, gcmp);
	curs_set(0);
	return line;
}

char *
screen_read_password(const char *prompt)
{
	struct window *window = &screen.status_bar.window;
	WINDOW *w = window->w;
	char *ret;

	wmove(w, 0,0);
	curs_set(1);
	colors_use(w, COLOR_STATUS_ALERT);

	if (prompt == NULL)
		prompt = _("Password");
	ret = wreadln_masked(w, prompt, NULL, window->cols, NULL, NULL);

	curs_set(0);
	return ret;
}

/* query user for a string and find it in a list window */
int
screen_find(list_window_t *lw,
	    int rows,
	    command_t findcmd,
	    list_window_callback_fn_t callback_fn,
	    void *callback_data)
{
	int reversed = 0;
	bool found;
	const char *prompt = FIND_PROMPT;
	char *value = options.find_show_last_pattern ? (char *) -1 : NULL;

	if (findcmd == CMD_LIST_RFIND || findcmd == CMD_LIST_RFIND_NEXT) {
		prompt = RFIND_PROMPT;
		reversed = 1;
	}

	switch (findcmd) {
	case CMD_LIST_FIND:
	case CMD_LIST_RFIND:
		if (screen.findbuf) {
			g_free(screen.findbuf);
			screen.findbuf=NULL;
		}
		/* continue... */

	case CMD_LIST_FIND_NEXT:
	case CMD_LIST_RFIND_NEXT:
		if (!screen.findbuf)
			screen.findbuf=screen_readln(prompt,
						     value,
						     &screen.find_history,
						     NULL);

		if (screen.findbuf == NULL)
			return 1;

		found = reversed
			? list_window_rfind(lw,
					    callback_fn, callback_data,
					    screen.findbuf,
					    options.find_wrap,
					    options.bell_on_wrap,
					    rows)
			: list_window_find(lw,
					   callback_fn, callback_data,
					   screen.findbuf,
					   options.find_wrap,
					   options.bell_on_wrap);
		if (!found) {
			screen_status_printf(_("Unable to find \'%s\'"),
					     screen.findbuf);
			screen_bell();
		}
		return 1;
	default:
		break;
	}
	return 0;
}

/* query user for a string and jump to the entry
 * which begins with this string while the users types */
void
screen_jump(struct list_window *lw,
		list_window_callback_fn_t callback_fn,
		void *callback_data)
{
	char *search_str, *iter;
	const int WRLN_MAX_LINE_SIZE = 1024;
	int key = 65;
	command_t cmd;

	if (screen.findbuf) {
		g_free(screen.findbuf);
		screen.findbuf = NULL;
	}
	screen.findbuf = g_malloc0(WRLN_MAX_LINE_SIZE);
	/* In screen.findbuf is the whole string which is displayed in the status_window
	 * and search_str is the string the user entered (without the prompt) */
	search_str = screen.findbuf + g_snprintf(screen.findbuf, WRLN_MAX_LINE_SIZE, "%s: ", JUMP_PROMPT);
	iter = search_str;

	/* unfortunately wgetch returns "next/previous-page" not as an ascii-char */
	while(!g_ascii_iscntrl(key) && key != KEY_NPAGE && key != KEY_PPAGE) {
		key = screen_getch(screen.findbuf);
		/* if backspace or delete was pressed */
		if (key == KEY_BACKSPACE || key == 330) {
			int i;
			/* don't end the loop */
			key = 65;
			if (search_str <= g_utf8_find_prev_char(screen.findbuf, iter))
				iter = g_utf8_find_prev_char(screen.findbuf, iter);
			for (i = 0; *(iter + i) != '\0'; i++)
				*(iter + i) = '\0';
			continue;
		}
		else {
			*iter = key;
			if (iter < screen.findbuf + WRLN_MAX_LINE_SIZE - 3)
				++iter;
		}
		list_window_jump(lw, callback_fn, callback_data, search_str);
		/* repaint the list_window */
		list_window_paint(lw, callback_fn, callback_data);
		wrefresh(lw->w);
	}

	/* ncmpc should get the command */
	ungetch(key);
	if ((cmd=get_keyboard_command()) != CMD_NONE)
		do_input_event(cmd);
}

void
screen_display_completion_list(GList *list)
{
	static GList *prev_list = NULL;
	static guint prev_length = 0;
	static guint offset = 0;
	WINDOW *w = screen.main_window.w;
	guint length, y=0;

	length = g_list_length(list);
	if (list == prev_list && length == prev_length) {
		offset += screen.main_window.rows;
		if (offset >= length)
			offset = 0;
	} else {
		prev_list = list;
		prev_length = length;
		offset = 0;
	}

	colors_use(w, COLOR_STATUS_ALERT);
	while (y < screen.main_window.rows) {
		GList *item = g_list_nth(list, y+offset);

		wmove(w, y++, 0);
		wclrtoeol(w);
		if (item) {
			gchar *tmp = g_strdup(item->data);
			waddstr(w, g_basename(tmp));
			g_free(tmp);
		}
	}

	wrefresh(w);
	doupdate();
	colors_use(w, COLOR_LIST);
}

#ifndef NCMPC_MINI
void
set_xterm_title(const char *format, ...)
{
	/* the current xterm title exists under the WM_NAME property */
	/* and can be retrieved with xprop -id $WINDOWID */

	if (options.enable_xterm_title) {
		if (g_getenv("WINDOWID")) {
			char *msg;
			va_list ap;

			va_start(ap,format);
			msg = g_strdup_vprintf(format,ap);
			va_end(ap);
			printf("%c]0;%s%c", '\033', msg, '\007');
			g_free(msg);
		} else
			options.enable_xterm_title = FALSE;
	}
}
#endif
