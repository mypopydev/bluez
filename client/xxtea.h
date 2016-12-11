/***********************************************************************

    Copyright 2006-2009 Ma Bingyao
    Copyright 2013 Gao Chunhui, Liu Tao

    These sources is free software. Redistributions of source code must
    retain the above copyright notice. Redistributions in binary form
    must reproduce the above copyright notice. You can redistribute it
    freely. You can use it with any free or commercial software.

    These sources is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY. Without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

        github: https://github.com/liut/pecl-xxtea

*************************************************************************/

#ifndef XXTEA_H_
#define XXTEA_H_

#include <stddef.h> /* for size_t & NULL declarations */
#include <stdint.h>

typedef uint32_t xxtea_long;


#define XXTEA_MX (z >> 5 ^ y << 2) + (y >> 3 ^ z << 4) ^ (sum ^ y) + (k[p & 3 ^ e] ^ z)
//#define XXTEA_MX ((z >> 5 ^ y << 2) + (y >> 3 ^ z << 4) ^ (sum ^ y) + (k[p & 3 ^ e] ^ z))
#define XXTEA_DELTA 0x9e3779b9

void xxtea_long_encrypt(xxtea_long *v, xxtea_long len, xxtea_long *k);
void xxtea_long_decrypt(xxtea_long *v, xxtea_long len, xxtea_long *k);

xxtea_long *xxtea_to_long_array(unsigned char*, xxtea_long, int, xxtea_long *);

unsigned char *xxtea_to_byte_array(xxtea_long *, xxtea_long, int, xxtea_long *);

unsigned char *decrypt(unsigned char *, xxtea_long, unsigned char *, xxtea_long *);

unsigned char *encrypt(unsigned char *, xxtea_long, unsigned char *, xxtea_long *);

#endif
