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

#ifndef CUTE_PROTOCOL_H
#define CUTE_PROTOCOL_H

#include "cute_defines.h"
#include "cute_crypto.h"
#include "cute_net.h"

#define CUTE_PROTOCOL_VERSION_STRING ((const uint8_t*)"CUTE 1.00")
#define CUTE_PROTOCOL_VERSION_STRING_LEN (9 + 1)
#define CUTE_PROTOCOL_SERVER_MAX_CLIENTS 32
#define CUTE_PROTOCOL_PACKET_SIZE_MAX (CUTE_KB + 256)
#define CUTE_PROTOCOL_PACKET_PAYLOAD_MAX (1207 - 2)
#define CUTE_PROTOCOL_CLIENT_SEND_BUFFER_SIZE (256 * CUTE_KB)
#define CUTE_PROTOCOL_CLIENT_RECEIVE_BUFFER_SIZE (256 * CUTE_KB)
#define CUTE_PROTOCOL_SERVER_SEND_BUFFER_SIZE (CUTE_MB * 2)
#define CUTE_PROTOCOL_SERVER_RECEIVE_BUFFER_SIZE (CUTE_MB * 2)
#define CUTE_PROTOCOL_EVENT_QUEUE_SIZE (CUTE_MB * 4)
#define CUTE_PROTOCOL_SIGNATURE_SIZE 64

#define CUTE_CONNECT_TOKEN_PACKET_SIZE 1024
#define CUTE_CONNECT_TOKEN_SIZE 1114
#define CUTE_CONNECT_TOKEN_USER_DATA_SIZE 256
#define CUTE_CONNECT_TOKEN_SECRET_SECTION_SIZE (64 + 8 + 32 + 32 + 256)
#define CUTE_CONNECT_TOKEN_ENDPOINT_MAX 32

#define CUTE_REPLAY_BUFFER_SIZE 256
#define CUTE_PROTOCOL_SEND_RATE (1.0f / 10.0f)
#define CUTE_DISCONNECT_REDUNDANT_PACKET_COUNT 10
#define CUTE_CHALLENGE_DATA_SIZE 256
#define CUTE_PROTOCOL_REDUNDANT_DISCONNECT_PACKET_COUNT 10

namespace cute
{
namespace protocol
{

enum packet_type_t : uint8_t
{
	PACKET_TYPE_CONNECT_TOKEN,
	PACKET_TYPE_CONNECTION_ACCEPTED,
	PACKET_TYPE_CONNECTION_DENIED,
	PACKET_TYPE_KEEPALIVE,
	PACKET_TYPE_DISCONNECT,
	PACKET_TYPE_CHALLENGE_REQUEST,
	PACKET_TYPE_CHALLENGE_RESPONSE,
	PACKET_TYPE_PAYLOAD,

	PACKET_TYPE_COUNT,
};

struct packet_allocator_t;

CUTE_API packet_allocator_t* CUTE_CALL packet_allocator_create(void* user_allocator_context = NULL);
CUTE_API void CUTE_CALL packet_allocator_destroy(packet_allocator_t* packet_allocator);
CUTE_API void* CUTE_CALL packet_allocator_alloc(packet_allocator_t* packet_allocator, packet_type_t type);
CUTE_API void CUTE_CALL packet_allocator_free(packet_allocator_t* packet_allocator, packet_type_t type, void* packet);

// -------------------------------------------------------------------------------------------------

CUTE_API error_t CUTE_CALL generate_connect_token(
	uint64_t application_id,
	uint64_t creation_timestamp,
	const crypto_key_t* client_to_server_key,
	const crypto_key_t* server_to_client_key,
	uint64_t expiration_timestamp,
	uint32_t handshake_timeout,
	int endpoint_count,
	const char** endpoint_list,
	uint64_t client_id,
	const uint8_t* user_data,
	const crypto_sign_secret_t* shared_secret_key,
	uint8_t* token_ptr_out
);

// -------------------------------------------------------------------------------------------------

struct client_t;

enum client_state_t : int
{
	CLIENT_STATE_CONNECT_TOKEN_EXPIRED         = -6,
	CLIENT_STATE_INVALID_CONNECT_TOKEN         = -5,
	CLIENT_STATE_CONNECTION_TIMED_OUT          = -4,
	CLIENT_STATE_CHALLENGED_RESPONSE_TIMED_OUT = -3,
	CLIENT_STATE_CONNECTION_REQUEST_TIMED_OUT  = -2,
	CLIENT_STATE_CONNECTION_DENIED             = -1,
	CLIENT_STATE_DISCONNECTED                  =  0,
	CLIENT_STATE_SENDING_CONNECTION_REQUEST    =  1,
	CLIENT_STATE_SENDING_CHALLENGE_RESPONSE    =  2,
	CLIENT_STATE_CONNECTED                     =  3,
};

CUTE_API client_t* CUTE_CALL client_make(uint16_t port, uint64_t application_id, bool use_ipv6, void* user_allocator_context = NULL);
CUTE_API void CUTE_CALL client_destroy(client_t* client);

CUTE_API error_t CUTE_CALL client_connect(client_t* client, const uint8_t* connect_token);
CUTE_API void CUTE_CALL client_disconnect(client_t* client);
CUTE_API void CUTE_CALL client_update(client_t* client, double dt, uint64_t current_time);

CUTE_API bool CUTE_CALL client_get_packet(client_t* client, void** data, int* size, uint64_t* sequence);
CUTE_API void CUTE_CALL client_free_packet(client_t* client, void* packet);
CUTE_API error_t CUTE_CALL client_send(client_t* client, const void* data, int size);

CUTE_API client_state_t CUTE_CALL client_get_state(client_t* client);
CUTE_API uint64_t CUTE_CALL client_get_id(client_t* client);
CUTE_API uint32_t CUTE_CALL client_get_max_clients(client_t* client);
CUTE_API endpoint_t CUTE_CALL client_get_server_address(client_t* client);
CUTE_API uint16_t CUTE_CALL client_get_port(client_t* client);
CUTE_API void CUTE_CALL client_enable_network_simulator(client_t* client, double latency, double jitter, double drop_chance, double duplicate_chance);

// -------------------------------------------------------------------------------------------------

struct server_t;

CUTE_API server_t* CUTE_CALL server_make(uint64_t application_id, const crypto_sign_public_t* public_key, const crypto_sign_secret_t* secret_key, void* mem_ctx = NULL);
CUTE_API void CUTE_CALL server_destroy(server_t* server);

CUTE_API error_t CUTE_CALL server_start(server_t* server, const char* address, uint32_t connection_timeout);
CUTE_API void CUTE_CALL server_stop(server_t* server);
CUTE_API bool CUTE_CALL server_running(server_t* server);

CUTE_API void CUTE_CALL server_update(server_t* server, double dt, uint64_t current_time);
CUTE_API void CUTE_CALL server_disconnect_client(server_t* server, int client_index, bool notify_client);

CUTE_API int CUTE_CALL server_client_count(server_t* server);
CUTE_API uint64_t CUTE_CALL server_get_client_id(server_t* server, int client_index);
CUTE_API bool CUTE_CALL server_is_client_connected(server_t* server, int client_index);

enum server_event_type_t : int
{
	SERVER_EVENT_NEW_CONNECTION,
	SERVER_EVENT_DISCONNECTED,
	SERVER_EVENT_PAYLOAD_PACKET,
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
CUTE_API void CUTE_CALL server_free_packet(server_t* server, void* packet);
CUTE_API void CUTE_CALL server_disconnect_client(server_t* server, int client_index, bool notify_client);
CUTE_API error_t CUTE_CALL server_send_to_client(server_t* server, const void* packet, int size, int client_index);
CUTE_API void CUTE_CALL server_enable_network_simulator(server_t* server, double latency, double jitter, double drop_chance, double duplicate_chance);

// -------------------------------------------------------------------------------------------------

}
}

#endif // CUTE_PROTOCOL_H
