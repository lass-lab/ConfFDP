#include <libxnvme.h>
// #include <queue>
#include <stdio.h>
// #define DEVICE "/dev/nvme0n1"
// #define BUF_SIZE (1<<20)
#define MAX_NR_QUEUE 1

#define print_sungjin(member) printf("%s %lu\n",(#member),(uint64_t)(member));
/*
device name --> dev_type
nvme --> block
ng --> char
pci --> spdk

/////
dev_type ---> async_io
spdk ---> nvme
else(block,chark) -> selected





*/

enum {
	NVME_DIR_IDENTIFY	= 0x00,
	NVME_DIR_STREAMS	= 0x01,
};


enum nvme_admin_opcode {
 	nvme_admin_download_fw		= 0x11,
 	nvme_admin_ns_attach		= 0x15,
 	nvme_admin_keep_alive		= 0x18,
nvme_admin_directive_send	= 0x19,
nvme_admin_directive_recv	= 0x1a,
 	nvme_admin_format_nvm		= 0x80,
 	nvme_admin_security_send	= 0x81,
 	nvme_admin_security_recv	= 0x82,

 	NVME_FWACT_REPL		= (0 << 3),
 	NVME_FWACT_REPL_ACTV	= (1 << 3),
 	NVME_FWACT_ACTV		= (2 << 3),

NVME_DIR_ID_RCVOP_PARAM		= 0x01,
NVME_DIR_ID_SNDOP_ENABLE	= 0x01,
NVME_DIR_ST_RCVOP_PARAM		= 0x01,
NVME_DIR_ST_RCVOP_STATUS	= 0x02,
NVME_DIR_ST_RCVOP_RESOURCE	= 0x03,
NVME_DIR_ST_SNDOP_REL_ID	= 0x01,
NVME_DIR_ST_SNDOP_REL_RSC	= 0x02,
 };

struct nvme_fdp_ruh_status_desc {
  uint16_t pid;
  uint16_t ruhid;
  uint32_t earutr;
  uint64_t ruamw;
  uint8_t rg_mapped_bitmap[16];
};
struct nvme_fdp_ruh_status {
  //  union{ 
  //   struct{
  //       uint8_t free_space_ratio;
  //       uint32_t copied_page;
  //       uint32_t block_erased;
  //       uint8_t rsvd0_tmp[5];
  //   };
  //   uint8_t  rsvd0[14];
  //  };
  // uint16_t nruhsd;
  uint8_t rsvd0[10];
  uint16_t reclaim_group_nr;
  uint16_t max_placement_id_nr;
  uint16_t nruhsd;
  struct nvme_fdp_ruh_status_desc ruhss[256];
};



enum NvmeIomr2Mo {
    NVME_IOMR_MO_NOP = 0x0,
    NVME_IOMR_MO_RUH_STATUS = 0x1,
    NVME_IOMR_MO_VENDOR_SPECIFIC = 0x255,
};

const char *async_str[] = {"thrpool",
                            "libaio",
                            "io_uring",
                            "io_uring_cmd",
                            "nvme"};


void async_cb(struct xnvme_cmd_ctx* ctx,void* cb_arg){
    printf("hello i am async cb\n");
    struct xnvme_queue* xqueue;
    xqueue = (struct xnvme_queue*)cb_arg;
    if(xnvme_cmd_ctx_cpl_status(ctx)){
        xnvme_cmd_ctx_pr(ctx,XNVME_PR_DEF);
    }
    xnvme_queue_put_cmd_ctx(xqueue,ctx);
}

void nvme_directive_show_fields(__u8 dtype, __u8 doper, unsigned int result, unsigned char *buf)
{
	__u8 *field = buf;
	int count, i;
        switch (dtype) {
        case NVME_DIR_IDENTIFY:
                switch (doper) {
                case NVME_DIR_ID_RCVOP_PARAM:
			printf("\tDirective support \n");
			printf("\t\tStreams Directive   : %s\n", (*field & 0x1) ? "supported":"not supported");
			printf("\t\tIdentify Directive  : %s\n", (*field & 0x2) ? "supported":"not supported");
			printf("\tDirective status \n");
			printf("\t\tStreams Directive   : %s\n", (*(field + 32) & 0x1) ? "enabled" : "disabled");
			printf("\t\tIdentify Directive  : %s\n", (*(field + 32) & 0x2) ? "enabled" : "disabled");
                        break;
                default:
                        fprintf(stderr, "invalid directive operations for Identify Directives\n");
                }
                break;
        case NVME_DIR_STREAMS:
                switch (doper) {
                case NVME_DIR_ST_RCVOP_PARAM:
			printf("\tMax Streams Limit                          (MSL): %u\n", *(__u16 *) field);
			printf("\tNumber of NVM Subsystem Stream Resources (NNSSR): %u\n", *(__u16 *) (field + 2));
			printf("\tNumber of Open Streams in NVM subsystem  (NOSNS): %u\n", *(__u16 *) (field + 4));
			printf("\tOptimal Stream Write Size                 (OSWS): %u\n", *(__u32 *) (field + 16));
			printf("\tStream Granularity Size                    (SGS): %u\n", *(__u16 *) (field + 20));
			printf("\tAllocated Stream Resources                 (ASR): %u\n", *(__u16 *) (field + 22));
			printf("\tNumber of Open Streams in Namespace       (NOSN): %u\n", *(__u16 *) (field + 24));
                        break;
                case NVME_DIR_ST_RCVOP_STATUS:
			count = *(__u16 *) field;
			printf("\tOpen Stream Count  : %u\n", *(__u16 *) field);
			for ( i = 0; i < count; i++ ) {
				printf("\tStream Identifier %.6u : %u\n", i + 1, *(__u16 *) (field + ((i + 1) * 2)));
			}
                        break;
                case NVME_DIR_ST_RCVOP_RESOURCE:
			printf("\tAllocated Stream Resource Count (ASR): %u\n", result & 0xffff);
                        break;
                default:
                        fprintf(stderr, "invalid directive operations for Streams Directives\n");
                }
                break;
        default:
                fprintf(stderr, "invalid directive type\n");
                break;
        }
        return;
}

int main(int argc,char**argv){
    if(argc<5){
        printf("%s <dev> dspec dtype doper",argv[0]);
        return 0;
    }


    const char * DEVICE=argv[1];
    int dspec = atoi(argv[2]);
    int dtype = atoi(argv[3]);
    int doper = atoi(argv[4]);

    uint64_t slba=0;
    uint64_t nlb;
    uint64_t nsid;
    struct xnvme_opts opts = xnvme_opts_default();
    struct xnvme_dev* dev_ = NULL;
    const struct xnvme_geo* geo_ = NULL;

    struct xnvme_queue* queues_[MAX_NR_QUEUE];
    const unsigned int qdepth = MAX_NR_QUEUE;
    // std::queue<struct xnvme_queue*> xnvme_queues_;
    struct xnvme_queue* xnvme_queues_;
    struct xnvme_cmd_ctx* xnvme_ctx;
    struct xnvme_queue* xqueue;

    opts.async="thrpool";
    opts.direct=0;
    int err;
    
    // dev_ = xnvme_dev_open(DEVICE, &opts);
    dev_ = xnvme_dev_open(DEVICE, &opts);
    print_sungjin(xnvme_dev_open);
    if (!dev_) {
        printf("error 1\n");
        return 0;
    }
    geo_ = xnvme_dev_get_geo(dev_);
    print_sungjin(xnvme_dev_get_geo);

    for(int i=0;i<MAX_NR_QUEUE;i++){
        queues_[i] = NULL;
        err=xnvme_queue_init(dev_,qdepth,0,&queues_[i]);
        //  print_sungjin(xnvme_queue_init);
        if(err){
            printf("Error 2 :%d\n",i);
            return 0;
        }
        // xnvme_queues_.push(queues_[i]);
        xnvme_queues_=queues_[i];
    }
    size_t BUF_SIZE;
    // void* buf = xnvme_buf_alloc(dev_, BUF_SIZE);
    // if(!buf){
    //     printf("xnvme_buf_alloc error\n");
    //     return 0;
    // }
    print_sungjin(xnvme_buf_alloc);
    xqueue=xnvme_queues_;
    // xnvme_queues_.pop();
    xnvme_ctx = xnvme_queue_get_cmd_ctx(xqueue);
     print_sungjin(xnvme_queue_get_cmd_ctx);
    xnvme_ctx->async.cb = async_cb;
    xnvme_ctx->async.cb_arg = xqueue;
    xnvme_ctx->dev = dev_;

    // nlb=(BUF_SIZE>>geo_->ssw)-1;
    nsid=xnvme_dev_get_nsid(dev_);
    print_sungjin(nsid);
    // printf("nsid %lu\n",nsid);
    
    // xnvme_prep_nvm(xnvme_ctx,XNVME_SPEC_NVM_OPC_WRITE,nsid,slba,nlb);
    //  print_sungjin(xnvme_prep_nvm);
    // xnvme_ctx->cmd.nvm.dtype = 2;
    // xnvme_ctx->cmd.nvm.cdw13.dspec = 1;
    struct nvme_fdp_ruh_status ruh_status;
    uint16_t mos = 1;
// int
// xnvme_nvm_mgmt_recv(struct xnvme_cmd_ctx *ctx, uint32_t nsid, uint8_t mo, uint16_t mos, void *dbuf,
// 		    uint32_t dbuf_nbytes);



/*

+	switch (cfg.dtype) {
+	case NVME_DIR_IDENTIFY:
+		switch (cfg.doper) {
+		case NVME_DIR_ID_RCVOP_PARAM:
+			cfg.data_len = 4096;
+			break;
+		default:
+			fprintf(stderr, "invalid directive operations for Identify Directives\n");
+			return EINVAL;
+		}
+		break;
+	case NVME_DIR_STREAMS:
+		switch (cfg.doper) {
+		case NVME_DIR_ST_RCVOP_PARAM:
+			cfg.data_len = 32;
+			break;
+		case NVME_DIR_ST_RCVOP_STATUS:
+			cfg.data_len = 128 * 1024;
+			break;
+		case NVME_DIR_ST_RCVOP_RESOURCE:
+			dw12 = cfg.rsr;
+			break;
+		default:
+			fprintf(stderr, "invalid directive operations for Streams Directives\n");
+			return EINVAL;
+		}
+		break;
+	default:
+		fprintf(stderr, "invalid directive type\n");
+		return EINVAL;
+		break;
+	}

*/

    xnvme_ctx->cmd.common.opcode=XNVME_SPEC_NVM_OPC_IO_MGMT_RECV;
    xnvme_ctx->cmd.common.nsid=nsid;
    // xnvme_ctx->cmd.dsend.doper=NVME_DIR_ST_RCVOP_PARAM;
    // xnvme_ctx->cmd.dsend.doper=NVME_DIR_ST_RCVOP_PARAM;
    // xnvme_ctx->cmd.dsend.doper=NVME_DIR_ST_RCVOP_RESOURCE;

    xnvme_ctx->cmd.dsend.dtype=dtype; // 0 or 1
    xnvme_ctx->cmd.dsend.doper=doper;
    xnvme_ctx->cmd.dsend.dspec=dspec;


    
	switch (xnvme_ctx->cmd.dsend.dtype) {
	case NVME_DIR_IDENTIFY:
		switch (xnvme_ctx->cmd.dsend.doper) {
		case NVME_DIR_ID_RCVOP_PARAM:
			BUF_SIZE = 4096;
			break;
		default:
			fprintf(stderr, "invalid directive operations for Identify Directives\n");
			// return EINVAL;
            break;
		}
		break;
	case NVME_DIR_STREAMS:
		switch (doper) {
		case NVME_DIR_ST_RCVOP_PARAM:
			// cfg.data_len = 32;
            BUF_SIZE=32;
			break;
		case NVME_DIR_ST_RCVOP_STATUS:
			// cfg.data_len = 128 * 1024;
            BUF_SIZE=128*1024;
			break;
		case NVME_DIR_ST_RCVOP_RESOURCE:
			// dw12 = 8;
            // xnvme_ctx.
            xnvme_ctx->cmd.common.cdw12=8;
			break;
		default:
			fprintf(stderr, "invalid directive operations for Streams Directives\n");
			// return EINVAL;
            break;
		}
		break;
	default:
		fprintf(stderr, "invalid directive type\n");
		// return EINVAL;
        break;
		break;
	}
    void* buf = xnvme_buf_alloc(dev_, BUF_SIZE);
    if(!buf){
        printf("xnvme_buf_alloc error\n");
        return 0;
    }
    err = xnvme_cmd_pass(xnvme_ctx, buf, BUF_SIZE, nullptr, 0); 
    // err=xnvme_nvm_mgmt_recv(xnvme_ctx,nsid,
    //     NVME_IOMR_MO_RUH_STATUS,mos,&ruh_status,sizeof(struct nvme_fdp_ruh_status));
      if (err) {
      printf("Failed to perform xnvme_nvm_mgmt_recv\n");
      return err;
    }
    if (err) {
      printf("Failed to perform xNVMe IO command\n");
      return err;
    }
    print_sungjin(xnvme_cmd_pass);
  err = xnvme_queue_drain(xqueue);
  print_sungjin(xnvme_queue_drain);
  if (err < 0) printf("Failed to drain xNVMe queue\n");
    nvme_directive_show_fields(dtype,doper,1,(unsigned char*)buf);

/////////////////
    // printf("ruh_status.nruhsd %d /// fr ratio %u copied page %u block erase %u\n",
    // ruh_status.nruhsd,ruh_status.free_space_ratio
    // ,ruh_status.copied_page,ruh_status.block_erased)
  
    // printf("")
    // print_sungjin(ruh_status.nruhsd);
    // print_sungjin(ruh_status.max_placement_id_nr);

    // print_sungjin(ruh_status.reclaim_group_nr);
    // for(int i=0;i<ruh_status.nruhsd;i++){
    //     /*
    //       uint16_t pid;
    //     uint16_t ruhid;
    //     uint32_t earutr;
    //     uint64_t ruamw;
    //     0 0 0 0
    //     1 1 0 0
    //     2 2 0 0
    //     3 3 0 0

    //     */
    //     printf("%d %d %d %ld\n",
    //     ruh_status.ruhss[i].pid,
    //     ruh_status.ruhss[i].ruhid,
    //     ruh_status.ruhss[i].earutr,
    //     ruh_status.ruhss[i].ruamw);
    // }
    for(int i=0;i<MAX_NR_QUEUE;i++){
        err = xnvme_queue_term(queues_[i]);
        if (err) {
        printf("The [%d]th queue is failed\n", i);
        }
    }
  xnvme_dev_close(dev_);
    return 0;
}
