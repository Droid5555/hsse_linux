#ifndef TG_PROTO_H
#define TG_PROTO_H

#include <linux/ioctl.h>

#define MAX_MSG_LEN             256
#define MAX_CHAT_NAME_LEN       64
#define MAX_MESSAGES            100
#define MAX_CHATS               16
#define MAX_RESPONSE_DATA       (MAX_MESSAGES * (MAX_MSG_LEN + 32))

// ioctl stuff
#define TG_GET_MSG_COUNT        _IOR('T', 1, int)
#define TG_CLEAR_CHAT           _IO('T',  2)
#define TG_SET_MSG_LIMIT        _IOW('T', 3, int)
#define TG_CREATE_CHAT          _IOW('T', 4, char[MAX_CHAT_NAME_LEN])

#define TG_OP_OPEN              1
#define TG_OP_READ              2
#define TG_OP_WRITE             3
#define TG_OP_RELEASE           4
#define TG_OP_CREATE            5
#define TG_OP_COUNT             6
#define TG_OP_CLEAR             7
#define TG_OP_SET_LIMIT         8

#define PIPE_K2D                "/tg_k2d"
#define PIPE_D2K                "/tg_d2k"

struct tg_request {
    int op;
    char chat[MAX_CHAT_NAME_LEN];
    char msg[MAX_MSG_LEN];
    int limit;
};

struct tg_response {
    int status;
    int count;
    int data_len;
    char data[MAX_RESPONSE_DATA];
};

#endif
