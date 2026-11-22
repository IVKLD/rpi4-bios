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
SERIAL_LOG="/tmp/diag_serial.log"

echo "================================================================="
echo "   АВТОМАТИЗИРОВАННЫЙ БЕНЧМАРК И ТЕСТИРОВАНИЕ В QEMU ARM64"
echo "================================================================="

if [ ! -f "${FW_CODE}" ]; then
    echo "==> [ОШИБКА] Образ прошивки ${FW_CODE} не найден!"
    exit 1
fi

mkdir -p "${ESP_DIR}/EFI/BOOT"

# Автоматический скрипт startup.nsh: запуск диагностики и выключение ВМ
cat << 'EOF' > "${ESP_DIR}/startup.nsh"
@echo -off
DiagModule.efi
reset -s
EOF

# Подготовка чистого FAT32 образа виртуального диска (64MB)
rm -f "${DISK_IMG}"
dd if=/dev/zero of="${DISK_IMG}" bs=1M count=64 status=none
mkfs.vfat -F 32 -n "UEFI_ESP" "${DISK_IMG}" > /dev/null
mcopy -s -i "${DISK_IMG}" "${ESP_DIR}/"* ::/ 2>/dev/null || true

rm -f "${SERIAL_LOG}"

echo "==> Запуск эмулятора QEMU Cortex-A72 (4 ядра, 2048 MB RAM, VirtIO ESP)..."
timeout 40s qemu-system-aarch64 \
    -M virt,secure=off,virtualization=on \
    -cpu cortex-a72 \
    -smp 4 \
    -m 2048 \
    -drive if=pflash,format=raw,file="${FW_CODE}",readonly=on \
    -drive if=pflash,format=raw,file="${FW_VARS}" \
    -drive format=raw,file="${DISK_IMG}",if=virtio \
    -display none \
    -serial file:"${SERIAL_LOG}" || true

echo ""
echo "================================================================="
echo "                ЛОГ ДИАГНОСТИЧЕСКОГО МОДУЛЯ (UART)               "
echo "================================================================="
if [ -f "${SERIAL_LOG}" ]; then
    cat "${SERIAL_LOG}" | tr -d "\r" | grep -A 100 "ARM64 PRE-BOOT HARDWARE DIAGNOSTIC MODULE" || cat "${SERIAL_LOG}" | tr -d "\r" | tail -n 80
fi

echo ""
echo "================================================================="
echo "       ОТЧЕТ, СОХРАНЕННЫЙ НА РАЗДЕЛ ESP (diag_report.txt)        "
echo "================================================================="
if mdir -i "${DISK_IMG}" ::/diag_report.txt >/dev/null 2>&1; then
    mtype -i "${DISK_IMG}" ::/diag_report.txt
    echo "==> [УСПЕХ] Файл diag_report.txt успешно подтвержден на носителе!"
else
    echo "==> [ВНИМАНИЕ] Файл отчета не найден на диске."
fi
