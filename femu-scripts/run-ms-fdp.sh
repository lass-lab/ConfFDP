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
handle_number=1 # should be power of 2, smaller than luns_per_ch*nchs

# MS
stream_number=$rg_number

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


ssd_size=$G30

#Compose the entire FEMU BBSSD command line options
FEMU_OPTIONS_MS="-device femu"

# FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",mdts=18"
FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",devsz_mb=${ssd_size}"
FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",namespaces=${stream_number}"
FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",femu_mode=7"
FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",secsz=${secsz}"
FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",nand_page_size_kb=${NAND_PAGE_SIZE_KB}"
FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",nand_block_size_mb=${NAND_BLOCK_SIZE_MB}"
# FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",blks_per_pl=${blks_per_pl}"
FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",pls_per_lun=${pls_per_lun}"
FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",luns_per_ch=${luns_per_ch}"
FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",nchs=${nchs}"
FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",pg_rd_lat=${pg_rd_lat}"
FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",pg_wr_lat=${pg_wr_lat}"
FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",blk_er_lat=${blk_er_lat}"
FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",ch_xfer_lat=${ch_xfer_lat}"
FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",gc_thres_pcent=${gc_thres_pcent}"
FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",gc_thres_pcent_high=${gc_thres_pcent_high}"
FEMU_OPTIONS_MS=${FEMU_OPTIONS_MS}",stream_number=${stream_number}"

# echo ${FEMU_OPTIONS}



FEMU_OPTIONS_FDP="-device femu"

# FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",mdts=18"
FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",devsz_mb=${ssd_size}"
FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",namespaces=${rg_number}"
FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",femu_mode=6"
FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",secsz=${secsz}"
FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",nand_page_size_kb=${NAND_PAGE_SIZE_KB}"
FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",nand_block_size_mb=${NAND_BLOCK_SIZE_MB}"
# FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",blks_per_pl=${blks_per_pl}"
FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",pls_per_lun=${pls_per_lun}"
FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",luns_per_ch=${luns_per_ch}"
FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",nchs=${nchs}"
FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",pg_rd_lat=${pg_rd_lat}"
FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",pg_wr_lat=${pg_wr_lat}"
FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",blk_er_lat=${blk_er_lat}"
FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",ch_xfer_lat=${ch_xfer_lat}"
FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",gc_thres_pcent=${gc_thres_pcent}"
FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",gc_thres_pcent_high=${gc_thres_pcent_high}"
FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",handle_number=${handle_number}"
FEMU_OPTIONS_FDP=${FEMU_OPTIONS_FDP}",rg_number=${rg_number}"



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
    -smp 16 \
    -m 20G \
    -device virtio-scsi-pci,id=scsi0 \
    -device scsi-hd,drive=hd0 \
    -drive file=$OSIMGF,if=none,aio=io_uring,cache=none,format=qcow2,id=hd0 \
    ${FEMU_OPTIONS_FDP} \
     ${FEMU_OPTIONS_MS} \
    -net user,hostfwd=tcp::8095-:22 \
    -net nic,model=virtio \
    -nographic \
    -qmp unix:./qmp-sock,server,nowait 2>&1 | tee log
