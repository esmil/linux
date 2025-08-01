#!/bin/bash -e

SCRIPT="./scripts/build_kernel.sh"

CLEAN=false
BUILD_DEB=false
CONFIG_FILE="k3_defconfig"

while [[ "$#" -gt 0 ]];
do
    case $1 in
        -c)
            CLEAN=true
	    shift
            ;;
        -d)
            BUILD_DEB=true
	    shift
            ;;
    --config)
	    CONFIG_FILE="$2"
	    shift 2
	    ;;
        *)
            echo "Usage: Run on the top of linux directory"
            echo "$SCRIPT [-c] [-d] [--config xx_defconfig]"
            exit 1
            ;;
    esac
done

if [ $0 != $SCRIPT ]
then
    echo "Run $(basename $SCRIPT) on the top of linux directory"
    exit 1
fi

command -v riscv64-unknown-linux-gnu-gcc > /dev/null || \
    (echo "Install cross compile and add to PATH" && exit 1)

VERSION=$(grep -oP '^VERSION\s*=\s*\K\d+' Makefile)
PATCHLEVEL=$(grep -oP '^PATCHLEVEL\s*=\s*\K\d+' Makefile)
SUBLEVEL=$(grep -oP '^SUBLEVEL\s*=\s*\K\d+' Makefile)
export ARCH=riscv
export CROSS_COMPILE=riscv64-unknown-linux-gnu-
export KERNELRELEASE=$VERSION.$PATCHLEVEL.$SUBLEVEL
export LOCALVERSION=""

if [ $CLEAN = true ]
then
    make distclean
    if [ $BUILD_DEB = true ]
    then
        rm -f ../linux-headers-$KERNELRELEASE_*_riscv64.deb
        rm -f ../linux-image-$KERNELRELEASE_*_riscv64.deb
        rm -f ../linux-image-$KERNELRELEASE-dbg_*_riscv64.deb
        rm -f ../linux-libc-dev_*_riscv64.deb
        rm -f ../linux-riscv-spacemit_*_riscv64.*
    fi
fi

make $CONFIG_FILE

if [ $BUILD_DEB = true ]
then
    KDEB_SOURCENAME=linux-riscv-spacemit \
    KDEB_PKGVERSION=$KERNELRELEASE-$(TZ=Asia/Shanghai date +"%Y%m%d%H%M%S") \
    KDEB_CHANGELOG_DIST=mantic-porting \
    make -j$(nproc) bindeb-pkg
else
    make -j$(nproc)
fi
