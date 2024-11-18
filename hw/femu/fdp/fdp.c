// nvme_io_mgmt_recv_ruhs
// nvme_update_ruh

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