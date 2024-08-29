#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
    OUTDIR=/tmp/aeld
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

# Create the directory if it doesn't exist
mkdir -p $OUTDIR

if [ $? -ne 0 ]; then 
    echo "Error: failed creating $OUTDIR"
    exit 1
fi

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
    git restore .
    git apply ${FINDER_APP_DIR}/fix.patch

    # [x] TODO: Add your kernel build steps here
    # 1- perform deep clean
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE mrproper 
    # 2- generate default configuration
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE defconfig 
    # 3- build the vmlinux image
    make -j4 ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE all 
    # 4- build the kernel modules against the kernel
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE modules 
    # 5- build the device tree
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE dtbs 

fi

echo "Adding the Image in outdir"
cp "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" "${OUTDIR}"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# [x] TODO: Create necessary base directories
mkdir rootfs
cd rootfs
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin /usr/lib /usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # [x] TODO:  Configure busybox
    make distclean
    make defconfig
else
    cd busybox
fi

# [x] TODO: Make and install busybox
make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE install
cd ${OUTDIR}/rootfs

echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# [x] TODO: Add library dependencies to rootfs
LIBC=$(${CROSS_COMPILE}gcc -print-sysroot)
cp -r ${LIBC}/lib/* ${OUTDIR}/rootfs/lib
cp -r ${LIBC}/lib64/* ${OUTDIR}/rootfs/lib64

# [x] TODO: Make device nodes
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/consul c 5 1

# [x] TODO: Clean and build the writer utility
cd $FINDER_APP_DIR
make clean
make CROSS_COMPILE=$CROSS_COMPILE

# [x] TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cp writer ${OUTDIR}/rootfs/home
cp finder.sh ${OUTDIR}/rootfs/home
cp finder-test.sh ${OUTDIR}/rootfs/home
mkdir ${OUTDIR}/rootfs/home/conf
cp conf/* ${OUTDIR}/rootfs/home/conf
cp autorun-qemu.sh ${OUTDIR}/rootfs/home


# [x] TODO: Chown the root directory
cd ${OUTDIR}/rootfs
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
# [x] TODO: Create initramfs.cpio.gz
cd ${OUTDIR}
gzip -f initramfs.cpio



