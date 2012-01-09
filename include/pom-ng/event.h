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


#ifndef __POM_NG_EVENT_H__
#define __POM_NG_EVENT_H__

#include <pom-ng/proto.h>
#include <pom-ng/conntrack.h>

// Indicate that the data will be an array
#define EVENT_DATA_REG_FLAG_LIST	0x1
// Indicate that the data should not be allocated automatically
#define EVENT_DATA_REG_FLAG_NO_ALLOC	0x2

// Indicate that the event processing has started
#define EVENT_FLAG_PROCESS_BEGAN	0x1
// Indicate that the event processing is done
#define EVENT_FLAG_PROCESS_DONE		0x2

// Indicate that the event data shouldn't be cleaned up by the API
#define EVENT_DATA_FLAG_NO_CLEAN	0x1

struct event_data_item {
	char *key;
	struct ptype *value;
	struct event_data_item *next;
};

struct event_data {

	union {
		struct ptype *value;
		struct event_data_item *items;
	};
	unsigned int flags;

};

struct event_data_reg {
	int flags;
	char *name;
	struct ptype *value_template;
};

struct event {
	struct event_reg *reg;
	unsigned int flags;
	struct conntrack_entry *ce;
	void *priv;
	unsigned int refcount;
	struct event_data *data;
};

struct event_listener {
	void *obj;
	int (*process_begin) (struct event *evt, void *obj, struct proto_process_stack *stack, unsigned int stack_index);
	int (*process_end) (struct event *evt, void *obj);

};

struct event_listener_list {
	struct event_listener *l;
	struct event_listener_list *prev, *next;
};

struct event_reg {
	struct event_reg_info *info;
	struct event_listener_list *listeners;
	struct event_reg *prev, *next;
};

struct event_reg_info {
	char *source_name;
	void *source_obj;
	char *name;
	char *description;
	struct event_data_reg *data_reg;
	unsigned int data_count;
	int (*listeners_notify) (void *obj, struct event_reg *evt_reg, int has_listeners);
	int (*cleanup) (struct event *evt);
};

struct event_reg *event_register(struct event_reg_info *reg_info);
int event_unregister(struct event_reg *evt);

struct event *event_alloc(struct event_reg *evt_reg);
int event_cleanup(struct event *evt);

struct event_reg *event_find(char *name);
struct ptype *event_data_item_add(struct event *evt, unsigned int data_id, char *key);

int event_listener_register(struct event_reg *evt_reg, struct event_listener *listener);
int event_listener_unregister(struct event_reg *evt_reg, void *obj);

int event_process_begin(struct event *evt, struct proto_process_stack *stack, int stack_index);
int event_process_end(struct event *evt);

int event_refcount_inc(struct event *evt);
int event_refcount_dec(struct event *evt);

#endif
