/*
 * Leap-second aware console clock
 *
 * Copyright (C) 2015  Miroslav Lichvar <mlichvar@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <sys/time.h>
#include <sys/timex.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ncurses.h>

#define CLOCK_COLS 35
#define CLOCK_LINES 7

static int debug;

int get_tai_offset(time_t utc) {
	struct tm tm;
	int offset;

	setenv("TZ", "posix/UTC", 1);
	tzset();
	tm = *gmtime(&utc);
	setenv("TZ", "right/UTC", 1);
	tzset();
	offset = mktime(&tm) - utc + 10;
	unsetenv("TZ");
	tzset();

	return offset;
}

void print_border(int line, int col) {
	int i;

	for (i = 1; i < CLOCK_COLS; i++) {
		mvaddch(line, col + i, ACS_HLINE);
		mvaddch(line + CLOCK_LINES, col + i, ACS_HLINE);
	}
	for (i = 1; i < CLOCK_LINES; i++) {
		mvaddch(line + i, col, ACS_VLINE);
		mvaddch(line + i, col + CLOCK_COLS, ACS_VLINE);
	}
	mvaddch(line, col, ACS_ULCORNER);
	mvaddch(line, col + CLOCK_COLS, ACS_URCORNER);
	mvaddch(line + CLOCK_LINES, col, ACS_LLCORNER);
	mvaddch(line + CLOCK_LINES, col + CLOCK_COLS, ACS_LRCORNER);
}

void print_time(int line, int col, const char *label, struct timeval *tv, int local, int leap) {
	char buf1[20], buf2[100];
	struct tm tm;
	time_t t;
	int dsecs;

	dsecs = tv->tv_usec / 100000;

	t = tv->tv_sec;
	tm = local ? *localtime(&t) : *gmtime(&t);
	if (leap && tm.tm_sec == 59)
		tm.tm_sec++;

	if (!label) {
		strftime(buf1, sizeof (buf1), "%Z", &tm);
		label = buf1;
	}
	strftime(buf2, sizeof (buf2), "%Y-%m-%d %H:%M:%S", &tm);

	if (!debug)
		mvprintw(line, col, "%-7s: %s.%01d", label, buf2, dsecs);
	else
		printf("%-7s: %s.%01d\n", label, buf2, dsecs);
}

double diff_tv(struct timeval *tv1, struct timeval *tv2) {
	return (tv1->tv_sec - tv2->tv_sec) + (tv1->tv_usec - tv2->tv_usec) / 1e6;
}

int main(int argc, char **argv) {
	WINDOW *win;
	struct timeval tv_system, last_tv_system, tv_utc, tv_tai;
	struct timex timex;
	int tai_offset, last_tai_offset, ch, leap, step, slew, tick, slew_tick;
	int col, line;
	double diff;

	debug = argc > 1 && !strcmp(argv[1], "-d");

	if (!debug) {
		win = initscr();
		cbreak();
		nodelay(win, TRUE);
		noecho();
		curs_set(0);
	}

	col = line = 0;
	last_tai_offset = tai_offset = 0;
	last_tv_system.tv_sec = last_tv_system.tv_usec = 0;
	leap = step = slew = tick = slew_tick = 0;

	while ((ch = getch()) == ERR || ch == KEY_RESIZE) {
		if (!debug) {
			col = (COLS - CLOCK_COLS) / 2;
			line = (LINES - CLOCK_LINES) / 2;

			erase();
			print_border(line, col);
		}

		timex.modes = 0;
		if (adjtimex(&timex) < 0)
			return 1;

		tv_system = timex.time;
		if (timex.status & STA_NANO)
			tv_system.tv_usec /= 1000;

		diff = diff_tv(&tv_system, &last_tv_system);
		if (diff > -1.0 && diff < -0.8)
			step = 1, slew = 0;
		else if (diff > 1.0 || diff < -1.0)
			step = slew = leap = 0;

		if (debug)
			printf("diff=%f step=%d slew=%d leap=%d\n", diff, step, slew, leap);

		if (step || last_tv_system.tv_sec != tv_system.tv_sec) {
			tai_offset = get_tai_offset(tv_system.tv_sec + step);
			leap = last_tai_offset && tai_offset > last_tai_offset;
			last_tai_offset = tai_offset;
			step = 0;
		}

		last_tv_system = tv_system;
		tv_utc = tv_system;

		if (leap && tv_utc.tv_sec % 86400 == 0)
			slew = 1;
		if (slew) {
			if (!slew_tick || slew_tick > timex.tick)
				slew_tick = timex.tick;
			diff = 1.0 - (double)(tick - slew_tick) / slew_tick *
				(tv_utc.tv_sec % 86400 + tv_utc.tv_usec / 1e6);
			if (diff <= 0.0 || timex.tick > (tick + slew_tick) / 2) {
				diff = 0.0;
				slew = slew_tick = 0;
			}

			tv_utc.tv_usec -= diff * 1e6;
			while (tv_utc.tv_usec < 0) {
				tv_utc.tv_sec--;
				tv_utc.tv_usec += 1000000;
			}
		} else {
			tick = timex.tick;
		}

		tv_tai = tv_utc;
		tv_tai.tv_sec += tai_offset;

		print_time(line + 2, col + 3, "System", &tv_system, 0, 0);
		print_time(line + 3, col + 3, "UTC", &tv_utc, 0, leap);
		print_time(line + 4, col + 3, "TAI", &tv_tai, 0, 0);
		print_time(line + 5, col + 3, NULL, &tv_utc, 1, leap);

		if (!debug)
			refresh();

		usleep(50000);
	}

	if (!debug)
		endwin();

	return 0;
}
