#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "xxtea.h"
#include "urlencode.h"
#include "rl_protocol.h"

static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                '4', '5', '6', '7', '8', '9', '+', '/'};
static char *decoding_table = NULL;
static int mod_table[] = {0, 2, 1};

static char *base64_encode(const unsigned char *data,
                    uint32_t input_length,
                    uint32_t *output_length)
{

        *output_length = 4 * ((input_length + 2) / 3);

        char *encoded_data = malloc(*output_length + 1);
        if (encoded_data == NULL) return NULL;
        int i,j;
        for (i = 0, j = 0; i < input_length;) {
                uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
                uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
                uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;

                uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

                encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
                encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
                encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
                encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
        }
        for (i = 0; i < mod_table[input_length % 3]; i++)
                encoded_data[*output_length - 1 - i] = '=';

        return encoded_data;
}

static void build_decoding_table()
{
        decoding_table = malloc(256);
        int i;
        for (i = 0; i < 64; i++)
                decoding_table[(unsigned char) encoding_table[i]] = i;
}

static void base64_cleanup()
{
        free(decoding_table);
        decoding_table = NULL;
}

static unsigned char *base64_decode(const char *data,
                             uint32_t input_length,
                             uint32_t *output_length)
{

        if (decoding_table == NULL) build_decoding_table();

        if (input_length % 4 != 0) return NULL;

        *output_length = input_length / 4 * 3;
        if (data[input_length - 1] == '=') (*output_length)--;
        if (data[input_length - 2] == '=') (*output_length)--;

        unsigned char *decoded_data = malloc(*output_length + 1);
        if (decoded_data == NULL) return NULL;

        int i,j;
        for (i = 0, j = 0; i < input_length;) {
                uint32_t sextet_a = data[i] == '=' ? 0 & i++ : decoding_table[data[i++]];
                uint32_t sextet_b = data[i] == '=' ? 0 & i++ : decoding_table[data[i++]];
                uint32_t sextet_c = data[i] == '=' ? 0 & i++ : decoding_table[data[i++]];
                uint32_t sextet_d = data[i] == '=' ? 0 & i++ : decoding_table[data[i++]];

                uint32_t triple = (sextet_a << 3 * 6)
                        + (sextet_b << 2 * 6)
                        + (sextet_c << 1 * 6)
                        + (sextet_d << 0 * 6);

                if (j < *output_length) decoded_data[j++] = (triple >> 2 * 8) & 0xFF;
                if (j < *output_length) decoded_data[j++] = (triple >> 1 * 8) & 0xFF;
                if (j < *output_length) decoded_data[j++] = (triple >> 0 * 8) & 0xFF;
        }
        base64_cleanup();
        return decoded_data;
}

/*
 * retired life protocol encode flow:
 *
 *  base64 -> xxtea -> base64 -> url_encode
 *
 * NOTE: the caller need to free the memory.
 *
 * */
unsigned char *rl_encode(char *meta_str, size_t len, unsigned char *meta_key)
{
        char *base64_orig = NULL;
        char *base64 = NULL;
        uint32_t base64_orig_len, base64_len;
        unsigned char *result = NULL;
        uint32_t ret_length;

        base64_orig = base64_encode((const unsigned char *)meta_str, (uint32_t)len, &base64_orig_len);
        result = encrypt((unsigned char *)base64_orig, base64_orig_len, (unsigned char *)meta_key, &ret_length);
        base64 = base64_encode(result, ret_length, &base64_len);
        char buf[2048] = {0};
        strncpy(buf, base64, base64_len);

        free(base64_orig);
        free(result);
        free(base64);

        return url_encode(buf);
}

/*
 * retired life protocol encode flow:
 *
 *  xxtea -> base64 -> url_encode
 *
 * NOTE: the caller need to free the memory.
 *
 * */
unsigned char *rl_encode1(char *meta_str, size_t len, unsigned char *meta_key)
{
        char base64_orig[1024] = {0};
        char *base64 = NULL;
        uint32_t base64_orig_len, base64_len;
        unsigned char *result = NULL;
        uint32_t ret_length;

        //base64_orig = meta_str;
        snprintf(base64_orig, len, "%s", meta_str);
        base64_orig_len = len;
        result = encrypt((unsigned char *)base64_orig, base64_orig_len, (unsigned char *)meta_key, &ret_length);
        base64 = base64_encode(result, ret_length, &base64_len);
        char buf[2048] = {0};
        strncpy(buf, base64, base64_len);

        free(result);
        free(base64);

        return url_encode(buf);
}

char *rl_encode2(char *meta_str, size_t len, unsigned char *meta_key)
{
        char base64_orig[1024] = {0};
        char *base64 = NULL;
        uint32_t base64_orig_len, base64_len;
        unsigned char *result = NULL;
        uint32_t ret_length;

        //base64_orig = meta_str;
        //snprintf(base64_orig, len, "%s", meta_str);
        strncpy(base64_orig, meta_str, len);
        base64_orig_len = len;
        //result = encrypt((unsigned char *)base64_orig, base64_orig_len, (unsigned char *)meta_key, &ret_length);
        base64 = base64_encode(base64_orig, base64_orig_len, &base64_len);
        //char buf[2048] = {0};
        //strncpy(buf, base64, base64_len);

        //free(result);
        //free(base64);
        //return base64;
        char *tmp = malloc(1024);
        memset(tmp, 1024, 0);
        strncpy(tmp, base64, base64_len);
        return tmp;
        //return url_encode(buf);
}

/*
 * retired life protocol decode flow:
 *
 * NOTE: the caller need to free the memory.
 *
 * */
unsigned char *rl_decode(char *meta_str, size_t len, unsigned char *meta_key)
{
        unsigned char *base64_orig = NULL;
        unsigned char *base64 = NULL;
        unsigned char *result = NULL;
        uint32_t base64_orig_len, base64_len;
        uint32_t ret_length;
        char *str = NULL;

        str = meta_str;
        /*
         * "T:" or "F:" do not need to decode, and by pass 2 chars
         * */
        if (len <= 2) {
                printf("Error, get error data.\n");
                return NULL;
        }
        base64_orig = base64_decode(str+2, (uint32_t)len - 2, &base64_orig_len);
        result = decrypt(base64_orig, base64_orig_len, (unsigned char *)meta_key, &ret_length);
        base64 = base64_decode((char *)result, ret_length, &base64_len);
        printf("%s:%s\n", __FUNCTION__, base64);

        char *temp = malloc(base64_len + 1);
        strncpy(temp, (char *)base64, base64_len);

        free(base64_orig);
        free(result);
        free(base64);

        return (unsigned char *)temp;
}

/*
 * retired life protocol decode flow:
 *
 * NOTE: the caller need to free the memory.
 *
 * */
unsigned char *rl_decode1(char *meta_str, size_t len, unsigned char *meta_key)
{
        unsigned char *base64_orig = NULL;
        unsigned char *base64 = NULL;
        unsigned char *result = NULL;
        uint32_t base64_orig_len, base64_len;
        uint32_t ret_length;
        char *str = NULL;

        str = meta_str;
        /*
         * "T:" or "F:" do not need to decode, and by pass 2 chars
         * */
        if (len <= 2) {
                printf("Error, get error data.\n");
                return NULL;
        }
        base64_orig = base64_decode(str+2, (uint32_t)len - 2, &base64_orig_len);
        result = decrypt(base64_orig, base64_orig_len, (unsigned char *)meta_key, &ret_length);
        base64 = base64_decode((char *)result, ret_length, &base64_len);
        printf("%s:%s\n", __FUNCTION__, base64);

        char *temp = malloc(base64_len + 1);
        strncpy(temp, (char *)base64, base64_len);

        free(base64_orig);
        free(result);
        free(base64);

        return (unsigned char *)temp;
}
