# Subnets add subtract

A C utility for combining IPv4 CIDR lists and optionally subtracting a second list. It
writes the resulting address space as a compact CIDR set.

## Описание

Subnets add subtract - утилита на C для объединения списков IPv4 CIDR, вычитания необязательного второго списка и сохранения оставшегося адресного пространства в компактном виде CIDR.

Вход `-a` задает сети, которые нужно включить в рабочий набор, а `-s` - сети, которые нужно из него удалить. Получившийся набор IPv4 адресов снова компактно представляется в виде CIDR и записывается в `result.txt`.

Реализация делит пространство IPv4 адресов между рабочими потоками и использует префиксное дерево для построения компактного итогового вывода. Количество потоков должно быть степенью двойки.

## Сборка

```sh
cmake --preset release
cmake --build --preset release
```

Исполняемый файл:

```text
build/release/subnets
```

## Использование

```text
Commands:
  Required parameters:
    -t  "x"          Thread count
    -a  "/test.txt"  Path to the subnets to add
  Optional parameters:
    -s  "/test.txt"  Path to the subnets to subtract
```

Пример:

```sh
./build/release/subnets -t 8 -a add.txt -s subtract.txt
```

Результат сохраняется в:

```text
result.txt
```

## Формат входа

Входные файлы содержат IPv4 сети в CIDR notation, по одной сети на строку. Сначала объединяются сети из `-a`, после чего из полученного множества исключяются сети из `-s`.

Число потоков должно быть степенью двойки и не превышать внутренний максимум программы.
