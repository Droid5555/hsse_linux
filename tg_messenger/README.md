# Kernel Telegram

ДИСКЛЕЙМЕР

Использовал Claude Sonnet в README для красоты)

## Architecture

```
User space                          Kernel space
────────────────────────────────────────────────────────────
                                    /proc/telegram/ch1
cat /proc/telegram/ch1  ──────►  chat_read()
                                        │
                                    struct tg_request
                                    { op=TG_OP_READ,
                                      chat="ch1" }
                                        │
                              /tmp/tg_k2d  (FIFO)
                                        │
tg_server (daemon)  ◄─────────────┘
  finds ch1 in memory
  builds message list
  fills struct tg_response
        │
        └──────────────────►  /tmp/tg_d2k  (FIFO)
                                        │
                                    kernel reads response
                                    copy_to_user(buf, resp.data)
                                        │
cat receives message history ◄──────────┘
```

---

## Components

### `tg_proto.h`

Определение структур `tg_request` и `tg_response`, операций (TG_OP_), номеров ioctl-команд и пути к FIFO.
Используется и модулем, и демоном.

### `tg_fs.c` — kernel модуль

- Создаёт директорию `/proc/telegram/` с тремя дефолтными чатами
- Создаёт `/proc/telegram/create` для динамического создания чатов
- Реализует `open`, `read`, `write`, `release`, `ioctl`
- Каждая файловая операция отправляет `tg_request` в `PIPE_K2D` и читает `tg_response` из `PIPE_D2K`
- Юзаю 'mutex' для многопоточности

### `tg_server.c` — userspace демон

- Хранит массив `struct chat` с ринг буффером
- При старте создаёт 3 дефолтных чата (`ch1`, `ch2`, `ch3`)
- Открывает `PIPE_K2D` и `PIPE_D2K`
- Циклично читает запрос потом обрабатывает и пишет ответ

### `tg_client.c` — клиент

Интерактивная оболочка для работы с мессенджером из командной строки.
Напрямую открывает файлы `/proc/telegram/<chat>` и делает стандартные вызовы `read`, `write`, `ioctl` всё идёт через
procfs


---

## ioctl интерфейс

| Команда            | Направление | Аргумент   | Действие                              |
|--------------------|-------------|------------|---------------------------------------|
| `TG_GET_MSG_COUNT` | чтение      | `int *`    | количество сообщений в чате           |
| `TG_CLEAR_CHAT`    | —           | —          | удалить все сообщения                 |
| `TG_SET_MSG_LIMIT` | запись      | `int *`    | сколько сообщений возвращает `read()` |
| `TG_CREATE_CHAT`   | запись      | `char[64]` | создать новый чат                     |

---

## 0. Билд

```bash
make
make clean
```

---

### 1. Копируем и запихиваем

```bash
cp tg_fs.ko  /home/droidbook/LinuxMIPT-2026/root/
cp tg_server /home/droidbook/LinuxMIPT-2026/root/
cp tg_client /home/droidbook/LinuxMIPT-2026/root/

cd /home/droidbook/LinuxMIPT-2026/root
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../boot/initramfs.gz
```

### 2. Запускаем qemu

```bash
cd /home/droidbook/LinuxMIPT-2026/boot
qemu-system-x86_64 -kernel ./vmlinuz-6.18.8 \
                   -initrd initramfs.gz \
                   -append console=ttyS0 \
                   -nographic
```

### 3. Загружаем модуль и демон на фоне

```bash
insmod /tg_fs.ko
./tg_server &
```

### 4. Тестируем

```bash
# Читаем сообщения
cat /proc/telegram/ch3

# Пишем
echo "Hello world" > /proc/telegram/ch3
cat /proc/telegram/ch3

# Динамически создаём новый чат
echo "ch4" > /proc/telegram/create
echo "First message in ch4" > /proc/telegram/ch4
cat /proc/telegram/ch4
```

## 5. Открываем ioctl интерфейс

```bash
./tg_client

# Commands:
#   read   <chat>           read messages from chat
#   write  <chat> <message> send a message to chat
#   count  <chat>           get message count        [ioctl]
#   clear  <chat>           clear all messages       [ioctl]
#   limit  <chat> <n>       set read message limit   [ioctl]
#   create <name>           create a new chat        [ioctl]
#   help                    show this help
#   quit                    exit

# tg> 

```

## 6. Вырубаем

```bash
rmmod tg_fs
poweroff -f
```

---

## Error Handling

| Situation                          | Response                                         |
|------------------------------------|--------------------------------------------------|
| Chat not found                     | `-ENOENT` from daemon → returned to user         |
| Message too long                   | truncated to `MAX_MSG_LEN` (256 bytes) in kernel |
| `copy_from_user` fails             | `-EFAULT`                                        |
| `copy_to_user` fails               | `-EFAULT`                                        |
| Daemon not running                 | `-EIO` (pipe open fails)                         |
| Too many chats (> 16)              | `-ENOMEM` from daemon                            |
| Invalid ioctl limit (< 1 or > 100) | `-EINVAL` in kernel before forwarding            |
| Unknown ioctl                      | `-ENOTTY`                                        |
