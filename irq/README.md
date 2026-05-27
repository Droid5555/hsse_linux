# edu_kbd_monitor

## Архитектура

```text
Keyboard (PS/2)                  Kernel space
────────────────────────────────────────────────────────────────────────────
key press/release -> i8042 -> IRQ1 -> top half (irq handler)
                                         │
                                         ├─ inb(0x60) -> raw scan code
                                         ├─ enqueue в RAW FIFO
                                         └─ schedule_work()
                                                   │
                                                   ▼
                                         bottom half (workqueue)
                                         ├─ decode press/release
                                         ├─ map scan code -> key name
                                         ├─ append в event ring buffer
                                         └─ pr_info лог события
                                                   │
                                                   ▼
                                         /proc/edu_kbd_monitor
                                         (чтение последних событий)
```

---

## Компоненты

### `edu_kbd_monitor.c`

- Регистрирует shared IRQ handler для `IRQ1` через `request_irq(..., IRQF_SHARED, ...)`
- В top half читает scan code из порта `0x60`
- Переносит тяжёлую обработку в `workqueue` (`schedule_work`)
- Хранит события в кольцевом буфере (`ring buffer`, 256 записей)
- Экспортирует события через `/proc/edu_kbd_monitor`
- Экспортирует набранный текст через `/proc/edu_kbd_text`
- Корректно освобождает ресурсы в unload: `free_irq`, `cancel_work_sync`, удаление `/proc`

### `edu_kbd_monitor.h`

- Вынесены `#define` константы (`IRQ`, IO port, размеры буферов)
- Описана структура события `struct kbd_event`
- Хранится таблица `scan code -> key name` 

---

## Реализованные итерации из задания

| Итерация | Статус | Что сделано |
|----------|--------|-------------|
| 1 | да | Распознавание `key press` и `key release` |
| 2 | да | Таблица `scan code -> key name` |
| 3 | да | Обработка перенесена в `workqueue` |
| 4 | да | Кольцевой буфер событий |
| 5 | да | Экспорт событий в `/proc/edu_kbd_monitor` |

---

## Build

```bash
make clean && make
```

---

## Запуск в VM

```bash
cp edu_kbd_monitor.ko /home/droidbook/LinuxMIPT-2026/root/

cd /home/droidbook/LinuxMIPT-2026/root
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../boot/initramfs.gz
```

```bash
cd /home/droidbook/LinuxMIPT-2026/boot
qemu-system-x86_64 -kernel ./vmlinuz-6.18.8 \
                   -initrd initramfs.gz \
                   -append "console=tty0" \
                   -machine pc
```

---

## Проверка модуля

```bash
insmod /edu_kbd_monitor.ko
```

```bash
# Чтобы printk не перебивал ввод
dmesg -n 1

# Чекаем ринг буффер через procfs
cat /proc/edu_kbd_monitor

# Сплошной текст
cat /proc/edu_kbd_text

# смотрим логи:
dmesg | grep edu_kbd_monitor

dmesg -n 7
```

`cat /proc/edu_kbd_monitor`:

```text
P 0x1e A
R 0x9e A
```

`cat /proc/edu_kbd_text`:

```text
hello world
bye world
```

В `dmesg`:

```text
edu_kbd_monitor: press key=A scan=0x1e
edu_kbd_monitor: release key=A scan=0x9e
```

# Проверка IRQ

```bash
cat /proc/interrupts
```


---

## Остановка

```bash
rmmod edu_kbd_monitor
poweroff -f
```

---

## Ограничения и заметки

- Неопознанные символы помечаются как `UNKNOWN`
- `RAW FIFO` и `event ring` имеют фиксированный размер 256
- В тексте поддержаны буквы, цифры, базовая пунктуация, `Space`, `Enter`, `Tab`, `Backspace`
