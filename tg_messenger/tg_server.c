#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include "tg_proto.h"

struct message {
    char text[MAX_MSG_LEN + 32];
};

// ring-buffer
struct chat {
    char name[MAX_CHAT_NAME_LEN];
    struct message messages[MAX_MESSAGES];
    int head;
    int count;
    int msg_limit;
    int in_use;
};

static struct chat chats[MAX_CHATS];
static int chat_count = 0;

static struct chat *find_chat(const char *name) {
    for (int idx = 0; idx < chat_count; ++idx) {
        if (strcmp(chats[idx].name, name) == 0) {
            return &chats[idx];
        }
    }
    return NULL;
}

static struct chat *create_chat(const char *name) {
    if (chat_count >= MAX_CHATS)
        return NULL;

    struct chat *chat_ = &chats[chat_count];
    ++chat_count;
    memset(chat_, 0, sizeof(*chat_));
    strncpy(chat_->name, name, MAX_CHAT_NAME_LEN - 1);
    chat_->msg_limit = 10;
    chat_->in_use = 1;
    return chat_;
}

static void chat_add_message(struct chat *chat_, const char *sender,
                             const char *text) {
    int idx = (chat_->head + chat_->count) % MAX_MESSAGES;

    if (chat_->count == MAX_MESSAGES) {
        chat_->head = (chat_->head + 1) % MAX_MESSAGES;
    } else {
        chat_->count++;
    }
    snprintf(chat_->messages[idx].text, sizeof(chat_->messages[idx].text), "[%s] %s", sender, text);
}

static int chat_read_data(struct chat *c, char *out) {
    int written = 0;

    int limit = c->count < c->msg_limit ? c->count : c->msg_limit;

    for (int i = 0; i < limit; i++) {
        int idx = (c->head + (c->count - limit) + i) % MAX_MESSAGES;

        written += snprintf(out + written, MAX_RESPONSE_DATA - written, "%s\n", c->messages[idx].text);
        if (written >= MAX_RESPONSE_DATA)
            break;
    }

    if (written == 0) {
        written = snprintf(out, MAX_RESPONSE_DATA, "(no messages)\n");
    }

    return written;
}

static void fill_chats(void) {
    struct chat *chat_;

    chat_ = create_chat("ch1");
    chat_add_message(chat_, "Gloria", "Hi!!!");
    chat_add_message(chat_, "Marty", "Hi Gloria");
    chat_add_message(chat_, "Gloria", "Have you heard about Claude Mythos? It is going to destroy all the internet!");
    chat_add_message(chat_, "Gloria", "Hey, are you still here??");

    chat_ = create_chat("ch2");
    chat_add_message(chat_, "Marty", "Guys GUYS! Gloria is an AI. I am like 95% sure");
    chat_add_message(chat_, "Melman", "Chill man don't forget that you are just some text in one's dude HW");
    chat_add_message(chat_, "King Julien", "You've got to move it move it");

    chat_ = create_chat("ch3");
    chat_add_message(chat_, "Skipper", "Kowalski, analysis");
    chat_add_message(chat_, "Kowalski", "It seems like we are in a Linux Kernel, Skipper");
    chat_add_message(chat_, "Private", "Skipper, i am scared");
    chat_add_message(chat_, "Rico", "@^#$*@#&!)!@($&)!#@*&%((*!&!*@(!*$&*@*#&%!**!@&$(!@@&$^");
}

static void check_fifo(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISFIFO(st.st_mode)) {
            return;
        }
        unlink(path);
    }
    if (mkfifo(path, 0666) < 0) {
        perror(path);
        exit(EXIT_FAILURE);
    }
}

static int write_full(int fd, const void *buf, size_t size) {
    size_t done = 0;
    const char *p = buf;

    while (done < size) {
        ssize_t w = write(fd, p + done, size - done);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += w;
    }
    return 0;
}

static int read_full(int fd, void *buf, size_t size) {
    size_t done = 0;
    char *p = buf;

    while (done < size) {
        ssize_t r = read(fd, p + done, size - done);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) {
            return -1;
        }

        done += r;
    }
    return 0;
}

static void handle_request(const struct tg_request *req,
                           struct tg_response *resp) {
    struct chat *chat_;

    memset(resp, 0, sizeof(*resp));

    switch (req->op) {
        case TG_OP_OPEN:
            chat_ = find_chat(req->chat);
            if (!chat_) {
                fprintf(stderr, "chat '%s' not found\n", req->chat);
                resp->status = -ENOENT;
            } else {
                resp->status = 0;
            }
            break;

        case TG_OP_RELEASE:
            resp->status = 0;
            break;

        case TG_OP_READ:
            chat_ = find_chat(req->chat);
            if (!chat_) {
                resp->status = -ENOENT;
                break;
            }
            resp->data_len = chat_read_data(chat_, resp->data);
            resp->status = 0;
            break;

        case TG_OP_WRITE:
            chat_ = find_chat(req->chat);
            if (!chat_) {
                resp->status = -ENOENT;
                break;
            }
            chat_add_message(chat_, "user", req->msg);
            resp->status = 0;
            break;

        case TG_OP_CREATE:
            chat_ = find_chat(req->chat);
            if (chat_) {
                resp->status = -EEXIST;
                break;
            }
            chat_ = create_chat(req->chat);
            if (!chat_) {
                resp->status = -ENOMEM;
                break;
            }
            resp->status = 0;
            break;

        case TG_OP_COUNT:
            chat_ = find_chat(req->chat);
            if (!chat_) {
                resp->status = -ENOENT;
                break;
            }
            resp->count = chat_->count;
            resp->status = 0;
            break;

        case TG_OP_CLEAR:
            chat_ = find_chat(req->chat);
            if (!chat_) {
                resp->status = -ENOENT;
                break;
            }
            chat_->head = 0;
            chat_->count = 0;
            resp->status = 0;
            break;

        case TG_OP_SET_LIMIT:
            chat_ = find_chat(req->chat);
            if (!chat_) {
                resp->status = -ENOENT;
                break;
            }
            chat_->msg_limit = req->limit;
            resp->status = 0;
            break;

        default:
            fprintf(stderr, "unknown op %d\n", req->op);
            resp->status = -EINVAL;
            break;
    }
}

int main(void) {
    struct tg_request req;
    struct tg_response resp;

    fill_chats();

    check_fifo(PIPE_K2D);
    check_fifo(PIPE_D2K);

    int fd_k2d = open(PIPE_K2D, O_RDONLY);
    if (fd_k2d < 0) {
        perror(PIPE_K2D);
        return EXIT_FAILURE;
    }

    int fd_d2k = open(PIPE_D2K, O_WRONLY);
    if (fd_d2k < 0) {
        perror(PIPE_D2K);
        return EXIT_FAILURE;
    }

    while (1) {
        if (read_full(fd_k2d, &req, sizeof(req)) < 0) {
            fprintf(stderr, "disconnected\n");
            break;
        }

        handle_request(&req, &resp);

        if (write_full(fd_d2k, &resp, sizeof(resp)) < 0) {
            fprintf(stderr, "write failed\n");
            break;
        }
    }

    close(fd_k2d);
    close(fd_d2k);
    return EXIT_SUCCESS;
}
