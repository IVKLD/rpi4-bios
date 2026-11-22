#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
RPIDIR="${WORKSPACE_ROOT}/rpi4_sdcard"

echo "==> Подготовка пакета прошивки и диагностического модуля для Raspberry Pi 4 Model B..."

rm -rf "${RPIDIR}"
mkdir -p "${RPIDIR}/EFI/BOOT"

DIAG_EFI="${WORKSPACE_ROOT}/Build/DiagPkg/DEBUG_GCC5/AARCH64/DiagModule.efi"

if [ ! -f "${DIAG_EFI}" ]; then
    echo "==> Сборка DiagModule..."
    "${WORKSPACE_ROOT}/build.sh"
fi

# Копируем диагностический модуль в корень и в стандартный fallback path UEFI
cp -f "${DIAG_EFI}" "${RPIDIR}/DiagModule.efi"
cp -f "${DIAG_EFI}" "${RPIDIR}/EFI/BOOT/BOOTAA64.EFI"

# Создаем сценарий автоматического запуска при подаче питания
cat << 'EOF' > "${RPIDIR}/startup.nsh"
@echo -off
echo =================================================================
echo   Auto-starting ARM64 Pre-boot Diagnostics on Raspberry Pi 4...
echo =================================================================
DiagModule.efi
EOF

# Конфигурация для загрузчика Raspberry Pi VideoCore (config.txt)
cat << 'EOF' > "${RPIDIR}/config.txt"
# Raspberry Pi 4 Model B (BCM2711) UEFI Boot Configuration
arm_64bit=1
enable_uart=1
uart_2ndstage=1
enable_gic=1
armstub=RPI_EFI.fd
disable_commandline_tags=1
device_tree_address=0x3000000
device_tree_end=0x3100000
dtoverlay=miniuart-bt
total_mem=4096
EOF

cat << 'EOF' > "${RPIDIR}/README_FLASHING.md"
# Инструкция по развертыванию на реальную microSD карту Raspberry Pi 4 Model B

1. Отформатируйте microSD карту в файловую систему FAT32 (MBR):
   ```bash
   sudo mkfs.vfat -F 32 -n "RPI4_UEFI" /dev/sdX1
   ```

2. Скачайте официальный релиз UEFI прошивки Raspberry Pi 4 (EDK2 RPi4 Firmware v1.38+):
   https://github.com/pftf/RPi4/releases/latest

3. Распакуйте архив прошивки pftf/RPi4 в корень microSD карты.

4. Скопируйте все файлы из этой папки (`rpi4_sdcard/*`) с заменой на microSD карту:
   - `DiagModule.efi`
   - `EFI/BOOT/BOOTAA64.EFI`
   - `startup.nsh`
   - `config.txt`

5. Вставьте microSD карту в слот Raspberry Pi 4, подключите USB-UART переходник к GPIO 14 (TX), 15 (RX), GND
   (скорость 115200 8N1) или HDMI монитор.

6. Подайте питание 5V/3A. Модуль выполнит полную аппаратную диагностику SoC BCM2711, RAM и периферии до загрузки ОС.
EOF

echo "==> [УСПЕХ] Пакет для физического стенда RPi 4 сформирован в ${RPIDIR}/"
