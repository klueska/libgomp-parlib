/* Copyright (C) 2005, 2006, 2008, 2009 Free Software Foundation, Inc.
   Contributed by Richard Henderson <rth@redhat.com>.

   This file is part of the GNU OpenMP Library (libgomp).

   Libgomp is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   Libgomp is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   Under Section 7 of GPL version 3, you are granted additional
   permissions described in the GCC Runtime Library Exception, version
   3.1, as published by the Free Software Foundation.

   You should have received a copy of the GNU General Public License and
   a copy of the GCC Runtime Library Exception along with this program;
   see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
   <http://www.gnu.org/licenses/>.  */

/* This file contains Lithe specific routines related to counting
   online processors and dynamic load balancing.  It is expected that
   a system may well want to write special versions of each of these.
*/

#include "libgomp.h"
#include <lithe/hart.h>
#include <unistd.h>
#include <stdlib.h>

/* At startup, determine the default number of threads.  It would seem
   this should be related to the number of cpus online.  */

void
gomp_init_num_threads (void)
{
  gomp_global_icv.nthreads_var = max_harts();
}

/* When OMP_DYNAMIC is set, at thread launch determine the number of
   threads we should spawn for this team.  */
/* ??? I have no idea what best practice for this is.  Surely some
   function of the number of processors that are *still* online and
   the load average.  Here I use the number of processors online
   minus the 15 minute load average.  */

unsigned
gomp_dynamic_max_threads (void)
{
  int num = max_harts() - num_harts();
  return num ? num : 1;
}

int
omp_get_num_procs (void)
{
  return max_harts();
}

ialias (omp_get_num_procs)
