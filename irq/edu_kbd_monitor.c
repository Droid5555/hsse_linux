#include <asm/io.h> // для inb()
#include <linux/interrupt.h> // request_irq, free_irq, irqreturn_t.
#include <linux/jiffies.h> // метки времени
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

#include "edu_kbd_monitor.h"

static void kb_work_fn(struct work_struct *work);

static DEFINE_KFIFO(raw_fifo, u8, RAW_FIFO_SIZE); // очередь из cырых байтов
static DEFINE_SPINLOCK(raw_fifo_lock); 
static DEFINE_MUTEX(event_ring_lock);
static DECLARE_WORK(kbd_work, kb_work_fn);

static struct kbd_event event_ring[RING_BUFF_SIZE];
static size_t event_head;
static size_t event_count;
static int dev_id;
static struct proc_dir_entry *proc_entry;

static const char *key_name_from_scancode(u8 scancode) {
    u8 base_code = scancode & 0x7F; // отбрасываем 0x80, чтобы мапилось в 1 имя

    if (set_key_names[base_code] != NULL) {
        return set_key_names[base_code];
    }

    return "UNKNOWN";
}

// записываем событие в ринг буффер
static void event_ring_push(u8 scancode) {
    mutex_lock(&event_ring_lock);

    struct kbd_event *event = &event_ring[event_head];
    const char *key_name = key_name_from_scancode(scancode);

    event->scancode = scancode;
    event->is_press = !(scancode & 0x80);
    event->jiffies_ts = jiffies;
    strscpy(event->key_name, key_name, sizeof(event->key_name));

    event_head = (event_head + 1) % RING_BUFF_SIZE;
    if (event_count < RING_BUFF_SIZE) {
        ++event_count;
    }

    mutex_unlock(&event_ring_lock);
}

static void kb_work_fn(struct work_struct *work) {
    u8 scancode;

    while (kfifo_out_spinlocked(&raw_fifo, &scancode, 1, &raw_fifo_lock) == 1) {
        event_ring_push(scancode);
        pr_info("edu_kbd_monitor: %s key=%s scan=0x%02x\n", (scancode & 0x80) ? "release" : "press",
                key_name_from_scancode(scancode), scancode);
    }
}

static irqreturn_t kb_irq(int irq, void *dev) {
    u8 scancode = inb(KBD_DATA_PORT);

    if (!kfifo_is_full(&raw_fifo)) {
        kfifo_in_spinlocked(&raw_fifo, &scancode, 1, &raw_fifo_lock);
    }

    schedule_work(&kbd_work);
    return IRQ_HANDLED;
}

static ssize_t kb_proc_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos) {
    char *buf = kmalloc(PROC_BUFFER_SIZE, GFP_KERNEL);
    if (buf == NULL) {
        return -ENOMEM;
    }

    struct kbd_event *snapshot = kmalloc_array(RING_BUFF_SIZE, sizeof(*snapshot), GFP_KERNEL);

    if (snapshot == NULL) {
        kfree(buf);
        return -ENOMEM;
    }

    mutex_lock(&event_ring_lock);

    size_t snapshot_count = event_count;
    size_t oldest_idx = (event_head + RING_BUFF_SIZE - event_count) % RING_BUFF_SIZE;
    for (size_t idx = 0; idx < snapshot_count; ++idx) {
        snapshot[idx] = event_ring[(oldest_idx + idx) % RING_BUFF_SIZE];
    }

    mutex_unlock(&event_ring_lock);

    size_t len = 0;
    for (size_t idx = 0; idx < snapshot_count && len < PROC_BUFFER_SIZE; ++idx) {
        len += scnprintf(buf + len, PROC_BUFFER_SIZE - len, "%s 0x%02x %s\n",
                         snapshot[idx].is_press ? "P" : "R", snapshot[idx].scancode,
                         snapshot[idx].key_name);
    }

    ssize_t ret = simple_read_from_buffer(ubuf, count, ppos, buf, len);
    kfree(snapshot);
    kfree(buf);
    return ret;
}

static const struct proc_ops kb_proc_ops = {
    .proc_read = kb_proc_read,
};

static int __init kb_init(void) {
    proc_entry = proc_create("edu_kbd_monitor", 0444, NULL, &kb_proc_ops);
    if (proc_entry == NULL) {
        pr_err("edu_kbd_monitor: failed to create /proc entry\n");
        return -ENOMEM;
    }

    int ret = request_irq(KBD_IRQ, kb_irq, IRQF_SHARED, "edu_kbd_monitor", &dev_id);
    if (ret) {
        pr_err("edu_kbd_monitor: request_irq failed: %d\n", ret);
        remove_proc_entry("edu_kbd_monitor", NULL);
        return ret;
    }

    pr_info("edu_kbd_monitor: loaded on IRQ%d\n", KBD_IRQ);
    return 0;
}

static void __exit kb_exit(void) {
    free_irq(KBD_IRQ, &dev_id);
    cancel_work_sync(&kbd_work);
    remove_proc_entry("edu_kbd_monitor", NULL);
    pr_info("edu_kbd_monitor: unloaded\n");
}

module_init(kb_init);
module_exit(kb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("droid55");
MODULE_DESCRIPTION("keyboard monitor");
