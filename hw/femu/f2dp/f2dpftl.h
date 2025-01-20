#ifndef __FEMU_FTL_H
#define __FEMU_FTL_H

#include "../nvme.h"

#define INVALID_PPA     (~(0ULL))
#define INVALID_LPN     (~(0ULL))
#define UNMAPPED_PPA    (~(0ULL))


#define F2DP_RG_NUMBER 32
#define F2DP_MAX_PID_NR 33
#define F2DP_DEFAULT_STREAM (F2DP_MAX_PID_NR-1)

#define RGIF 0
#define VALID_FDP 0
#define FDPA 0
#define NVME_FDP_MAXPIDS 16
// REG8(FDPA, 0x0)
//     FIELD(FDPA, RGIF, 0, 4)
//     FIELD(FDPA, VWC, 4, 1)
//     FIELD(FDPA, VALID, 7, 1);

enum {
    NAND_READ =  0,
    NAND_WRITE = 1,
    NAND_ERASE = 2,

    NAND_READ_LATENCY = 40000,
    NAND_PROG_LATENCY = 200000,
    NAND_ERASE_LATENCY = 2000000,
};

enum {
    USER_IO = 0,
    GC_IO = 1,
};

enum {
    SEC_FREE = 0,
    SEC_INVALID = 1,
    SEC_VALID = 2,

    PG_FREE = 0,
    PG_INVALID = 1,
    PG_VALID = 2
};

enum {
    FEMU_ENABLE_GC_DELAY = 1,
    FEMU_DISABLE_GC_DELAY = 2,

    FEMU_ENABLE_DELAY_EMU = 3,
    FEMU_DISABLE_DELAY_EMU = 4,

    FEMU_RESET_ACCT = 5,
    FEMU_ENABLE_LOG = 6,
    FEMU_DISABLE_LOG = 7,
};


#define BLK_BITS    (16)
#define PG_BITS     (16)
#define SEC_BITS    (8)
#define PL_BITS     (8)
#define LUN_BITS    (8)
#define CH_BITS     (7)

/* describe a physical page addr */
struct ppa {
    union {
        struct {
            uint64_t blk : BLK_BITS;
            uint64_t pg  : PG_BITS;
            uint64_t sec : SEC_BITS;
            uint64_t pl  : PL_BITS;
            uint64_t lun : LUN_BITS;
            uint64_t ch  : CH_BITS;
            uint64_t rsv : 1;
        } g;

        uint64_t ppa;
    };
};

typedef int nand_sec_status_t;

struct nand_page {
    nand_sec_status_t *sec;
    int nsecs;
    int status;
};

struct nand_block {
    struct nand_page *pg;
    int npgs;
    int ipc; /* invalid page count */
    int vpc; /* valid page count */
    int erase_cnt;
    int wp; /* current write pointer */
    int pid;
};

struct nand_plane {
    struct nand_block *blk;
    int nblks;
};

struct nand_lun {
    struct nand_plane *pl;
    int npls;
    uint64_t next_lun_avail_time;
    bool busy;
    uint64_t gc_endtime;
};

struct ssd_channel {
    struct nand_lun *lun;
    int nluns;
    uint64_t next_ch_avail_time;
    bool busy;
    uint64_t gc_endtime;
};

struct ssdparams {
    int secsz;        /* sector size in bytes */
    int secs_per_pg;  /* # of sectors per page */
    int pgs_per_blk;  /* # of NAND pages per block */
    int blks_per_pl;  /* # of blocks per plane */
    int pls_per_lun;  /* # of planes per LUN (Die) */
    int luns_per_ch;  /* # of LUNs per channel */
    int nchs;         /* # of channels in the SSD */


    //fdp
    int luns_per_rg;
    int chnls_per_rg;
    int rgs_per_chnl;
    

    int pg_rd_lat;    /* NAND page read latency in nanoseconds */
    int pg_wr_lat;    /* NAND page program latency in nanoseconds */
    int blk_er_lat;   /* NAND block erase latency in nanoseconds */
    int ch_xfer_lat;  /* channel transfer latency for one page in nanoseconds
                       * this defines the channel bandwith
                       */

    double gc_thres_pcent;
    int gc_thres_lines;
    double gc_thres_pcent_high;
    int gc_thres_lines_high;
    bool enable_gc_delay;

    /* below are all calculated values */
    int secs_per_blk; /* # of sectors per block */
    int secs_per_pl;  /* # of sectors per plane */
    int secs_per_lun; /* # of sectors per LUN */
    int secs_per_ch;  /* # of sectors per channel */
    int tt_secs;      /* # of sectors in the SSD */

    int pgs_per_pl;   /* # of pages per plane */
    int pgs_per_lun;  /* # of pages per LUN (Die) */
    int pgs_per_ch;   /* # of pages per channel */
    int tt_pgs;       /* total # of pages in the SSD */

    int blks_per_lun; /* # of blocks per LUN */
    int blks_per_ch;  /* # of blocks per channel */
    int tt_blks;      /* total # of blocks in the SSD */

    int secs_per_line;
    int pgs_per_line;
    int blks_per_line;
    int tt_lines;

    int pls_per_ch;   /* # of planes per channel */
    int tt_pls;       /* total # of planes in the SSD */

    int tt_luns;      /* total # of LUNs in the SSD */
};

typedef struct line {
    int id;  /* line id, the same as corresponding block id */
    int ipc; /* invalid page count in this line */
    int vpc; /* valid page count in this line */
    QTAILQ_ENTRY(line) entry; /* in either {free,victim,full} list */
    /* position in the priority queue for victim lines */
    size_t                  pos;
    int stream_id;
    int rg_id;
} line;

/* wp: record next write addr */
struct write_pointer {
    struct line ** curline;
    // int ch;
    
    // int pg;
    // int blk;
    int pl;

    // int start_die;
    // int end_die;
    int lun_nr;
    int logical_lun; // logical

    int physical_lun_map[64]; //spp->tt_luns == 64
    int physical_blk_map[64];
    int physical_pg_map[64];
    unsigned int rg_bitmap;
};

struct sungjin_stat{
    uint64_t copied;
    uint64_t block_erased;
};

struct line_mgmt {
    struct line *lines;
    /* free line list, we only need to maintain a list of blk numbers */
    QTAILQ_HEAD(free_line_list, line) free_line_list;
    pqueue_t *victim_line_pq;
    //QTAILQ_HEAD(victim_line_list, line) victim_line_list;
    QTAILQ_HEAD(full_line_list, line) full_line_list;
    int tt_lines;
    int free_line_cnt;
    int victim_line_cnt;
    int full_line_cnt;
};

struct nand_cmd {
    int type;
    int cmd;
    int64_t stime; /* Coperd: request arrival time */
};

struct ssd {
    char *ssdname;
    struct ssdparams sp;
    struct ssd_channel *ch;
    struct ppa *maptbl; /* page level mapping table */
    uint64_t *rmap;     /* reverse mapptbl, assume it's stored in OOB */
    struct write_pointer* wp;
    struct line_mgmt* lm;
    uint8_t stream_number;
    uint8_t rg_number;
    void* femuctrl;
    /* lockless ring for communication with NVMe IO thread */
    struct rte_ring **to_ftl;
    struct rte_ring **to_poller;
    bool *dataplane_started_ptr;
    QemuThread msftl_thread;
    
    bool f2dp_pid_map[(F2DP_MAX_PID_NR+1)];
    struct sungjin_stat sungjin_stat;

};

void f2dpssd_init(FemuCtrl *n);


static inline NvmeLBAF *ms_ns_lbaf(NvmeNamespace *ns)
{
    NvmeIdNs *id_ns = &ns->id_ns;
    return &id_ns->lbaf[NVME_ID_NS_FLBAS_INDEX(id_ns->flbas)];
}

static inline uint8_t ms_ns_lbads(NvmeNamespace *ns)
{
    /* NvmeLBAF */
    return ms_ns_lbaf(ns)->lbads;
}

static inline size_t ms_l2b(NvmeNamespace *ns, uint64_t lba)
{
    return lba << ms_ns_lbads(ns);
}


////////////////////


// static inline bool nvme_ph_valid(struct ssd *ns, uint16_t ph)
// {
//     return ph < ns->stream_number;
// }

// static inline bool nvme_rg_valid(struct ssd *ns, uint16_t rg)
// {
//     return rg < (spp->luns_per_ch*spp->nchs)/spp->luns_per_rg;
// }

// static inline uint16_t nvme_pid2rg(struct ssd *ns, uint16_t pid)
// {
//     uint16_t rgif = ns->rgif;

//     if (!rgif) {
//         return 0;
//     }

//     return pid >> (16 - rgif);
// }


// static inline uint16_t nvme_pid2ph(struct ssd *ns, uint16_t pid)
// {
//     uint16_t rgif = ns->endgrp->fdp.rgif;

//     if (!rgif) {
//         return pid;
//     }

//     return pid & ((1 << (15 - rgif)) - 1);
// }

// static inline bool nvme_parse_pid(struct ssd *ns, uint16_t pid,
//                                   uint16_t *ph, uint16_t *rg)
// {
//     *rg = nvme_pid2rg(ns, pid);
//     *ph = nvme_pid2ph(ns, pid);

//     return nvme_ph_valid(ns, *ph) && nvme_rg_valid(ns->endgrp, *rg);
// }









// uint64_t msssd_trim2(FemuCtrl *n,uint64_t slba,uint64_t nlb);

#ifdef FEMU_DEBUG_FTL
#define ftl_debug(fmt, ...) \
    do { printf("[FEMU] FTL-Dbg: " fmt, ## __VA_ARGS__); } while (0)
#else
#define ftl_debug(fmt, ...) \
    do { } while (0)
#endif

#define ftl_err(fmt, ...) \
    do { fprintf(stderr, "[FEMU] FTL-Err: " fmt, ## __VA_ARGS__); } while (0)

#define ftl_log(fmt, ...) \
    do { printf("[FEMU] FTL-Log: " fmt, ## __VA_ARGS__); } while (0)


/* FEMU assert() */
#ifdef FEMU_DEBUG_FTL
#define ftl_assert(expression) assert(expression)
#else
#define ftl_assert(expression)
#endif

#endif
