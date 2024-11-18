#include "../nvme.h"
#include "./msftl.h"

static void ms_init_ctrl_str(FemuCtrl *n)
{
    static int fsid_vbb = 0;
    const char *vbbssd_mn = "FEMU BlackBox-SSD Controller";
    const char *vbbssd_sn = "vSSD";
    print_sungjin(ms_init_ctrl_str);
    nvme_set_ctrl_name(n, vbbssd_mn, vbbssd_sn, &fsid_vbb);
}

/* bb <=> black-box */
static void ms_init(FemuCtrl *n, Error **errp)
{
    struct ssd *ssd = n->ssd = g_malloc0(sizeof(struct ssd));
    // print_sungjin(ms_init);
    ms_init_ctrl_str(n);

    ssd->dataplane_started_ptr = &n->dataplane_started;
    ssd->ssdname = (char *)n->devname;
    femu_debug("Starting FEMU in Blackbox-SSD mode ...\n");
    msssd_init(n);
}

static void bb_flip(FemuCtrl *n, NvmeCmd *cmd)
{
    struct ssd *ssd = n->ssd;
    int64_t cdw10 = le64_to_cpu(cmd->cdw10);

    switch (cdw10) {
    case FEMU_ENABLE_GC_DELAY:
        ssd->sp.enable_gc_delay = true;
        femu_log("%s,FEMU GC Delay Emulation [Enabled]!\n", n->devname);
        break;
    case FEMU_DISABLE_GC_DELAY:
        ssd->sp.enable_gc_delay = false;
        femu_log("%s,FEMU GC Delay Emulation [Disabled]!\n", n->devname);
        break;
    case FEMU_ENABLE_DELAY_EMU:
        ssd->sp.pg_rd_lat = NAND_READ_LATENCY;
        ssd->sp.pg_wr_lat = NAND_PROG_LATENCY;
        ssd->sp.blk_er_lat = NAND_ERASE_LATENCY;
        ssd->sp.ch_xfer_lat = 0;
        femu_log("%s,FEMU Delay Emulation [Enabled]!\n", n->devname);
        break;
    case FEMU_DISABLE_DELAY_EMU:
        ssd->sp.pg_rd_lat = 0;
        ssd->sp.pg_wr_lat = 0;
        ssd->sp.blk_er_lat = 0;
        ssd->sp.ch_xfer_lat = 0;
        femu_log("%s,FEMU Delay Emulation [Disabled]!\n", n->devname);
        break;
    case FEMU_RESET_ACCT:
        n->nr_tt_ios = 0;
        n->nr_tt_late_ios = 0;
        femu_log("%s,Reset tt_late_ios/tt_ios,%lu/%lu\n", n->devname,
                n->nr_tt_late_ios, n->nr_tt_ios);
        break;
    case FEMU_ENABLE_LOG:
        n->print_log = true;
        femu_log("%s,Log print [Enabled]!\n", n->devname);
        break;
    case FEMU_DISABLE_LOG:
        n->print_log = false;
        femu_log("%s,Log print [Disabled]!\n", n->devname);
        break;
    default:
        printf("FEMU:%s,Not implemented flip cmd (%lu)\n", n->devname, cdw10);
    }
}

static uint16_t ms_nvme_rw(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd,
                           NvmeRequest *req)
{
    return nvme_rw(n, ns, cmd, req);
}

static uint16_t ms_io_cmd(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd,
                          NvmeRequest *req)
{
    // print_sungjin(ms_io_cmd);
    switch (cmd->opcode) {
    case NVME_CMD_READ:
    case NVME_CMD_WRITE:
        return ms_nvme_rw(n, ns, cmd, req);
    case NVME_CMD_DSM:
    //     // sungjin
        return NVME_SUCCESS;

    case NVME_CMD_IO_MGMT_RECV:
        // print_sungjin()
        printf("ms_io_cmd NVME_CMD_IO_MGMT_RECV");
        return NVME_SUCCESS;
    case NVME_CMD_IO_MGMT_SEND:
        printf("ms_io_cmd NVME_CMD_IO_MGMT_SEND");
        return NVME_SUCCESS;
    default:
        return NVME_INVALID_OPCODE | NVME_DNR;
    }
}

static uint16_t ms_admin_cmd(FemuCtrl *n, NvmeCmd *cmd)
{
    switch (cmd->opcode) {
    case NVME_ADM_CMD_FEMU_FLIP:
        bb_flip(n, cmd);
        return NVME_SUCCESS;
    default:
        return NVME_INVALID_OPCODE | NVME_DNR;
    }
}

static uint16_t ms_get_log(FemuCtrl* n, NvmeCmd* cmd){
    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    // uint32_t dw11 = le32_to_cpu(cmd->cdw11);
    // uint32_t dw12 = le32_to_cpu(cmd->cdw12);
    // uint32_t dw13 = le32_to_cpu(cmd->cdw13);
    uint16_t lid = dw10 & 0xffff;

    switch (lid) {
    case NVME_LOG_FDP_CONFS:
        // return nvme_error_log_info(n, cmd, len);
        return NVME_SUCCESS;
    case NVME_LOG_FDP_RUH_USAGE:
        // return nvme_smart_info(n, cmd, len);
        return NVME_SUCCESS;
    case NVME_LOG_FDP_STATS:
        // return nvme_fw_log_info(n, cmd, len);
        return NVME_SUCCESS;
    case NVME_LOG_FDP_EVENTS:
        // return nvme_cmd_effects(n, cmd, csi, len, off);
        return NVME_SUCCESS;
    default:
        // if (n->ext_ops.get_log) {
        //     return n->ext_ops.get_log(n, cmd);
        // }
        return NVME_INVALID_LOG_ID | NVME_DNR;
    }
}

int nvme_register_msssd(FemuCtrl *n)
{
    n->ext_ops = (FemuExtCtrlOps) {
        .state            = NULL,
        .init             = ms_init,
        .exit             = NULL,
        .rw_check_req     = NULL,
        .admin_cmd        = ms_admin_cmd,
        .io_cmd           = ms_io_cmd,
        .get_log          = ms_get_log,
    };

    return 0;
}

