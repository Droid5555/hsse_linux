# Лабораторная работа №8
# MiniFS: файловая система в памяти

## Цель

Реализовать простую файловую систему в памяти, которая интегрируется с VFS Linux.

Студент должен понять:

- `super_block`
- `inode`
- `dentry`
- `struct file`
- `file_operations`
- `inode_operations`
- регистрацию файловой системы в ядре
- путь `open() -> VFS -> filesystem`

## Идея

Нужно написать модуль ядра `minifs.ko`, который регистрирует файловую систему:

```text
minifs
```

ФС должна хранить данные в RAM и монтироваться так:

```bash
sudo mount -t minifs none /mnt/minifs
```

После монтирования должны работать:

```bash
echo hello > /mnt/minifs/file.txt
cat /mnt/minifs/file.txt
ls /mnt/minifs
mkdir /mnt/minifs/dir
```

## Архитектура

```text
user space
   ↓
open/read/write/mkdir
   ↓
VFS
   ↓
minifs
   ↓
kernel memory
```

## Минимальный функционал

| Фича                 | Обязательно |
|----------------------|-------------|
| регистрация FS       | да          |
| mount / umount       | да          |
| root directory       | да          |
| create file          | да          |
| read/write           | да          |
| ls                   | да          |
| mkdir                | желательно  |
| unlink               | бонус       |
| вложенные директории | бонус       |

## Рекомендуемый путь реализации

1. Зарегистрировать FS через `register_filesystem()`.
2. Реализовать `file_system_type`.
3. Использовать `mount_nodev()`.
4. Создать `super_block` и root inode.
5. Реализовать inode для директорий и файлов.
6. Реализовать `read` и `write`.

## Проверка

```bash
cd minifs
make
sudo insmod minifs.ko
sudo mkdir -p /mnt/minifs
sudo mount -t minifs none /mnt/minifs
../tests/test_minifs.sh /mnt/minifs
sudo umount /mnt/minifs
sudo rmmod minifs
```

## Что сдать

1. Исходники модуля.
2. Вывод тестов (желательно)

## Критерии

### Удовлитворительно

- модуль собирается;
- FS регистрируется;
- FS монтируется;
- root directory доступен;
- можно создать файл;
- `read/write` работает для простого текста.

### Хорошо

- работает несколько файлов;
- работает `ls`;
- работает `mkdir`;
- корректно освобождается память.

### Отлично

- поддерживается `unlink`;
- поддерживаются вложенные директории;
- есть ограничение размера ФС;
- есть `/proc/minifs_stats`;

## Полезные файлы ядра

```text
include/linux/fs.h
fs/libfs.c
fs/ramfs/
fs/proc/
fs/tmpfs/
```
