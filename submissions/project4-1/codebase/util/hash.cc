#include "hash.h"
#include <string.h>

namespace util
{
	unsigned int datahash(const void* data, size_t length, fn_one_integer_hash hash)
	{
		char buffer[4];
		const char* input = (const char*)data;
		unsigned int* strAsUInt = (unsigned int*)buffer;

		size_t i;
		unsigned int h = 0;
		
		// Take string in chunks of 4 bytes, treat them as an integer and hash on that value
		for (i = 4; i <= length; i += 4)
		{
			memcpy(buffer, input + i - 4, 4);
			h ^= hash(*strAsUInt);
		}

		// length was not divisible by 4, so we have leftovers
		i -= 4;
		if (i != length)
		{
			unsigned int leftoverBytes = length - i;
			memset(buffer, 0, 4);
			memcpy(buffer, input + i, leftoverBytes);

			h ^= hash(*strAsUInt);
		}

		// return final hashed value
		return h;
	}
}
