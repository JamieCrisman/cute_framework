/*
	Cute Framework
	Copyright (C) 2019 Randy Gaul https://randygaul.net

	This software is provided 'as-is', without any express or implied
	warranty.  In no event will the authors be held liable for any damages
	arising from the use of this software.

	Permission is granted to anyone to use this software for any purpose,
	including commercial applications, and to alter it and redistribute it
	freely, subject to the following restrictions:

	1. The origin of this software must not be misrepresented; you must not
	   claim that you wrote the original software. If you use this software
	   in a product, an acknowledgment in the product documentation would be
	   appreciated but is not required.
	2. Altered source versions must be plainly marked as such, and must not be
	   misrepresented as being the original software.
	3. This notice may not be removed or altered from any source distribution.
*/

#include <cute_alloc.h>
#include <cute_c_runtime.h>
#include <cute_buffer.h>
#include <cute_handle_table.h>

#include <internal/cute_defines_internal.h>
#include <internal/cute_transport_internal.h>
#include <internal/cute_serialize_internal.h>

#include <math.h>
#include <float.h>

namespace cute
{

// Sequence buffer implementation strategy comes from Glenn's online articles:
// https://gafferongames.com/post/reliable_ordered_messages/

int sequence_buffer_init(sequence_buffer_t* buffer, int capacity, int stride, void* mem_ctx)
{
	CUTE_MEMSET(buffer, 0, sizeof(sequence_buffer_t));
	buffer->capacity = capacity;
	buffer->stride = stride;
	buffer->entry_sequence = (uint32_t*)CUTE_ALLOC(sizeof(uint32_t) * capacity, mem_ctx);
	CUTE_CHECK_POINTER(buffer->entry_sequence);
	buffer->entry_data = (uint8_t*)CUTE_ALLOC(stride * capacity, mem_ctx);
	CUTE_CHECK_POINTER(buffer->entry_data);
	buffer->mem_ctx = mem_ctx;
	sequence_buffer_reset(buffer);
	return 0;

cute_error:
	CUTE_FREE(buffer->entry_sequence, mem_ctx);
	CUTE_FREE(buffer->entry_data, mem_ctx);
	return -1;
}

void sequence_buffer_cleanup(sequence_buffer_t* buffer, sequence_buffer_cleanup_entry_fn* cleanup_fn)
{
	for (int i = 0; i < buffer->capacity; ++i)
	{
		sequence_buffer_remove(buffer, i, cleanup_fn);
	}

	CUTE_FREE(buffer->entry_sequence, buffer->mem_ctx);
	CUTE_FREE(buffer->entry_data, buffer->mem_ctx);
	CUTE_MEMSET(buffer, 0, sizeof(sequence_buffer_t));
}

void sequence_buffer_reset(sequence_buffer_t* buffer, sequence_buffer_cleanup_entry_fn* cleanup_fn)
{
	for (int i = 0; i < buffer->capacity; ++i)
	{
		sequence_buffer_remove(buffer, i, cleanup_fn);
	}

	buffer->sequence = 0;
	CUTE_MEMSET(buffer->entry_sequence, ~0, sizeof(uint32_t) * buffer->capacity);
}

static void s_sequence_buffer_remove_entries(sequence_buffer_t* buffer, int sequence_a, int sequence_b, sequence_buffer_cleanup_entry_fn* cleanup_fn = NULL)
{
	if (sequence_b < sequence_a) sequence_b += 65536;
	if (sequence_b - sequence_a < buffer->capacity) {
		for (int sequence = sequence_a; sequence <= sequence_b; ++sequence)
		{
			int index = sequence % buffer->capacity;
			if (cleanup_fn) cleanup_fn(buffer->entry_data + buffer->stride * index, buffer->mem_ctx);
			buffer->entry_sequence[index] = 0xFFFFFFFF;
		}
	} else {
		for (int i = 0; i < buffer->capacity; ++i)
		{
			if (cleanup_fn) cleanup_fn(buffer->entry_data + buffer->stride * i, buffer->mem_ctx);
			buffer->entry_sequence[i] = 0xFFFFFFFF;
		}
	}
}

static CUTE_INLINE int s_sequence_greater_than(uint16_t a, uint16_t b)
{
	return ((a > b) && (a - b <= 32768)) |
	       ((a < b) && (b - a  > 32768));
}

static CUTE_INLINE int s_sequence_less_than(uint16_t a, uint16_t b)
{
	return s_sequence_greater_than(b, a);
}

void sequence_buffer_advance(sequence_buffer_t* buffer, uint16_t sequence)
{
	if (s_sequence_greater_than(sequence + 1, buffer->sequence)) {
		s_sequence_buffer_remove_entries(buffer, buffer->sequence, sequence, NULL);
		buffer->sequence = sequence + 1;
	}
}

static CUTE_INLINE int s_sequence_is_stale(sequence_buffer_t* buffer, uint16_t sequence)
{
	return s_sequence_less_than(sequence, buffer->sequence - ((uint16_t)buffer->capacity));
}

void* sequence_buffer_insert(sequence_buffer_t* buffer, uint16_t sequence, sequence_buffer_cleanup_entry_fn* cleanup_fn)
{
	if (s_sequence_greater_than(sequence + 1, buffer->sequence)) {
		s_sequence_buffer_remove_entries(buffer, buffer->sequence, sequence, cleanup_fn);
		buffer->sequence = sequence + 1;
	} else if (s_sequence_is_stale(buffer, sequence)) {
		return NULL;
	}
	int index = sequence % buffer->capacity;
	if (cleanup_fn && buffer->entry_sequence[index] != 0xFFFFFFFF) {
		cleanup_fn(buffer->entry_data + buffer->stride * (sequence % buffer->capacity), buffer->mem_ctx);
	}
	buffer->entry_sequence[index] = sequence;
	return buffer->entry_data + index * buffer->stride;
}

void sequence_buffer_remove(sequence_buffer_t* buffer, uint16_t sequence, sequence_buffer_cleanup_entry_fn* cleanup_fn)
{
	int index = sequence % buffer->capacity;
	if (buffer->entry_sequence[index] != 0xFFFFFFFF)
	{
		buffer->entry_sequence[index] = 0xFFFFFFFF;
		if (cleanup_fn) cleanup_fn(buffer->entry_data + buffer->stride * index, buffer->mem_ctx);
	}
}

int sequence_buffer_is_empty(sequence_buffer_t* sequence_buffer, uint16_t sequence)
{
	return sequence_buffer->entry_sequence[sequence % sequence_buffer->capacity] == 0xFFFFFFFF;
}

void* sequence_buffer_find(sequence_buffer_t* sequence_buffer, uint16_t sequence)
{
	int index = sequence % sequence_buffer->capacity;
	return ((sequence_buffer->entry_sequence[index] == (uint32_t)sequence)) ? (sequence_buffer->entry_data + index * sequence_buffer->stride) : NULL;
}

void* sequence_buffer_at_index(sequence_buffer_t* sequence_buffer, int index)
{
	CUTE_ASSERT(index >= 0);
	CUTE_ASSERT(index < sequence_buffer->capacity);
	return sequence_buffer->entry_sequence[index] != 0xFFFFFFFF ? (sequence_buffer->entry_data + index * sequence_buffer->stride) : NULL;
}

void sequence_buffer_generate_ack_bits(sequence_buffer_t* sequence_buffer, uint16_t* ack, uint32_t* ack_bits)
{
	*ack = sequence_buffer->sequence - 1;
	*ack_bits = 0;
	uint32_t mask = 1;
	for (int i = 0; i < 32; ++i)
	{
		uint16_t sequence = *ack - ((uint16_t)i);
		if (sequence_buffer_find(sequence_buffer, sequence)) {
			*ack_bits |= mask;
		}
		mask <<= 1;
	}
}

// -------------------------------------------------------------------------------------------------

struct ack_system_t
{
	double time;
	int max_packet_size;
	int initial_ack_capacity;

	int (*send_packet_fn)(uint16_t sequence, void* packet, int size, void* udata);
	int (*open_packet_fn)(uint16_t sequence, void* packet, int size, void* udata);

	void* udata;
	void* mem_ctx;

	uint16_t sequence;
	buffer_t acks;
	sequence_buffer_t sent_packets;
	sequence_buffer_t received_packets;

	float rtt;
	float packet_loss;
	float outgoing_bandwidth_kbps;
	float incoming_bandwidth_kbps;

	uint64_t counters[ACK_SYSTEM_COUNTERS_MAX];
};

struct sent_packet_t
{
	double timestamp;
	int acked;
	int size;
};

struct received_packet_t
{
	double timestamp;
	int size;
};

ack_system_t* ack_system_make(const ack_system_config_t* config)
{
	int sent_packets_init = 0;
	int received_packets_init = 0;
	void* mem_ctx = config->user_allocator_context;

	if (!config->send_packet_fn || !config->open_packet_fn) return NULL;
	if (config->max_packet_size > CUTE_TRANSPORT_PACKET_PAYLOAD_MAX) return NULL;

	ack_system_t* ack_system = (ack_system_t*)CUTE_ALLOC(sizeof(ack_system_t), mem_ctx);
	if (!ack_system) return NULL;

	ack_system->time = 0;
	ack_system->max_packet_size = config->max_packet_size;
	ack_system->initial_ack_capacity = config->initial_ack_capacity;
	ack_system->send_packet_fn = config->send_packet_fn;
	ack_system->open_packet_fn = config->open_packet_fn;
	ack_system->udata = config->udata;
	ack_system->mem_ctx = config->user_allocator_context;

	ack_system->sequence = 0;
	ack_system->acks = buffer_make(sizeof(uint16_t));
	CUTE_CHECK(sequence_buffer_init(&ack_system->sent_packets, config->sent_packets_sequence_buffer_size, sizeof(sent_packet_t), mem_ctx));
	sent_packets_init = 1;
	CUTE_CHECK(sequence_buffer_init(&ack_system->received_packets, config->received_packets_sequence_buffer_size, sizeof(received_packet_t), mem_ctx));
	received_packets_init = 1;

	ack_system->rtt = 0;
	ack_system->packet_loss = 0;
	ack_system->outgoing_bandwidth_kbps = 0;
	ack_system->incoming_bandwidth_kbps = 0;

	for (int i = 0; i < ACK_SYSTEM_COUNTERS_MAX; ++i) {
		ack_system->counters[i] = 0;
	}

	return ack_system;

cute_error:
	if (ack_system)
	{
		if (sent_packets_init) sequence_buffer_cleanup(&ack_system->sent_packets);
		if (received_packets_init) sequence_buffer_cleanup(&ack_system->received_packets);
	}
	CUTE_FREE(ack_system, config->user_allocator_context);
	return NULL;
}

void ack_system_destroy(ack_system_t* ack_system)
{
	buffer_free(&ack_system->acks);
	sequence_buffer_cleanup(&ack_system->sent_packets);
	sequence_buffer_cleanup(&ack_system->received_packets);
	CUTE_FREE(ack_system, ack_system->mem_ctx);
}

void ack_system_reset(ack_system_t* ack_system)
{
	ack_system->sequence = 0;

	buffer_clear(&ack_system->acks);
	sequence_buffer_reset(&ack_system->sent_packets);
	sequence_buffer_reset(&ack_system->received_packets);

	ack_system->rtt = 0;
	ack_system->packet_loss = 0;
	ack_system->outgoing_bandwidth_kbps = 0;
	ack_system->incoming_bandwidth_kbps = 0;

	for (int i = 0; i < ACK_SYSTEM_COUNTERS_MAX; ++i) {
		ack_system->counters[i] = 0;
	}
}

static int s_write_ack_system_header(uint8_t* buffer, uint16_t sequence, uint16_t ack, uint32_t ack_bits)
{
	uint8_t* buffer_start = buffer;
	write_uint16(&buffer, sequence);
	write_uint16(&buffer, ack);
	write_uint32(&buffer, ack_bits);
	return (int)(buffer - buffer_start);
}

int ack_system_send_packet(ack_system_t* ack_system, void* data, int size, uint16_t* sequence_out)
{
	if (size > ack_system->max_packet_size || size > CUTE_ACK_SYSTEM_MAX_PACKET_SIZE) {
		ack_system->counters[ACK_SYSTEM_COUNTERS_PACKETS_TOO_LARGE_TO_SEND]++;
		return -1;
	}

	uint16_t sequence = ack_system->sequence++;
	uint16_t ack;
	uint32_t ack_bits;

	sequence_buffer_generate_ack_bits(&ack_system->received_packets, &ack, &ack_bits);
	sent_packet_t* packet = (sent_packet_t*)sequence_buffer_insert(&ack_system->sent_packets, sequence);

	packet->timestamp = ack_system->time;
	packet->acked = 0;
	packet->size = size + CUTE_ACK_SYSTEM_HEADER_SIZE;

	uint8_t buffer[CUTE_TRANSPORT_PACKET_PAYLOAD_MAX];
	int header_size = s_write_ack_system_header(buffer, sequence, ack, ack_bits);
	CUTE_ASSERT(header_size == CUTE_ACK_SYSTEM_HEADER_SIZE);
	CUTE_ASSERT(size + header_size < CUTE_TRANSPORT_PACKET_PAYLOAD_MAX);
	CUTE_MEMCPY(buffer + header_size, data, size);
	if (ack_system->send_packet_fn(sequence, buffer, size + header_size, ack_system->udata) < 0) {
		ack_system->counters[ACK_SYSTEM_COUNTERS_PACKETS_INVALID]++;
		return -1;
	}

	ack_system->counters[ACK_SYSTEM_COUNTERS_PACKETS_SENT]++;

	if (sequence_out) *sequence_out = sequence;
	return 0;
}

uint16_t ack_system_get_sequence(ack_system_t* ack_system)
{
	return ack_system->sequence;
}

static int s_read_ack_system_header(uint8_t* buffer, int size, uint16_t* sequence, uint16_t* ack, uint32_t* ack_bits)
{
	if (size < CUTE_ACK_SYSTEM_HEADER_SIZE) return -1;
	uint8_t* buffer_start = buffer;
	*sequence = read_uint16(&buffer);
	*ack = read_uint16(&buffer);
	*ack_bits = read_uint32(&buffer);
	return (int)(buffer - buffer_start);
}

int ack_system_receive_packet(ack_system_t* ack_system, void* data, int size)
{
	if (size > ack_system->max_packet_size || size > CUTE_ACK_SYSTEM_MAX_PACKET_SIZE) {
		ack_system->counters[ACK_SYSTEM_COUNTERS_PACKETS_TOO_LARGE_TO_RECEIVE]++;
		return -1;
	}

	ack_system->counters[ACK_SYSTEM_COUNTERS_PACKETS_RECEIVED]++;

	uint16_t sequence;
	uint16_t ack;
	uint32_t ack_bits;
	uint8_t* buffer = (uint8_t*)data;

	int header_size = s_read_ack_system_header(buffer, size, &sequence, &ack, &ack_bits);
	if (header_size < 0) {
		ack_system->counters[ACK_SYSTEM_COUNTERS_PACKETS_INVALID]++;
		return -1;
	}
	CUTE_ASSERT(header_size == CUTE_ACK_SYSTEM_HEADER_SIZE);

	if (s_sequence_is_stale(&ack_system->received_packets, sequence)) {
		ack_system->counters[ACK_SYSTEM_COUNTERS_PACKETS_STALE]++;
		return -1;
	}

	if (ack_system->open_packet_fn(sequence, buffer + header_size, size - header_size, ack_system->udata) < 0) {
		return -1;
	}

	received_packet_t* packet = (received_packet_t*)sequence_buffer_insert(&ack_system->received_packets, sequence);
	packet->timestamp = ack_system->time;
	packet->size = size;
	
	for (int i = 0; i < 32; ++i)
	{
		int bit_was_set = ack_bits & 1;
		ack_bits >>= 1;

		if (bit_was_set) {
			uint16_t ack_sequence = ack - ((uint16_t)i);
			sent_packet_t* sent_packet = (sent_packet_t*)sequence_buffer_find(&ack_system->sent_packets, ack_sequence);

			if (sent_packet && !sent_packet->acked) {
				buffer_check_grow(&ack_system->acks, ack_system->initial_ack_capacity);
				buffer_push(&ack_system->acks, &ack_sequence);
				ack_system->counters[ACK_SYSTEM_COUNTERS_PACKETS_ACKED]++;
				sent_packet->acked = 1;

				float rtt = (float)(ack_system->time - sent_packet->timestamp);
				ack_system->rtt += (rtt - ack_system->rtt) * 0.001f;
				if (ack_system->rtt < 0) ack_system->rtt = 0;
			}
		}

	}

	return 0;
}

uint16_t* ack_system_get_acks(ack_system_t* ack_system)
{
	return (uint16_t*)ack_system->acks.data;
}

int ack_system_get_acks_count(ack_system_t* ack_system)
{
	return ack_system->acks.count;
}

void ack_system_clear_acks(ack_system_t* ack_system)
{
	buffer_clear(&ack_system->acks);
}

static CUTE_INLINE float s_calc_packet_loss(float packet_loss, sequence_buffer_t* sent_packets)
{
	int packet_count = 0;
	int packet_drop_count = 0;

	for (int i = 0; i < sent_packets->capacity; ++i)
	{
		sent_packet_t* packet = (sent_packet_t*)sequence_buffer_at_index(sent_packets, i);
		if (packet) {
			packet_count++;
			if (!packet->acked) packet_drop_count++;
		}
	}

	float loss = (float)packet_drop_count / (float)packet_count;
	packet_loss += (loss - packet_loss) * 0.1f;
	if (packet_loss < 0) packet_loss = 0;
	return packet_loss;
}

static CUTE_INLINE float s_calc_bandwidth(float bandwidth, sequence_buffer_t* sent_packets)
{
	int bytes_sent = 0;
	double start_timestamp = DBL_MAX;
	double end_timestamp = 0;

	for (int i = 0; i < sent_packets->capacity; ++i)
	{
		sent_packet_t* packet = (sent_packet_t*)sequence_buffer_at_index(sent_packets, i);
		if (packet) {
			bytes_sent += packet->size;
			if (packet->timestamp < start_timestamp) start_timestamp = packet->timestamp;
			if (packet->timestamp > end_timestamp) end_timestamp = packet->timestamp;
		}
	}

	if (start_timestamp != DBL_MAX) {
		float sent_bandwidth = (float)(((double)bytes_sent / 1024.0) / (end_timestamp - start_timestamp));
		bandwidth += (sent_bandwidth - bandwidth) * 0.1f;
		if (bandwidth < 0) bandwidth = 0;
	}

	return bandwidth;
}

void ack_system_update(ack_system_t* ack_system, float dt)
{
	ack_system->time += dt;
	ack_system->packet_loss = s_calc_packet_loss(ack_system->packet_loss, &ack_system->sent_packets);
	ack_system->incoming_bandwidth_kbps = s_calc_bandwidth(ack_system->incoming_bandwidth_kbps, &ack_system->sent_packets);
	ack_system->outgoing_bandwidth_kbps = s_calc_bandwidth(ack_system->outgoing_bandwidth_kbps, &ack_system->received_packets);
}

float ack_system_rtt(ack_system_t* ack_system)
{
	return ack_system->rtt;
}

float ack_system_packet_loss(ack_system_t* ack_system)
{
	return ack_system->packet_loss;
}

float ack_system_bandwidth_outgoing_kbps(ack_system_t* ack_system)
{
	return ack_system->outgoing_bandwidth_kbps;
}

float ack_system_bandwidth_incoming_kbps(ack_system_t* ack_system)
{
	return ack_system->incoming_bandwidth_kbps;
}

uint64_t ack_system_get_counter(ack_system_t* ack_system, ack_system_counter_t counter)
{
	return ack_system->counters[counter];
}

// -------------------------------------------------------------------------------------------------

struct fragment_t
{
	double timestamp;
	uint8_t* data;
};

struct transport_t
{
	int fragment_size;
	int max_fragments_in_flight;
	int fragment_memory_pool_element_count;
	int max_size_single_send;

	int fragment_count;
	int fragment_capacity;
	fragment_t* fragments;
	handle_table_t fragment_handle_table;

	ack_system_t* ack_system;

	uint16_t last_acked_reassembly_sequence;
	uint16_t reassembly_sequence;
	sequence_buffer_t reliable_sent_fragments;
	sequence_buffer_t fragment_reassembly;
	sequence_buffer_t reliable_received_packets;

	void* mem_ctx;
};

struct fragment_entry_t
{
	handle_t fragment_handle;
};

struct fragment_reassembly_entry_t
{
	int packet_size;
	uint8_t* packet;

	int fragment_count_so_far;
	int fragments_total;
	uint8_t* fragment_received;
};

struct reliable_packet_entry_t
{
	int size;
	uint8_t* packet;
};

static void s_fragment_reassembly_entry_cleanup(void* data, void* mem_ctx)
{
	fragment_reassembly_entry_t* reassembly = (fragment_reassembly_entry_t*)data;
	CUTE_FREE(reassembly->packet, mem_ctx);
	CUTE_FREE(reassembly->fragment_received, mem_ctx);
}

transport_t* transport_make(const transport_configuration_t* config)
{
}

void transport_destroy(transport_t* transport)
{
}

void transport_reset(transport_t* tranpsport)
{
}

static CUTE_INLINE int s_transport_write_header(uint8_t* buffer, int size, uint8_t prefix, uint16_t sequence, uint16_t fragment_count, uint16_t fragment_index, uint16_t fragment_size)
{
	if (size < CUTE_TRANSPORT_HEADER_SIZE) return -1;
	uint8_t* buffer_start = buffer;
	write_uint8(&buffer, prefix);
	write_uint16(&buffer, sequence);
	write_uint16(&buffer, fragment_count);
	write_uint16(&buffer, fragment_index);
	write_uint16(&buffer, fragment_size);
	return (int)(buffer - buffer_start);
}

int transport_send_reliably_and_in_order(transport_t* transport, void* data, int size)
{
	if (size < 1) return -1;
	if (size > transport->max_size_single_send) return -1;

	int fragment_size = transport->fragment_size;
	int fragment_count = size / fragment_size;
	int final_fragment_size = size - (fragment_count * fragment_size);
	if (final_fragment_size > 0) fragment_count++;

	double timestamp = transport->ack_system->time;
	uint64_t reassembly_sequence = transport->reassembly_sequence++;

	if (fragment_count > 1) {
		uint8_t* data_ptr = (uint8_t*)data;
		for (int i = 0; i < fragment_count; ++i)
		{
			// Allocate fragment.
			int this_fragment_size = i != fragment_count - 1 ? fragment_size : final_fragment_size;
			uint8_t* fragment_src = data_ptr + fragment_size * i;
			CUTE_ASSERT(this_fragment_size + CUTE_TRANSPORT_HEADER_SIZE <= CUTE_ACK_SYSTEM_MAX_PACKET_SIZE);
			fragment_t* fragment = (fragment_t*)memory_pool_alloc(transport->fragment_pool);
			if (!fragment) {
				return -1;
			}
			fragment->timestamp = timestamp;
			list_init_node(&fragment->node);
			list_push_front(&transport->unacked_fragment_list, &fragment->node);

			// Write the transport header.
			int header_size = s_transport_write_header(fragment->data, this_fragment_size + CUTE_TRANSPORT_HEADER_SIZE, 1, reassembly_sequence, fragment_count, (uint16_t)i, (uint16_t)this_fragment_size);
			if (header_size != CUTE_TRANSPORT_HEADER_SIZE) {
				memory_pool_free(transport->fragment_pool, fragment);
				return -1;
			}

			// Copy over the `data` from user.
			CUTE_MEMCPY(fragment->data + header_size, data, this_fragment_size);

			// Send to ack system.
			uint16_t sequence;
			if (ack_system_send_packet(transport->ack_system, fragment->data, this_fragment_size, &sequence) < 0) {
				memory_pool_free(transport->fragment_pool, fragment);
				return -1;
			}

			// If all succeeds, record fragment entry. Hopefully it will be acked later.
			fragment_entry_t* fragment_entry = (fragment_entry_t*)sequence_buffer_insert(&transport->reliable_sent_fragments, sequence);
			fragment_entry->fragment = fragment;
		}
	} else {
		CUTE_ASSERT(size == final_fragment_size);

		fragment_t* fragment = (fragment_t*)memory_pool_alloc(transport->fragment_pool);
		if (!fragment) {
			return -1;
		}

		// Write the fragment header of 0 byte, represents a lonesome fragment.
		uint8_t* fragment_data = fragment->data;
		write_uint8(&fragment_data, 0);
		write_uint64(&fragment_data, reassembly_sequence);

		CUTE_MEMCPY(fragment->data + 1, data, size);

		uint16_t sequence;
		if (ack_system_send_packet(transport->ack_system, fragment->data, size, &sequence) < 0) {
			memory_pool_free(transport->fragment_pool, fragment);
			return -1;
		}

		fragment_entry_t* fragment_entry = (fragment_entry_t*)sequence_buffer_insert(&transport->reliable_sent_fragments, sequence);
		fragment_entry->fragment = fragment;
	}
}

int transport_send_fire_and_forget(transport_t* transport, void* data, int size)
{
}

int transport_recieve(transport_t* transport, void** data, int* size)
{
	uint16_t sequence = transport->last_acked_reassembly_sequence;
	uint16_t end_sequence = transport->reassembly_sequence;

	if (s_sequence_less_than(sequence, end_sequence))
	{
		reliable_packet_entry_t* entry = (reliable_packet_entry_t*)sequence_buffer_insert(&transport->reliable_received_packets, sequence);
		if (!entry) {
			return -1;
		}

		*data = entry->packet;
		*size = entry->size;
		transport->last_acked_reassembly_sequence++;

		return 0;
	} else {
		return -1;
	}
}

void transport_free(transport_t* transport, void* data)
{
	CUTE_FREE(data, transport->mem_ctx);
}

int transport_process_packet(transport_t* transport, uint8_t* data, int size)
{
	if (size < CUTE_TRANSPORT_HEADER_SIZE) return -1;

	uint8_t* buffer = data;
	uint8_t prefix = read_uint8(&buffer);
	uint16_t reassembly_sequence = read_uint16(&buffer);
	uint16_t fragment_count = read_uint16(&buffer);
	uint16_t fragment_index = read_uint16(&buffer);
	uint16_t fragment_size = read_uint16(&buffer);
	int total_packet_size = fragment_count * transport->fragment_size;

	if (total_packet_size > transport->max_size_single_send) {
		return -1;
	}

	if (fragment_index > fragment_count) {
		return -1;
	}

	if (fragment_size > transport->fragment_size) {
		return -1;
	}

	fragment_reassembly_entry_t* reassembly = (fragment_reassembly_entry_t*)sequence_buffer_find(&transport->fragment_reassembly, reassembly_sequence);
	if (!reassembly) {
		reassembly = (fragment_reassembly_entry_t*)sequence_buffer_insert(&transport->fragment_reassembly, reassembly_sequence, s_fragment_reassembly_entry_cleanup);
		reassembly->packet_size = total_packet_size;
		reassembly->packet = (uint8_t*)CUTE_ALLOC(total_packet_size, transport->mem_ctx);
		if (!reassembly->packet) return -1;
		reassembly->fragment_received = (uint8_t*)CUTE_ALLOC(fragment_count, transport->mem_ctx);
		if (!reassembly->fragment_received) {
			CUTE_FREE(reassembly->packet, transport->mem_ctx);
			return -1;
		}
		CUTE_MEMSET(reassembly->fragment_received, 0, fragment_count);
		reassembly->fragment_count_so_far = 0;
		reassembly->fragments_total = fragment_count;
	}

	if (fragment_count != reassembly->fragments_total) {
		return -1;
	}

	if (reassembly->fragment_received[fragment_index]) {
		return -1;
	}

	reassembly->fragment_count_so_far++;
	reassembly->fragment_received[fragment_index] = 1;

	uint8_t* packet_fragment = reassembly->packet + fragment_index * transport->fragment_size;
	CUTE_MEMCPY(packet_fragment, buffer, fragment_size - CUTE_TRANSPORT_HEADER_SIZE);

	if (reassembly->fragment_count_so_far == fragment_count) {
		reliable_packet_entry_t* entry = (reliable_packet_entry_t*)sequence_buffer_insert(&transport->reliable_received_packets, reassembly_sequence);
		entry->size = total_packet_size;
		entry->packet = reassembly->packet;
		reassembly->packet = NULL;
		sequence_buffer_remove(&transport->fragment_reassembly, reassembly_sequence, s_fragment_reassembly_entry_cleanup);
	}
}

void transport_process_acks(transport_t* transport, uint16_t* acks, int ack_count)
{
	for (int i = 0; i < ack_count; ++i)
	{
		uint16_t sequence = acks[i];
		fragment_entry_t* fragment_entry = (fragment_entry_t*)sequence_buffer_find(&transport->reliable_sent_fragments, sequence);

		if (fragment_entry) {
			handle_t h = fragment_entry->fragment_handle;
			if (handle_is_valid(&transport->fragment_handle_table, h)) {
				// Free the fragment data and destroy the handle.
				int index = handle_table_get_index(&transport->fragment_handle_table, h);
				fragment_t fragment;
				buffer_at(&transport->fragments, index, &fragment);
				CUTE_FREE(fragment.data, transport->mem_ctx);
				handle_table_free(&transport->fragment_handle_table, h);

				// Remove element and swap with last.
				buffer_at(&transport->fragments, --transport->fragments.count, &fragment);
				buffer_set(&transport->fragments, index, &fragment);
			}
		}

		sequence_buffer_remove(&transport->reliable_sent_fragments, sequence);
	}
}

void transport_resend_unacked_fragments(transport_t* transport)
{
}

}
