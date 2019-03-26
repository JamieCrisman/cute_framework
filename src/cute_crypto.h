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

#ifndef CUTE_CRYPTO_H
#define CUTE_CRYPTO_H

#include <cute_defines.h>

#include <libsodium/sodium.h>

#define CUTE_CRYPTO_ASYMMETRIC_BYTES ((int)crypto_box_SEALBYTES)
#define CUTE_CRYPTO_SYMMETRIC_BYTES ((int)crypto_secretbox_MACBYTES)

namespace cute
{

struct crypto_key_t;
struct crypto_nonce_t;

extern CUTE_API int CUTE_CALL crypto_encrypt_asymmetric(const crypto_key_t* endpoint_public_key, uint8_t* buffer, int size_to_encrypt, int buffer_size);
extern CUTE_API int CUTE_CALL crypto_decrypt_asymmetric(const crypto_key_t* your_public_key, const crypto_key_t* your_secret_key, uint8_t* buffer, int buffer_size);

extern CUTE_API int CUTE_CALL crypto_encrypt(const crypto_key_t* symmetric_key, uint8_t* buffer, int size_to_encrypt, int buffer_size, const crypto_nonce_t* nonce);
extern CUTE_API int CUTE_CALL crypto_decrypt(const crypto_key_t* symmetric_key, uint8_t* buffer, int buffer_size, const crypto_nonce_t* nonce);

}

#include <cute_crypto_utils.h>

#endif // CUTE_CRYPTO_H
