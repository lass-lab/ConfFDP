#!/bin/bash
# Huaicheng Li <huaicheng@cs.uchicago.edu>
# Run FEMU as a black-box SSD (FTL managed by the device)

# image directory
HOME=/home/sungjin
IMGDIR=$HOME/images
# Virtual machine disk image
OSIMGF=$IMGDIR/fdp.qcow2

# Configurable SSD Controller layout parameters (must be power of 2)
secsz=4096 # sector size in bytes
# secs_per_pg=1 # number of sectors in a flash page


blks_per_pl=256 # number of blocks per plane not used /////////////////////////////////

pls_per_lun=1 # keep it at one, no multiplanes support
luns_per_ch=8 # number of chips per channel
nchs=8 # number of channels
 # in megabytes, if you change the above layout parameters, make sure you manually recalculate the ssd size and modify it here, please consider a default 25% overprovisioning ratio.

# Latency in nanoseconds
pg_rd_lat=40000 # page read latency
pg_wr_lat=200000 # page write latency
blk_er_lat=2000000 # block erase latency
ch_xfer_lat=25000 # channel transfer time, ignored for now

# GC Threshold (1-100)
gc_thres_pcent=75
gc_thres_pcent_high=95

#-----------------------------------------------------------------------
NAND_PAGE_SIZE_KB=4
NAND_BLOCK_SIZE_MB=64

# FDP
# luns_per_rg=32
rg_number=2 # 1~64
handle_number=2 # should be power of 2, smaller than luns_per_ch*nchs

# sudo gdb /home/sungjin/ConfFDP/build-femu/qemu-system-x86_64
#  run   -name "FEMU-MSSSD-VM" \
#     -enable-kvm \
#     -cpu host \
#     -smp 64 \
#     -m 32G \
#     -device virtio-scsi-pci,id=scsi0 \
#     -device scsi-hd,drive=hd0 \
#     -drive file=/home/sungjin/images/fdp.qcow2,if=none,aio=io_uring,cache=none,format=qcow2,id=hd0 \
#    -device femu,devsz_mb=65536,namespaces=1,femu_mode=8,secsz=4096,nand_page_size_kb=4,nand_block_size_mb=64,pls_per_lun=1,luns_per_ch=8,nchs=8,pg_rd_lat=40000,pg_wr_lat=200000,blk_er_lat=2000000,ch_xfer_lat=25000,gc_thres_pcent=75,gc_thres_pcent_high=95,handle_number=2,rg_number=2,multipoller_enabled=1 \
#     -net user,hostfwd=tcp::8095-:22 \
#     -net nic,model=virtio \
#     -nographic \
#     -qmp unix:./qmp-sock,server,nowait 2>&1 | tee log

# if [ $NAND_BLOCK_SIZE -eq 64 ]; then
#     pgs_per_blk=4096 # number of pages per flash block
# elif [ $NAND_BLOCK_SIZE -eq 32 ]; then
#     pgs_per_blk=4096 # number of pages per flash block
# else  # 4MB
#     pgs_per_blk=256 # number of pages per flash block
# fi

# NAND_4MB_BLOCK=256
G12=12288
G30=30720
G64=65536


ssd_size=$G64

#Compose the entire FEMU BBSSD command line options
FEMU_OPTIONS="-device femu"

# FEMU_OPTIONS=${FEMU_OPTIONS}",mdts=18"
FEMU_OPTIONS=${FEMU_OPTIONS}",devsz_mb=${ssd_size}"
FEMU_OPTIONS=${FEMU_OPTIONS}",namespaces=1"
FEMU_OPTIONS=${FEMU_OPTIONS}",femu_mode=6"
FEMU_OPTIONS=${FEMU_OPTIONS}",secsz=${secsz}"
FEMU_OPTIONS=${FEMU_OPTIONS}",nand_page_size_kb=${NAND_PAGE_SIZE_KB}"
FEMU_OPTIONS=${FEMU_OPTIONS}",nand_block_size_mb=${NAND_BLOCK_SIZE_MB}"
# FEMU_OPTIONS=${FEMU_OPTIONS}",blks_per_pl=${blks_per_pl}"
FEMU_OPTIONS=${FEMU_OPTIONS}",pls_per_lun=${pls_per_lun}"
FEMU_OPTIONS=${FEMU_OPTIONS}",luns_per_ch=${luns_per_ch}"
FEMU_OPTIONS=${FEMU_OPTIONS}",nchs=${nchs}"
FEMU_OPTIONS=${FEMU_OPTIONS}",pg_rd_lat=${pg_rd_lat}"
FEMU_OPTIONS=${FEMU_OPTIONS}",pg_wr_lat=${pg_wr_lat}"
FEMU_OPTIONS=${FEMU_OPTIONS}",blk_er_lat=${blk_er_lat}"
FEMU_OPTIONS=${FEMU_OPTIONS}",ch_xfer_lat=${ch_xfer_lat}"
FEMU_OPTIONS=${FEMU_OPTIONS}",gc_thres_pcent=${gc_thres_pcent}"
FEMU_OPTIONS=${FEMU_OPTIONS}",gc_thres_pcent_high=${gc_thres_pcent_high}"
FEMU_OPTIONS=${FEMU_OPTIONS}",handle_number=${handle_number}"
FEMU_OPTIONS=${FEMU_OPTIONS}",rg_number=${rg_number}"
FEMU_OPTIONS=${FEMU_OPTIONS}",multipoller_enabled=1"


echo ${FEMU_OPTIONS}

if [[ ! -e "$OSIMGF" ]]; then
	echo ""
	echo "VM disk image couldn't be found ..."
	echo "Please prepare a usable VM image and place it as $OSIMGF"
	echo "Once VM disk image is ready, please rerun this script again"
	echo ""
	exit
fi
QEMU=$HOME/ConfFDP/build-femu/qemu-system-x86_64
sudo $QEMU \
    -name "FEMU-MSSSD-VM" \
    -enable-kvm \
    -cpu host \
    -smp 64 \
    -m 32G \
    -device virtio-scsi-pci,id=scsi0 \
    -device scsi-hd,drive=hd0 \
    -drive file=$OSIMGF,if=none,aio=io_uring,cache=none,format=qcow2,id=hd0 \
    ${FEMU_OPTIONS} \
    -net user,hostfwd=tcp::8085-:22 \
    -net nic,model=virtio \
    -nographic \
    -qmp unix:./qmp-sock,server,nowait 2>&1 | tee log
