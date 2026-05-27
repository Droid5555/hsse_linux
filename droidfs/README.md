# droidfs

Учебная файловая система в памяти (kernel module), интегрированная с Linux VFS.

## Архитектура

```text
User space                                      Kernel space
────────────────────────────────────────────────────────────────────────────────
echo/cat/mkdir/rm /mnt/droidfs/*  --->  VFS syscall layer  
                                                      │
                                                      ▼
                                                   droidfs.ko
                                                 - register_filesystem
                                                 - init_fs_context
                                                 - get_tree_nodev
                                                 - droidfs_fill_super
                                                    │
                                                    ▼
                                                RAM storage per inode:
                                                 - inode->i_private -> payload
                                                 - payload.data[4096], payload.size
```

---

## Компоненты

### `droidfs.c`

- Регистрирует ФС `droidfs` через `register_filesystem()`
- Монтируется через `init_fs_context()` + `get_tree_nodev()` (без блочного устройства)
- Создаёт root inode/dentry в `fill_super`
- Реализует:
  - `create` для regular file
  - `mkdir` для директорий
  - `read`/`write` для обычных файлов
  - `unlink` (бонус)
- Хранит содержимое файлов только в RAM (`inode->i_private`)
- Освобождает память в `evict_inode`

### `Makefile`

- Сборка модуля через KBuild (`obj-m += droidfs.o`)
- Поддерживает `make` и `make clean`

---

## Поддержанный функционал

| Функционал                       | Сделано |
|----------------------------------|--------|
| register filesystem              | да    |
| mount / umount                   | да    |
| root directory                   | да    |
| create file                      | да    |
| read / write                     | да    |
| `ls`                             | да    |
| `mkdir`                          | да    |
| `unlink`                         | да    |
| nested directories               | да    |
| max file size (4 KiB per file)   | да    |

---

## Build

```bash
cd droidfs
make
make clean
```

---

## Запуск

```bash
cp droidfs.ko /home/droidbook/LinuxMIPT-2026/root/

cd /home/droidbook/LinuxMIPT-2026/root
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../boot/initramfs.gz
```


```bash
cd /home/droidbook/LinuxMIPT-2026/boot
qemu-system-x86_64 -kernel ./vmlinuz-6.18.8 \
                   -initrd initramfs.gz \
                   -append console=ttyS0 \
                   -nographic
```

```bash
insmod droidfs.ko
mkdir -p /mnt/droidfs
mount -t droidfs none /mnt/droidfs
```

Базовая проверка:

```bash
echo "hello from droidfs" > /mnt/droidfs/file.txt
cat /mnt/droidfs/file.txt
ls -la /mnt/droidfs

mkdir /mnt/droidfs/dir
echo "nested data" > /mnt/droidfs/dir/nested.txt
cat /mnt/droidfs/dir/nested.txt

rm /mnt/droidfs/file.txt
ls -la /mnt/droidfs
```

Остановка:

```bash
umount /mnt/droidfs
rmmod droidfs
poweroff -f
```

---

## Заметки

Данные не переживают `umount`/`rmmod`
Ограничение файла: `4096` байт

register_filesystem
структура file_system_type

fill_super

droidfs_file_payload

inode_operations
file_operations

при create/mkdir
- lookup
- droidfs_create
- создаю inode new_inode
- d_instantiate
  
dget(dentry) чтобы dentry не исчезал из dcache

read:
- беру payload из inode->i_private,
- simple_read_from_buffer
write:
- при первом write лениво выделяю payload и буфер
- simple_write_to_buffer
- обновляю i_size, mtime, ctime
- ограничиваю запись 4 KiB и возвращаю -ENOSPC

unlink через simple_unlink

evict_inode:

- truncate_inode_pages_final
- clear_inode
- kfree(payload->data) и kfree(payload)


```bash
qemu-system-x86_64 -m 64M \
                   -kernel ./vmlinuz-6.18.8 \
                   -initrd initramfs.gz \
                   -append console=ttyS0 \
                   -nographic
```
 
```bash
grep -E 'MemTotal|MemFree|MemAvailable|SwapTotal|SwapFree' /proc/meminfo
```

# добавляем свап в виртуалку

создаём диск
```bash
cd /home/droidbook/LinuxMIPT-2026/boot
truncate -s 128M swap.img
```

```bash
qemu-system-x86_64 -m 64M \
  -kernel ./vmlinuz-6.18.8 \
  -initrd initramfs.gz \
  -append "console=ttyS0" \
  -drive file=swap.img,format=raw,if=virtio \
  -nographic
```

```bash
ls /dev/vd* /dev/sd* 2>/dev/null
mkswap /dev/vda
swapon /dev/vda
cat /proc/swaps
free -h
```