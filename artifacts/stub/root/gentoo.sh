#!/bin/sh
set -e
/lib/systemd/systemd-networkd-wait-online

case "$(uname -m)" in
    x86_64)
        ARCH="amd64"
	ARCH2="amd64"
        ;;
    aarch64)
        ARCH="arm64"
	ARCH2="arm64"
        ;;
    riscv64)
        ARCH="riscv"
	ARCH2="rv64_lp64d"
	;;
    *)
        echo "Unsupported architecture"
        exit 1
        ;;
esac

BASE_URL=http://ftp.iij.ad.jp/pub/linux/gentoo/
LATEST_STAGE3_URL=${BASE_URL}releases/${ARCH}/autobuilds/`curl -s ${BASE_URL}releases/${ARCH}/autobuilds/latest-stage3-${ARCH2}-systemd.txt|grep -e '\.tar\.xz [0-9]\+$'|sed 's/\s[0-9]\+$//'`
PORTAGE_URL=${BASE_URL}snapshots/portage-latest.tar.xz

if /sbin/mkfs.xfs -f /dev/vdb; then
	mount /dev/vdb /mnt
else
	mount -t virtiofs fs /mnt
fi

echo "Downloading stage3..."
curl -s "$LATEST_STAGE3_URL" | tar Jxpf - --exclude='./dev/*' -C /mnt
mkdir -p /mnt/var/db/repos/gentoo
echo "Downloading portage..."
curl -s "$PORTAGE_URL" | tar Jxpf - --strip-components=1 -C /mnt/var/db/repos/gentoo

sed -i 's/^root:\*:/root::/' /mnt/etc/shadow
echo -e '[Match]\nName=eth0 host0\n[Network]\nDHCP=yes\nMulticastDNS=yes\nLLMNR=yes\n' > /mnt/etc/systemd/network/50-eth0.network

mount -o bind /proc /mnt/proc
mount -o bind /sys /mnt/sys
mount -o bind /dev /mnt/dev
cp /etc/resolv.conf /mnt/etc/
[ -f /etc/localtime ] && cp -a /etc/localtime /mnt/etc/
[ -d /root/.ssh ] && cp -a /root/.ssh /mnt/root/
[ -d /etc/ssh -a -d /mnt/etc/ssh ] && cp -a /etc/ssh/*_key /etc/ssh/*_key.pub /mnt/etc/ssh/
echo 'sys-kernel/installkernel dracut' >> /mnt/etc/portage/package.use/kernel
chroot /mnt env USE="-quic -http2 -http3 -curl_quic_openssl" emerge gentoo-kernel-bin
chroot /mnt systemctl enable systemd-resolved sshd
mv /mnt/etc/issue.logo /mnt/etc/issue
umount /mnt/dev
umount /mnt/sys
umount /mnt/proc
umount /mnt

reboot -f

