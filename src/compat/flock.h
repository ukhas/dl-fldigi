/* Emulate flock on platforms that lack it, primarily Windows and MinGW.

   This is derived from sqlite3 sources.
   http://www.sqlite.org/cvstrac/rlog?f=sqlite/src/os_win.c
   http://www.sqlite.org/copyright.html

   Written by Richard W.M. Jones <rjones.at.redhat.com>
   (Tweaked by Daniel Richman to use as a MINGW "patch" only)

   Copyright (C) 2008-2010 Free Software Foundation, Inc.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifdef __cplusplus
extern "C" {
#endif

#if (defined _WIN32 || defined __WIN32__) && ! defined __CYGWIN__

#ifndef LOCK_SH
/* Operations for the 'flock' call (same as Linux kernel constants).  */
# define LOCK_SH 1       /* Shared lock.  */
# define LOCK_EX 2       /* Exclusive lock.  */
# define LOCK_UN 8       /* Unlock.  */

/* Can be OR'd in to one of the above.  */
# define LOCK_NB 4       /* Don't block when locking.  */
#endif

#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif

int flock (int fd, int operation);

#endif /* Windows */

#ifdef __cplusplus
}
#endif
