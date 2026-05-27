# Лабораторная работа
# Монитор клавиатурных IRQ PS/2 в Linux

## Цель

Написать модуль ядра Linux, который:

- регистрирует IRQ handler для IRQ1;
- читает scan code из порта `0x60`;
- логирует события клавиатуры;
- корректно освобождает IRQ;
- запускается внутри KVM/QEMU.

---

# Теория

Клавиатура PS/2 исторически использует:

```text
IRQ1
```

Контроллер:

```text
i8042
```

Основные IO-порты:

```text
0x60 -> data
0x64 -> status/control
```

При нажатии клавиши клавиатура генерирует:

```text
scan code
```

Пример:

```text
A press   -> 0x1E
A release -> 0x9E
```

---

# Требования

- Linux 6.x
- x86_64
- KVM/QEMU VM
- GCC
- kernel headers

---

# Структура проекта

```text
edu_kbd_monitor.c
Makefile
```

---

# Исходный код модуля

```c
#include <linux/module.h>
#include <linux/interrupt.h>
#include <asm/io.h>

#define KBD_IRQ 1

static int dev_id;

static irqreturn_t kb_irq(int irq, void *dev)
{
    unsigned char scancode;

    scancode = inb(0x60);

    pr_info("edu_kbd_monitor: scan=0x%x\n", scancode);

    return IRQ_HANDLED;
}

static int __init kb_init(void)
{
    int ret;

    ret = request_irq(
        KBD_IRQ,
        kb_irq,
        IRQF_SHARED,
        "edu_kbd_monitor",
        &dev_id
    );

    if (ret) {
        pr_err("failed to register irq\n");
        return ret;
    }

    pr_info("keyboard monitor loaded\n");

    return 0;
}

static void __exit kb_exit(void)
{
    free_irq(KBD_IRQ, &dev_id);

    pr_info("keyboard monitor unloaded\n");
}

module_init(kb_init);
module_exit(kb_exit);

MODULE_LICENSE("GPL");
```

---

# Makefile

```make
obj-m += edu_kbd_monitor.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

---

# Сборка

```bash
make
```

---

# Загрузка

```bash
sudo insmod edu_kbd_monitor.ko
```

---

# Просмотр логов

```bash
sudo dmesg -w
```

---

# Проверка IRQ

```bash
cat /proc/interrupts
```

Ожидаемый вывод:

```text
1:  12345  IO-APIC-edge  i8042
```

---

# Выгрузка

```bash
sudo rmmod edu_kbd_monitor
```

---

1)

Распознавать:
- key press
- key release

---

2)

Сделать таблицу:

```text
scan code -> key name
```

---
3)

Перенести обработку в workqueue.


4)Сделать ring buffer.
1) Экспортировать события через:
/proc/edu_kbd_monitor

---

# Запуск QEMU

```bash
qemu-system-x86_64 \
    -enable-kvm \
    -m 512 \
    -cpu host \
    -kernel vmlinux \
    -initrd initramfs.img \
    -append "console=ttyS0" \
    -machine pc \
    -drive file=debian.qcow2,format=qcow2 # Если есть желание добавить HDD
```
