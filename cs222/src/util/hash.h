#ifndef _util_hash_h_
#define _util_hash_h_

#include <limits>
#include <iostream>

typedef unsigned int(*fn_one_integer_hash)(unsigned int);
typedef unsigned int(*fn_two_integer_hash)(unsigned int, unsigned int);


namespace hash
{
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
        return parallel_multiply_hash(x * y + x + y);
    }

    // Designed so that hash(x,y) != hash(y,x)
    inline unsigned int distinct_xy(unsigned int x, unsigned int y)
    {
        // Szudzik's function
        return x >= y
            ? (x * x) + (x + y)
            : (y * y) + x;
    }
}

#endif // _util_hash_h_
