# rpi4-bios

Аппаратная предзагрузочная диагностика (Pre-boot UEFI Diagnostics) для платформы Raspberry Pi 4 Model B (Broadcom BCM2711 / ARM Cortex-A72) на базе фреймворка EDK2 / TianoCore.

---

## 📌 О проекте

Данный проект представляет собой программно-аппаратный комплекс предзагрузочной диагностики вычислительных узлов архитектуры ARM64 (AArch64), оптимизированный для одноплатного компьютера Raspberry Pi 4 Model B, а также виртуализированных стендов QEMU (`virt`, `cortex-a72`).

Диагностический модуль `DiagModule` выполняется на самом раннем этапе инициализации платформы в среде UEFI Shell (DXE/BDS phase) до передачи управления операционной системе, что позволяет выявлять скрытые аппаратные дефекты, сбои шин и деградацию полупроводниковых компонентов.

### Ключевые диагностические подсистемы:
1. **CPU & Архитектура SoC (ARM64)**:
   - Идентификация ядер Cortex-A72 (BCM2711), ревизий, Implementer/PartNum.
   - Определение текущего уровня привилегий (`EL2` / `EL1`).
   - Верификация инструкций и расширений: FP, NEON / Advanced SIMD, GIC CPU Interface.
   - Точное измерение базовой частоты таймера ARM Generic Timer (`cntfrq_el0`, `cntpct_el0`).
2. **SMP Diagnostics (Многопроцессорность)**:
   - Проверка протокола `EFI_MP_SERVICES_PROTOCOL`.
   - Синхронизация и запуск тестовых нагрузок на всех вторичных ядрах (Application Processors).
3. **RAM BIST (Встроенное тестирование ОЗУ)**:
   - Алгоритм **March C-** (сложность $10N$): выявление статических залипаний (Stuck-At Faults) и межбазовых переходов (Transition Faults, Coupling Faults).
   - Алгоритм **Walking 1s / 0s**: обнаружение емкостных наводок, паразитных связей и коротких замыканий линий шины данных.
   - Локализация сбойного адреса и конкретной битовой линии шины (Fault Pinpointing).
   - Замер пропускной способности и длительности тестирования.
4. **Периферия и системные шины**:
   - Сканирование шины **PCI Express (PCIe)** через `EFI_PCI_IO_PROTOCOL` / `EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL`.
   - Проверка целостности таблиц **ACPI** (RSDP, XSDT, FACP, APIC/MADT, GTDT, DBG2 и др.) и их контрольных сумм.
   - Проверка блочных устройств (**Block I/O**).
   - Проверка графического вывода (**GOP** - Graphics Output Protocol).
5. **Fault Injection (Симуляция отказов)**:
   - Программная инжекция битовых сбоев (SAF-1 / Single-bit flip).
   - Симуляция замыкания смежных линий шины данных (Bridging Fault) для валидации отказоустойчивости самого диагностического алгоритма.

---

## 🛠 Структура репозитория

```
├── DiagPkg/             # Исходный код пакета диагностики для EDK2
│   ├── DiagModule/      # Модули CPU, SMP, RAM, Platform, UI и Fault Injection
│   ├── DiagPkg.dec      # Объявление пакета EDK2
│   └── DiagPkg.dsc      # Описание сборки платформы DiagPkg
├── Conf/                # Конфигурационные файлы EDK2 (target.txt, tools_def.txt)
├── docs/                # Документация, пояснительная записка диплома и речь на защиту
├── scripts/             # Скрипты сборки, автоматизации бенчмарков и подготовки SD
│   ├── build_firmware.sh
│   ├── prepare_rpi4_sdcard.sh
│   ├── run_automated_benchmark.sh
│   ├── run_qemu.sh
│   └── setup_submodules.sh
├── rpi4_sdcard/         # Шаблоны и конфигурация для записи на физическую microSD
├── build.sh             # Главный скрипт сборки DiagModule.efi в Nix/Linux
├── flake.nix            # Воспроизводимое окружение сборки Nix Flake
└── THESIS_DOCUMENTATION.md # Полная техническая спецификация
```

---

## 🚀 Быстрый старт

### 1. Подготовка окружения (Nix Flake)

Для сборки проекта рекомендуется использовать Nix Flake с установленным кросс-компилятором AArch64:

```bash
nix develop
```

Либо классический Linux с установленными пакетами `gcc-aarch64-linux-gnu`, `qemu-system-aarch64`, `mtools`, `python3`.

### 2. Инициализация подмодулей (EDK2)

```bash
git submodule update --init --recursive
./scripts/setup_submodules.sh
```

### 3. Сборка диагностического модуля

```bash
./build.sh
```
Бинарный файл `DiagModule.efi` будет собран в директории `Build/DiagPkg/DEBUG_GCC5/AARCH64/`.

### 4. Тестирование в эмуляторе QEMU ARM64

Сборка тестовой прошивки AAVMF и запуск:
```bash
./scripts/build_firmware.sh
./scripts/run_qemu.sh
```

### 5. Запуск бенчмарков и тестирование

```bash
./scripts/run_automated_benchmark.sh
```

### 6. Развертывание на реальный Raspberry Pi 4

1. Подготовьте microSD карту FAT32:
   ```bash
   ./scripts/prepare_rpi4_sdcard.sh
   ```
2. Подробнее см. [Инструкция по прошивке](rpi4_sdcard/README_FLASHING.md).

---

## 📄 Документация

- [Текст выпускной квалификационной работы (диплома)](docs/THESIS_TEXT.md)
- [Речь и презентация к защите диплома](docs/DEFENSE_PRESENTATION_AND_SPEECH.md)
- [Техническая архитектура и протоколы](THESIS_DOCUMENTATION.md)

---

## 📜 Лицензия

BSD 2-Clause / MIT (в соответствии с лицензиями компонентов EDK2).
