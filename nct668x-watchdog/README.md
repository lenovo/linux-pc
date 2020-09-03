# NCT668x WatchDog Timer Driver

## Introduction

This repository provides the customized Linux driver code for the NCT668x
WatchDog Timer device which is used in Lenovo products listed in the following
table:

| Chip | Product |
| ---- | ---- |
| NCT6686D-L | ThinkCentre M90n-1 Nano |

## Required Firmware

- BIOS: V23 or later
- EC: V9 or later


## Build Kernel Module from Source

``` bash
$ cd nct668x
$ make
```

## Legal
The source code in this repository is licensed under GNU General Public License
version 2, please check the [COPYING](COPYING) for more details.

> **NO WARRANTY**
> 
> This driver is distributed in the hope that it will be useful, but WITHOUT ANY
> WARRANTY.

