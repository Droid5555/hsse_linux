# VFS cheatsheet for MiniFS

## Основные объекты

### `struct file_system_type`
Описывает тип файловой системы.

### `struct super_block`
Описывает конкретный смонтированный экземпляр файловой системы.

### `struct inode`
Описывает объект файловой системы: файл, каталог, device node.

### `struct dentry`
Связывает имя с inode.

### `struct file`
Описывает открытый файл.

## Ключевые операции

### Создание файла

```text
touch /mnt/minifs/a.txt
  ↓
VFS
  ↓
inode_operations.create
```

### Чтение файла

```text
cat /mnt/minifs/a.txt
  ↓
read()
  ↓
VFS
  ↓
file_operations.read
```

### Запись файла

```text
echo hello > /mnt/minifs/a.txt
  ↓
write()
  ↓
VFS
  ↓
file_operations.write
```

## Useful helpers

- `mount_nodev()`
- `d_make_root()`
- `new_inode()`
- `inode_init_owner()`
- `d_instantiate()`
- `simple_lookup()`
- `simple_readdir()`
- `simple_read_from_buffer()`
- `simple_write_to_buffer()`
- `simple_statfs()`
- `kill_litter_super()`

## Где читать

```text
fs/libfs.c
fs/ramfs/
fs/proc/
include/linux/fs.h
```
