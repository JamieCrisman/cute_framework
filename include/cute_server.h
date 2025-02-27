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

#ifndef CUTE_SERVER_H
#define CUTE_SERVER_H

#include "cute_defines.h"
#include "cute_error.h"

#define CUTE_SERVER_MAX_CLIENTS 32

namespace cute
{

struct server_t;

struct server_config_t
{
	uint64_t application_id = 0;
	int max_incoming_bytes_per_second = 0;
	int max_outgoing_bytes_per_second = 0;
	int connection_timeout = 10;
	double resend_rate = 0.1f;
	crypto_sign_public_t public_key;
	crypto_sign_secret_t secret_key;
};

CUTE_API server_t* CUTE_CALL server_create(server_config_t* config, void* user_allocator_context = NULL);
CUTE_API void CUTE_CALL server_destroy(server_t* server);

CUTE_API error_t CUTE_CALL server_start(server_t* server, const char* address_and_port);
CUTE_API void CUTE_CALL server_stop(server_t* server);

enum server_event_type_t
{
	SERVER_EVENT_TYPE_NEW_CONNECTION,
	SERVER_EVENT_TYPE_DISCONNECTED,
	SERVER_EVENT_TYPE_PAYLOAD_PACKET,
};

struct server_event_t
{
	server_event_type_t type;
	union
	{
		struct
		{
			int client_index;
			uint64_t client_id;
			endpoint_t endpoint;
		} new_connection;

		struct
		{
			int client_index;
		} disconnected;

		struct
		{
			int client_index;
			void* data;
			int size;
		} payload_packet;
	} u;
};

CUTE_API bool CUTE_CALL server_pop_event(server_t* server, server_event_t* event);
CUTE_API void CUTE_CALL server_free_packet(server_t* server, int client_index, void* data);

CUTE_API void CUTE_CALL server_update(server_t* server, double dt, uint64_t current_time);
CUTE_API void CUTE_CALL server_disconnect_client(server_t* server, int client_index, bool notify_client = true);
CUTE_API void CUTE_CALL server_send(server_t* server, const void* packet, int size, int client_index, bool send_reliably);
CUTE_API void CUTE_CALL server_send_to_all_clients(server_t* server, const void* packet, int size, bool send_reliably);
CUTE_API void CUTE_CALL server_send_to_all_but_one_client(server_t* server, const void* packet, int size, int client_index, bool send_reliably);

CUTE_API bool CUTE_CALL server_is_client_connected(server_t* server, int client_index);
CUTE_API void CUTE_CALL server_enable_network_simulator(server_t* server, double latency, double jitter, double drop_chance, double duplicate_chance);

}

#endif // CUTE_SERVER_H
