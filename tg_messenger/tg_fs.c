#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/namei.h>
#include <linux/file.h>

#include "tg_proto.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Telegram-like chat interface");

static struct proc_dir_entry *tg_create_proc_chat(const char *name);

static struct file *pipe_k2d = NULL;
static struct file *pipe_d2k = NULL;
static DEFINE_MUTEX(pipe_lock);

static int pipes_open(void) {
    pipe_k2d = filp_open(PIPE_K2D, O_WRONLY, 0);
    if (IS_ERR(pipe_k2d)) {
        pr_err("tg_fs: cannot open " PIPE_K2D ": %ld\n",
               PTR_ERR(pipe_k2d));
        pipe_k2d = NULL;
        return -EIO;
    }
    pipe_d2k = filp_open(PIPE_D2K, O_RDONLY, 0);
    if (IS_ERR(pipe_d2k)) {
        pr_err("tg_fs: cannot open " PIPE_D2K ": %ld\n",
               PTR_ERR(pipe_d2k));
        filp_close(pipe_k2d, NULL);
        pipe_k2d = NULL;
        pipe_d2k = NULL;
        return -EIO;
    }
    pr_info("tg_fs: pipes opened\n");
    return 0;
}

static void pipes_close(void) {
    if (pipe_k2d) {
        filp_close(pipe_k2d, NULL);
        pipe_k2d = NULL;
    }
    if (pipe_d2k) {
        filp_close(pipe_d2k, NULL);
        pipe_d2k = NULL;
    }
}

static int kwrite_full(struct file *f, const void *buf, size_t size) {
    ssize_t ret;
    size_t done = 0;
    const char *p = buf;

    while (done < size) {
        ret = kernel_write(f, p + done, size - done, &f->f_pos);
        if (ret <= 0) {
            return ret ? (int) ret : -EIO;
        }
        done += ret;
    }
    return 0;
}

static int kread_full(struct file *f, void *buf, size_t size) {
    ssize_t ret;
    size_t done = 0;
    char *p = buf;

    while (done < size) {
        ret = kernel_read(f, p + done, size - done, &f->f_pos);
        if (ret <= 0) {
            return ret ? (int) ret : -EIO;
        }
        done += ret;
    }
    return 0;
}

static int send_request(struct tg_request *request, struct tg_response *resp) {
    int ret;

    if (!pipe_k2d || !pipe_d2k) {
        ret = pipes_open();
        if (ret)
            return ret;
    }

    ret = kwrite_full(pipe_k2d, request, sizeof(*request));
    if (ret) {
        pr_err("tg_fs: write to daemon failed: %d\n", ret);
        pipes_close();
        return ret;
    }

    ret = kread_full(pipe_d2k, resp, sizeof(*resp));
    if (ret) {
        pr_err("tg_fs: read from daemon failed: %d\n", ret);
        pipes_close();
        return ret;
    }

    return resp->status;
}

static int chat_open(struct inode *inode, struct file *file_ptr) {
    const char *chat = pde_data(inode);
    int ret = -ENOMEM;

    if (!chat) {
        return -ENODEV;
    }

    struct tg_request *req = kzalloc(sizeof(*req), GFP_KERNEL);
    struct tg_response *resp = kzalloc(sizeof(*resp), GFP_KERNEL);

    if (req && resp) {
        file_ptr->private_data = (void *) chat;
        req->op = TG_OP_OPEN;
        strscpy(req->chat, chat, MAX_CHAT_NAME_LEN);

        mutex_lock(&pipe_lock);
        ret = send_request(req, resp);
        mutex_unlock(&pipe_lock);
    }

    kfree(req);
    kfree(resp);
    return ret;
}

static int chat_release(struct inode *inode, struct file *file_ptr) {
    const char *chat = file_ptr->private_data;

    if (!chat) {
        return 0;
    }


    struct tg_request *req = kzalloc(sizeof(*req), GFP_KERNEL);
    struct tg_response *resp = kzalloc(sizeof(*resp), GFP_KERNEL);

    if (req && resp) {
        req->op = TG_OP_RELEASE;
        strscpy(req->chat, chat, MAX_CHAT_NAME_LEN);
        mutex_lock(&pipe_lock);
        send_request(req, resp);
        mutex_unlock(&pipe_lock);
    }

    kfree(req);
    kfree(resp);
    file_ptr->private_data = NULL;
    return 0;
}

static ssize_t chat_read(struct file *file_ptr, char __user *buf, size_t len, loff_t *ppos) {
    const char *chat = file_ptr->private_data;
    ssize_t ret = -ENOMEM;

    if (!chat) {
        return -ENODEV;
    }

    if (*ppos > 0) {
        return 0;
    }

    struct tg_request *req = kzalloc(sizeof(*req), GFP_KERNEL);
    struct tg_response *resp = kzalloc(sizeof(*resp), GFP_KERNEL);
    if (req && resp) {
        req->op = TG_OP_READ;
        strscpy(req->chat, chat, MAX_CHAT_NAME_LEN);

        mutex_lock(&pipe_lock);
        ret = send_request(req, resp);
        mutex_unlock(&pipe_lock);

        if (ret < 0) {
            kfree(req);
            kfree(resp);
            return ret;
        }

        size_t copy_len = min((size_t)resp->data_len, len);
        if (copy_to_user(buf, resp->data, copy_len)) {
            ret = -EFAULT;
            kfree(req);
            kfree(resp);
            return ret;
        }

        *ppos += copy_len;
        ret = (ssize_t) copy_len;
    }

    kfree(req);
    kfree(resp);
    return ret;
}

static ssize_t chat_write(struct file *file_ptr, const char __user *buf, size_t len, loff_t *ppos) {
    const char *chat = file_ptr->private_data;

    if (!chat) {
        return -ENODEV;
    }

    if (len == 0) {
        return 0;
    }


    struct tg_request *req = kzalloc(sizeof(*req), GFP_KERNEL);
    struct tg_response *resp = kzalloc(sizeof(*resp), GFP_KERNEL);
    if (!req || !resp) {
        kfree(req);
        kfree(resp);
        return -ENOMEM;
    }

    size_t copy_len = min(len, (size_t)(MAX_MSG_LEN - 1));
    if (copy_from_user(req->msg, buf, copy_len)) {
        kfree(req);
        kfree(resp);
        return -EFAULT;
    }

    req->msg[copy_len] = '\0';
    if (copy_len > 0 && req->msg[copy_len - 1] == '\n')
        req->msg[copy_len - 1] = '\0';

    req->op = TG_OP_WRITE;
    strscpy(req->chat, chat, MAX_CHAT_NAME_LEN);

    mutex_lock(&pipe_lock);
    ssize_t ret = send_request(req, resp);
    mutex_unlock(&pipe_lock);

    kfree(req);
    kfree(resp);

    return ret < 0 ? ret : (ssize_t) len;
}

static long chat_ioctl(struct file *file_ptr, unsigned int cmd, unsigned long arg) {
    const char *chat = file_ptr->private_data;

    if (!chat)
        return -ENODEV;

    struct tg_request *req = kzalloc(sizeof(*req), GFP_KERNEL);
    struct tg_response *resp = kzalloc(sizeof(*resp), GFP_KERNEL);

    if (!req || !resp) {
        kfree(req);
        kfree(resp);
        return -ENOMEM;
    }

    strscpy(req->chat, chat, MAX_CHAT_NAME_LEN);

    long ret = 0;

    switch (cmd) {
        case TG_GET_MSG_COUNT: {
            req->op = TG_OP_COUNT;

            mutex_lock(&pipe_lock);
            ret = send_request(req, resp);
            mutex_unlock(&pipe_lock);

            if (ret >= 0 &&
                copy_to_user((int __user *) arg, &resp->count, sizeof(int)))
                ret = -EFAULT;
            break;
        }

        case TG_CLEAR_CHAT:
            req->op = TG_OP_CLEAR;

            mutex_lock(&pipe_lock);
            ret = send_request(req, resp);
            mutex_unlock(&pipe_lock);
            break;

        case TG_SET_MSG_LIMIT: {
            int val;

            if (copy_from_user(&val, (int __user *) arg, sizeof(val))) {
                ret = -EFAULT;
                break;
            }

            if (val < 1 || val > MAX_MESSAGES) {
                ret = -EINVAL;
                break;
            }

            req->op = TG_OP_SET_LIMIT;
            req->limit = val;

            mutex_lock(&pipe_lock);
            ret = send_request(req, resp);
            mutex_unlock(&pipe_lock);
            break;
        }

        case TG_CREATE_CHAT: {
            char new_name[MAX_CHAT_NAME_LEN];

            if (copy_from_user(new_name, (char __user *) arg,
                               MAX_CHAT_NAME_LEN)) {
                ret = -EFAULT;
                break;
            }

            new_name[MAX_CHAT_NAME_LEN - 1] = '\0';

            req->op = TG_OP_CREATE;
            strscpy(req->chat, new_name, MAX_CHAT_NAME_LEN);

            mutex_lock(&pipe_lock);
            ret = send_request(req, resp);
            mutex_unlock(&pipe_lock);

            if (ret == 0 || ret == -EEXIST) {
                if (ret == 0)
                    tg_create_proc_chat(new_name);
                ret = 0;
            }
            break;
        }

        default:
            ret = -ENOTTY;
    }

    kfree(req);
    kfree(resp);
    return ret;
}

static const struct proc_ops chat_fops = {
    .proc_open = chat_open,
    .proc_read = chat_read,
    .proc_write = chat_write,
    .proc_release = chat_release,
    .proc_ioctl = chat_ioctl,
};

static struct proc_dir_entry *tg_proc_root;
static struct proc_dir_entry *tg_create_entry;

static ssize_t create_write(struct file *file_ptr, const char __user *buf, size_t len, loff_t *ppos) {
    char name[MAX_CHAT_NAME_LEN];
    size_t copy_len = min(len, (size_t)(MAX_CHAT_NAME_LEN - 1));

    if (copy_from_user(name, buf, copy_len))
        return -EFAULT;

    name[copy_len] = '\0';

    if (copy_len > 0 && name[copy_len - 1] == '\n')
        name[copy_len - 1] = '\0';

    if (!name[0])
        return -EINVAL;

    struct tg_request *req = kzalloc(sizeof(*req), GFP_KERNEL);
    struct tg_response *resp = kzalloc(sizeof(*resp), GFP_KERNEL);

    if (!req || !resp) {
        kfree(req);
        kfree(resp);
        return -ENOMEM;
    }

    req->op = TG_OP_CREATE;
    strscpy(req->chat, name, MAX_CHAT_NAME_LEN);

    mutex_lock(&pipe_lock);
    ssize_t ret = send_request(req, resp);
    mutex_unlock(&pipe_lock);

    if (ret == -EEXIST) {
        ret = -EEXIST;
    } else if (ret >= 0) {
        if (!tg_create_proc_chat(name)) {
            ret = -ENOMEM;
        } else {
            ret = len;
        }
    }

    kfree(req);
    kfree(resp);
    return ret;
}

static const struct proc_ops create_fops = {
    .proc_write = create_write,
};

static char name_pool[MAX_CHATS][MAX_CHAT_NAME_LEN];
static int name_pool_idx = 0;

static struct proc_dir_entry *tg_create_proc_chat(const char *name) {
    if (name_pool_idx >= MAX_CHATS) {
        return NULL;
    }

    char *stored = name_pool[name_pool_idx];
    ++name_pool_idx;
    strscpy(stored, name, MAX_CHAT_NAME_LEN);

    struct proc_dir_entry *pde = proc_create_data(stored, 0666, tg_proc_root, &chat_fops, stored);
    if (!pde) {
        pr_err("tg_fs: failed to create proc entry for '%s'\n", name);
    }

    return pde;
}

static void create_fifo(const char *path) {
    char *argv[] = {"/bin/mknod", "-m", "0666", (char *) path, "p", NULL};
    char *envp[] = {"HOME=/", "PATH=/sbin:/bin:/usr/bin", NULL};

    int ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);

    if (ret) {
        pr_info("tg_fs: mknod %s returned %d\n", path, ret);
    } else {
        pr_info("tg_fs: created FIFO %s\n", path);
    }
}

static int __init telegram_fs_init(void) {
    create_fifo(PIPE_K2D);
    create_fifo(PIPE_D2K);

    tg_proc_root = proc_mkdir("telegram", NULL);
    if (!tg_proc_root) {
        return -ENOMEM;
    }


    tg_create_entry =
            proc_create("telegram/create", 0222, NULL, &create_fops);

    if (!tg_create_entry) {
        proc_remove(tg_proc_root);
        return -ENOMEM;
    }

    tg_create_proc_chat("ch1");
    tg_create_proc_chat("ch2");
    tg_create_proc_chat("ch3");

    pr_info("tg_fs: loaded\n");
    return 0;
}

static void __exit telegram_fs_exit(void) {
    pipes_close();
    proc_remove(tg_create_entry);
    proc_remove(tg_proc_root);
    pr_info("tg_fs: unloaded\n");
}

module_init(telegram_fs_init);
module_exit(telegram_fs_exit);
