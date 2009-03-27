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

#include "i18n.h"
#include "screen.h"
#include "screen_utils.h"
#include "charset.h"
#include "utils.h"

#include <glib/gprintf.h>
#include <string.h>

static list_window_t *lw;

static const struct mpd_song *next_song;

static struct {
	struct mpd_song *selected_song;
	struct mpd_song *played_song;
	GPtrArray *lines;
} current;

static void
screen_song_clear(void)
{
	for (guint i = 0; i < current.lines->len; ++i)
		g_free(g_ptr_array_index(current.lines, i));

	g_ptr_array_set_size(current.lines, 0);

	if (current.selected_song != NULL) {
		mpd_freeSong(current.selected_song);
		current.selected_song = NULL;
	}
	if (current.played_song != NULL) {
		mpd_freeSong(current.played_song);
		current.played_song = NULL;
	}
}

static void
screen_song_paint(void);

/**
 * Repaint and update the screen.
 */
static void
screen_song_repaint(void)
{
	screen_song_paint();
	wrefresh(lw->w);
}

static const char *
screen_song_list_callback(unsigned idx, G_GNUC_UNUSED bool *highlight,
			  G_GNUC_UNUSED void *data)
{
	static char buffer[256];
	char *value;

	if (idx >= current.lines->len)
		return NULL;

	value = utf8_to_locale(g_ptr_array_index(current.lines, idx));
	g_strlcpy(buffer, value, sizeof(buffer));
	g_free(value);

	return buffer;
}


static void
screen_song_init(WINDOW *w, int cols, int rows)
{
	/* We will need at least 10 lines, so this saves 10 reallocations :) */
	current.lines = g_ptr_array_sized_new(10);
	lw = list_window_init(w, cols, rows);
	lw->hide_cursor = true;
}

static void
screen_song_exit(void)
{
	list_window_free(lw);

	screen_song_clear();

	g_ptr_array_free(current.lines, TRUE);
	current.lines = NULL;
}

static void
screen_song_resize(int cols, int rows)
{
	lw->cols = cols;
	lw->rows = rows;
}

static const char *
screen_song_title(G_GNUC_UNUSED char *str, G_GNUC_UNUSED size_t size)
{
	return _("Song viewer");
}

static void
screen_song_paint(void)
{
	list_window_paint(lw, screen_song_list_callback, NULL);
}

/* Appends a line with a fixed width for the label column
 * Handles NULL strings gracefully */
static void
screen_song_append(const char *label, const char *value, unsigned label_col)
{
	int value_col, linebreaks, entry_size, label_size;
	int i, k;
	gchar *entry, *entry_iter;
	const gchar *value_iter;

	assert(label != NULL);
	assert(g_utf8_validate(label, -1, NULL));

	if (value != NULL) {
		assert(g_utf8_validate(value, -1, NULL));
		/* +2 for ': ' */
		label_col += 2;
		value_col = lw->cols - label_col;
		/* calculate the number of required linebreaks */
		linebreaks = (utf8_width(value) - 1) / value_col + 1;
		value_iter = value;
		label_size = strlen(label) + label_col - utf8_width(label);
		entry_size = label_size + strlen(value) + 2;

		for (i = 0; i < linebreaks; ++i)
		{
			entry = g_malloc(entry_size);
			if (i == 0) {
				entry_iter = entry + g_sprintf(entry, "%s: ", label);
				/* fill the label column with whitespaces */
				for ( ; entry_iter < entry + label_size; ++entry_iter)
					*entry_iter = ' ';
			}
			else {
				entry_iter = entry;
				/* fill the label column with whitespaces */
				for ( ; entry_iter < entry + label_col; ++entry_iter)
					*entry_iter = ' ';
			}
			/* skip whitespaces */
			while (g_ascii_isspace(*value_iter)) ++value_iter;
			k = 0;
			while (value_iter && k < value_col)
			{
				g_utf8_strncpy(entry_iter, value_iter, 1);
				value_iter = g_utf8_find_next_char(value_iter, NULL);
				entry_iter = g_utf8_find_next_char(entry_iter, NULL);
				++k;
			}
			*entry_iter = '\0';
			g_ptr_array_add(current.lines, entry);
		}
	}
}

static void
screen_song_add_song(const struct mpd_song *song, const mpdclient_t *c)
{
	unsigned i, max_label_width;
	enum label {
		ARTIST, TITLE, ALBUM, COMPOSER, NAME, DISC, TRACK,
		DATE, GENRE, COMMENT, PATH, BITRATE
	};
	const char *labels[] = { [ARTIST] = _("Artist"),
		[TITLE] = _("Title"),
		[ALBUM] = _("Album"),
		[COMPOSER] = _("Composer"),
		[NAME] = _("Name"),
		[DISC] = _("Disc"),
		[TRACK] = _("Track"),
		[DATE] = _("Date"),
		[GENRE] = _("Genre"),
		[COMMENT] = _("Comment"),
		[PATH] = _("Path"),
		[BITRATE] = _("Bitrate"),
	};
	/* Determine the width of the longest label */
	max_label_width = utf8_width(labels[0]);
	for (i = 1; i < G_N_ELEMENTS(labels); ++i) {
		if (utf8_width(labels[i]) > max_label_width)
			max_label_width = utf8_width(labels[i]);
	}

	assert(song != NULL);

	screen_song_append(labels[ARTIST], song->artist, max_label_width);
	screen_song_append(labels[TITLE], song->title, max_label_width);
	screen_song_append(labels[ALBUM], song->album, max_label_width);
	screen_song_append(labels[COMPOSER], song->composer, max_label_width);
	screen_song_append(labels[NAME], song->name, max_label_width);
	screen_song_append(labels[DISC], song->disc, max_label_width);
	screen_song_append(labels[TRACK], song->track, max_label_width);
	screen_song_append(labels[DATE], song->date, max_label_width);
	screen_song_append(labels[GENRE], song->genre, max_label_width);
	screen_song_append(labels[COMMENT], song->comment, max_label_width);
	screen_song_append(labels[PATH], song->file, max_label_width);
	if (c->status != NULL && c->song != NULL &&
			 g_strcmp0(c->song->file, song->file) == 0 &&
			(c->status->state == MPD_STATUS_STATE_PLAY ||
			 c->status->state == MPD_STATUS_STATE_PAUSE) ) {
		char buf[16];
		g_snprintf(buf, sizeof(buf), _("%d kbps"), c->status->bitRate);
		screen_song_append(labels[BITRATE], buf, max_label_width);
	}
}

static void
screen_song_add_stats(const mpdclient_t *c)
{
	unsigned i, max_label_width;
	char buf[64];
	char *duration;
	GDate *date;
	enum label {
		ARTISTS, ALBUMS, SONGS, UPTIME,
		DBUPTIME, PLAYTIME, DBPLAYTIME
	};
	const char *labels[] = { [ARTISTS] = _("Number of artists"),
		[ALBUMS] = _("Number of albums"),
		[SONGS] = _("Number of songs"),
		[UPTIME] = _("Uptime"),
		[DBUPTIME] =_("DB last updated"),
		[PLAYTIME] = _("Playtime"),
		[DBPLAYTIME] = _("DB playtime")
	};
	mpd_Stats *mpd_stats = NULL;
	if (c->connection != NULL) {
		mpd_sendStatsCommand(c->connection);
		mpd_stats = mpd_getStats(c->connection);
	}

	if (mpd_stats != NULL) {
		/* Determine the width of the longest label */
		max_label_width = utf8_width(labels[0]);
		for (i = 1; i < G_N_ELEMENTS(labels); ++i) {
			if (utf8_width(labels[i]) > max_label_width)
				max_label_width = utf8_width(labels[i]);
		}

		g_ptr_array_add(current.lines, g_strdup(_("MPD statistics")) );
		g_snprintf(buf, sizeof(buf), "%d", mpd_stats->numberOfArtists);
		screen_song_append(labels[ARTISTS], buf, max_label_width);
		g_snprintf(buf, sizeof(buf), "%d", mpd_stats->numberOfAlbums);
		screen_song_append(labels[ALBUMS], buf, max_label_width);
		g_snprintf(buf, sizeof(buf), "%d", mpd_stats->numberOfSongs);
		screen_song_append(labels[SONGS], buf, max_label_width);
		duration = time_seconds_to_durationstr(mpd_stats->dbPlayTime);
		screen_song_append(labels[DBPLAYTIME], duration, max_label_width);
		g_free(duration);
		duration = time_seconds_to_durationstr(mpd_stats->playTime);
		screen_song_append(labels[PLAYTIME], duration, max_label_width);
		g_free(duration);
		duration = time_seconds_to_durationstr(mpd_stats->uptime);
		screen_song_append(labels[UPTIME], duration, max_label_width);
		g_free(duration);
		date = g_date_new();
		g_date_set_time_t(date, mpd_stats->dbUpdateTime);
		g_date_strftime(buf, sizeof(buf), "%x", date);
		screen_song_append(labels[DBUPTIME], buf, max_label_width);
		g_date_free(date);
	}
}

static void
screen_song_update(mpdclient_t *c)
{
	/* Clear all lines */
	for (guint i = 0; i < current.lines->len; ++i)
		g_free(g_ptr_array_index(current.lines, i));
	g_ptr_array_set_size(current.lines, 0);

	/* If a song was selected before the song screen was opened */
	if (next_song != NULL) {
		assert(current.selected_song == NULL);
		current.selected_song = mpd_songDup(next_song);
		next_song = NULL;
	}

	if (current.selected_song != NULL &&
			(c->song == NULL ||
			 g_strcmp0(current.selected_song->file, c->song->file) != 0 ||
			 c->status == NULL ||
			(c->status->state != MPD_STATUS_STATE_PLAY &&
			 c->status->state != MPD_STATUS_STATE_PAUSE)) ) {
		g_ptr_array_add(current.lines, g_strdup(_("Selected song")) );
		screen_song_add_song(current.selected_song, c);
		g_ptr_array_add(current.lines, g_strdup("\0"));
	}

	if (c->song != NULL && c->status != NULL &&
			(c->status->state == MPD_STATUS_STATE_PLAY ||
			 c->status->state == MPD_STATUS_STATE_PAUSE) ) {
		if (current.played_song != NULL) {
			mpd_freeSong(current.played_song);
		}
		current.played_song = mpd_songDup(c->song);
		g_ptr_array_add(current.lines, g_strdup(_("Currently playing song")));
		screen_song_add_song(current.played_song, c);
		g_ptr_array_add(current.lines, g_strdup("\0"));
	}

	/* Add some statistics about mpd */
	if (c->connection != NULL)
		screen_song_add_stats(c);

	screen_song_repaint();
}

static bool
screen_song_cmd(mpdclient_t *c, command_t cmd)
{
	if (list_window_scroll_cmd(lw, current.lines->len, cmd)) {
		screen_song_repaint();
		return true;
	}

	switch(cmd) {
	case CMD_LOCATE:
		if (current.selected_song != NULL) {
			screen_file_goto_song(c, current.selected_song);
			return true;
		}
		if (current.played_song != NULL) {
			screen_file_goto_song(c, current.played_song);
			return true;
		}

		return false;

#ifdef ENABLE_LYRICS_SCREEN
	case CMD_SCREEN_LYRICS:
		if (current.selected_song != NULL) {
			screen_lyrics_switch(c, current.selected_song);
			return true;
		}
		if (current.played_song != NULL) {
			screen_lyrics_switch(c, current.played_song);
			return true;
		}
		return false;

#endif

	case CMD_SCREEN_SWAP:
		if (current.selected_song != NULL)
			screen_swap(c, current.selected_song);
		else
		// No need to check if this is null - we'd pass null anyway
			screen_swap(c, current.played_song);
		return true;

	default:
		break;
	}

	if (screen_find(lw, current.lines->len,
			cmd, screen_song_list_callback, NULL)) {
		/* center the row */
		list_window_center(lw, current.lines->len, lw->selected);
		screen_song_repaint();
		return true;
	}

	return false;
}

const struct screen_functions screen_song = {
	.init = screen_song_init,
	.exit = screen_song_exit,
	.open = screen_song_update,
	.close = screen_song_clear,
	.resize = screen_song_resize,
	.paint = screen_song_paint,
	.update = screen_song_update,
	.cmd = screen_song_cmd,
	.get_title = screen_song_title,
};

void
screen_song_switch(mpdclient_t *c, const struct mpd_song *song)
{
	assert(song != NULL);
	assert(current.selected_song == NULL);
	assert(current.played_song == NULL);

	next_song = song;
	screen_switch(&screen_song, c);
}
