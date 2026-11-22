#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${WORKSPACE_ROOT}"

echo "==> [1/3] Инициализация EDK II и сабмодулей..."

if [ ! -d ".git" ]; then
    git init -b main
fi

if [ ! -d "edk2/.git" ]; then
    echo "==> Клонирование EDK II по тегу edk2-stable202408..."
    git submodule add --force https://github.com/tianocore/edk2.git edk2
    cd edk2
    git checkout -B local-stable202408 tags/edk2-stable202408
    cd "${WORKSPACE_ROOT}"
else
    echo "==> EDK II уже инициализирован..."
    cd edk2
    git checkout -B local-stable202408 tags/edk2-stable202408 2>/dev/null || true
    cd "${WORKSPACE_ROOT}"
fi

echo "==> Инициализация Brotli и BaseFdtLib для EDK II..."
cd edk2
git submodule update --init BaseTools/Source/C/BrotliCompress/brotli || true
git submodule update --init MdeModulePkg/Library/BrotliCustomDecompressLib/brotli || true
git submodule update --init MdePkg/Library/BaseFdtLib/libfdt || true
cd "${WORKSPACE_ROOT}"

echo "==> [2/3] Настройка окружения сборки..."
export WORKSPACE="${WORKSPACE_ROOT}"
export PACKAGES_PATH="${WORKSPACE_ROOT}/edk2"
export EDK_TOOLS_PATH="${WORKSPACE_ROOT}/edk2/BaseTools"
export CONF_PATH="${WORKSPACE_ROOT}/Conf"

mkdir -p "${CONF_PATH}"

echo "==> [3/3] Сборка EDK II BaseTools в 32 потока (-j32)..."
make -C edk2/BaseTools clean
make -C edk2/BaseTools -j32

echo "==> Инициализация конфигурации EDK II..."
source edk2/edksetup.sh BaseTools

echo "==> BaseTools успешно скомпилированы."
