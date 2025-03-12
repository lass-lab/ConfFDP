#include "fdpftl.h"
#include <time.h>
//#define FEMU_DEBUG_FTL

static void *msftl_thread(void *arg);

static inline bool should_gc(struct ssd *ssd,int rg_id)
{
    return (ssd->lm[rg_id].free_line_cnt <= ssd->sp.gc_thres_lines);
}


static inline bool should_gc_high(struct ssd *ssd,int rg_id)
{
    return (ssd->lm[rg_id].free_line_cnt <= ssd->sp.gc_thres_lines_high);
}

static inline int get_right_rg(struct ssd*ssd,int rg_id){
    for(;rg_id<ssd->rg_number;rg_id++){
        if(!should_gc_high(ssd,rg_id)){
            return rg_id;
        }
    }
    return -1;
}

static inline int get_left_rg(struct ssd*ssd,int rg_id){
    for(;rg_id>=0;rg_id--){
        if(!should_gc_high(ssd,rg_id)){
            return rg_id;
        }
    }
    return -1;
}

static int find_near_rg_id(struct ssd*ssd,int rg_id){
    int right_rg=get_right_rg(ssd,rg_id+1);
    int left_rg=get_left_rg(ssd,rg_id-1);
    // if(right_rg==-1)
    if(right_rg==-1&&left_rg==-1){
        return rg_id;
    }
    if(right_rg!=-1&&left_rg!=-1){
        return rg_id-left_rg > right_rg-rg_id ? right_rg :left_rg;
    }
    if(right_rg==-1&&left_rg!=-1){
        return left_rg;
    }
    return right_rg;
    // int ret=rg_id;
    // bool flip=false;

}


static inline struct ppa get_maptbl_ent(struct ssd *ssd, uint64_t lpn)
{
    return ssd->maptbl[lpn];
}

static inline void set_maptbl_ent(struct ssd *ssd, uint64_t lpn, struct ppa *ppa)
{
    ftl_assert(lpn < ssd->sp.tt_pgs);
    ssd->maptbl[lpn] = *ppa;
}

static uint64_t ppa2pgidx(struct ssd *ssd, struct ppa *ppa)
{
    struct ssdparams *spp = &ssd->sp;
    uint64_t pgidx;

    pgidx = ppa->g.ch  * spp->pgs_per_ch  + \
            ppa->g.lun * spp->pgs_per_lun + \
            ppa->g.pl  * spp->pgs_per_pl  + \
            ppa->g.blk * spp->pgs_per_blk + \
            ppa->g.pg;

    ftl_assert(pgidx < spp->tt_pgs);

    return pgidx;
}

static inline uint64_t get_rmap_ent(struct ssd *ssd, struct ppa *ppa)
{
    uint64_t pgidx = ppa2pgidx(ssd, ppa);

    return ssd->rmap[pgidx];
}

/* set rmap[page_no(ppa)] -> lpn */
static inline void set_rmap_ent(struct ssd *ssd, uint64_t lpn, struct ppa *ppa)
{
    uint64_t pgidx = ppa2pgidx(ssd, ppa);

    ssd->rmap[pgidx] = lpn;
}

static inline int victim_line_cmp_pri(pqueue_pri_t next, pqueue_pri_t curr)
{
    return (next > curr);
}

static inline pqueue_pri_t victim_line_get_pri(void *a)
{
    return ((struct line *)a)->vpc;
}

static inline void victim_line_set_pri(void *a, pqueue_pri_t pri)
{
    ((struct line *)a)->vpc = pri;
}

static inline size_t victim_line_get_pos(void *a)
{
    return ((struct line *)a)->pos;
}

static inline void victim_line_set_pos(void *a, size_t pos)
{
    ((struct line *)a)->pos = pos;
}

static void fdpssd_init_lines(struct ssd *ssd,bool is_init, int rg)
{
    struct ssdparams *spp = &ssd->sp;
    struct line_mgmt *lm = &ssd->lm[rg];
    struct line *line;

    lm->tt_lines = spp->blks_per_pl;
    ftl_assert(lm->tt_lines == spp->tt_lines);
    if(is_init)
        lm->lines = g_malloc0(sizeof(struct line) * lm->tt_lines);

    QTAILQ_INIT(&lm->free_line_list);
    lm->victim_line_pq = pqueue_init(spp->tt_lines, victim_line_cmp_pri,
            victim_line_get_pri, victim_line_set_pri,
            victim_line_get_pos, victim_line_set_pos);
    QTAILQ_INIT(&lm->full_line_list);

    lm->free_line_cnt = 0;
    for (int i = 0; i < lm->tt_lines; i++) {
        line = &lm->lines[i];
        line->id = i;
        line->ipc = 0;
        line->vpc = 0;
        line->pos = 0;
        if(!is_init){
            print_sungjin(line->stream_id);
        }
        line->stream_id=-1;
        line->rg_id=rg;
        /* initialize all the lines as free lines */
        QTAILQ_INSERT_TAIL(&lm->free_line_list, line, entry);
        lm->free_line_cnt++;
    }

    ftl_assert(lm->free_line_cnt == lm->tt_lines);
    lm->victim_line_cnt = 0;
    lm->full_line_cnt = 0;
}

static inline void rg2physical(struct ssdparams *spp , 
    int rg_id, int* start_ch, int* start_lun,int *end_ch, int* end_lun){
    if(spp->chnls_per_rg>1){ // big rg, include many channels
    

        *start_ch=rg_id*spp->chnls_per_rg;
        *start_lun=0;

        *end_ch=*start_ch +spp->chnls_per_rg-1;
        *end_lun=spp->luns_per_ch-1;
        return;
    }else if(spp->rgs_per_chnl>1){ // rgs in channel

        *start_ch=rg_id/spp->rgs_per_chnl;
        *start_lun=(rg_id%spp->rgs_per_chnl)*spp->luns_per_rg;
        
        *end_ch=rg_id/spp->rgs_per_chnl;
        *end_lun=(*start_lun ) + spp->luns_per_rg-1;
        return;
    }else if(spp->rgs_per_chnl==1&&spp->chnls_per_rg==1){
        // return ppa.g.ch;
        *start_ch=rg_id;
        *start_lun=0;
        *end_ch=rg_id;
        *end_lun=spp->luns_per_ch-1;
        // rg == channel
        return;
    }
        printf("rg2physical\n");
        print_sungjin(spp->rgs_per_chnl);
        print_sungjin(spp->chnls_per_rg);
        print_sungjin(rg_id);
        *start_ch=-1;
        *start_lun=-1;
        *end_ch=-1;
        *end_lun=-1;
        // return -1;
}

static void fdpssd_init_write_pointer(struct ssd *ssd,int stream_id,int rg_id)
{


    struct ssdparams *spp = &ssd->sp;
    struct write_pointer *wpp = &(ssd->wp[stream_id][rg_id]);


    struct line_mgmt *lm = &ssd->lm[rg_id];
    struct line *curline = NULL;

    curline = QTAILQ_FIRST(&lm->free_line_list);
    QTAILQ_REMOVE(&lm->free_line_list, curline, entry);
    lm->free_line_cnt--;
 print_sungjin(wpp->written);
    /* wpp->curline is always our next-to-write super-block */
    wpp->curline = curline;
    curline->stream_id=stream_id;
    // int rgs_per_chnl = ssd->rg_number/spp->nchs; // 4
    // fdp
    /*
        luns_per_rg = 2
        0~32
        3 -> 0 , 3
        8*c + l

        0~3 -> ch 0
        ---- 0 -> lun 0,1
        ---- 1 -> lun 2,3
        ---- 2 -> lun 4,5
        ---- 3 -> lun 6,7
        4~7 -> ch 1
        .
        .
        .
        27~31 -> ch 7
    */
   int tmp1,tmp2;
    rg2physical(spp,rg_id,&wpp->ch,&wpp->lun,&tmp1,&tmp2);
    // wpp->ch = rg/spp->rgs_per_chnl;
    // wpp->lun = (rg%spp->rgs_per_chnl)*spp->luns_per_rg;

    wpp->pg = 0;
    wpp->blk = curline->id;
    wpp->pl = 0;
    wpp->written=0;
    // printf("")
   
    // print_sungjin(fdpssd_init_write_pointer);
}

static inline void check_addr(int a, int max)
{
    ftl_assert(a >= 0 && a < max);
}

static struct line *get_next_free_line(struct ssd *ssd, int rg_id)
{
    struct line_mgmt *lm = &ssd->lm[rg_id];
    struct line *curline = NULL;

    curline = QTAILQ_FIRST(&lm->free_line_list);
    if (!curline) {
        ftl_err("No free lines left in [%s] !!!!\n", ssd->ssdname);
        return NULL;
    }

    QTAILQ_REMOVE(&lm->free_line_list, curline, entry);
    lm->free_line_cnt--;
    return curline;
}

static void ssd_advance_write_pointer(struct ssd *ssd,int stream_id,int rg_id)
{
    struct ssdparams *spp = &ssd->sp;
    struct write_pointer *wpp = &ssd->wp[stream_id][rg_id];
    struct line_mgmt *lm = &ssd->lm[rg_id];

    // int ch_id;
    // int lun_id;
    // print_sungjin(ssd_advance_write_pointer);
    // print_sungjin(rg_id);
    // print_sungjin(stream_id);
    int start_ch_id,start_lun_id,end_ch_id,end_lun_id;
    rg2physical(spp,rg_id,&start_ch_id,&start_lun_id,&end_ch_id,&end_lun_id);

    // int end_ch_id=start_ch_id+spp->chnls_per_rg;
    // int end_lun_id=start_lun_id+(spp->luns_per_rg%spp->luns_per_ch);

    wpp->ch++;
    if(wpp->ch > end_ch_id){ // sungjin
        wpp->ch=start_ch_id;
        wpp->lun++;
        if(wpp->lun>end_lun_id){
            wpp->lun=start_lun_id;
            wpp->pg++;
            if (wpp->pg == spp->pgs_per_blk) {
                wpp->pg = 0;


                if (wpp->curline->vpc == spp->pgs_per_line) {
                    /* all pgs are still valid, move to full line list */
                    ftl_assert(wpp->curline->ipc == 0);
                    QTAILQ_INSERT_TAIL(&lm->full_line_list, wpp->curline, entry);
                    lm->full_line_cnt++;
                } else {
                    ftl_assert(wpp->curline->vpc >= 0 && wpp->curline->vpc < spp->pgs_per_line);
                    /* there must be some invalid pages in this line */
                    ftl_assert(wpp->curline->ipc > 0);
                    pqueue_insert(lm->victim_line_pq, wpp->curline);
                    lm->victim_line_cnt++;
                }
                wpp->curline = NULL;
                wpp->curline = get_next_free_line(ssd,rg_id);
                wpp->curline->stream_id=stream_id;
                wpp->blk = wpp->curline->id;


            }
        }
    }


    //////////////////////////////
    // check_addr(wpp->ch, spp->nchs);
    // wpp->ch++;
    // if (wpp->ch == spp->nchs) {
    //     wpp->ch = 0;
    //     check_addr(wpp->lun, spp->luns_per_ch);
    //     wpp->lun++;
    //     /* in this case, we should go to next lun */
    //     if (wpp->lun == spp->luns_per_ch) {
    //         wpp->lun = 0;
    //         /* go to next page in the block */
    //         check_addr(wpp->pg, spp->pgs_per_blk);
    //         wpp->pg++;
    //         if (wpp->pg == spp->pgs_per_blk) {
    //             wpp->pg = 0;
    //             /* move current line to {victim,full} line list */
    //             if (wpp->curline->vpc == spp->pgs_per_line) {
    //                 /* all pgs are still valid, move to full line list */
    //                 ftl_assert(wpp->curline->ipc == 0);
    //                 QTAILQ_INSERT_TAIL(&lm->full_line_list, wpp->curline, entry);
    //                 lm->full_line_cnt++;
    //             } else {
    //                 ftl_assert(wpp->curline->vpc >= 0 && wpp->curline->vpc < spp->pgs_per_line);
    //                 /* there must be some invalid pages in this line */
    //                 ftl_assert(wpp->curline->ipc > 0);
    //                 pqueue_insert(lm->victim_line_pq, wpp->curline);
    //                 lm->victim_line_cnt++;
    //             }
    //             /* current line is used up, pick another empty line */
    //             check_addr(wpp->blk, spp->blks_per_pl);
    //             wpp->curline = NULL;
    //             wpp->curline = get_next_free_line(ssd);
    //             wpp->curline->stream_id=stream_id;
    //             if (!wpp->curline) {
    //                 /* TODO */
    //                 while(true){

    //                             printf("sungjin error here? abort\n");
    //                              wpp->curline = get_next_free_line(ssd);
    //                              if(wpp->curline){
    //                                 break;
    //                              }
    //                 }
                    
    //                 // abort();
    //                 // do
    //                 // {
    //                 //     /* code */
    //                 // } while (/* condition */);
                    
                   
    //             }
    //             wpp->blk = wpp->curline->id;
    //             check_addr(wpp->blk, spp->blks_per_pl);
    //             /* make sure we are starting from page 0 in the super block */
    //             ftl_assert(wpp->pg == 0);
    //             ftl_assert(wpp->lun == 0);
    //             ftl_assert(wpp->ch == 0);
    //             /* TODO: assume # of pl_per_lun is 1, fix later */
    //             ftl_assert(wpp->pl == 0);
    //         }
    //     }
    // }
}

static struct ppa get_new_page(struct ssd *ssd,int stream_id,int rg_id)
{
    struct write_pointer *wpp = &ssd->wp[stream_id][rg_id];
    struct ppa ppa;
    ppa.ppa = 0;
    ppa.g.ch = wpp->ch;
    ppa.g.lun = wpp->lun;
    ppa.g.pg = wpp->pg;
    ppa.g.blk = wpp->blk;
    ppa.g.pl = wpp->pl;
    ftl_assert(ppa.g.pl == 0);

    return ppa;
}

static void check_params(struct ssdparams *spp)
{
    /*
     * we are using a general write pointer increment method now, no need to
     * force luns_per_ch and nchs to be power of 2
     */

    //ftl_assert(is_power_of_2(spp->luns_per_ch));
    //ftl_assert(is_power_of_2(spp->nchs));
}

// static void ssd_init_params(struct ssdparams *spp, FemuCtrl *n)
// {
    

    // print_sungjin(n->memsz);

//     spp->secsz = n->bb_params.secsz; // 512
//     spp->secs_per_pg = n->bb_params.secs_per_pg; // 8
//     spp->pgs_per_blk = n->bb_params.pgs_per_blk; //256
//     spp->blks_per_pl = n->bb_params.blks_per_pl; /* 256 16GB */
//     spp->pls_per_lun = n->bb_params.pls_per_lun; // 1
//     spp->luns_per_ch = n->bb_params.luns_per_ch; // 8
//     spp->nchs = n->bb_params.nchs; // 8

//     spp->pg_rd_lat = n->bb_params.pg_rd_lat;
//     spp->pg_wr_lat = n->bb_params.pg_wr_lat;
//     spp->blk_er_lat = n->bb_params.blk_er_lat;
//     spp->ch_xfer_lat = n->bb_params.ch_xfer_lat;

//     /* calculated values */
//     spp->secs_per_blk = spp->secs_per_pg * spp->pgs_per_blk;
//     spp->secs_per_pl = spp->secs_per_blk * spp->blks_per_pl;
//     spp->secs_per_lun = spp->secs_per_pl * spp->pls_per_lun;
//     spp->secs_per_ch = spp->secs_per_lun * spp->luns_per_ch;
//     spp->tt_secs = spp->secs_per_ch * spp->nchs;

//     spp->pgs_per_pl = spp->pgs_per_blk * spp->blks_per_pl;
//     spp->pgs_per_lun = spp->pgs_per_pl * spp->pls_per_lun;
//     spp->pgs_per_ch = spp->pgs_per_lun * spp->luns_per_ch;
//     spp->tt_pgs = spp->pgs_per_ch * spp->nchs;

//     spp->blks_per_lun = spp->blks_per_pl * spp->pls_per_lun; // 256 *1
//     spp->blks_per_ch = spp->blks_per_lun * spp->luns_per_ch; // 256 * 8
//     spp->tt_blks = spp->blks_per_ch * spp->nchs; // 256 * 8 * 8

//     spp->pls_per_ch =  spp->pls_per_lun * spp->luns_per_ch; // 1 * 8
//     spp->tt_pls = spp->pls_per_ch * spp->nchs; // 8*8

//     spp->tt_luns = spp->luns_per_ch * spp->nchs;//8*8

//     /* line is special, put it at the end */
//     spp->blks_per_line = spp->tt_luns; /* TODO: to fix under multiplanes */
//     spp->pgs_per_line = spp->blks_per_line * spp->pgs_per_blk;
//     spp->secs_per_line = spp->pgs_per_line * spp->secs_per_pg;
//     spp->tt_lines = spp->blks_per_lun; /* TODO: to fix under multiplanes */ // 256

//     spp->gc_thres_pcent = n->bb_params.gc_thres_pcent/100.0;
//     spp->gc_thres_lines = (int)((1 - spp->gc_thres_pcent) * spp->tt_lines);
//     spp->gc_thres_pcent_high = n->bb_params.gc_thres_pcent_high/100.0;
//     spp->gc_thres_lines_high = (int)((1 - spp->gc_thres_pcent_high) * spp->tt_lines);
//     spp->enable_gc_delay = true;


//     check_params(spp);
// }

static void ssd_init_params(struct ssdparams *spp, FemuCtrl *n)
{
    

    print_sungjin(n->memsz);
    
    uint64_t nand_block_size=(n->bb_params.nand_block_size_mb<<20);
    uint64_t nand_page_size=(n->bb_params.nand_page_size_kb<<10);

    uint64_t lun_size_mb;
    spp->gc_thres_pcent = n->bb_params.gc_thres_pcent/100.0;
    spp->gc_thres_pcent_high = n->bb_params.gc_thres_pcent_high/100.0;
    uint64_t user_space_ratio=100*(spp->gc_thres_pcent); // 25
    printf("gc_thres_pcent %lf\n",spp->gc_thres_pcent);
    // print_sungjin(user_space_ratio);

    uint64_t user_device_size_mb = n->memsz;
    // print_sungjin(user_device_size_mb);
    uint64_t total_device_size_mb= (user_device_size_mb*100)/user_space_ratio;
    // print_sungjin(total_device_size_mb);
    // spp->secsz = n->bb_params.secsz; // 4096
    spp->secsz = 4096; // 4096
    spp->secs_per_pg = nand_page_size/spp->secsz; // 8
    spp->pgs_per_blk = nand_block_size/nand_page_size;

    // nand_block_size_mb= (spp->secsz*spp->secs_per_pg*spp->pgs_per_blk)>>20;
    
    
    // print_sungjin(nand_page_size);
    // print_sungjin(nand_block_size);
    // print_sungjin(spp->pgs_per_blk);
    
    // spp->blks_per_pl = n->bb_params.blks_per_pl; /* 256 16GB */


    spp->pls_per_lun = n->bb_params.pls_per_lun; // 1
    spp->luns_per_ch = n->bb_params.luns_per_ch; // 8

    // if()
    

    spp->nchs = n->bb_params.nchs; // 8

    lun_size_mb=total_device_size_mb/(spp->nchs*spp->luns_per_ch);
    
    spp->blks_per_pl = lun_size_mb/(n->bb_params.nand_block_size_mb); /* 256 16GB */

    // print_sungjin(lun_size_mb);
    // print_sungjin(spp->blks_per_pl);


    spp->pg_rd_lat = n->bb_params.pg_rd_lat;
    spp->pg_wr_lat = n->bb_params.pg_wr_lat;
    spp->blk_er_lat = n->bb_params.blk_er_lat;
    spp->ch_xfer_lat = n->bb_params.ch_xfer_lat;

    /* calculated values */
    spp->secs_per_blk = spp->secs_per_pg * spp->pgs_per_blk;
    spp->secs_per_pl = spp->secs_per_blk * spp->blks_per_pl;
    spp->secs_per_lun = spp->secs_per_pl * spp->pls_per_lun;
    spp->secs_per_ch = spp->secs_per_lun * spp->luns_per_ch;
    spp->tt_secs = spp->secs_per_ch * spp->nchs;

    spp->pgs_per_pl = spp->pgs_per_blk * spp->blks_per_pl;
    spp->pgs_per_lun = spp->pgs_per_pl * spp->pls_per_lun;
    spp->pgs_per_ch = spp->pgs_per_lun * spp->luns_per_ch;
    spp->tt_pgs = spp->pgs_per_ch * spp->nchs;

    spp->blks_per_lun = spp->blks_per_pl * spp->pls_per_lun; // 256 *1
    spp->blks_per_ch = spp->blks_per_lun * spp->luns_per_ch; // 256 * 8
    spp->tt_blks = spp->blks_per_ch * spp->nchs; // 256 * 8 * 8

    spp->pls_per_ch =  spp->pls_per_lun * spp->luns_per_ch; // 1 * 8
    spp->tt_pls = spp->pls_per_ch * spp->nchs; // 8*8

    spp->tt_luns = spp->luns_per_ch * spp->nchs;//8*8

    /* line is special, put it at the end */
    spp->blks_per_line = spp->tt_luns/n->rg_number; /* TODO: to fix under multiplanes */
    spp->pgs_per_line = spp->blks_per_line * spp->pgs_per_blk;
    spp->secs_per_line = spp->pgs_per_line * spp->secs_per_pg;
    spp->tt_lines = spp->blks_per_lun; /* TODO: to fix under multiplanes */ // 256

    
    spp->gc_thres_lines = (int)((1 - spp->gc_thres_pcent) * spp->tt_lines);
    
    spp->gc_thres_lines_high = (int)((1 - spp->gc_thres_pcent_high) * spp->tt_lines);
    spp->enable_gc_delay = true;

    // fdp
    spp->rgs_per_chnl = n->rg_number/spp->nchs;
    spp->luns_per_rg = (spp->luns_per_ch*spp->nchs)/n->rg_number;
    spp->chnls_per_rg = spp->luns_per_rg/spp->luns_per_ch;
    
    check_params(spp);
}

static void ssd_init_nand_page(struct nand_page *pg, struct ssdparams *spp,bool is_init)
{
    pg->nsecs = spp->secs_per_pg;
    if(is_init){
        pg->sec = g_malloc0(sizeof(nand_sec_status_t) * pg->nsecs);
    }
    for (int i = 0; i < pg->nsecs; i++) {
        pg->sec[i] = SEC_FREE;
    }
    pg->status = PG_FREE;
}

static void ssd_init_nand_blk(struct nand_block *blk, struct ssdparams *spp,bool is_init)
{
    blk->npgs = spp->pgs_per_blk;
    if(is_init){
        blk->pg = g_malloc0(sizeof(struct nand_page) * blk->npgs);
    }
    
    for (int i = 0; i < blk->npgs; i++) {
        ssd_init_nand_page(&blk->pg[i], spp,is_init);
    }
    blk->ipc = 0;
    blk->vpc = 0;
    blk->erase_cnt = 0;
    blk->wp = 0;
}

static void ssd_init_nand_plane(struct nand_plane *pl, struct ssdparams *spp,bool is_init)
{
    pl->nblks = spp->blks_per_pl;
    if(is_init){
        pl->blk = g_malloc0(sizeof(struct nand_block) * pl->nblks);
    }
    
    for (int i = 0; i < pl->nblks; i++) {
        ssd_init_nand_blk(&pl->blk[i], spp,is_init);
    }
}

static void ssd_init_nand_lun(struct nand_lun *lun, struct ssdparams *spp,bool is_init)
{
    lun->npls = spp->pls_per_lun;
    if(is_init){
        lun->pl = g_malloc0(sizeof(struct nand_plane) * lun->npls);
    }
    
    for (int i = 0; i < lun->npls; i++) {
        ssd_init_nand_plane(&lun->pl[i], spp,is_init);
    }
    lun->next_lun_avail_time = 0;
    lun->busy = false;
}

static void ssd_init_ch(struct ssd_channel *ch, struct ssdparams *spp,bool is_init)
{
    ch->nluns = spp->luns_per_ch;
    if(is_init){
        ch->lun = g_malloc0(sizeof(struct nand_lun) * ch->nluns);
    }
    
    for (int i = 0; i < ch->nluns; i++) {
        ssd_init_nand_lun(&ch->lun[i], spp,is_init);
    }
    ch->next_ch_avail_time = 0;
    ch->busy = 0;
}

static void ssd_init_maptbl(struct ssd *ssd,bool is_init)
{
    struct ssdparams *spp = &ssd->sp;
    if(is_init ){
         ssd->maptbl = g_malloc0(sizeof(struct ppa) * spp->tt_pgs);
    }
   
    for (int i = 0; i < spp->tt_pgs; i++) {
        ssd->maptbl[i].ppa = UNMAPPED_PPA;
    }
}

static void ssd_init_rmap(struct ssd *ssd,bool is_init)
{
    struct ssdparams *spp = &ssd->sp;
    if(is_init)
        ssd->rmap = g_malloc0(sizeof(uint64_t) * spp->tt_pgs);
    for (int i = 0; i < spp->tt_pgs; i++) {
        ssd->rmap[i] = INVALID_LPN;
    }
}

void fdpssd_init(FemuCtrl *n)
{
    struct ssd *ssd = n->ssd;
    struct ssdparams *spp = &ssd->sp;
    int i,j;

    ftl_assert(ssd);

    ssd->rg_number=n->rg_number;
    ssd->handle_number = n->handle_number;
    ssd_init_params(spp, n);
    ssd->debug=false;
    
    // if(spp->luns_per_rg>spp->nchs*spp->luns_per_ch){
    //     spp->luns_per_rg=spp->nchs*spp->luns_per_ch;
    // }
    // ssd->rg_number= (spp->nchs*spp->luns_per_ch)/spp->luns_per_rg;


    /* initialize ssd internal layout architecture */
    ssd->ch = g_malloc0(sizeof(struct ssd_channel) * spp->nchs);

    ssd->lm=g_malloc0(sizeof(struct line_mgmt)*ssd->rg_number);
    ssd->wp=g_malloc(sizeof(struct write_pointer*)*ssd->handle_number);
    for(i=0;i<ssd->handle_number;i++){
        ssd->wp[i]=g_malloc(sizeof(struct write_pointer)*n->rg_number);
    }
    
    for (i = 0; i < spp->nchs; i++) {
        ssd_init_ch(&ssd->ch[i], spp,true);
    }

    /* initialize maptbl */
    ssd_init_maptbl(ssd,true);

    /* initialize rmap */
    ssd_init_rmap(ssd,true);

    /* initialize all the lines */
    // ssd->lm=g_malloc0
    for(i=0;i<ssd->rg_number;i++){
        // printf("\nRG %u-----------------------------\n",i);
        fdpssd_init_lines(ssd,true,i);
        // printf("------------------------------------\n");
    }
    /* initialize write pointer, this is how we allocate new pages for writes */

    for(i=0;i<ssd->handle_number;i++){
        for(j=0;j<ssd->rg_number;j++){
            fdpssd_init_write_pointer(ssd,i,j);
        }
    }
    

    qemu_thread_create(&ssd->msftl_thread, "FEMU-MSFTL-Thread", msftl_thread, n,
                       QEMU_THREAD_JOINABLE);
    //  print_sungjin(msssd_init);
}

static inline void print_ppa(struct ssd *ssd, struct ppa *ppa){
    struct ssdparams *spp = &ssd->sp;
    int ch = ppa->g.ch;
    int lun = ppa->g.lun;
    int pl = ppa->g.pl;
    int blk = ppa->g.blk;
    int pg = ppa->g.pg;
    int sec = ppa->g.sec;
    printf("ch %d lun %d pl %d blk %d pg %d sec %d ppa %lu\n",
        ch,lun,pl,blk,pg,sec,ppa->ppa);
    printf("nchs %d luns_per_ch %d pls_per_lun %d blks_per_pl %d pgs_per_blk %d secs_per_pg %d\n",
        spp->nchs,spp->luns_per_ch,spp->pls_per_lun,spp->blks_per_pl,
        spp->pgs_per_blk,spp->secs_per_pg);
}

static inline bool valid_ppa(struct ssd *ssd, struct ppa *ppa)
{
    struct ssdparams *spp = &ssd->sp;
    int ch = ppa->g.ch;
    int lun = ppa->g.lun;
    int pl = ppa->g.pl;
    int blk = ppa->g.blk;
    int pg = ppa->g.pg;
    int sec = ppa->g.sec;

    if (ch >= 0 && ch < spp->nchs && lun >= 0 && lun < spp->luns_per_ch && pl >=
        0 && pl < spp->pls_per_lun && blk >= 0 && blk < spp->blks_per_pl && pg
        >= 0 && pg < spp->pgs_per_blk && sec >= 0 && sec < spp->secs_per_pg)
        return true;

    return false;
}






static inline int get_rg_id(struct ssd* ssd,struct ppa* ppa){

    if(ssd->sp.chnls_per_rg>1){ // big rg, include many channels
        return ppa->g.ch/ssd->sp.chnls_per_rg;
    }else if(ssd->sp.rgs_per_chnl>1){ // rgs in channel
        return (ppa->g.ch*ssd->sp.rgs_per_chnl)+
        (ppa->g.lun/ssd->sp.luns_per_rg);
    }else if(ssd->sp.rgs_per_chnl==1&&ssd->sp.chnls_per_rg==1){
        return ppa->g.ch;
        // rg == channel
    }
    // else
    // {

        printf("get_rg_id\n");
        print_sungjin(ssd->sp.rgs_per_chnl);
        print_sungjin(ssd->sp.chnls_per_rg);
        return -1;
    // }


}

static inline bool valid_lpn(struct ssd *ssd, uint64_t lpn)
{
    return (lpn < ssd->sp.tt_pgs);
}

static inline bool mapped_ppa(struct ppa *ppa)
{
    return !(ppa->ppa == UNMAPPED_PPA);
}

static inline struct ssd_channel *get_ch(struct ssd *ssd, struct ppa *ppa)
{
    return &(ssd->ch[ppa->g.ch]);
}

static inline struct nand_lun *get_lun(struct ssd *ssd, struct ppa *ppa)
{
    struct ssd_channel *ch = get_ch(ssd, ppa);
    return &(ch->lun[ppa->g.lun]);
}

static inline struct nand_plane *get_pl(struct ssd *ssd, struct ppa *ppa)
{
    struct nand_lun *lun = get_lun(ssd, ppa);
    return &(lun->pl[ppa->g.pl]);
}

static inline struct nand_block *get_blk(struct ssd *ssd, struct ppa *ppa)
{
    struct nand_plane *pl = get_pl(ssd, ppa);
    return &(pl->blk[ppa->g.blk]);
}



static inline struct line *get_line(struct ssd *ssd, struct ppa *ppa)
{
    int rg_id=(get_rg_id(ssd,ppa));

    return &(ssd->lm[rg_id].lines[ppa->g.blk]);
}

static inline struct nand_page *get_pg(struct ssd *ssd, struct ppa *ppa)
{
    struct nand_block *blk = get_blk(ssd, ppa);
    return &(blk->pg[ppa->g.pg]);
}






static uint64_t ssd_advance_status(struct ssd *ssd, struct ppa *ppa, struct
        nand_cmd *ncmd)
{
    int c = ncmd->cmd;
    uint64_t cmd_stime = (ncmd->stime == 0) ? \
        qemu_clock_get_ns(QEMU_CLOCK_REALTIME) : ncmd->stime;

    struct ssdparams *spp = &ssd->sp;
    struct nand_lun *lun = get_lun(ssd, ppa);
    // ssd_channel *get_ch
    struct ssd_channel* ch = get_ch(ssd,ppa);
    uint64_t lat = 0;

    uint64_t nand_stime;
    uint64_t chnl_stime =0;
    // uint64_t cmd_stime=
    //     (req->stime == 0) ? qemu_clock_get_ns(QEMU_CLOCK_REALTIME) 
            // : req->stime ;
    switch (c) {
    case NAND_READ:


////////////
        nand_stime = (lun->next_lun_avail_time < cmd_stime) ? cmd_stime : \
                     lun->next_lun_avail_time;
        lun->next_lun_avail_time = nand_stime + spp->pg_rd_lat;
        // lat = lun->next_lun_avail_time - cmd_stime;
        chnl_stime = (ch->next_ch_avail_time < lun->next_lun_avail_time) ?
                        lun->next_lun_avail_time : ch->next_ch_avail_time;
        ch->next_ch_avail_time = chnl_stime + spp->ch_xfer_lat;
        lat = ch->next_ch_avail_time - cmd_stime;
///////////
#if 0
        /* read: perform NAND cmd first */
        nand_stime = (lun->next_lun_avail_time < cmd_stime) ? cmd_stime : \
                     lun->next_lun_avail_time;
        lun->next_lun_avail_time = nand_stime + spp->pg_rd_lat;
        lat = lun->next_lun_avail_time - cmd_stime;
// #if 0
        lun->next_lun_avail_time = nand_stime + spp->pg_rd_lat;

        /* read: then data transfer through channel */
        chnl_stime = (ch->next_ch_avail_time < lun->next_lun_avail_time) ? \
            lun->next_lun_avail_time : ch->next_ch_avail_time;
        ch->next_ch_avail_time = chnl_stime + spp->ch_xfer_lat;

        lat = ch->next_ch_avail_time - cmd_stime;
#endif
        break;

    case NAND_WRITE:

        chnl_stime= ch->next_ch_avail_time < cmd_stime ?  cmd_stime : ch->next_ch_avail_time;
        ch->next_ch_avail_time = chnl_stime + spp->ch_xfer_lat;

        nand_stime = lun->next_lun_avail_time < ch->next_ch_avail_time ? 
                ch->next_ch_avail_time : lun->next_lun_avail_time;
        lun->next_lun_avail_time = nand_stime + spp->pg_wr_lat;

        lat = lun->next_lun_avail_time-cmd_stime;
    //////////////////// 
        // nand_stime = (lun->next_lun_avail_time < cmd_stime) ? cmd_stime : 
        //              lun->next_lun_avail_time;
        // lun->next_lun_avail_time = nand_stime + spp->pg_wr_lat;
        // // lat = lun->next_lun_avail_time - cmd_stime;
        // chnl_stime = (ch->next_ch_avail_time < lun->next_lun_avail_time) ?
        //                 lun->next_lun_avail_time : ch->next_ch_avail_time;
        // ch->next_ch_avail_time = chnl_stime + spp->ch_xfer_lat;
        // lat = ch->next_ch_avail_time - cmd_stime;
#if 0
        /* write: transfer data through channel first */
        nand_stime = (lun->next_lun_avail_time < cmd_stime) ? cmd_stime : \
                     lun->next_lun_avail_time;
        if (ncmd->type == USER_IO) {
            lun->next_lun_avail_time = nand_stime + spp->pg_wr_lat;
        } else {
            lun->next_lun_avail_time = nand_stime + spp->pg_wr_lat;
        }
        lat = lun->next_lun_avail_time - cmd_stime;

// #if 0
        chnl_stime = (ch->next_ch_avail_time < cmd_stime) ? cmd_stime : \
                     ch->next_ch_avail_time;
        ch->next_ch_avail_time = chnl_stime + spp->ch_xfer_lat;

        /* write: then do NAND program */
        nand_stime = (lun->next_lun_avail_time < ch->next_ch_avail_time) ? \
            ch->next_ch_avail_time : lun->next_lun_avail_time;
        lun->next_lun_avail_time = nand_stime + spp->pg_wr_lat;

        lat = lun->next_lun_avail_time - cmd_stime;
#endif
        break;

    case NAND_ERASE:
        /* erase: only need to advance NAND status */
        nand_stime = (lun->next_lun_avail_time < cmd_stime) ? cmd_stime : \
                     lun->next_lun_avail_time;
        lun->next_lun_avail_time = nand_stime + spp->blk_er_lat;

        lat = lun->next_lun_avail_time - cmd_stime;
        break;

    default:
        ftl_err("Unsupported NAND command: 0x%x\n", c);
    }

    return lat;
}

/* update SSD status about one page from PG_VALID -> PG_VALID */
static int mark_page_invalid(struct ssd *ssd, struct ppa *ppa)
{
    // struct line_mgmt *lm = &ssd->lm;
    struct line_mgmt *lm;
    struct ssdparams *spp = &ssd->sp;
    struct nand_block *blk = NULL;
    struct nand_page *pg = NULL;
    bool was_full_line = false;
    struct line *line;
    int rg_id;
    /* update corresponding page status */
    pg = get_pg(ssd, ppa);
    ftl_assert(pg->status == PG_VALID);
    if(pg->status==PG_INVALID){
        printf("mark_page_invalid to invalid?? lpn %lu\n",
        get_rmap_ent(ssd, ppa));
        // dump_stack();
        return 1;
    }
    pg->status = PG_INVALID;
    rg_id=(get_rg_id(ssd,ppa));
    lm = &ssd->lm[rg_id];

    /* update corresponding block status */
    blk = get_blk(ssd, ppa);
    ftl_assert(blk->ipc >= 0 && blk->ipc < spp->pgs_per_blk);
    blk->ipc++;
    ftl_assert(blk->vpc > 0 && blk->vpc <= spp->pgs_per_blk);
    blk->vpc--;

    /* update corresponding line status */
    line = get_line(ssd, ppa);
    ftl_assert(line->ipc >= 0 && line->ipc < spp->pgs_per_line);
    if (line->vpc == spp->pgs_per_line) {
        // ftl_assert(line->ipc == 0);
        was_full_line = true;
    }
    line->ipc++;
    ftl_assert(line->vpc > 0 && line->vpc <= spp->pgs_per_line);


    
    /* Adjust the position of the victime line in the pq under over-writes */
    if (line->pos) {
        /* Note that line->vpc will be updated by this call */
        pqueue_change_priority(lm->victim_line_pq, line->vpc - 1, line);
    } else {
        line->vpc--;
    }

    if (was_full_line) {
        /* move line: "full" -> "victim" */
        QTAILQ_REMOVE(&lm->full_line_list, line, entry);
        lm->full_line_cnt--;
        pqueue_insert(lm->victim_line_pq, line);
        lm->victim_line_cnt++;
    }
    return 0;
}

static void mark_page_valid(struct ssd *ssd, struct ppa *ppa)
{
    struct nand_block *blk = NULL;
    struct nand_page *pg = NULL;
    struct line *line;

    /* update page status */
    pg = get_pg(ssd, ppa);
    ftl_assert(pg->status == PG_FREE);
    // if(pg->status==PG_VALID){
    //     return;
    // }
    pg->status = PG_VALID;

    /* update corresponding block status */
    blk = get_blk(ssd, ppa);
    ftl_assert(blk->vpc >= 0 && blk->vpc < ssd->sp.pgs_per_blk);
    blk->vpc++;

    /* update corresponding line status */
    line = get_line(ssd, ppa);
    ftl_assert(line->vpc >= 0 && line->vpc < ssd->sp.pgs_per_line);
    line->vpc++;
}

// static void mark_block_free(struct ssd *ssd, struct ppa *ppa)
// {
//     struct ssdparams *spp = &ssd->sp;
//     struct nand_block *blk = get_blk(ssd, ppa);
//     struct nand_page *pg = NULL;

//     for (int i = 0; i < spp->pgs_per_blk; i++) {
//         /* reset page status */
//         pg = &blk->pg[i];
//         ftl_assert(pg->nsecs == spp->secs_per_pg);
//         pg->status = PG_FREE;
//     }

//     /* reset block status */
//     ftl_assert(blk->npgs == spp->pgs_per_blk);
//     blk->ipc = 0;
//     blk->vpc = 0;
//     blk->erase_cnt++;
// }

static void gc_read_page(struct ssd *ssd, struct ppa *ppa)
{
    /* advance ssd status, we don't care about how long it takes */
    // print_sungjin(gc_read_page);
    // if (ssd->sp.enable_gc_delay) {
        struct nand_cmd gcr;
        gcr.type = GC_IO;
        gcr.cmd = NAND_READ;
        gcr.stime = 0;
        ssd_advance_status(ssd, ppa, &gcr);
    // }
}

/* move valid page data (already in DRAM) from victim line to a new page */
static uint64_t gc_write_page(struct ssd *ssd, struct ppa *old_ppa,int stream_id,int rg_id,bool to_other_rg)
{
    // int gc_i=0;
    // int rg_id;
    struct ppa new_ppa;
    struct nand_lun *new_lun;
    uint64_t lpn = get_rmap_ent(ssd, old_ppa);
    // print_sungjin(gc_write_page);
    // print_sungjin(stream_id);
    ftl_assert(valid_lpn(ssd, lpn));
    int new_rg_id=rg_id;
    if(to_other_rg){
        new_rg_id=find_near_rg_id(ssd,rg_id);
    }
    new_ppa = get_new_page(ssd,stream_id,new_rg_id);
    // print_sungjin(gc_i++);
    /* update maptbl */
    // rg_id=get_rg_id(ssd,&new_ppa);
    set_maptbl_ent(ssd, lpn, &new_ppa);
    /* update rmap */
    //  print_sungjin(gc_i++);
    set_rmap_ent(ssd, lpn, &new_ppa);
    //  print_sungjin(gc_i++);
    mark_page_valid(ssd, &new_ppa);
    //  print_sungjin(gc_i++);
    mark_page_invalid(ssd,old_ppa);
    //  print_sungjin(gc_i++);
    /* need to advance the write pointer here */
    ssd_advance_write_pointer(ssd,stream_id,new_rg_id);
    //  print_sungjin(gc_i++);
    // if (ssd->sp.enable_gc_delay) {
        struct nand_cmd gcw;
        gcw.type = GC_IO;
        gcw.cmd = NAND_WRITE;
        gcw.stime = 0;
        ssd_advance_status(ssd, &new_ppa, &gcw);
    // }
    //  print_sungjin(gc_i++);
    /* advance per-ch gc_endtime as well */
#if 0
    new_ch = get_ch(ssd, &new_ppa);
    new_ch->gc_endtime = new_ch->next_ch_avail_time;
#endif

    new_lun = get_lun(ssd, &new_ppa);
    //  print_sungjin(gc_i++);
    new_lun->gc_endtime = new_lun->next_lun_avail_time;
    //  print_sungjin(gc_i++);
    return 0;
}

static struct line *select_victim_line(struct ssd *ssd, bool force,int rg_id)
{
    struct line_mgmt *lm = &ssd->lm[rg_id];
    struct line *victim_line = NULL;

    victim_line = pqueue_peek(lm->victim_line_pq);
    if (!victim_line) {
        // printf("select_victim_line nulptr,eturn\n");
        return NULL;
    }

    if (!force && victim_line->ipc < ssd->sp.pgs_per_line / 8) {
        // printf("!force select_victim_line nulptr,eturn\n");
        return NULL;
    }
    // if(victim_line->ipc < ssd->sp.pgs_per_line / 16)

    pqueue_pop(lm->victim_line_pq);
    victim_line->pos = 0;
    lm->victim_line_cnt--;
    printf("lm->victim_line_cnt %d\n",lm->victim_line_cnt);
    /* victim_line is a danggling node now */
    return victim_line;
}

/* here ppa identifies the block we want to clean */
// static void clean_one_block(struct ssd *ssd, struct ppa *ppa,int stream_id)
// {
//     struct ssdparams *spp = &ssd->sp;
//     struct nand_page *pg_iter = NULL;
//     int cnt = 0;

//     for (int pg = 0; pg < spp->pgs_per_blk; pg++) {
//         ppa->g.pg = pg;
//         pg_iter = get_pg(ssd, ppa);
//         /* there shouldn't be any free page in victim blocks */
//         ftl_assert(pg_iter->status != PG_FREE);
//         if (pg_iter->status == PG_VALID) {
//             gc_read_page(ssd, ppa);
//             /* delay the maptbl update until "write" happens */
//             gc_write_page(ssd, ppa,stream_id);
//             cnt++;
//         }
//     }

//     ftl_assert(get_blk(ssd, ppa)->vpc == cnt);
// }

static void mark_line_free(struct ssd *ssd, struct ppa *ppa,int rg_id)
{
    struct line_mgmt *lm = &ssd->lm[rg_id];
    struct line *line = get_line(ssd, ppa);
    line->stream_id=-1;
    line->ipc = 0;
    line->vpc = 0;
    /* move this line to free line list */
    QTAILQ_INSERT_TAIL(&lm->free_line_list, line, entry);
    lm->free_line_cnt++;
}

static int do_gc(struct ssd *ssd, bool force,int rg_id)
{
    struct line *victim_line = NULL;
    struct ssdparams *spp = &ssd->sp;
    // struct nand_lun *lunp;
    bool to_other_rg=false;
    struct ppa ppa;
    int ch, lun,cnt=0;
    int stream_id;
    uint16_t pg;
    victim_line = select_victim_line(ssd, force,rg_id);

    struct nand_page *pg_iter = NULL;

    if (!victim_line) {
        // printf("no victim line, return\n");
        return -1;
    }

    stream_id=victim_line->stream_id;

    ppa.g.blk = victim_line->id;

    // ftl_debug("GC-ing line:%d,ipc=%d,victim=%d,full=%d,free=%d\n", ppa.g.blk,
    //           victim_line->ipc, ssd->lm[rg_id].victim_line_cnt, ssd->lm.full_line_cnt,
    //           ssd->lm.free_line_cnt);


    //////////////////////////////////
    // fdp

    int start_ch_id,start_lun_id,end_ch_id,end_lun_id;
    rg2physical(spp,rg_id,&start_ch_id,&start_lun_id,&end_ch_id,&end_lun_id);
    // print_sungjin(ssd->sungjin_stat.block_erased);
    // print_sungjin(ssd->sungjin_stat.copied);
    time_t t;
    struct tm *tm_info;

    time(&t);
    tm_info = localtime(&t);

    // printf("Current time: %s", asctime(tm_info));

    printf("[%s]GC-ing line:%d,ipc=%d,victim=%d,full=%d,free=%d,stream_id=%d,rg_id=%d,start_ch_id=%d,end_ch_id=%d\
               start_lun_id=%d,end_lun_id=%d discard %lu ignored %u block_erased %lu copied %lu\n",asctime(tm_info), ppa.g.blk,
              victim_line->ipc, ssd->lm[rg_id].victim_line_cnt, ssd->lm[rg_id].full_line_cnt,
              ssd->lm[rg_id].free_line_cnt,stream_id,rg_id,start_ch_id,end_ch_id,
              start_lun_id,end_lun_id,ssd->sungjin_stat.discard,ssd->sungjin_stat.discard_ignored ,ssd->sungjin_stat.block_erased,ssd->sungjin_stat.copied);
    /////////////////////////////////
    to_other_rg=(victim_line->ipc < (ssd->sp.pgs_per_line/10)) ;

    for(pg=0;pg<spp->pgs_per_blk;pg++){
        for(ch =start_ch_id ; ch<=end_ch_id; ch++){
            for(lun=start_lun_id;lun<=end_lun_id;lun++){
                ppa.g.ch=ch;
                ppa.g.lun=lun;
                ppa.g.pl=0;
                ppa.g.pg=pg;
                pg_iter = get_pg(ssd,&ppa);
                if (pg_iter->status == PG_VALID) {
                    gc_read_page(ssd, &ppa);
                    /* delay the maptbl update until "write" happens */
                    gc_write_page(ssd, &ppa, stream_id,rg_id,to_other_rg);
                    cnt++;
                }
                pg_iter->status=PG_FREE;
            }
        }
    }
    ssd->sungjin_stat.copied+=cnt;

     // erase
    for(ch =start_ch_id ; ch<=end_ch_id; ch++){
        for(lun=start_lun_id;lun<=end_lun_id;lun++){
            ppa.g.ch = ch;
            ppa.g.lun = lun;
            ppa.g.pl = 0;
            // mark_block_free(ssd, &ppa);
            struct nand_block *blk = get_blk(ssd, &ppa);
            blk->ipc = 0;
            blk->vpc = 0;
            blk->erase_cnt++;
            ssd->sungjin_stat.block_erased++;
            struct nand_cmd gce;
            gce.type = GC_IO;
            gce.cmd = NAND_ERASE;
            gce.stime = 0;
            ssd_advance_status(ssd, &ppa, &gce);
        }
    }
    mark_line_free(ssd, &ppa,rg_id);




    
    
//     for(pg=0;pg<spp->pgs_per_blk;pg++){
//         for(ch =0 ;ch<spp->nchs;ch++){
//             for(lun=0;lun<spp->luns_per_ch;lun++){
//                 ppa.g.ch=ch;
//                 ppa.g.lun=lun;
//                 ppa.g.pl=0;
//                 ppa.g.pg=pg;
                
//                 pg_iter = get_pg(ssd,&ppa);
//                 // ftl_assert(pg_iter->status != PG_FREE);
//                 // print_sungjin(pg_iter);
//                 if(pg_iter->status == PG_FREE){
//                     // print_sungjin(pg_iter->status);
//                 }
//                 if (pg_iter->status == PG_VALID) {
//                     gc_read_page(ssd, &ppa);
//                     /* delay the maptbl update until "write" happens */
//                     gc_write_page(ssd, &ppa, stream_id);
//                     cnt++;
//                 }
//                 pg_iter->status=PG_FREE;
//             }
//         }
//     }
//     ssd->sungjin_stat.copied+=cnt;
//     // printf("sungjin copy ok\n");

// // erase
//     for (ch = 0; ch < spp->nchs; ch++) {
//         for (lun = 0; lun < spp->luns_per_ch; lun++) {
//             ppa.g.ch = ch;
//             ppa.g.lun = lun;
//             ppa.g.pl = 0;
//             // mark_block_free(ssd, &ppa);
//             struct nand_block *blk = get_blk(ssd, &ppa);
//             blk->ipc = 0;
//             blk->vpc = 0;
//             blk->erase_cnt++;
//             ssd->sungjin_stat.block_erased++;
//             if (spp->enable_gc_delay) {
//                 struct nand_cmd gce;
//                 gce.type = GC_IO;
//                 gce.cmd = NAND_ERASE;
//                 gce.stime = 0;
//                 ssd_advance_status(ssd, &ppa, &gce);
//             }
//         }
//     }
//     ftl_assert(get_line(ssd, ppa)->vpc == cnt);
    
//     // printf("sungjin erase and advance status ok\n");
//     /* update line status */
//     mark_line_free(ssd, &ppa,rg_id);
    // printf("sungjin mark line free status ok\n");
    return 0;
}




static uint64_t ssd_read(struct ssd *ssd, NvmeRequest *req)
{
    struct ssdparams *spp = &ssd->sp;
    // req->ctrl
    // FemuCtrl* n =(FemuCtrl*)ssd->femuctrl;
    uint64_t lba = req->slba;
    int nsecs = req->nlb;
    struct ppa ppa;
    // uint64_t start_lpn = lba / spp->secs_per_pg;
    // uint64_t end_lpn = (lba + nsecs - 1) / spp->secs_per_pg;

    // uint64_t start_lpn = lba + req->ns->start_block;
    uint64_t start_lpn = lba;
    uint64_t end_lpn = (start_lpn + nsecs);

    uint64_t lpn;
    uint64_t sublat, maxlat = 0;
    
    // uint64_t debug_id = req->cmd.cdw11;

    // if(debug_id==1998){
    //     /*
    //         copy amount
    //         gc time
    //         block erased
    //     */
    //     // print_stat()
    //     print_sungjin(ssd->sungjin_stat.block_erased);
    //      print_sungjin(ssd->sungjin_stat.copied);
    //     return 0;
    // }
    if (end_lpn >= spp->tt_pgs) {
        ftl_err("start_lpn=%"PRIu64",tt_pgs=%d\n", start_lpn, ssd->sp.tt_pgs);
    }




    // print_sungjin(ssd_read);
    /* normal IO read path */
    for (lpn = start_lpn; lpn <= end_lpn; lpn++) {
        ppa = get_maptbl_ent(ssd, lpn);
        if (!mapped_ppa(&ppa) || !valid_ppa(ssd, &ppa)) {
            //printf("%s,lpn(%" PRId64 ") not mapped to valid ppa\n", ssd->ssdname, lpn);
            //printf("Invalid ppa,ch:%d,lun:%d,blk:%d,pl:%d,pg:%d,sec:%d\n",
            //ppa.g.ch, ppa.g.lun, ppa.g.blk, ppa.g.pl, ppa.g.pg, ppa.g.sec);
            continue;
        }

        struct nand_cmd srd;
        srd.type = USER_IO;
        srd.cmd = NAND_READ;
        srd.stime = req->stime;
        sublat = ssd_advance_status(ssd, &ppa, &srd);
        maxlat = (sublat > maxlat) ? sublat : maxlat;
    }
    // uint64_t data_offset= ms_l2b(req->ns,lba);
    // backend_rw(n->mbe, &req->qsg, &data_offset, req->is_write);
    // backend_rw(req->ns->ctrl->mbe,&req->qsg,&data_offset,false);
    return maxlat;
}


// uint64_t msssd_trim2(FemuCtrl *n,uint64_t slba,uint64_t nlb){
//     // int j
//     // for(j=0;j<nlb ;j++){
//     //     ppa=get_maptbl_ent(ssd,(slba+j));
//     //     if (!mapped_ppa(&ppa) || !valid_ppa(ssd, &ppa)) {
//     //         continue;
//     //     }
//     //     mark_page_invalid(ssd, &ppa);
//     //     set_rmap_ent(ssd, INVALID_LPN, &ppa);
//     // }
    // print_sungjin(msssd_trim2);
//     return 0;
// }


// static uint64_t msssd_io_mgmt_send()
static uint64_t msssd_io_mgmt_recv_ruhs(struct ssd* ssd, NvmeRequest* req,size_t len){
    // unsigned int nruhsd= ssd->stream_number;
    uint8_t ph,rg;
    unsigned int nruhsd= ssd->handle_number*ssd->rg_number;
    print_sungjin(msssd_io_mgmt_recv_ruhs);
    print_sungjin(len);
    print_sungjin(ssd->rg_number);
    print_sungjin(ssd->handle_number);
    NvmeRuhStatus *hdr;
    NvmeRuhStatusDescr *ruhsd;
    uint64_t prp1 = le64_to_cpu(req->cmd.dptr.prp1);
    uint64_t prp2 = le64_to_cpu(req->cmd.dptr.prp2);

    size_t trans_len=sizeof(NvmeRuhStatus)+nruhsd*sizeof(NvmeRuhStatusDescr);
    void *buf = NULL;
    buf = g_malloc0(trans_len);

    trans_len = MIN(trans_len, len);

    hdr = (NvmeRuhStatus *)buf;
    ruhsd = (NvmeRuhStatusDescr *)(buf + sizeof(NvmeRuhStatus));

    hdr->nruhsd=cpu_to_le16(nruhsd);
    // return (ssd->lm.free_line_cnt <= ssd->sp.gc_thres_lines);
    int total_free_line=0;
    for(rg=0;rg<ssd->rg_number;rg++){
        total_free_line+=ssd->lm[rg].free_line_cnt;
    }
    // total_free_line/=rg;
    // hdr->free_space_ratio=(uint8_t)(total_free_line*100/(ssd->lm[0].tt_lines*ssd->rg_number));
    // hdr->copied_page=ssd->sungjin_stat.copied;
    // hdr->block_erased=ssd->sungjin_stat.block_erased;
    // ruhid=ns-
    // int stream;
    // for(stream=0;stream<nruhsd;stream++,ruhsd++){
    //     ruhsd->pid=stream;
    //     ruhsd->ruhid=stream;
    //     ruhsd->earutr = 0;
    //     ruhsd->ruamw=0;
    // }
    
    
    for(ph=0;ph<ssd->handle_number;ph++){
        for(rg=0;rg<ssd->rg_number;rg++,ruhsd++){
            // NvmeCmdDWORD13 dword13;
            // dword13.parsed.rg=cpu_to_le16(rg);
            // dword13.parsed.ph=cpu_to_le16(ph);
            //ruhsd->pid=cpu_to_le16(dword13.dspec); 
            //ruhsd->ruhid=cpu_to_le16(dword13.dspec); 
            ruhsd->earutr = 0;
            ruhsd->ruamw=0;
            // ruhsd->pid = cpu_to_le16((uint16_t)(dword13.parsed.rg<<8 | dword13.parsed.ph));
            // ruhsd->ruhid=cpu_to_le16((uint16_t)(dword13.parsed.rg<<8 | dword13.parsed.ph)); // how to do ? pid == ruhid?
            ruhsd->pid = cpu_to_le16((uint16_t)(rg<<8 | ph));
            ruhsd->ruhid=cpu_to_le16((uint16_t)(rg<<8 | ph)); // how to do ? pid == ruhid?
            printf("ruhsd->ruhid %u\b",ruhsd->ruhid);
            // printf("dword13.dspec %u ruhsd->pid %u\n",dword13.parsed.dspec,ruhsd->pid);
        }
    }

    // nvme_c2h(n, buf, trans_len, req)
    dma_read_prp((FemuCtrl*)ssd->femuctrl, (uint8_t *)buf, trans_len, prp1, prp2);
    g_free(buf);
    return 0;
}

static uint64_t msssd_io_mgmt_send_sungjin(struct ssd* ssd, NvmeRequest* req){
    // uint64_t slpn=0;
    // struct ppa ppa;
    int i,j;
    struct ssdparams* spp= &ssd->sp;
    ssd->debug =true;
    // struct line_mgmt *lm = &ssd->lm[];
    printf("msssd_io_mgmt_send_sungjin\n");
    print_sungjin(spp->blks_per_line);
    print_sungjin(spp->pgs_per_line);

    print_sungjin(ssd->sungjin_stat.block_erased);
    print_sungjin(ssd->sungjin_stat.copied);
    print_sungjin(ssd->sungjin_stat.discard);
    ssd->sungjin_stat.block_erased=0;
    ssd->sungjin_stat.copied=0;
    ssd->sungjin_stat.discard=0;
    ssd->sungjin_stat.discard_ignored=0;
    for (i = 0; i < spp->nchs; i++) {
        ssd_init_ch(&ssd->ch[i], spp,false);
    }

    /* initialize maptbl */
    ssd_init_maptbl(ssd,false);

    /* initialize rmap */
    ssd_init_rmap(ssd,false);

    /* initialize all the lines */
    for(i=0;i<ssd->rg_number;i++){
        printf("\nRG %u-----------------------------\n",i);
        fdpssd_init_lines(ssd,false,i);
        printf("------------------------------------\n");
    }

    for(i=0;i<ssd->rg_number;i++){
        int start_ch_id,start_lun_id,end_ch_id,end_lun_id;
        rg2physical(spp,i,&start_ch_id,&start_lun_id,&end_ch_id,&end_lun_id);
        printf("RG %d channel %d-%d / lun %d-%d\n",i,start_ch_id,end_ch_id,start_lun_id,end_lun_id);
    }

    /* initialize write pointer, this is how we allocate new pages for writes */

    // for(i=0;i<ssd->stream_number;i++){
    //     fdpssd_init_write_pointer(ssd,i);
    // }
    for(i=0;i<ssd->handle_number;i++){
        for(j=0;j<ssd->rg_number;j++){
            fdpssd_init_write_pointer(ssd,i,j);
        }
    }
    time_t t;
    struct tm *tm_info;

    time(&t);
    tm_info = localtime(&t);

    printf("INITIALIZED current time: %s", asctime(tm_info));
    // print_sungjin(lm->free_line_cnt);
    // print_sungjin(lm->victim_line_cnt);
    // print_sungjin(lm->full_line_cnt);


    // for(slpn=0;;slpn++){
    //     ppa=get_maptbl_ent(ssd,(slpn));
    //     if ( !valid_ppa(ssd, &ppa)) {
    //         // continue;
    //         break;
    //     }
    //     if(!mapped_ppa(&ppa)){
    //         continue;
    //     }
        
    //     // get_line(ssd, &ppa)->vpc=ssd->sp.pgs_per_line;

    //     mark_page_invalid(ssd, &ppa);
    //     set_rmap_ent(ssd, INVALID_LPN, &ppa);
        
    // }
    // printf("invalidate all ok\n");
    // for (i = 0; i < spp->nchs; i++) {
    //     ssd_init_ch(&ssd->ch[i], spp,false);
    // }

//     ssd->sp.enable_gc_delay=false;

//     while(true){
//         if(do_gc(ssd,true)==-1){
//             break;
//         }
//     }   
// printf("do gc ok\n");
//     ssd->sp.enable_gc_delay=true;

    // sungjin todo
    // 
    // mark line free, but do not touch stream id

    return 0;
}

static uint64_t msssd_io_mgmt_send(struct ssd* ssd, NvmeRequest*req){
    NvmeCmd *cmd = &req->cmd;
    uint32_t cdw10 = le32_to_cpu(cmd->cdw10);
    uint8_t mo = (cdw10 & 0xff);

    switch (mo) {
    case NVME_IOMS_MO_NOP:
        return 0;
    case NVME_IOMS_MO_RUH_UPDATE:
        // return nvme_io_mgmt_send_ruh_update(n, req);
        return NVME_SUCCESS;
    case NVME_IOMS_MO_SUNGJIN:
        return msssd_io_mgmt_send_sungjin(ssd,req);
    default:
        return NVME_INVALID_FIELD | NVME_DNR;
    };
}

static uint64_t msssd_io_mgmt_recv(struct ssd* ssd, NvmeRequest* req){
    NvmeCmd *cmd = &req->cmd;
    uint32_t cdw10 = le32_to_cpu(cmd->cdw10);
    uint32_t numd = le32_to_cpu(cmd->cdw11);
    uint8_t mo = (cdw10 & 0xff);
    size_t len = (numd + 1) << 2;
    print_sungjin(msssd_io_mgmt_recv);
    switch (mo) {
    case NVME_IOMR_MO_NOP:
        return 0;
    case NVME_IOMR_MO_RUH_STATUS:
        return msssd_io_mgmt_recv_ruhs(ssd, req, len);
    default:
        return NVME_INVALID_FIELD | NVME_DNR;
    };
}

static uint64_t msssd_trim(struct ssd* ssd,NvmeRequest* req){
    int i,j;
    struct ssdparams *spp = &ssd->sp;
    // NvmeNamespace *ns = req->ns;
    NvmeDsmCmd *dsm = (NvmeDsmCmd *) &req->cmd;
    uint32_t attr = le32_to_cpu(dsm->attributes);
    uint32_t nr_org = (le32_to_cpu(dsm->nr) & 0xff) + 1;
    uint32_t dw10 = le32_to_cpu(req->cmd.cdw10);
    uint16_t nr = (dw10 & 0xff) + 1;
    if(nr!=nr_org){
        // print_sungjin
        printf("msssd_trim why ? %u, %u\n",nr,nr_org);
    }
    // uint64_t slba;
    uint32_t nlp;
    uint64_t slpn;
    // uint32_t slpn;
    struct ppa ppa;
    // struct nand_page *pg_iter = NULL;
    // struct nand_page* pg_iter;
    // uint64_t plp1 = req->cmd->plp1;

    // print_sungjin(msssd_trim);
    // print_sungjin(nr);
    if (attr & NVME_DSMGMT_AD) {
        // NvmeDSMAIOCB iocb;
        // iocb.req=
        // NvmeDsmRange *range = g_malloc0(sizeof(struct NvmeDsmRange)*nr);
        // nvme_h2c()
        NvmeDsmRange *range= (NvmeDsmRange *)req->cmd.discard_range_pointer;
        // read in range
        // dma_read_prp(req->ns->ctrl,(uint8_t*)range,sizeof(struct NvmeDsmRange)*nr,dsm->prp1,dsm->prp2);
        // print_sungjin(range);
        for(i = 0; i<nr;i++){
            NvmeDsmRange* dmr = &range[i];
            slpn=le64_to_cpu(dmr->slba)/spp->secs_per_pg;
            // slpn=le32_to_cpu(dmr->slba)/spp->secs_per_pg;
            nlp=le32_to_cpu(dmr->nlb+1)/spp->secs_per_pg;
            // print_sungjin(spp->secs_per_pg);
            // print_sungjin(slpn);
            // print_sungjin(nlp);
            
            
            // ssd->sungjin_stat.discard+=(dmr->nlb+1);
            
            
            // if(dmr->nlb==0){
            //     printf(" 0 occurs\n");
            // }
            // msssd_trim2(req->ns->ctrl,slpn,nlp);
                // bool yesvalidppa= true;
                // int tmp1=0,tmp2=0;
                // bool yesmappedppa = true;

            for(j=0;j<nlp ;j++){
                ppa=get_maptbl_ent(ssd,(slpn+j));
                if (!valid_ppa(ssd, &ppa)) {
                    // printf("not valid ppa trim?");
                    // print_sungjin(slpn);
                    // tmp1++;
                    // yesvalidppa=false;
                    // print_ppa(ssd,&ppa);
                    // print_sungjin(slpn+j);
                     ssd->sungjin_stat.discard_ignored++;
                    continue;
                }


                // if(ppa.ppa==PG_VALID)
                if(mapped_ppa(&ppa)){
                    ssd->sungjin_stat.discard++;
                    int ret = mark_page_invalid(ssd, &ppa);
                    if(ret){
                        printf("mark page invalid from trim?? slpn+j %lu\n",slpn+j);
                    }
                    set_rmap_ent(ssd, INVALID_LPN, &ppa);
                    ppa.ppa=UNMAPPED_PPA;
                    set_maptbl_ent(ssd,(slpn+j),&ppa);
                }
                // else{
                //     tmp2++;
                //     yesmappedppa=false;
                // }
                
                // pg_iter = get_pg(ssd, &ppa);
                // if(pg_iter->status==PG_VALID){
                //     mark_page_invalid(ssd, &ppa);
                // }
                // set_rmap_ent(ssd, INVALID_LPN, &ppa);
                
            }


            // if(!yesvalidppa&& ssd->debug==true){
            //     // not valid ppa trim? slpn 0 nlp 1048576 tmp1 1048576
            //     printf("not valid ppa trim? slpn %lu nlp %u tmp1 %u\n",slpn,nlp,tmp1);
            //     // print_sungjin(slpn);
            // }
            // if(!yesmappedppa&&ssd->debug==true){
            //     printf("not mapped ppa trim?slpn %lu nlp %u tmp1 %u\n",slpn,nlp,tmp2);
            //     // print_sungjin(slpn);
            // }
        }

        // print_sungjin(msssd_trim);
        g_free(range);
    }
   
    return 0;
}

static uint64_t fdpssd_write(struct ssd *ssd, NvmeRequest *req)
{
    uint64_t lba = req->slba;
    struct ssdparams *spp = &ssd->sp;
    int len = req->nlb;
    // uint64_t start_lpn = lba / spp->secs_per_pg;
    // uint64_t end_lpn = (lba + len - 1) / spp->secs_per_pg;
    // uint64_t start_lpn = lba + req->ns->start_block;

    uint64_t start_lpn = lba;
    uint64_t end_lpn = (start_lpn + len );
    // struct line_mgmt *lm;

    struct ppa ppa;
    uint64_t lpn;
    uint64_t curlat = 0, maxlat = 0;
    int i;
    // if(sizeof(NvmeCmd))
    // uint32_t dw13=req->cmd.cdw13;
    // print_sungjin(req->cmd.cdw13);
#if 1
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    uint16_t pid = le16_to_cpu(rw->dspec);
    uint8_t stream_id = pid & 0xff;
    uint8_t rg_id =pid>>8;
    
    // uint8_t     dsmgmt=(rw->dsmgmt);;
    // uint8_t     rsvd=(rw->rsvd);
#else
    uint16_t rg_id=(req->cmd.nsid-1);
    uint16_t stream_id=0;
#endif
    // print_sungjin(rg_id);
    // print_sungjin(dsmgmt);
    // print_sungjin(rsvd);
    // print_sungjin(pid);

    // print_sungjin(req->cmd.nsid);
    // printf("sizeof(NvmeCmd) %lu sizeof(NvmeRwCmd) %lu\n",sizeof(NvmeCmd),sizeof(NvmeRwCmd));
    // xnvme_ctx->cmd.nvm.cdw13.dspec = geo.dspec_;  // place_id_
    // printf("sungjin test %d %d dtype %u pid %u, sizeofdword13 %lu req->slba %lu\n",stream_id,rg_id,dtype,pid,sizeof(NvmeCmdDWORD13),req->slba);
 
//  sungjin 164/92 -> 7/92
// sungjin 7/92 -> 7/0

    if(stream_id>=ssd->handle_number){
        // printf("sungjin : stream id %u -> %u sizeof %lu\n",stream_id,ssd->stream_number-1,sizeof(NvmeCmdDWORD13));
        printf("sungjin %u/%u -> %u/%u\n",stream_id,rg_id,ssd->handle_number-1,rg_id);
        stream_id=ssd->handle_number-1;
        // return 0;
    }
    if(rg_id>=ssd->rg_number){
        printf("sungjin %u/%u -> %u/%u\n",stream_id,rg_id,stream_id,ssd->rg_number-1);
        rg_id=ssd->rg_number-1;
    }
    struct write_pointer *wpp = &(ssd->wp[stream_id][rg_id]);

    wpp->written+=(len+1);
    
    int r;
    // print_sungjin(fdpssd_write);
    // print_sungjin(req->ns->start_block);
    // print_sungjin(req->slba);
    // print_sungjin(lba);
    // print_sungjin(len);
    // print_sungjin(start_lpn);
    // print_sungjin(end_lpn);
    // print_sungjin(rg_id);
    // print_sungjin(req->cmd.nsid);
    // print_sungjin(req->ns->ns_blks);
    // print_sungjin(ssd->sp.tt_pgs);

    if (end_lpn >= spp->tt_pgs) {
        ftl_err("start_lpn=%"PRIu64",tt_pgs=%d\n", start_lpn, ssd->sp.tt_pgs);
    }

    for (i=0;i<5&&should_gc_high(ssd,rg_id);i++) {
        /* perform GC here until !should_gc(ssd) */
        printf("doing gc? %d\n",rg_id);
        // if(should_gc_high(ssd,rg_id)){
            r = do_gc(ssd, true,rg_id);
            if (r == -1)
                break;
        // }

    }
    if(should_gc_high(ssd,rg_id)){
        // rg_id++;
        // print_sungjin(should_gc_high);
        rg_id=find_near_rg_id(ssd,rg_id);
    }
    // print_sungjin(rg_id);
    // printf(:)
    for (lpn = start_lpn; lpn <= end_lpn; lpn++) {
        // printf("sungjin loop : lpn %lu\n",lpn);
        ppa = get_maptbl_ent(ssd, lpn);
        if (mapped_ppa(&ppa)) {
            /* update old page information first */
            int ret =mark_page_invalid(ssd, &ppa);
            if(ret){
                printf("mark_page_invalid from ssdwrite? lpn %lu\n",lpn);
            }
            set_rmap_ent(ssd, INVALID_LPN, &ppa);
        }
        // print_sungjin(lpn);
        /* new write */
        ppa = get_new_page(ssd,stream_id,rg_id);
        /* update maptbl */
        set_maptbl_ent(ssd, lpn, &ppa);
        /* update rmap */
        set_rmap_ent(ssd, lpn, &ppa);

        mark_page_valid(ssd, &ppa);

        /* need to advance the write pointer here */
        ssd_advance_write_pointer(ssd,stream_id,rg_id);

        struct nand_cmd swr;
        swr.type = USER_IO;
        swr.cmd = NAND_WRITE;
        swr.stime = req->stime;
        /* get latency statistics */
        curlat = ssd_advance_status(ssd, &ppa, &swr);
        maxlat = (curlat > maxlat) ? curlat : maxlat;
    }
    // uint64_t data_offset= ms_l2b(req->ns,lba);
    // backend_rw(req->ns->ctrl->mbe,&req->qsg,&data_offset,true);
    // printf("sungjin mssd write return\n");

    return maxlat;
}

static void *msftl_thread(void *arg)
{
    FemuCtrl *n = (FemuCtrl *)arg;
    struct ssd *ssd = n->ssd;
    ssd->femuctrl=(void*)n;
    NvmeRequest *req = NULL;
    uint64_t lat = 0;
    int rc;
    int i;
    // int r;
    print_sungjin(msftl_thread);
    printf("msftl_thread start@@@@@@@@@\n");
    while (!*(ssd->dataplane_started_ptr)) {
        usleep(100000);
    }

    /* FIXME: not safe, to handle ->to_ftl and ->to_poller gracefully */
    ssd->to_ftl = n->to_ftl;
    ssd->to_poller = n->to_poller;
    ssd->sungjin_stat.block_erased=0;
    ssd->sungjin_stat.copied=0;
    ssd->debug=false;
    while (1) {
        for (i = 1; i <= n->nr_pollers; i++) {
            if (!ssd->to_ftl[i] || !femu_ring_count(ssd->to_ftl[i]))
                continue;

            rc = femu_ring_dequeue(ssd->to_ftl[i], (void *)&req, 1);
            if (rc != 1) {
                printf("FEMU: FTL to_ftl dequeue failed\n");
            }
            // printf("sungjin : msftl_thread : femu_ring_dequeue\n");
            ftl_assert(req);
            switch (req->cmd.opcode) {
            case NVME_CMD_WRITE:
                lat = fdpssd_write(ssd, req);
                break;
            case NVME_CMD_READ:
                lat = ssd_read(ssd, req);
                break;
            case NVME_CMD_DSM:
                lat = msssd_trim(ssd,req);
                break;
            case NVME_CMD_IO_MGMT_RECV:
                lat = msssd_io_mgmt_recv(ssd,req);
                break;
            case NVME_CMD_IO_MGMT_SEND:

                // if fdp ssd, handle update
                lat =msssd_io_mgmt_send(ssd,req);
                break;
            default:
                //ftl_err("FTL received unkown request type, ERROR\n");
                ;
            }

            req->reqlat = lat;
            req->expire_time += lat;

            rc = femu_ring_enqueue(ssd->to_poller[i], (void *)&req, 1);
            if (rc != 1) {
                ftl_err("FTL to_poller enqueue failed\n");
            }

            /* clean one line if needed (in the background) */

            for(i=0;i<ssd->rg_number;i++){
                if (should_gc(ssd,i)) {
                    do_gc(ssd, false,i);
                    // if(r==-1){
                    //     continue;
                    // }
                }
            }
            // if (should_gc(ssd)) {
            //     do_gc(ssd, false);
            //     // if(r==-1){
            //     //     continue;
            //     // }
            // }
        }
    }
    printf("msftl_thread end@@@@@@@@@\n");

    return NULL;
}
