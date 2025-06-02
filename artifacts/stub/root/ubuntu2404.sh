#!/bin/sh
set -e

ROOTFS_DEVICE=/dev/vdb
ROOTFS_TYPE=xfs

if /sbin/mkfs.$ROOTFS_TYPE -f $ROOTFS_DEVICE; then
	mount $ROOTFS_DEVICE /mnt
else
	ROOTFS_DEVICE=fs
	ROOTFS_TYPE=virtiofs
	mount -t $ROOTFS_TYPE $ROOTFS_DEVICE /mnt
fi

MIRROR="http://ports.ubuntu.com/ubuntu-ports"

case "$(uname -m)" in
    x86_64)
        ARCH="amd64"
	MIRROR="http://jp.archive.ubuntu.com/ubuntu"
        ;;
    aarch64)
        ARCH="arm64"
        ;;
    riscv64)
        ARCH="riscv64"
	;;
    *)
        echo "Unsupported architecture"
        exit 1
        ;;
esac

/usr/sbin/debootstrap --include="ubuntu-minimal,initramfs-tools,openssh-server,linux-generic,avahi-daemon,llmnrd,qemu-guest-agent,locales-all" --components=main,universe --arch=$ARCH noble /mnt $MIRROR

sed -i 's/^\(root:\)[^:]*\(:.*\)$/\1\2/' /mnt/etc/shadow
echo -e "$ROOTFS_DEVICE /                       $ROOTFS_TYPE     defaults        1 1" > /mnt/etc/fstab
echo -e 'network:\n  version: 2\n  renderer: networkd\n  ethernets:\n    eth0:\n      dhcp4: true\n      dhcp6: true' > /mnt/etc/netplan/99_config.yaml

[ -f /etc/localtime ] && cp -a /etc/localtime /mnt/etc/

[ -d /root/.ssh ] && cp -a /root/.ssh /mnt/root/
[ -d /etc/ssh -a -d /mnt/etc/ssh ] && cp -a /etc/ssh/*_key /etc/ssh/*_key.pub /mnt/etc/ssh/

echo -e 'deb http://archive.ubuntu.com/ubuntu/ noble-updates main universe\ndeb http://security.ubuntu.com/ubuntu/ noble-security universe' >> /mnt/etc/apt/sources.list

echo 'virtiofs' >> /mnt/etc/initramfs-tools/modules
PATH=$PATH:/usr/sbin chroot /mnt /usr/sbin/update-initramfs -u

umount /mnt
reboot
