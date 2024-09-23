# Huaicheng Li <hcli@cmu.edu>
# Run FEMU as Zoned-Namespace (ZNS) SSDs
#

# Image directory
G128=131072
G96=98304
G76=77824
G72=73728
G68=69632
G64=65536
G66=67584
G67=68608
G20=20480
G10=10240
G5=5120

G72=73728

G80=81920
IMGDIR=/home/sungjin/images
# Virtual machine disk image
OSIMGF=$IMGDIR/fdp.qcow2

# SHARED_DIR=/home/gs201/images/shared/F2FS_WITH_FAR



if [[ ! -e "$OSIMGF" ]]; then
	echo ""
	echo "VM disk image couldn't be found ..."
	echo "Please prepare a usable VM image and place it as $OSIMGF"
	echo "Once VM disk image is ready, please rerun this script again"
	echo ""
	exit
fi
QEMU=/home/sungjin/ConfFDP/build-femu/qemu-system-x86_64

# sudo $QEMU \
#     -name "FEMU-FDP-SSD-VM" \
#     -enable-kvm \
#     -cpu host \
#     -smp 64 \
#     -m 32G \
#     -device virtio-scsi-pci,id=scsi0 \
#     -device scsi-hd,drive=hd0 \
#     -drive file=$OSIMGF,if=none,aio=native,cache=none,format=qcow2,id=hd0 \
#     -device femu,devsz_mb=${G20},femu_mode=1 \
#     -device nvme-subsys=on,id=subsys0,fdp=true,fdp.nruh=8,fdp.nrg=32,fdp.runs=40960 \
#     -device nvme,id=ctrl0,serial=deadbeef,bus=pcie_root_port0,subsys=subsys0 \
#     -drive id=nvm-1,file=/home/sungjin/images/nvm-1.img,format=raw,if=none,discard=unmap,media=disk,read-only=no \
#     -device nvme-ns,id=nvm-1,drive=nvme-1,bus=ctrl0,nsid=1,logical_block_size=4096,physical_block_size=4096 \
#     -net user,hostfwd=tcp::8080-:22 \
#     -net nic,model=virtio \
#     -nographic \
#     -qmp unix:./qmp-sock,server,nowait 2>&1 | tee log


# sudo $QEMU \
#     -name "FEMU-FDP-SSD-VM" \
#     -enable-kvm \
#     -cpu host \
#     -smp 64 \
#     -m 32G \
#     -device virtio-scsi-pci,id=scsi0 \
#     -device scsi-hd,drive=hd0 \
#     -drive file=$OSIMGF,if=none,aio=native,cache=none,format=qcow2,id=hd0 \
#     -device nvme-subsys=on,id=subsys0,fdp=true,fdp.nruh=8,fdp.nrg=32,fdp.runs=40960 \
#     -device femu,devsz_mb=${G20},femu_mode=1 \
#     -net user,hostfwd=tcp::8080-:22 \
#     -net nic,model=virtio \
#     -nographic \
#     -qmp unix:./qmp-sock,server,nowait 2>&1 | tee log



sudo $QEMU \
    -name "FEMU-FDP-SSD-VM" \
    -enable-kvm \
    -cpu host \
    -smp 64 \
    -m 32G \
    -device virtio-scsi-pci,id=scsi0 \
    -device scsi-hd,drive=hd0 \
    -drive file=$OSIMGF,if=none,aio=native,cache=none,format=qcow2,id=hd0 \
    -device pcie-root-port,id=pcie_root_port0,chassis=1,slot=0\
    -device nvme-subsys=on,id=subsys0,fdp=true,fdp.nruh=8,fdp.nrg=32,fdp.runs=40960 \
    -device nvme,id=ctrl0,serial=deadbeef,bus=pcie_root_port0,subsys=subsys0 \
    -drive id=nvm-1,file=/home/sungjin/images/nvm-1.img,format=raw,if=none,discard=unmap,media=disk,read-only=no \
    -device nvme-ns,id=nvm-1,drive=nvme-1,bus=ctrl0,nsid=1,logical_block_size=4096,physical_block_size=4096 \
    -net user,hostfwd=tcp::8080-:22 \
    -net nic,model=virtio \
    -nographic \
    -qmp unix:./qmp-sock,server,nowait 2>&1 | tee log


# -device "nvme-subsys,id=subsys0,fdp=true,fdp.nruh=8,fdp.nrg=32,fdp.runs=40960"