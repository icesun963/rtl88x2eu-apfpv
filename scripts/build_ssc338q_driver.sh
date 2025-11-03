#!/usr/bin/env bash
set -euo pipefail

# This helper automates cloning the rtl88x2eu AP/FPV driver tree and building it
# against the Sigmastar ssc338q kernel headers. It is designed to run on Ubuntu/
# Debian derivatives such as Lubuntu.

if [[ $(id -u) -eq 0 ]]; then
  echo "[!] Run this script as a regular user so the build artefacts are owned by you." >&2
  exit 1
fi

: "${WORKDIR:=$PWD}"         # Directory where the repo should be cloned/built
: "${TOOLCHAIN_PREFIX:?Set TOOLCHAIN_PREFIX to the Sigmastar toolchain prefix (e.g. arm-openipc-linux-gnueabihf-)}"
: "${KERNEL_DIR:?Set KERNEL_DIR to the path of the ssc338q kernel source or headers (e.g. ~/openipc/output/build/linux-ssc338q)}"
: "${KERNEL_VERSION:?Set KERNEL_VERSION to the kernel release string (e.g. 5.10.113-openipc-ssc338q)}"

REPO_URL="https://github.com/OpenIPC/rtl88x2eu-apfpv.git"
REPO_DIR="${WORKDIR}/rtl88x2eu-apfpv"

# Ensure required host packages are installed.
if command -v apt-get >/dev/null 2>&1; then
  sudo apt-get update
  sudo apt-get install -y build-essential git bc flex bison libncurses-dev libssl-dev libelf-dev
else
  echo "[!] apt-get not found; install build dependencies manually." >&2
fi

# Clone or update the repository.
if [[ -d "${REPO_DIR}" ]]; then
  echo "[*] Repository already exists at ${REPO_DIR}; updating..."
  git -C "${REPO_DIR}" fetch --all --prune
  git -C "${REPO_DIR}" reset --hard origin/main
else
  echo "[*] Cloning rtl88x2eu-apfpv into ${REPO_DIR}"
  git clone "${REPO_URL}" "${REPO_DIR}"
fi

cd "${REPO_DIR}"

echo "[*] Building rtl88x2eu module for ssc338q"
make clean || true
make ARCH=arm \
     CROSS_COMPILE="${TOOLCHAIN_PREFIX}" \
     KSRC="${KERNEL_DIR}" \
     KVER="${KERNEL_VERSION}" \
     modules

# Optionally strip the module if the toolchain provides a strip binary.
if command -v "${TOOLCHAIN_PREFIX}strip" >/dev/null 2>&1; then
  "${TOOLCHAIN_PREFIX}strip" -g 88x2eu.ko
fi

echo "[*] Build complete. Module located at $(realpath 88x2eu.ko)"

echo "[*] To install into a rootfs, set INSTALL_MOD_PATH and rerun make modules_install, e.g.:"
echo "    INSTALL_MOD_PATH=/path/to/rootfs make ARCH=arm CROSS_COMPILE=${TOOLCHAIN_PREFIX} KSRC=${KERNEL_DIR} KVER=${KERNEL_VERSION} modules_install"
