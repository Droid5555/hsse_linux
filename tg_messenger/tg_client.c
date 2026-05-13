#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "tg_proto.h"


static void chat_path(const char *name, char *buf, size_t sz) {
    snprintf(buf, sz, "/proc/telegram/%s", name);
}

static void cmd_read(const char *chat) {
    char path[128];
    char buf[MAX_RESPONSE_DATA];

    chat_path(chat, path, sizeof(path));

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "cannot open '%s': %s\n", path, strerror(errno));
        return;
    }

    ssize_t read_data = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (read_data < 0) {
        fprintf(stderr, "read failed: %s\n", strerror(errno));
        return;
    }

    buf[read_data] = '\0';
    printf("%s", buf);
}

static void cmd_write(const char *chat, const char *msg) {
    char path[128];

    chat_path(chat, path, sizeof(path));

    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "cannot open '%s': %s\n", path, strerror(errno));
        return;
    }

    if (write(fd, msg, strlen(msg)) < 0) {
        fprintf(stderr, "write failed: %s\n", strerror(errno));
    } else {
        printf("message sent.\n");
    }

    close(fd);
}

static void cmd_count(const char *chat) {
    char path[128];
    int count = -1;

    chat_path(chat, path, sizeof(path));

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "cannot open '%s': %s\n", path, strerror(errno));
        return;
    }

    if (ioctl(fd, TG_GET_MSG_COUNT, &count) < 0) {
        fprintf(stderr, "ioctl TG_GET_MSG_COUNT failed: %s\n", strerror(errno));
    } else {
        printf("message count: %d\n", count);
    }
    close(fd);
}

static void cmd_clear(const char *chat) {
    char path[128];

    chat_path(chat, path, sizeof(path));

    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "cannot open '%s': %s\n", path, strerror(errno));
        return;
    }

    if (ioctl(fd, TG_CLEAR_CHAT) < 0) {
        fprintf(stderr, "ioctl TG_CLEAR_CHAT failed: %s\n", strerror(errno));
    } else {
        printf("chat cleared\n");
    }

    close(fd);
}

static void cmd_limit(const char *chat, int limit) {
    char path[128];

    if (limit < 1 || limit > MAX_MESSAGES) {
        fprintf(stderr, "limit must be between 1 and %d\n", MAX_MESSAGES);
        return;
    }

    chat_path(chat, path, sizeof(path));

    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "cannot open '%s': %s\n", path, strerror(errno));
        return;
    }

    if (ioctl(fd, TG_SET_MSG_LIMIT, &limit) < 0) {
        fprintf(stderr, "ioctl TG_SET_MSG_LIMIT failed: %s\n", strerror(errno));
    } else {
        printf("message limit set to %d.\n", limit);
    }

    close(fd);
}

static void cmd_create(const char *name) {
    int fd = open("/proc/telegram/create", O_WRONLY);

    if (fd < 0) {
        fprintf(stderr, "cannot open '/proc/telegram/create': %s\n",
                strerror(errno));
        return;
    }

    if (write(fd, name, strlen(name)) < 0) {
        fprintf(stderr, "create failed: %s\n", strerror(errno));
    } else {
        printf("chat '%s' created\n", name);
    }

    close(fd);
}

// Это честно навайбкожено (как и обработка ошибок по большей части)
static void print_help(void) {
    printf(
        "\nCommands:\n"
        "  read   <chat>           read messages from chat\n"
        "  write  <chat> <message> send a message to chat\n"
        "  count  <chat>           get message count\n"
        "  clear  <chat>           clear all messages\n"
        "  limit  <chat> <n>       set read message limit\n"
        "  create <name>           create a new chat\n"
        "  help                    show this help\n"
        "  quit                    exit\n\n"
    );
}

int main(void) {
    char line[512];
    print_help();

    while (1) {
        printf("tg> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) { break; }

        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0) { continue; }

        char cmd[64] = {};
        char arg1[128] = {};
        char arg2[256] = {};
        int n = sscanf(line, "%63s %127s %255[^\n]", cmd, arg1, arg2);

        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            break;
        }
        if (strcmp(cmd, "help") == 0) {
            print_help();
        } else if (strcmp(cmd, "read") == 0) {
            if (n < 2) {
                printf("usage: read <chat>\n");
                continue;
            }
            cmd_read(arg1);
        } else if (strcmp(cmd, "write") == 0) {
            if (n < 3) {
                printf("usage: write <chat> <message>\n");
                continue;
            }
            cmd_write(arg1, arg2);
        } else if (strcmp(cmd, "count") == 0) {
            if (n < 2) {
                printf("usage: count <chat>\n");
                continue;
            }
            cmd_count(arg1);
        } else if (strcmp(cmd, "clear") == 0) {
            if (n < 2) {
                printf("usage: clear <chat>\n");
                continue;
            }
            cmd_clear(arg1);
        } else if (strcmp(cmd, "limit") == 0) {
            if (n < 3) {
                printf("usage: limit <chat> <n>\n");
                continue;
            }
            cmd_limit(arg1, atoi(arg2));
        } else if (strcmp(cmd, "create") == 0) {
            if (n < 2) {
                printf("usage: create <name>\n");
                continue;
            }
            cmd_create(arg1);
        } else {
            printf("unknown command '%s'. type 'help'.\n", cmd);
        }
    }

    return 0;
}
