/*
 *  This file is part of pom-ng.
 *  Copyright (C) 2010 Guy Martin <gmsoft@tuxicoman.be>
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



#include "common.h"
#include "proto.h"
#include "packet.h"
#include "main.h"
#include "core.h"
#include "input_client.h"

#include <pom-ng/ptype.h>

#if 0
#define debug_stream_parser(x ...) pomlog(POMLOG_DEBUG "stream_parser: " x)
#else
#define debug_stream_parser(x ...)
#endif

#if 0
#define debug_stream(x ...) pomlog(POMLOG_DEBUG "stream: " x)
#else
#define debug_stream(x ...)
#endif

static struct packet *packet_head, *packet_unused_head;
static pthread_mutex_t packet_list_mutex = PTHREAD_MUTEX_INITIALIZER;
unsigned int num = 0;

struct packet *packet_pool_get() {

	pom_mutex_lock(&packet_list_mutex);

	struct packet *tmp = packet_unused_head;

	if (!tmp) {
		// Alloc a new packet
		tmp = malloc(sizeof(struct packet));
		if (!tmp) {
			pom_mutex_unlock(&packet_list_mutex);
			pom_oom(sizeof(struct packet));
			return NULL;
		}
	} else {
		// Fetch it from the unused pool
		packet_unused_head = tmp->next;
		if (packet_unused_head)
			packet_unused_head->prev = NULL;
	}

	memset(tmp, 0, sizeof(struct packet));

	// Add the packet to the used pool
	tmp->next = packet_head;
	if (tmp->next)
		tmp->next->prev = tmp;
	
	packet_head = tmp;

	tmp->refcount = 1;
	
	pom_mutex_unlock(&packet_list_mutex);

	return tmp;
}

struct packet *packet_clone(struct packet *src, unsigned int flags) {

	struct packet *dst = NULL;

	if (!(flags & PACKET_FLAG_FORCE_NO_COPY) && src->input_pkt) {
		// It uses the input buffer, we cannot hold this ressource
		dst = packet_pool_get();
		if (!dst)
			return NULL;

		memcpy(&dst->ts, &src->ts, sizeof(struct timeval));
		dst->len = src->len;
		dst->buff = malloc(src->len);
		if (!dst->buff) {
			pom_oom(dst->len);
			packet_pool_release(dst);
			return NULL;
		}

		memcpy(dst->buff, src->buff, src->len);

		dst->datalink = src->datalink;
		dst->input = src->input;
		dst->id = src->id;

		// Multipart and stream are not copied
		
		return dst;
	}
	pom_mutex_lock(&packet_list_mutex); // Use this lock to prevent refcount race
	src->refcount++;
	pom_mutex_unlock(&packet_list_mutex);
	return src;
}

int packet_pool_release(struct packet *p) {

	struct packet_multipart *multipart = NULL;
	pom_mutex_lock(&packet_list_mutex);
	if (p->multipart) {
		multipart = p->multipart;
		p->multipart = NULL;
	}

	p->refcount--;
	if (p->refcount) {
		pom_mutex_unlock(&packet_list_mutex);
		if (multipart) // Always release the multipart
			return packet_multipart_cleanup(multipart);
		return POM_OK;
	}

	// Remove the packet from the used list
	if (p->next)
		p->next->prev = p->prev;

	if (p->prev)
		p->prev->next = p->next;
	else
		packet_head = p->next;

	struct input_client_entry *i = p->input;
	struct input_packet *input_pkt = p->input_pkt;
	unsigned char *buff = p->buff;

	memset(p, 0, sizeof(struct packet));
	
	// Add it back to the unused list
	
	p->next = packet_unused_head;
	if (p->next)
		p->next->prev = p;
	packet_unused_head = p;

	pom_mutex_unlock(&packet_list_mutex);

	int res = POM_OK;

	if (multipart)
		res = packet_multipart_cleanup(multipart);

	if (input_pkt) {
		if (input_client_release_packet(i, input_pkt) != POM_OK) {
			res = POM_ERR;
			pomlog(POMLOG_ERR "Error while releasing packet from the buffer");
		}
	} else {
		// Packet doesn't come from an input -> free the buffer
		free(buff);
	}

	return res;
}

int packet_pool_cleanup() {

	pom_mutex_lock(&packet_list_mutex);

	struct packet *tmp = packet_head;
	while (tmp) {
		pomlog(POMLOG_WARN "A packet was not released, refcount : %u", tmp->refcount);
		packet_head = tmp->next;

		free(tmp);
		tmp = packet_head;
	}

	tmp = packet_unused_head;

	while (tmp) {
		packet_unused_head = tmp->next;
		free(tmp);
		tmp = packet_unused_head;
	}

	pom_mutex_unlock(&packet_list_mutex);

	return POM_OK;
}

int packet_info_pool_init(struct packet_info_pool *pool) {

	if (pthread_mutex_init(&pool->lock, NULL)) {
		pomlog(POMLOG_ERR "Error while initializing the pkt_info_pool lock : ", pom_strerror(errno));
		return POM_ERR;
	}

	return POM_OK;
}

struct packet_info *packet_info_pool_get(struct proto *p) {

	struct packet_info *info = NULL;

	pom_mutex_lock(&p->pkt_info_pool.lock);

	if (!p->pkt_info_pool.unused) {
		// Allocate new packet_info
		info = malloc(sizeof(struct packet_info));
		if (!info) {
			pom_mutex_unlock(&p->pkt_info_pool.lock);
			pom_oom(sizeof(struct packet_info));
			return NULL;
		}
		memset(info, 0, sizeof(struct packet_info));
		struct proto_pkt_field *fields = p->info->pkt_fields;
		int i;
		for (i = 0; fields[i].name; i++);

		info->fields_value = malloc(sizeof(struct ptype*) * (i + 1));
		memset(info->fields_value, 0, sizeof(struct ptype*) * (i + 1));

		for (; i--; ){
			info->fields_value[i] = ptype_alloc_from(fields[i].value_template);
			if (!info->fields_value[i]) {
				i++;
				for (; fields[i].name; i++)
					ptype_cleanup(info->fields_value[i]);
				free(info);
				pom_mutex_unlock(&p->pkt_info_pool.lock);
				return NULL;
			}
		}

	} else {
		// Dequeue the packet_info from the unused pool
		info = p->pkt_info_pool.unused;
		p->pkt_info_pool.unused = info->pool_next;
		if (p->pkt_info_pool.unused)
			p->pkt_info_pool.unused->pool_prev = NULL;
	}


	// Queue the packet_info in the used pool
	info->pool_prev = NULL;
	info->pool_next = p->pkt_info_pool.used;
	if (info->pool_next)
		info->pool_next->pool_prev = info;
	p->pkt_info_pool.used = info;
	
	pom_mutex_unlock(&p->pkt_info_pool.lock);
	return info;
}


int packet_info_pool_release(struct packet_info_pool *pool, struct packet_info *info) {

	if (!pool || !info)
		return POM_ERR;

	pom_mutex_lock(&pool->lock);

	// Dequeue from used and queue to unused

	if (info->pool_prev)
		info->pool_prev->pool_next = info->pool_next;
	else
		pool->used = info->pool_next;

	if (info->pool_next)
		info->pool_next->pool_prev = info->pool_prev;

	
	info->pool_next = pool->unused;
	if (info->pool_next)
		info->pool_next->pool_prev = info;
	pool->unused = info;
	
	pom_mutex_unlock(&pool->lock);
	return POM_OK;
}


int packet_info_pool_cleanup(struct packet_info_pool *pool) {

	pthread_mutex_destroy(&pool->lock);

	struct packet_info *tmp = NULL;
	while (pool->used) {	
		printf("Unreleased packet info !\n");
		tmp = pool->used;
		pool->used = tmp->pool_next;

		int i;
		for (i = 0; tmp->fields_value[i]; i++)
			ptype_cleanup(tmp->fields_value[i]);

		free(tmp->fields_value);
		free(tmp);
	}

	while (pool->unused) {
		tmp = pool->unused;
		pool->unused = tmp->pool_next;

		int i;
		for (i = 0; tmp->fields_value[i]; i++)
			ptype_cleanup(tmp->fields_value[i]);

		free(tmp->fields_value);

		free(tmp);
	}


	return POM_OK;
}


struct packet_multipart *packet_multipart_alloc(struct proto_dependency *proto_dep, unsigned int flags) {

	struct packet_multipart *res = malloc(sizeof(struct packet_multipart));
	if (!res) {
		pom_oom(sizeof(struct packet_multipart));
		return NULL;
	}
	memset(res, 0, sizeof(struct packet_multipart));

	proto_dependency_refcount_inc(proto_dep);
	res->proto = proto_dep;
	if (!res->proto) {
		free(res);
		res = NULL;
	}

	res->flags = flags;

	return res;
}

int packet_multipart_cleanup(struct packet_multipart *m) {

	if (!m)
		return POM_ERR;

	struct packet_multipart_pkt *tmp;

	while (m->head) {
		tmp = m->head;
		m->head = tmp->next;

		packet_pool_release(tmp->pkt);
		free(tmp);

	}

	proto_remove_dependency(m->proto);

	free(m);

	return POM_OK;

}


int packet_multipart_add_packet(struct packet_multipart *multipart, struct packet *pkt, size_t offset, size_t len, size_t pkt_buff_offset) {

	struct packet_multipart_pkt *tmp = multipart->tail;

	// Check where to add the packet
	while (tmp) {

		if (tmp->offset + tmp->len <= offset)
			break; // Packet is after is one

		if (tmp->offset == offset) {
			if (tmp->len != len)
				pomlog(POMLOG_WARN "Size missmatch for packet already in the buffer");
			return POM_OK;
		}

		if (tmp->offset > offset) {
			pomlog(POMLOG_WARN "Offset missmatch for packet already in the buffer");
			return POM_OK;
		}
		
		tmp = tmp->next;

	}

	struct packet_multipart_pkt *res = malloc(sizeof(struct packet_multipart_pkt));
	if (!res) {
		pom_oom(sizeof(struct packet_multipart_pkt));
		return POM_ERR;
	}
	memset(res, 0, sizeof(struct packet_multipart_pkt));

	res->offset = offset;
	res->pkt_buff_offset = pkt_buff_offset;
	res->len = len;


	// Copy the packet

	
	res->pkt = packet_clone(pkt, multipart->flags);
	if (!res->pkt) {
		free(res);
		return POM_ERR;
	}

	multipart->cur += len;

	if (tmp) {
		// Packet is after this one, add it
	
		res->prev = tmp;
		res->next = tmp->next;

		tmp->next = res;

		if (res->next) {
			res->next->prev = res;

			if ((res->next->offset == res->offset + res->len) &&
				(res->prev->offset + res->prev->len == res->offset))
				// A gap was filled
				multipart->gaps--;
			else if ((res->next->offset > res->offset + res->len) &&
				(res->prev->offset + res->prev->len < res->offset))
				// A gap was created
				multipart->gaps++;

		} else {
			multipart->tail = res;
		}


		return POM_OK;
	} else {
		// Add it at the head
		res->next = multipart->head;
		if (res->next)
			res->next->prev = res;
		else
			multipart->tail = res;
		multipart->head = res;

		if (res->offset) {
			// There is a gap at the begining
			multipart->gaps++;
		} else if (res->next && res->len == res->next->offset) {
			// Gap filled
			multipart->gaps--;
		}
	}

	return POM_OK;
}

int packet_multipart_process(struct packet_multipart *multipart, struct proto_process_stack *stack, unsigned int stack_index) {

	struct packet *p = packet_pool_get();
	if (!p) {
		packet_multipart_cleanup(multipart);
		return PROTO_ERR;
	}

	p->buff = malloc(multipart->cur);
	if (!p->buff) {
		packet_pool_release(p);
		packet_multipart_cleanup(multipart);
		pom_oom(multipart->cur);
		return PROTO_ERR;
	}

	struct packet_multipart_pkt *tmp = multipart->head;
	for (; tmp; tmp = tmp->next) {
		memcpy(p->buff + tmp->offset, tmp->pkt->buff + tmp->pkt_buff_offset, tmp->len);
	}

	memcpy(&p->ts, &multipart->tail->pkt->ts, sizeof(struct timeval));
	
	p->multipart = multipart;
	p->len = multipart->cur;
	p->datalink = multipart->proto->proto;
	stack[stack_index].pload = p->buff;
	stack[stack_index].plen = p->len;
	stack[stack_index].proto = p->datalink;

	int res = core_process_multi_packet(stack, stack_index, p);

	packet_pool_release(p);

	return res;
}


struct packet_stream* packet_stream_alloc(uint32_t start_seq, uint32_t start_ack, int direction, uint32_t max_buff_size, unsigned int flags, int (*handler) (void *priv, struct packet *p, struct proto_process_stack *stack, unsigned int stack_index),  void *priv) {
	
	struct packet_stream *res = malloc(sizeof(struct packet_stream));
	if (!res) {
		pom_oom(sizeof(struct packet_stream));
		return NULL;
	}

	memset(res, 0, sizeof(struct packet_stream));
	int rev_direction = (direction == CT_DIR_FWD ? CT_DIR_REV : CT_DIR_FWD);
	res->cur_seq[direction] = start_seq;
	res->cur_ack[direction] = start_ack;
	res->cur_seq[rev_direction] = start_ack;
	res->cur_ack[rev_direction] = start_seq;
	res->max_buff_size = max_buff_size;
	res->handler = handler;
	res->priv = priv;

	if (pthread_mutex_init(&res->lock, NULL)) {
		pomlog(POMLOG_ERR "Error while initializing packet_stream list mutex : %s", pom_strerror(errno));
		return NULL;
	}

	res->flags = flags;

	debug_stream("entry %p, allocated, start_seq %u, start_ack %u, direction %u", res, start_seq, start_ack, direction);

	return res;
}

int packet_stream_cleanup(struct packet_stream *stream) {

	int i;
	for (i = 0; i < CT_DIR_TOT; i++) {
		struct packet_stream_pkt *p = stream->head[i];
		while (p) {
			stream->head[i] = p->next;
			if (p->pkt)
				packet_pool_release(p->pkt);
			if (p->stack) {
				int j;
				for (j = 1; j < CORE_PROTO_STACK_MAX && p->stack[j].pkt_info; j++)
					packet_info_pool_release(&p->stack[j].proto->pkt_info_pool, p->stack[j].pkt_info);
				free(p->stack);
			}
			free(p);
			p = stream->head[i];
		}
	}

	pthread_mutex_destroy(&stream->lock);

	free(stream);

	debug_stream("entry %p, released", stream);

	return POM_OK;
}

static int packet_stream_is_packet_old_dupe(struct packet_stream *stream, struct packet_stream_pkt *pkt, int direction) {

	uint32_t end_seq = pkt->seq + pkt->plen;
	uint32_t cur_seq = stream->cur_seq[direction];

	if ((cur_seq > end_seq && cur_seq - end_seq < PACKET_HALF_SEQ)
		|| (cur_seq < end_seq && end_seq - cur_seq > PACKET_HALF_SEQ)) {
		// cur_seq is after the end of the packet, discard it
		return 1;
	}
	
	return 0;
}

static int packet_stream_remove_dupe_bytes(struct packet_stream *stream, struct packet_stream_pkt *pkt, int direction) {

	uint32_t cur_seq = stream->cur_seq[direction];
	if ((cur_seq > pkt->seq && cur_seq - pkt->seq < PACKET_HALF_SEQ)
		|| (cur_seq < pkt->seq && pkt->seq - cur_seq > PACKET_HALF_SEQ)) {
		// We need to discard some of the packet
		uint32_t dupe = cur_seq - pkt->seq;

		if (dupe > pkt->stack[pkt->stack_index].plen) {
			pomlog(POMLOG_ERR "Internal error while computing duplicate bytes");
			return POM_ERR;
		}
		pkt->stack[pkt->stack_index].pload += dupe;
		pkt->stack[pkt->stack_index].plen -= dupe;
		pkt->seq += dupe;
	}

	return POM_OK;
}

static int packet_stream_is_packet_next(struct packet_stream *stream, struct packet_stream_pkt *pkt, int direction) {

	int rev_direction = (direction == CT_DIR_FWD ? CT_DIR_REV : CT_DIR_FWD);
	uint32_t cur_seq = stream->cur_seq[direction];
	uint32_t rev_seq = stream->cur_seq[rev_direction];


	// Check that there is no gap with what we expect
	if ((cur_seq < pkt->seq && pkt->seq - cur_seq < PACKET_HALF_SEQ)
		|| (cur_seq > pkt->seq && cur_seq - pkt->seq > PACKET_HALF_SEQ)) {
		// There is a gap
		debug_stream("entry %p, packet %u.%06u, seq %u, ack %u : GAP : cur_seq %u, rev_seq %u", stream, pkt->pkt->ts.tv_sec, pkt->pkt->ts.tv_usec, pkt->seq, pkt->ack, cur_seq, rev_seq);
		return 0;
	}


	if (stream->flags & PACKET_FLAG_STREAM_BIDIR) {
		// There is additional checking for bi dir stream

	
		if ((rev_seq < pkt->ack && pkt->ack - rev_seq < PACKET_HALF_SEQ)
			|| (rev_seq > pkt->ack && rev_seq - pkt->ack > PACKET_HALF_SEQ)) {
			// The host processed data in the reverse direction which we haven't processed yet
			debug_stream("entry %p, packet %u.%06u, seq %u, ack %u : reverse missing : cur_seq %u, rev_seq %u", stream, pkt->pkt->ts.tv_sec, pkt->pkt->ts.tv_usec, pkt->seq, pkt->ack, cur_seq, rev_seq);
			return 0;
		}

	}


	// This packet can be processed
	debug_stream("entry %p, packet %u.%06u, seq %u, ack %u : is next : cur_seq %u, rev_seq %u", stream, pkt->pkt->ts.tv_sec, pkt->pkt->ts.tv_usec, pkt->seq, pkt->ack, cur_seq, rev_seq);

	return 1;

}

int packet_stream_process_packet(struct packet_stream *stream, struct packet *pkt, struct proto_process_stack *stack, unsigned int stack_index, uint32_t seq, uint32_t ack) {

	if (!stream || !pkt || !stack)
		return POM_ERR;

	pom_mutex_lock(&stream->lock);

	debug_stream("entry %p, packet %u.%06u, seq %u, ack %u : start", stream, pkt->ts.tv_sec, pkt->ts.tv_usec, seq, ack);

	struct proto_process_stack *cur_stack = &stack[stack_index];
	int direction = cur_stack->direction;


	// Update the stream flags
	if (stream->flags & PACKET_FLAG_STREAM_BIDIR) {

		// Update flags
		if (direction == CT_DIR_FWD && !(stream->flags & PACKET_FLAG_STREAM_GOT_FWD_DIR)) {
			stream->flags |= PACKET_FLAG_STREAM_GOT_FWD_DIR;
		} else if (direction == CT_DIR_REV && !(stream->flags & PACKET_FLAG_STREAM_GOT_REV_DIR)) {
			stream->flags |= PACKET_FLAG_STREAM_GOT_REV_DIR;
		}

	}

	// Put this packet in our struct packet_stream_pkt
	struct packet_stream_pkt spkt = {0};
	spkt.pkt = pkt;
	spkt.seq = seq;
	spkt.ack = ack;
	spkt.plen = cur_stack->plen;
	spkt.stack = stack;


	// Check if the packet is worth processing
	uint32_t cur_seq = stream->cur_seq[direction];
	if (cur_seq != seq) {
		if (packet_stream_is_packet_old_dupe(stream, &spkt, direction)) {
			// cur_seq is after the end of the packet, discard it
			debug_stream("entry %p, packet %u.%06u, seq %u, ack %u : discard", stream, pkt->ts.tv_sec, pkt->ts.tv_usec, seq, ack);
			pom_mutex_unlock(&stream->lock);
			return POM_OK;
		}

		if (packet_stream_remove_dupe_bytes(stream, &spkt, direction) == POM_ERR)
			return POM_ERR;
	}


	// Ok let's process it then

	// Check if it is the packet we're waiting for
	if (packet_stream_is_packet_next(stream, &spkt, direction)) {

		// Process it
		stream->cur_seq[direction] += cur_stack->plen;
		stream->cur_ack[direction] = ack;
		debug_stream("entry %p, packet %u.%06u, seq %u, ack %u : process", stream, pkt->ts.tv_sec, pkt->ts.tv_usec, seq, ack);

		int res = stream->handler(stream->priv, pkt, stack, stack_index);
		if (res == POM_ERR) {
			pom_mutex_unlock(&stream->lock);
			return POM_ERR;
		}

		// Check if additional packets can be processed
		struct packet_stream_pkt *p = NULL;
		unsigned int cur_dir = direction;
		while ((p = packet_stream_get_next(stream, &cur_dir))) {


			debug_stream("entry %p, packet %u.%06u, seq %u, ack %u : process additional", stream, p->pkt->ts.tv_sec, p->pkt->ts.tv_usec, p->seq, p->ack);

			if (stream->handler(stream->priv, p->pkt, p->stack, p->stack_index) == POM_ERR) {
				pom_mutex_unlock(&stream->lock);
				return POM_ERR;
			}

			int i;
			for (i = 1; i < CORE_PROTO_STACK_MAX && p->stack[i].pkt_info; i ++)
				packet_info_pool_release(&p->stack[i].proto->pkt_info_pool, p->stack[i].pkt_info);

			free(p->stack);
			packet_pool_release(p->pkt);

			stream->cur_seq[cur_dir] += p->plen;
			stream->cur_ack[cur_dir] = p->ack;

			free(p);
		}
		debug_stream("entry %p, packet %u.%06u, seq %u, ack %u : done", stream, pkt->ts.tv_sec, pkt->ts.tv_usec, seq, ack);
		pom_mutex_unlock(&stream->lock);
		return POM_OK;
	}

	// Queue the packet then

	debug_stream("entry %p, packet %u.%06u, seq %u, ack %u : queue", stream, pkt->ts.tv_sec, pkt->ts.tv_usec, seq, ack);

	struct packet_stream_pkt *p = malloc(sizeof(struct packet_stream_pkt));
	if (!p) {
		pom_mutex_unlock(&stream->lock);
		pom_oom(sizeof(struct packet_stream_pkt));
		return PROTO_ERR;
	}
	memset(p, 0 , sizeof(struct packet_stream_pkt));


	if (cur_stack->plen) {
		// No need to backup this if there is no payload
		p->pkt = packet_clone(pkt, stream->flags);
		if (!p->pkt) {
			pom_mutex_unlock(&stream->lock);
			free(p);
			return PROTO_ERR;
		}
		p->stack = core_stack_backup(stack, pkt, p->pkt);
		if (!p->stack) {
			pom_mutex_unlock(&stream->lock);
			packet_pool_release(p->pkt);
			free(p);
			return PROTO_ERR;
		}
	}


	p->plen = cur_stack->plen;
	p->seq = seq;
	p->ack = ack;
	p->stack_index = stack_index;


	if (!stream->tail[direction]) {
		stream->head[direction] = p;
		stream->tail[direction] = p;
	} else { 

		struct packet_stream_pkt *tmp = stream->tail[direction];
		while ( tmp && 
			((tmp->seq > seq && tmp->seq - seq < PACKET_HALF_SEQ)
			|| (tmp->seq < seq && seq - tmp->seq > PACKET_HALF_SEQ))) {

			tmp = tmp->prev;

		}

		if (!tmp) {
			// Packet goes at the begining of the list
			p->next = stream->head[direction];
			if (p->next)
				p->next->prev = p;
			else
				stream->tail[direction] = p;
			stream->head[direction] = p;

		} else {
			// Insert the packet after the current one
			p->next = tmp->next;
			p->prev = tmp;

			if (p->next)
				p->next->prev = p;
			else
				stream->tail[direction] = p;

			tmp->next = p;

		}
	}
	
	stream->cur_buff_size += cur_stack->plen;


	// FIXME handle buffer overflow

	debug_stream("entry %p, packet %u.%06u, seq %u, ack %u : done", stream, pkt->ts.tv_sec, pkt->ts.tv_usec, seq, ack);
	pom_mutex_unlock(&stream->lock);
	return POM_OK;
}


struct packet_stream_pkt *packet_stream_get_next(struct packet_stream *stream, unsigned int *direction) {

	struct packet_stream_pkt *res = NULL;

	int dirs[2] = { *direction, (*direction == CT_DIR_FWD ? CT_DIR_REV : CT_DIR_FWD) };

	int i, cur_dir;
	for (i = 0; i < 2 && !res; i++) {
		
		*direction = dirs[i];
		cur_dir = *direction;

		while (stream->head[cur_dir]) {
			
			res = stream->head[cur_dir];

			if (stream->cur_buff_size >= stream->max_buff_size) {
				debug_stream("entry %p, packet %u.%06u, seq %u, ack %u : buffer full", stream, res->pkt->ts.tv_sec, res->pkt->ts.tv_usec, res->seq, res->ack);
				break; // Buffer is full, proceed with dequeueing
			}
		

			if (!packet_stream_is_packet_next(stream, res, *direction)) {
				res = NULL;
				break;
			}

			uint32_t cur_seq = stream->cur_seq[cur_dir];
			uint32_t seq = res->seq;
			// Check for duplicate bytes
			if (cur_seq != seq) {

				if (packet_stream_is_packet_old_dupe(stream, res, i)) {
					// Packet is a duplicate, remove it
					stream->head[cur_dir] = res->next;
					if (res->next) {
						res->next->prev = NULL;
					} else {
						stream->tail[cur_dir] = NULL;
					}

					if (res->prev) {
						pomlog(POMLOG_WARN "Dequeing packet which wasn't the first in the list. This shouldn't happen !");
						res->prev->next = res->next;
					}
					
					stream->cur_buff_size -= res->plen;
					packet_pool_release(res->pkt);
					free(res);
					// Next packet please
					continue;
				} else {
					if (packet_stream_remove_dupe_bytes(stream, res, *direction) == POM_ERR)
						return NULL;

				}
			}

			
			break;
			
		}
		
	}

	if (!res)
		return NULL;

	// Dequeue the packet
	

	stream->head[cur_dir] = res->next;
	if (res->next) {
		res->next->prev = res->prev;
	} else {
		stream->tail[cur_dir] = NULL;
	}

	stream->cur_buff_size -= res->plen;

	return res;
}


struct packet_stream_parser *packet_stream_parser_alloc(unsigned int max_line_size) {
	
	struct packet_stream_parser *res = malloc(sizeof(struct packet_stream_parser));
	if (!res) {
		pom_oom(sizeof(struct packet_stream_parser));
		return NULL;
	}

	memset(res, 0, sizeof(struct packet_stream_parser));

	res->max_line_size = max_line_size;

	debug_stream_parser("entry %p, allocated with max_line_size %u", res, max_line_size);

	return res;
}


int packet_stream_parser_add_payload(struct packet_stream_parser *sp, void *pload, unsigned int len) {

	if (sp->pload || sp->plen)
		pomlog(POMLOG_WARN "Warning, payload of last packet not entirely consumed !");

	sp->pload = pload;
	sp->plen = len;

	debug_stream_parser("entry %p, added pload %p with len %u", sp, pload, len);

	return POM_OK;
}

int packet_stream_parser_get_remaining(struct packet_stream_parser *sp, void **pload, unsigned int *len) {

	if (!sp->pload)
		return POM_OK;

	debug_stream_parser("entry %p, providing remaining pload %p with len %u", sp, sp->pload, sp->plen);

	*pload = sp->pload;
	*len = sp->plen;
	sp->pload = NULL;
	sp->plen = 0;

	return POM_OK;
};


int packet_stream_parser_get_line(struct packet_stream_parser *sp, char **line, unsigned int *len) {

	if (!line || !len)
		return POM_ERR;

	// Find the next line return in the current payload
	
	char *pload = sp->pload;
	
	int str_len = sp->plen, tmp_len = 0;
	
	char *lf = memchr(pload, '\n', sp->plen);
	if (lf) {
		tmp_len = lf - pload;
		str_len = tmp_len + 1;
		if (lf > pload && *(lf - 1) == '\r')
			tmp_len--;
	}

	if (sp->buffpos || !lf) {
		// If there is a buffer or line return is not found, we need to add to the buffer
		unsigned int new_len = sp->buffpos + tmp_len;
		if (sp->bufflen < new_len) {
			sp->buff = realloc(sp->buff, new_len);
			if (!sp->buff) {
				pom_oom(new_len + 1);
				return POM_ERR;
			}
			sp->bufflen = new_len + 1;
		}
		memcpy(sp->buff + sp->buffpos, sp->pload, tmp_len);
		sp->buffpos += tmp_len;

		if (sp->buffpos > sp->max_line_size) {
			// What to do ? discard it and send new partial line ?
			// Send it as is and send a new line afterwards ?
			// I'll take option two
			pomlog(POMLOG_DEBUG "Line longer than max size : %u , max %u", sp->buffpos, sp->max_line_size);
		}
	}

	if (!lf) {
		// \n not found
		*line = NULL;
		*len = 0;
		sp->pload = NULL;
		sp->plen = 0;
		debug_stream_parser("entry %p, no line found", sp);
		return POM_OK;
	}

	
	if (sp->buffpos) {
		pload = sp->buff;
		tmp_len = sp->buffpos;
		sp->buffpos = 0;
		return POM_OK;
	} else {
		sp->plen -= str_len;
		if (!sp->plen)
			sp->pload = NULL;
		else
			sp->pload += str_len;
	}

	// Trim the string
	while (*pload == ' ' && tmp_len) {
		pload++;
		tmp_len--;
	}
	while (pload[tmp_len] == ' ' && tmp_len)
		tmp_len--;

	*line = pload;
	*len = tmp_len;

	debug_stream_parser("entry %p, got line of %u bytes", sp, tmp_len);

	return POM_OK;
}



int packet_stream_parser_cleanup(struct packet_stream_parser *sp) {

	if (sp->buff)
		free(sp->buff);

	free(sp);

	return POM_OK;
}
