#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/uaccess.h>

#define DROIDFS_NAME "droidfs"
#define DROIDFS_MAGIC 0x20260520
#define DROIDFS_MAX_FILE_SIZE 4096 * 1024 * 102

struct droidfs_file_payload {
    char *data;
    size_t size;
};

static struct inode *droidfs_new_inode(struct super_block *sb, const struct inode *parent,
                                       umode_t mode);

static const struct inode_operations droidfs_file_iops = {
    .getattr = simple_getattr,
    .setattr = simple_setattr,
};

static ssize_t droidfs_read(struct file *file, char __user *buf, size_t len, loff_t *ppos) {
    struct inode *inode = file_inode(file);
    struct droidfs_file_payload *payload = inode->i_private;

    if (!payload || !payload->data)
        return 0;

    return simple_read_from_buffer(buf, len, ppos, payload->data, payload->size);
}

static ssize_t droidfs_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos) {
    struct inode *inode = file_inode(file);
    struct droidfs_file_payload *payload = inode->i_private;
    size_t writable_len;
    ssize_t written;

    if (!payload) {
        payload = kzalloc(sizeof(*payload), GFP_KERNEL);
        if (!payload)
            return -ENOMEM;

        payload->data = kzalloc(DROIDFS_MAX_FILE_SIZE, GFP_KERNEL);
        if (!payload->data) {
            kfree(payload);
            return -ENOMEM;
        }

        inode->i_private = payload;
    }

    if (*ppos >= DROIDFS_MAX_FILE_SIZE)
        return -ENOSPC;

    writable_len = min_t(size_t, len, DROIDFS_MAX_FILE_SIZE - *ppos);
    written = simple_write_to_buffer(payload->data, DROIDFS_MAX_FILE_SIZE, ppos, buf, writable_len);
    if (written <= 0)
        return written;

    if (*ppos > payload->size)
        payload->size = *ppos;

    i_size_write(inode, payload->size);
    inode_set_mtime_to_ts(inode, current_time(inode));
    inode_set_ctime_current(inode);
    return written;
}

static const struct file_operations droidfs_file_fops = {
    .read = droidfs_read,
    .write = droidfs_write,
    .llseek = generic_file_llseek,
};

static int droidfs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry,
                          umode_t mode, bool excl) {
    struct inode *inode = droidfs_new_inode(dir->i_sb, dir, S_IFREG | mode);
    (void)idmap;
    (void)excl;

    if (!inode)
        return -ENOMEM;

    d_instantiate(dentry, inode);
    dget(dentry);
    inode_set_mtime_to_ts(dir, current_time(dir));
    inode_set_ctime_current(dir);

    pr_info("droidfs: create %pd\n", dentry);
    return 0;
}

static struct dentry *droidfs_mkdir(struct mnt_idmap *idmap, struct inode *dir,
                                    struct dentry *dentry, umode_t mode) {
    struct inode *inode = droidfs_new_inode(dir->i_sb, dir, S_IFDIR | mode);
    (void)idmap;

    if (!inode)
        return ERR_PTR(-ENOMEM);

    inc_nlink(dir);
    d_instantiate(dentry, inode);
    dget(dentry);
    inode_set_mtime_to_ts(dir, current_time(dir));
    inode_set_ctime_current(dir);

    pr_info("droidfs: mkdir %pd\n", dentry);
    return NULL;
}

static int droidfs_unlink(struct inode *dir, struct dentry *dentry) {
    int ret = simple_unlink(dir, dentry);

    if (!ret) {
        inode_set_mtime_to_ts(dir, current_time(dir));
        inode_set_ctime_current(dir);
        pr_info("droidfs: unlink %pd\n", dentry);
    }

    return ret;
}

static const struct inode_operations droidfs_dir_iops = {
    .lookup = simple_lookup,
    .create = droidfs_create,
    .mkdir = droidfs_mkdir,
    .unlink = droidfs_unlink,
};

static struct inode *droidfs_new_inode(struct super_block *sb, const struct inode *parent,
                                       umode_t mode) {
    struct inode *inode = new_inode(sb);
    struct timespec64 now;

    if (!inode) {
        return NULL;
    }

    inode->i_ino = get_next_ino();
    inode_init_owner(&nop_mnt_idmap, inode, parent, mode);
    now = current_time(inode);
	
	inode_set_mtime_to_ts(inode, now);
    inode_set_ctime_to_ts(inode, now);
    inode_set_atime_to_ts(inode, now);


    switch (mode & S_IFMT) {
        case S_IFDIR:
            inode->i_op = &droidfs_dir_iops;
            inode->i_fop = &simple_dir_operations;
            inc_nlink(inode);
            break;
        case S_IFREG:
            inode->i_op = &droidfs_file_iops;
            inode->i_fop = &droidfs_file_fops;
            break;
        default:
            init_special_inode(inode, mode, 0);
            break;
    }

    return inode;
}

static void droidfs_evict_inode(struct inode *inode) {
    struct droidfs_file_payload *payload = inode->i_private;

    truncate_inode_pages_final(&inode->i_data);
    clear_inode(inode);

    if (payload) {
        kfree(payload->data);
        kfree(payload);
        inode->i_private = NULL;
    }
}

static const struct super_operations droidfs_super_ops = {
    .statfs = simple_statfs,
    .drop_inode = inode_just_drop,
    .evict_inode = droidfs_evict_inode,
};

static int droidfs_fill_super(struct super_block *sb, struct fs_context *fc) {
    struct inode *root_inode;
    (void)fc;

    sb->s_magic = DROIDFS_MAGIC;  // идентификатор
    sb->s_op = &droidfs_super_ops;
    sb->s_time_gran = 1;  // точность временных меток

    root_inode = droidfs_new_inode(sb, NULL, S_IFDIR | 0755);
    if (!root_inode) {
        return -ENOMEM;
	}

    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root) {
        return -ENOMEM;
	}

    pr_info("droidfs: superblock initialized\n");
    return 0;
}

static int droidfs_get_tree(struct fs_context *fc) {
    return get_tree_nodev(fc, droidfs_fill_super);
}

static const struct fs_context_operations droidfs_context_ops = {
    .get_tree = droidfs_get_tree,
};

static int droidfs_init_fs_context(struct fs_context *fc) {
    fc->ops = &droidfs_context_ops;
    return 0;
}

static struct file_system_type droidfs_type = {
    .owner = THIS_MODULE,
    .name = DROIDFS_NAME,
    .init_fs_context = droidfs_init_fs_context,
    .kill_sb = kill_litter_super,
};

static int __init droidfs_init(void) {
    int ret = register_filesystem(&droidfs_type);

    if (ret) {
        return ret;
	}


    pr_info("droidfs: registered\n");
    return 0;
}

static void __exit droidfs_exit(void) {
    int ret = unregister_filesystem(&droidfs_type);

    if (ret) {
		pr_err("droidfs: unregister failed: %d\n", ret);
	} else {
        pr_info("droidfs: unregistered\n");
	}

}

module_init(droidfs_init);
module_exit(droidfs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Linux Kernel course student");
MODULE_DESCRIPTION("Mini in-memory filesystem integrated with Linux VFS");
