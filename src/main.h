/*
 *  This file is part of pom-ng.
 *  Copyright (C) 2010-2012 Guy Martin <gmsoft@tuxicoman.be>
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


#ifndef __MAIN_H__
#define __MAIN_H__

#define POMNG_HTTPD_ADDRESSES	"0.0.0.0;::"
#define POMNG_HTTPD_PORT	8080
#define POMNG_HTTPD_WWW_DATA	DATAROOT "/pom-ng-webui/"
#define POMNG_SYSTEM_DATASTORE "sqlite:system?dbfile=~/.pom-ng/sys_datastore.db"

#define MAIN_TIMER_DELAY	2

struct main_timer {
	time_t expiry;
	void *priv;
	int (*handler) (void*);
	struct main_timer *prev, *next;
};


void signal_handler(int signal);
struct datastore *system_datastore_open(char *dstore_uri);
int system_datastore_close();
int main(int argc, char *argv[]);
int halt(char *reason, int error);
int halt_signal(char *reason);
struct datastore *system_datastore();


struct main_timer* main_timer_alloc(void *priv, int (*handler) (void*));
int main_timer_queue(struct main_timer *t, time_t timeout);
int main_timer_dequeue(struct main_timer *t);
int main_timer_cleanup(struct main_timer *t);

#endif
