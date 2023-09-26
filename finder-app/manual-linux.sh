#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
# /home/nake/coursera/linux-system-programming/toolchain/arm-cross/aarch64-none-linux-gnu/

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

ROOTFS="${OUTDIR}/rootfs"
mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    cat <<- 'EOF' | patch "${OUTDIR}/linux-stable/scripts/dtc/dtc-lexer.l" -i-
@@ -38,7 +23,6 @@
 #include "srcpos.h"
 #include "dtc-parser.tab.h"

-YYLTYPE yylloc;
 extern bool treesource_error;

 /* CAUTION: this will stop working if we ever use yyless() or yyunput() */
EOF

    # TEST: Add your kernel build steps here
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j10 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    # make -j10 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
    make -j10 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
    cp arch/${ARCH}/boot/Image "${OUTDIR}"
fi

echo "Adding the Image in outdir"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${ROOTFS}" ]
then
	echo "Deleting rootfs directory at ${ROOTFS} and starting over"
    sudo rm  -rf ${ROOTFS}
fi

# TEST: Create necessary base directories

mkdir -p "${ROOTFS}"
cd "${ROOTFS}"
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr/bin usr/lib usr/sbin var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TEST:  Configure busybox
    make distclean
    make defconfig
else
    cd busybox
fi

# TEST: Make and install busybox
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX="${ROOTFS}" ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

echo "Library dependencies"
cd "${ROOTFS}"
${CROSS_COMPILE}readelf -a bin/busybox | grep 'program interpreter'
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"


# TEST: Add library dependencies to rootfs
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)
cp -fv "${SYSROOT}/lib/ld-linux-aarch64.so.1" "${ROOTFS}/lib/"
cp -fv "${SYSROOT}/lib64/libm.so.6" "${ROOTFS}/lib64/"
cp -fv "${SYSROOT}/lib64/libresolv.so.2" "${ROOTFS}/lib64/"
cp -fv "${SYSROOT}/lib64/libc.so.6" "${ROOTFS}/lib64/"

# TEST: Make device nodes
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 666 dev/console c 5 1

# TEST: Clean and build the writer utility
cd "${FINDER_APP_DIR}"
make clean
make CROSS_COMPILE=${CROSS_COMPILE} writer

# TEST: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cp -rL "${FINDER_APP_DIR}"/* "${ROOTFS}/home/"

# TEST: Chown the root directory
sudo chown -R root:root "${ROOTFS}"

# TEST: Create initramfs.cpio.gz
cd "${ROOTFS}"
find . | cpio -H newc -ov --owner root:root > "${OUTDIR}/initramfs.cpio"
gzip -f "${OUTDIR}/initramfs.cpio"
