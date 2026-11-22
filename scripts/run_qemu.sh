#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BIN_DIR="${WORKSPACE_ROOT}/bin"
RUN_DIR="${WORKSPACE_ROOT}/run"
ESP_DIR="${RUN_DIR}/esp"
DISK_IMG="${RUN_DIR}/virt_disk.img"

FW_CODE="${BIN_DIR}/QEMU_EFI.fd"
FW_VARS="${BIN_DIR}/QEMU_VARS.fd"

if [ ! -f "${FW_CODE}" ]; then
    echo "==> [ОШИБКА] Прошивка ${FW_CODE} не найдена. Запустите сначала ./scripts/build_firmware.sh"
    exit 1
fi

mkdir -p "${ESP_DIR}/EFI/BOOT"

# Подготовка FAT виртуального диска (64MB)
rm -f "${DISK_IMG}"
dd if=/dev/zero of="${DISK_IMG}" bs=1M count=64 status=none
mkfs.vfat -F 32 -n "UEFI_ESP" "${DISK_IMG}" > /dev/null

# Копирование содержимого директории esp в образ диска через mtools
mcopy -s -i "${DISK_IMG}" "${ESP_DIR}/"* ::/ 2>/dev/null || true

echo "==> Запуск QEMU ARM64 (virt, Cortex-A72, 4 cores, 2048MB RAM)..."
echo "==> Для выхода из QEMU нажмите: Ctrl+A затем X"

exec qemu-system-aarch64 \
    -M virt,secure=off,virtualization=on \
    -cpu cortex-a72 \
    -smp 4 \
    -m 2048 \
    -drive if=pflash,format=raw,file="${FW_CODE}",readonly=on \
    -drive if=pflash,format=raw,file="${FW_VARS}" \
    -drive format=raw,file="${DISK_IMG}",if=virtio \
    -nographic \
    -serial mon:stdio
