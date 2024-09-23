# static Property nvme_subsystem_props[] = {
#     DEFINE_PROP_STRING("nqn", NvmeSubsystem, params.nqn),
#     DEFINE_PROP_BOOL("fdp", NvmeSubsystem, params.fdp.enabled, false),
#     DEFINE_PROP_SIZE("fdp.runs", NvmeSubsystem, params.fdp.runs,
#                      NVME_DEFAULT_RU_SIZE),
#     DEFINE_PROP_UINT32("fdp.nrg", NvmeSubsystem, params.fdp.nrg, 1),
#     DEFINE_PROP_UINT16("fdp.nruh", NvmeSubsystem, params.fdp.nruh, 0),
#     DEFINE_PROP_END_OF_LIST(),
# };

# enum {
#     FEMU_OCSSD_MODE = 0,
#     FEMU_BBSSD_MODE = 1,
#     FEMU_NOSSD_MODE = 2,
#     FEMU_ZNSSD_MODE = 3,
#     FEMU_SMARTSSD_MODE,
#     FEMU_KVSSD_MODE,
# };




: '

-device "nvme-subsys,id=subsys0,fdp=true,fdp.nruh=8,fdp.nrg=32,fdp.runs=40960"


Reclaim Unit(RU) ----> Erase Block
Reclaim Group ---> Die
Reclaim Unit Handle ----> ZEU ? 

Placement Handle
Placement Identifier

'



: '
FDP Configurations Log Page
FDP Configuration Descriptor
Reclaim Unit Handle Descriptor
Reclaim Unit Handle Usage Log Page
Reclaim Unit Handle Usage Descriptor
'


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

sudo $QEMU \
    -name "FEMU-FDP-SSD-VM" \
    -enable-kvm \
    -cpu host \
    -smp 64 \
    -m 32G \
    -device virtio-scsi-pci,id=scsi0 \
    -device scsi-hd,drive=hd0 \
    -drive file=$OSIMGF,if=none,aio=native,cache=none,format=qcow2,id=hd0 \
    -device femu,devsz_mb=${G80},femu_mode=1,nvme-subsys,id=subsys0,fdp=true,fdp.nruh=8,fdp.nrg=32,fdp.runs=40960 \
    -net user,hostfwd=tcp::8080-:22 \
    -net nic,model=virtio \
    -nographic \
    -qmp unix:./qmp-sock,server,nowait 2>&1 | tee log


# -device "nvme-subsys,id=subsys0,fdp=true,fdp.nruh=8,fdp.nrg=32,fdp.runs=40960"