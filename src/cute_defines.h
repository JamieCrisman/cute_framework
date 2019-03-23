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

#ifndef CUTE_DEFINES_H
#define CUTE_DEFINES_H

#ifndef _CRT_SECURE_NO_WARNINGS
#	define _CRT_SECURE_NO_WARNINGS
#endif

#ifndef _CRT_NONSTDC_NO_DEPRECATE
#	define _CRT_NONSTDC_NO_DEPRECATE
#endif

#ifndef NOMINMAX
#	define NOMINMAX
#endif

#ifdef _MSC_VER
#	ifdef CUTE_EXPORT
#		define CUTE_API __declspec(dllexport)
#	else
#		define CUTE_API __declspec(dllimport)
#	endif
#else
#	if ((__GNUC__ >= 4) || defined(__clang__))
#		define CUTE_API __attribute__((visibility("default")))
#	else
#		define CUTE_API
#	endif
#endif

#define CUTE_CALL __cdecl

#define CUTE_UNUSED(x) (void)x

#define CUTE_INLINE inline

#include <stdint.h>

#define CUTE_KB 1024
#define CUTE_MB (CUTE_KB * CUTE_KB)
#define CUTE_GB (CUTE_MB * CUTE_MB)

#define CUTE_SERIALIZE_CHECK(x) do { if ((x) != SERIALIZE_SUCCESS) goto cute_error; } while (0)

#define CUTE_PROTOCOL_VERSION "CUTE PROTOCOL VERSION 1.00"

namespace cute
{

struct app_t;

}

#endif // CUTE_DEFINES_H
