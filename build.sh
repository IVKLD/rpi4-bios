#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

export WORKSPACE="${SCRIPT_DIR}"
export PACKAGES_PATH="${SCRIPT_DIR}:${SCRIPT_DIR}/edk2"
export EDK_TOOLS_PATH="${SCRIPT_DIR}/edk2/BaseTools"
export CONF_PATH="${SCRIPT_DIR}/Conf"
export GCC5_AARCH64_PREFIX="${GCC5_AARCH64_PREFIX:-aarch64-unknown-linux-gnu-}"

if [ ! -f "${EDK_TOOLS_PATH}/Source/C/bin/GenFv" ]; then
    echo "==> [ВНИМАНИЕ] BaseTools не найдены. Запускаю сборку BaseTools..."
    ./scripts/setup_submodules.sh
fi

source edk2/edksetup.sh BaseTools

echo "==> Сборка DiagModule в 32 потока (-n 32)..."
build \
  -a AARCH64 \
  -t GCC5 \
  -p DiagPkg/DiagPkg.dsc \
  -m DiagPkg/DiagModule/DiagModule.inf \
  -b DEBUG \
  -n 32

OUTPUT_EFI="${WORKSPACE}/Build/DiagPkg/DEBUG_GCC5/AARCH64/DiagModule.efi"
RUN_ESP="${WORKSPACE}/run/esp"

if [ -f "${OUTPUT_EFI}" ]; then
    mkdir -p "${RUN_ESP}/EFI/BOOT"
    # Копируем как DiagModule.efi и как default fallback BOOTAA64.EFI
    cp -f "${OUTPUT_EFI}" "${RUN_ESP}/DiagModule.efi"
    cp -f "${OUTPUT_EFI}" "${RUN_ESP}/EFI/BOOT/BOOTAA64.EFI"

    # Создаем startup.nsh для автоматического выполнения при загрузке UEFI Shell
    cat << 'EOF' > "${RUN_ESP}/startup.nsh"
@echo -off
DiagModule.efi
EOF

    echo "==> [УСПЕХ] DiagModule.efi собран и развернут в ${RUN_ESP}/"
else
    echo "==> [ОШИБКА] Файл ${OUTPUT_EFI} не был скомпилирован!"
    exit 1
fi
