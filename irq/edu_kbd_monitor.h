#ifndef EDU_KBD_MONITOR_H
#define EDU_KBD_MONITOR_H

#include <linux/types.h>

#define KBD_IRQ 1
#define KBD_DATA_PORT 0x60
#define RAW_FIFO_SIZE 256
#define RING_BUFF_SIZE 256
#define PROC_BUFFER_SIZE (64 * 1024)
#define TEXT_BUFFER_SIZE 4096

struct kbd_event {
    u8 scancode;
    bool is_press;
    char key_name[16];
    unsigned long jiffies_ts;
};

static const char *const set_key_names[128] = {
    [0x01] = "ESC",     [0x02] = "1",         [0x03] = "2",   [0x04] = "3",
    [0x05] = "4",       [0x06] = "5",         [0x07] = "6",   [0x08] = "7",
    [0x09] = "8",       [0x0A] = "9",         [0x0B] = "0",   [0x0C] = "-",
    [0x0D] = "=",       [0x0E] = "BACKSPACE", [0x0F] = "TAB", [0x10] = "Q",
    [0x11] = "W",       [0x12] = "E",         [0x13] = "R",   [0x14] = "T",
    [0x15] = "Y",       [0x16] = "U",         [0x17] = "I",   [0x18] = "O",
    [0x19] = "P",       [0x1A] = "[",         [0x1B] = "]",   [0x1C] = "ENTER",
    [0x1D] = "LCTRL",   [0x1E] = "A",         [0x1F] = "S",   [0x20] = "D",
    [0x21] = "F",       [0x22] = "G",         [0x23] = "H",   [0x24] = "J",
    [0x25] = "K",       [0x26] = "L",         [0x27] = ";",   [0x28] = "'",
    [0x29] = "`",       [0x2A] = "LSHIFT",    [0x2B] = "\\",  [0x2C] = "Z",
    [0x2D] = "X",       [0x2E] = "C",         [0x2F] = "V",   [0x30] = "B",
    [0x31] = "N",       [0x32] = "M",         [0x33] = ",",   [0x34] = ".",
    [0x35] = "/",       [0x36] = "RSHIFT",    [0x37] = "KP*", [0x38] = "LALT",
    [0x39] = "SPACE",   [0x3A] = "CAPSLOCK",  [0x3B] = "F1",  [0x3C] = "F2",
    [0x3D] = "F3",      [0x3E] = "F4",        [0x3F] = "F5",  [0x40] = "F6",
    [0x41] = "F7",      [0x42] = "F8",        [0x43] = "F9",  [0x44] = "F10",
    [0x45] = "NUMLOCK", [0x46] = "SCROLL",    [0x47] = "KP7", [0x48] = "KP8",
    [0x49] = "KP9",     [0x4A] = "KP-",       [0x4B] = "KP4", [0x4C] = "KP5",
    [0x4D] = "KP6",     [0x4E] = "KP+",       [0x4F] = "KP1", [0x50] = "KP2",
    [0x51] = "KP3",     [0x52] = "KP0",       [0x53] = "KP.", [0x57] = "F11",
    [0x58] = "F12",
};

#endif
