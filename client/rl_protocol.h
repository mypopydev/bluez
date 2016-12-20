#ifndef RL_PROTOCOL_H_
#define RL_PROTOCOL_H_

unsigned char *rl_encode(char *meta_str, size_t len, unsigned char *meta_key);
unsigned char *rl_encode1(char *meta_str, size_t len, unsigned char *meta_key);
unsigned char *rl_decode(char *meta_str, size_t len, unsigned char *meta_key);

#endif
