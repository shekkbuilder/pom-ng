/*
 *  This file is part of pom-ng.
 *  Copyright (C) 2010-2011 Guy Martin <gmsoft@tuxicoman.be>
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


#ifndef __POM_NG_PROTO_H__
#define __POM_NG_PROTO_H__

#include <pom-ng/base.h>
#include <pom-ng/mod.h>
#include <pom-ng/ptype.h>
#include <pom-ng/conntrack.h>
#include <pom-ng/packet.h>
#include <pom-ng/registry.h>

// Current proto API version
#define PROTO_API_VER	1


#define PROTO_EVENT_DATA_FLAG_LIST	0x1


// Indicates the event is followed by a payload
#define PROTO_EVENT_FLAG_HAS_PAYLOAD	0x1
// Indicates the event has been processed and should be reset before reuse
#define PROTO_EVENT_FLAG_PROCESSED	0x2


// The listener needs the payload of the specified protocol
#define PROTO_PACKET_LISTENER_PLOAD_ONLY	0x2


// Error code definition
#define PROTO_OK	0
#define PROTO_ERR	-1
#define PROTO_STOP	-2
#define PROTO_INVALID	-3

struct proto_reg {

	struct proto_reg_info *info;
	struct proto_dependency *dep; // Corresponding dependency
	
	/// Conntrack tables
	struct conntrack_tables *ct;

	// Packet info pool
	struct packet_info_pool pkt_info_pool;

	struct registry_instance *reg_instance;

	void *priv;

	struct proto_event_analyzer_list *event_analyzers;
	struct proto_packet_listener *packet_listeners;

	struct proto_reg *next, *prev;

};

struct proto_dependency {
	char *name;
	unsigned int refcount;
	struct proto_reg *proto;
	struct proto_dependency *next, *prev;
};

struct proto_process_stack {
	struct proto_reg *proto;
	void *pload;
	uint32_t plen;
	int direction; // Used to pass direction to the next proto if he can't find out

	struct packet_info *pkt_info;

	struct conntrack_entry *ce;

};

struct proto_pkt_field {
	char *name;
	struct ptype *value_template;
	char *description;

};

struct proto_event_data_reg {

	char *name;
	struct ptype *value_template;
	unsigned int flags;
};

struct proto_event_data_item {
	char *key;
	struct ptype *value;
	struct proto_event_data_item *next;
};

struct proto_event_data {

	unsigned int set;
	union {
		struct ptype *value;
		struct proto_event_data_item *items;
	int (*process) (struct packet *p, struct proto_process_stack *s, unsigned int stack_index);
	};
};

struct proto_event_reg {
	char *name;
	unsigned int id;
	struct proto_event_data_reg *data;
	unsigned int data_count;
	struct proto_event_reg *next;
};


struct proto_event {

	struct proto_reg *proto;
	struct proto_event_reg *evt_reg;
	unsigned int flags;
	struct proto_event_data *data;

};

struct proto_event_analyzer_reg {

	struct analyzer_reg *analyzer;
	int (*process) (struct analyzer_reg *analyzer, struct proto_event *evt, struct proto_process_stack *stack, unsigned int stack_index);
	int (*expire) (struct analyzer_reg *analyzer, struct proto_event *evt, struct conntrack_entry *ce);

};

struct proto_reg_info {

	unsigned int api_ver;
	char *name;
	struct mod_reg *mod;
	struct proto_pkt_field *pkt_fields;
	struct conntrack_info ct_info;
	struct proto_event_reg *events;

	int (*init) (struct registry_instance *i);
	int (*process) (struct packet *p, struct proto_process_stack *s, unsigned int stack_index);
	int (*cleanup) ();

};

struct proto_packet_listener {

	int flags;
	struct proto_reg *proto;
	void *object;
	int (*process) (void *object, struct packet *p, struct proto_process_stack *s, unsigned int stack_index);
	struct proto_packet_listener *prev, *next;
};

/// Register a new protocol
int proto_register(struct proto_reg_info *reg);

/// Process the packet
int proto_process(struct packet *p, struct proto_process_stack *s, unsigned int stack_index);

/// Unregister a protocol
int proto_unregister(char *name);

/// Get a dependency for a specific protocol
struct proto_dependency *proto_add_dependency(char *dep);

/// Release a dependency record
int proto_remove_dependency(struct proto_dependency *dep);


/// Allocate an event
struct proto_event *proto_event_alloc(struct proto_reg *proto, unsigned int evt_id);

/// Allocate an event data item
struct ptype *proto_event_data_item_add(struct proto_event *evt, unsigned int data_id, char *key);

/// Process an event
int proto_event_process(struct proto_event *evt, struct proto_process_stack *stack, unsigned int stack_index);

/// Reset an event content for reuse
int proto_event_reset(struct proto_event *evt, struct conntrack_entry *ce);

/// Cleanup an event
int proto_event_cleanup(struct proto_event *evt);

/// Register an analyzer for a protocol
int proto_event_analyzer_register(struct proto_reg *proto, struct proto_event_analyzer_reg *analyzer_reg);

/// Unregister an analyzer from a protocol
int proto_event_analyzer_unregister(struct proto_reg *proto, struct analyzer_reg *analyzer);


// Register a packet listener
struct proto_packet_listener *proto_packet_listener_register(struct proto_reg *proto, unsigned int flags, void *object,  int (*process) (void *object, struct packet *p, struct proto_process_stack *s, unsigned int stack_index));

// Unregister a packet listener
int proto_packet_listener_unregister(struct proto_packet_listener *l);

#endif
