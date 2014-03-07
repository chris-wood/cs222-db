#ifndef _util_hash_h_
#define _util_hash_h_

#include <limits>
#include <iostream>
#include <cstring>

namespace util
{
	typedef unsigned int(*fn_one_integer_hash)(unsigned int);
	typedef unsigned int(*fn_two_integer_hash)(unsigned int, unsigned int);

	// hash arbitrary data to an int
	unsigned int datahash(const void* data, size_t length, fn_one_integer_hash hash);

	// hash a string to an int
	inline unsigned int strhash(const char* str, fn_one_integer_hash hash) { return datahash(str, strlen(str), hash); }

    // https://gist.github.com/badboy/6267743
    inline unsigned int multiplicitive(unsigned int x)
    {
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = ((x >> 16) ^ x);

        return x;
    }

    inline unsigned int shift32(unsigned int key)
    {
        key = ~key + (key << 15); // key = (key << 15) - key - 1;
        key = key ^ (key >> 12);
        key = key + (key << 2);
        key = key ^ (key >> 4);
        key = key * 2057; // key = (key + (key << 3)) + (key << 11);
        key = key ^ (key >> 16);

        return key;
    }

    inline unsigned int jenkins(unsigned int a)
    {
        a = (a + 0x7ed55d16) + (a << 12);
        a = (a ^ 0xc761c23c) ^ (a >> 19);
        a = (a + 0x165667b1) + (a << 5);
        a = (a + 0xd3a2646c) ^ (a << 9);
        a = (a + 0xfd7046c5) + (a << 3);
        a = (a ^ 0xb55a4f09) ^ (a >> 16);

        return a;
    }

    inline unsigned int parallel_shift(unsigned int key)
    {
        key ^= (key << 17) | (key >> 16);

        return key;
    }

    inline unsigned int parallel_multiply(unsigned int key)
    {
        key += (key << 3) + (key << 9); // key = key * (1 + 8 + 512)

        return key;
    }

    // http://burtleburtle.net/bob/hash/integer.html
    inline unsigned int half_avalanche(unsigned int a)
    {
        a = (a + 0x479ab41d) + (a << 8);
        a = (a ^ 0xe4aa10ce) ^ (a >> 5);
        a = (a + 0x9942f0a6) - (a << 14);
        a = (a ^ 0x5aedd67d) ^ (a >> 3);
        a = (a + 0x17bea992) + (a << 7);

        return a;
    }

    inline unsigned int thomas_wang(unsigned int a)
    {
        a += ~(a << 15);
        a ^= (a >> 10);
        a += (a << 3);
        a ^= (a >> 6);
        a += ~(a << 11);
        a ^= (a >> 16);

        return a;
    }

    // Designed so that hash(x,y) = hash(y,x) but keeps true hash(x,0) != hash(0.5x, 0.5x) etc...
    inline unsigned int commutative_xy(unsigned int x, unsigned int y)
    {
		return multiplicitive(x * y + x + y);
    }

    // Designed so that hash(x,y) != hash(y,x)
    inline unsigned int distinct_xy(unsigned int x, unsigned int y)
    {
        // Szudzik's function
        return x >= y
            ? (x * x) + (x + y)
            : (y * y) + x;
    }

	// arbitrary data hashes based on the various kinds of single integer hashes
	inline unsigned int datahash_ml(const void* data, size_t length) { return datahash(data, length, multiplicitive); }
	inline unsigned int datahash_sh(const void* data, size_t length) { return datahash(data, length, shift32); }
	inline unsigned int datahash_jk(const void* data, size_t length) { return datahash(data, length, jenkins); }
	inline unsigned int datahash_ps(const void* data, size_t length) { return datahash(data, length, parallel_shift); }
	inline unsigned int datahash_pm(const void* data, size_t length) { return datahash(data, length, parallel_multiply); }
	inline unsigned int datahash_ha(const void* data, size_t length) { return datahash(data, length, half_avalanche); }
	inline unsigned int datahash_tw(const void* data, size_t length) { return datahash(data, length, thomas_wang); }

	// String data hashes based on the various kinds of single integer hashes
	inline unsigned int strhash_ml(const char* str) { return strhash(str, multiplicitive); }
	inline unsigned int strhash_sh(const char* str) { return strhash(str, shift32); }
	inline unsigned int strhash_jk(const char* str) { return strhash(str, jenkins); }
	inline unsigned int strhash_ps(const char* str) { return strhash(str, parallel_shift); }
	inline unsigned int strhash_pm(const char* str) { return strhash(str, parallel_multiply); }
	inline unsigned int strhash_ha(const char* str) { return strhash(str, half_avalanche); }
	inline unsigned int strhash_tw(const char* str) { return strhash(str, thomas_wang); }

	inline unsigned int strhash(const char* str) { return strhash_ps(str); }
	inline unsigned int strhash(const std::string& str) { return strhash_ps(str.c_str()); }
}

#endif // _util_hash_h_
