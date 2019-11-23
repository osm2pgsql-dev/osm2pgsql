/*-------------------------------------------------------------------------
 *
 * sprompt.c
 *	  simple_prompt() routine
 *
 * Portions Copyright (c) 1996-2006, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/port/sprompt.c,v 1.18 2006/10/04 00:30:14 momjian Exp $
 *
 *-------------------------------------------------------------------------
 *
 * PostgreSQL Database Management System
 * (formerly known as Postgres, then as Postgres95)
 *
 * Portions Copyright (c) 1996-2006, PostgreSQL Global Development Group
 *
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written agreement
 * is hereby granted, provided that the above copyright notice and this
 * paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS
 * DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 */

/*
 * simple_prompt
 *
 * Generalized function especially intended for reading in usernames and
 * password interactively. Reads from /dev/tty or stdin/stderr.
 *
 * prompt:		The prompt to print
 * maxlen:		How many characters to accept
 * echo:		Set to false if you want to hide what is entered (for passwords)
 *
 * Returns a malloc()'ed string with the input (w/o trailing newline).
 */

#define DEVTTY "/dev/tty"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "config.h"

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

char *simple_prompt(const char *prompt, int maxlen, int echo)
{
    size_t length;
    char *destination;
    FILE *termin, *termout;

#ifdef HAVE_TERMIOS_H
    struct termios t_orig, t;
#else
#ifdef _WIN32
    HANDLE t = NULL;
    DWORD t_orig;
#endif
#endif

    destination = static_cast<char *>(malloc(maxlen + 1));
    if (!destination) {
        return NULL;
    }

    /*
	 * Do not try to collapse these into one "w+" mode file. Doesn't work on
	 * some platforms (eg, HPUX 10.20).
	 */
    termin = fopen(DEVTTY, "r");
    termout = fopen(DEVTTY, "w");
    if (!termin ||
        !termout
#ifdef _WIN32
        /* See DEVTTY comment for msys */
        || (getenv("OSTYPE") && strcmp(getenv("OSTYPE"), "msys") == 0)
#endif
    ) {
        if (termin) {
            fclose(termin);
        }
        if (termout) {
            fclose(termout);
        }
        termin = stdin;
        termout = stderr;
    }

#ifdef HAVE_TERMIOS_H
    if (!echo) {
        tcgetattr(fileno(termin), &t);
        t_orig = t;
        t.c_lflag &= ~ECHO;
        tcsetattr(fileno(termin), TCSAFLUSH, &t);
    }
#else
#ifdef _WIN32
    if (!echo) {
        /* get a new handle to turn echo off */
        t = GetStdHandle(STD_INPUT_HANDLE);

        /* save the old configuration first */
        GetConsoleMode(t, &t_orig);

        /* set to the new mode */
        SetConsoleMode(t, ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    }
#endif
#endif

    if (prompt) {
        fputs(prompt, termout);
        fflush(termout);
    }

    if (fgets(destination, maxlen + 1, termin) == NULL) {
        destination[0] = '\0';
    }

    length = strlen(destination);
    if (length > 0 && destination[length - 1] != '\n') {
        /* eat rest of the line */
        char buf[128];
        size_t buflen;

        do {
            if (fgets(buf, sizeof(buf), termin) == NULL) {
                break;
            }
            buflen = strlen(buf);
        } while (buflen > 0 && buf[buflen - 1] != '\n');
    }

    if (length > 0 && destination[length - 1] == '\n') {
        /* remove trailing newline */
        destination[length - 1] = '\0';
    }

#ifdef HAVE_TERMIOS_H
    if (!echo) {
        tcsetattr(fileno(termin), TCSAFLUSH, &t_orig);
        fputs("\n", termout);
        fflush(termout);
    }
#else
#ifdef _WIN32
    if (!echo) {
        /* reset to the original console mode */
        SetConsoleMode(t, t_orig);
        fputs("\n", termout);
        fflush(termout);
    }
#endif
#endif

    if (termin != stdin) {
        fclose(termin);
        fclose(termout);
    }

    return destination;
}
