#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${WORKSPACE_ROOT}"

echo "==> Сборка прошивки UEFI AAVMF для QEMU (ArmVirtQemu.dsc)..."

export WORKSPACE="${WORKSPACE_ROOT}"
export PACKAGES_PATH="${WORKSPACE_ROOT}/edk2"
export EDK_TOOLS_PATH="${WORKSPACE_ROOT}/edk2/BaseTools"
export CONF_PATH="${WORKSPACE_ROOT}/Conf"
export GCC5_AARCH64_PREFIX="${GCC5_AARCH64_PREFIX:-aarch64-unknown-linux-gnu-}"

source edk2/edksetup.sh BaseTools

build \
  -a AARCH64 \
  -t GCC5 \
  -p ArmVirtPkg/ArmVirtQemu.dsc \
  -b DEBUG \
  -n 32

OUTPUT_FD="${WORKSPACE_ROOT}/Build/ArmVirtQemu-AARCH64/DEBUG_GCC5/FV/QEMU_EFI.fd"
OUTPUT_VARS="${WORKSPACE_ROOT}/Build/ArmVirtQemu-AARCH64/DEBUG_GCC5/FV/QEMU_VARS.fd"
BIN_DIR="${WORKSPACE_ROOT}/bin"
mkdir -p "${BIN_DIR}"

if [ -f "${OUTPUT_FD}" ]; then
    # virt pflash0 ожидает ровно 64MB образ для flash0
    cp -f "${OUTPUT_FD}" "${BIN_DIR}/QEMU_EFI.fd"
    truncate -s 64M "${BIN_DIR}/QEMU_EFI.fd"

    # pflash1 для NVRAM переменных (64MB)
    if [ -f "${OUTPUT_VARS}" ]; then
        cp -f "${OUTPUT_VARS}" "${BIN_DIR}/QEMU_VARS.fd"
    else
        dd if=/dev/zero of="${BIN_DIR}/QEMU_VARS.fd" bs=1M count=64 status=none
    fi
    truncate -s 64M "${BIN_DIR}/QEMU_VARS.fd"
    echo "==> [УСПЕХ] Образ прошивки скопирован в ${BIN_DIR}/QEMU_EFI.fd"
else
    echo "==> [ОШИБКА] Файл ${OUTPUT_FD} не найден!"
    exit 1
fi
