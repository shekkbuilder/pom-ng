/*
 *  This file is part of pom-ng.
 *  Copyright (C) 2011 Guy Martin <gmsoft@tuxicoman.be>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */


#ifndef __ANALYZER_H__
#define __ANALYZER_H__

#include <pom-ng/analyzer.h>

// We require at least that ammount of bytes before passing the buffer to libmagic
#define ANALYZER_PLOAD_BUFFER_MAGIC_MIN_SIZE 64

struct analyzer_event_listener_list {

	struct analyzer_event_listener *listener;
	struct analyzer_event_listener_list *prev, *next;

};

int analyzer_init(char *mime_type_database);
int analyzer_cleanup();
int analyzer_pload_output(struct analyzer_pload_buffer *pload);

#endif