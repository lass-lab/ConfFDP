// nvme_io_mgmt_recv_ruhs
// nvme_update_ruh
/*

1. report
2. update
3. get log page - confs, ruhusage, fdp state, fdp events
4. get feature fdp
5. set feature fdp

*/
/*
uint16_t pid = le16_to_cpu(rw->dspec);
    if (dtype != NVME_DIRECTIVE_DATA_PLACEMENT ||
        !nvme_parse_pid(ns, pid, &ph, &rg)) {
        ph = 0;
        rg = 0;
    }

static inline bool nvme_parse_pid(NvmeNamespace *ns, uint16_t pid,
                                  uint16_t *ph, uint16_t *rg)
{
    *rg = nvme_pid2rg(ns, pid);
    *ph = nvme_pid2ph(ns, pid);

    return nvme_ph_valid(ns, *ph) && nvme_rg_valid(ns->endgrp, *rg);
}


static inline uint16_t nvme_pid2ph(NvmeNamespace *ns, uint16_t pid)
{
    uint16_t rgif = ns->endgrp->fdp.rgif;

    if (!rgif) {
        return pid;
    }

    return pid & ((1 << (15 - rgif)) - 1);
}

static inline uint16_t nvme_pid2rg(NvmeNamespace *ns, uint16_t pid)
{
    uint16_t rgif = ns->endgrp->fdp.rgif;

    if (!rgif) {
        return 0;
    }

    return pid >> (16 - rgif);
}

*/




#include "../nvme.h"
#include "./f2dpftl.h"



static void f2dp_init_ctrl_str(FemuCtrl *n)
{
    static int fsid_vbb = 0;
    const char *vbbssd_mn = "FEMU BlackBox-SSD Controller";
    const char *vbbssd_sn = "vSSD";
    print_sungjin(f2dp_init_ctrl_str);
    nvme_set_ctrl_name(n, vbbssd_mn, vbbssd_sn, &fsid_vbb);
}

/* bb <=> black-box */
static void f2dp_init(FemuCtrl *n, Error **errp)
{
    struct ssd *ssd = n->ssd = g_malloc0(sizeof(struct ssd));
    // print_sungjin(fdp_init);
    f2dp_init_ctrl_str(n);

    ssd->dataplane_started_ptr = &n->dataplane_started;
    ssd->ssdname = (char *)n->devname;
    femu_debug("Starting FEMU in Blackbox-SSD mode ...\n");
    f2dpssd_init(n);
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

static uint16_t f2dp_nvme_rw(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd,
                           NvmeRequest *req)
{
    // print_sungjin(f2dp_nvme_rw);
    // int ret= nvme_rw(n, ns, cmd, req);
    // print_sungjin(ret);
    // return ret;

    return nvme_rw(n, ns, cmd, req);
}

static uint16_t f2dp_io_cmd(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd,
                          NvmeRequest *req)
{
    // print_sungjin(fdp_io_cmd);
    switch (cmd->opcode) {
    case NVME_CMD_WRITE:
        // print_sungjin(cmd->cdw13);
        // print_sungjin(req->cmd.cdw13);
        /*fall through*/
    case NVME_CMD_READ:
        return f2dp_nvme_rw(n, ns, cmd, req);
    case NVME_CMD_DSM:
    //     // sungjin
        return NVME_SUCCESS;

    case NVME_CMD_IO_MGMT_RECV:
        // print_sungjin()
        printf("f2dp_io_cmd NVME_CMD_IO_MGMT_RECV");
        return NVME_SUCCESS;
    case NVME_CMD_IO_MGMT_SEND:
        printf("f2dp_io_cmd NVME_CMD_IO_MGMT_SEND");
        return NVME_SUCCESS;
    default:
        return NVME_INVALID_OPCODE | NVME_DNR;
    }
}

static uint16_t f2dp_admin_cmd(FemuCtrl *n, NvmeCmd *cmd)
{
    switch (cmd->opcode) {
    case NVME_ADM_CMD_FEMU_FLIP:
        bb_flip(n, cmd);
        return NVME_SUCCESS;
    default:
        return NVME_INVALID_OPCODE | NVME_DNR;
    }
}


static size_t sizeof_f2dp_conf_descr(size_t nruh, size_t vss)
{
    size_t entry_siz = sizeof(NvmeFdpDescrHdr) + nruh * sizeof(NvmeRuhDescr)
                       + vss;
    return ROUND_UP(entry_siz, 8);
}


// static uint16_t fdp_confs(NvmeCtrl *n, uint32_t endgrpid, uint32_t buf_len,
//                                uint64_t off, NvmeRequest *req)
static uint16_t f2dp_confs(FemuCtrl* n, NvmeCmd* cmd)
{
    printf("FDP CONFFS@@@@@@@@@@@@@@@22\n");
    struct ssd *ssd = n->ssd;
    struct ssdparams *spp = &ssd->sp;
    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    uint32_t dw11 = le32_to_cpu(cmd->cdw11);
    uint32_t dw12 = le32_to_cpu(cmd->cdw12);
    uint32_t dw13 = le32_to_cpu(cmd->cdw13);
    // uint8_t  lid = dw10 & 0xff;
    // uint8_t  lsp = (dw10 >> 8) & 0xf;
    // uint8_t  rae = (dw10 >> 15) & 0x1;
    // uint8_t  csi = le32_to_cpu(cmd->cdw14) >> 24;
    uint32_t numdl, numdu;
    //  endgrpid;
    uint64_t off, lpol, lpou;
    size_t   buf_len;
    // uint16_t status;
    uint64_t prp1, prp2;
    prp1 = le64_to_cpu(cmd->dptr.prp1);
    prp2 = le64_to_cpu(cmd->dptr.prp2);

    numdl = (dw10 >> 16);
    numdu = (dw11 & 0xffff);
    // endgrpid = (dw11 >> 16);
    lpol = dw12;
    lpou = dw13;

    buf_len = (((numdu << 16) | numdl) + 1) << 2;
    off = (lpou << 32ULL) | lpol;




    uint32_t log_size, trans_len;
    g_autofree uint8_t *buf = NULL;
    NvmeFdpDescrHdr *hdr;
    NvmeRuhDescr *ruhd;
    // NvmeEnduranceGroup *endgrp;
    NvmeFdpConfsHdr *log;
    size_t nruh, fdp_descr_size;
    int i;

    // if (endgrpid != 1 || !n->subsys) {
    //     return NVME_INVALID_FIELD | NVME_DNR;
    // }

    // endgrp = &n->subsys->endgrp;

    if (true) {
        nruh = n->stream_number*n->rg_number;
    } else {
        nruh = 1;
    }
    // FDPVSS =0;
    fdp_descr_size = sizeof_f2dp_conf_descr(nruh, 0);
    log_size = sizeof(NvmeFdpConfsHdr) + fdp_descr_size;

    if (off >= log_size) {
        printf("off >= log_size %lu %u\n",off,log_size);
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    trans_len = MIN(log_size - off, buf_len);

    buf = g_malloc0(log_size);
    log = (NvmeFdpConfsHdr *)buf;
    hdr = (NvmeFdpDescrHdr *)(log + 1);
    ruhd = (NvmeRuhDescr *)(buf + sizeof(*log) + sizeof(*hdr));

    log->num_confs = cpu_to_le16(0);
    log->size = cpu_to_le32(log_size);

    hdr->descr_size = cpu_to_le16(fdp_descr_size);
    if (true) {
        // endgrp->fdp.rgif 8
        // hdr->fdpa = FIELD_DP8(hdr->fdpa, FDPA, VALID_FDP, 1);
        // hdr->fdpa = FIELD_DP8(hdr->fdpa, FDPA, RGIF, 8);
        hdr->fdpa = 0;
        hdr->fdpa = 0;

        hdr->nrg = cpu_to_le16(n->rg_number);


        hdr->nruh = cpu_to_le16(n->stream_number*n->rg_number);
        hdr->maxpids = cpu_to_le16(NVME_FDP_MAXPIDS - 1);
        hdr->nnss = cpu_to_le32(n->num_namespaces);
        // hdr->runs = cpu_to_le64(endgrp->fdp.runs);
         hdr->runs = cpu_to_le64(spp->tt_blks);

        for (i = 0; i < nruh; i++) {
            ruhd->ruht = NVME_RUHT_PERSISTENTLY_ISOLATED;
            ruhd++;
        }
    } else {
        /* 1 bit for RUH in PIF -> 2 RUHs max. */
        hdr->nrg = cpu_to_le16(1);
        hdr->nruh = cpu_to_le16(1);
        hdr->maxpids = cpu_to_le16(NVME_FDP_MAXPIDS - 1);
        hdr->nnss = cpu_to_le32(1);
        hdr->runs = cpu_to_le64(96 * MiB);

        ruhd->ruht = NVME_RUHT_INITIALLY_ISOLATED;
    }
    dma_read_prp(n, (uint8_t *)buf, trans_len, prp1, prp2);
    return NVME_SUCCESS;
    // return nvme_c2h(n, (uint8_t *)buf + off, trans_len, req);
}

static uint16_t f2dp_get_log(FemuCtrl* n, NvmeCmd* cmd){
    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    // uint32_t dw11 = le32_to_cpu(cmd->cdw11);
    // uint32_t dw12 = le32_to_cpu(cmd->cdw12);
    // uint32_t dw13 = le32_to_cpu(cmd->cdw13);
    uint16_t lid = dw10 & 0xffff;

    switch (lid) {
    case NVME_LOG_FDP_CONFS:
        return f2dp_confs(n, cmd);
        // return NVME_SUCCESS;
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

int nvme_register_f2dpssd(FemuCtrl *n)
{
    n->ext_ops = (FemuExtCtrlOps) {
        .state            = NULL,
        .init             = f2dp_init,
        .exit             = NULL,
        .rw_check_req     = NULL,
        .admin_cmd        = f2dp_admin_cmd,
        .io_cmd           = f2dp_io_cmd,
        .get_log          = f2dp_get_log,
    };

    return 0;
}

