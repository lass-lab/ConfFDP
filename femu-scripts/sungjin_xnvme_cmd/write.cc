#include <libxnvme.h>
#include <queue>
#include <stdio.h>
#define DEVICE "/dev/nvme0n1"
#define BUF_SIZE (1<<20)
#define MAX_NR_QUEUE 128

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

struct PlacementID
{
  union{
    struct {    
      // uint32_t rg : 8;
      // uint32_t ph : 8;
            
      uint32_t ph : 8;
      uint32_t rg : 8;
    };
      uint16_t pid;
  };
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

int main(int argc,char**argv){

    uint64_t slba = atoll(argv[1]);
    int ph = atoi(argv[2]);
    int rg= atoi(argv[3]);
    uint64_t nlb;
    uint64_t nsid;
    struct xnvme_opts opts = xnvme_opts_default();
    struct xnvme_dev* dev_ = nullptr;
    const struct xnvme_geo* geo_ = nullptr;

    struct xnvme_queue* queues_[MAX_NR_QUEUE];
    const unsigned int qdepth = MAX_NR_QUEUE;
    std::queue<struct xnvme_queue*> xnvme_queues_;
    
    struct xnvme_cmd_ctx* xnvme_ctx;
    struct xnvme_queue* xqueue;

    opts.async="libaio";
    opts.direct=1;
    int err;
    
    dev_ = xnvme_dev_open(DEVICE, &opts);
    print_sungjin(xnvme_dev_open);
    if (!dev_) {
        printf("error 1\n");
        return 0;
    }
    geo_ = xnvme_dev_get_geo(dev_);
    print_sungjin(xnvme_dev_get_geo);

    for(int i=0;i<MAX_NR_QUEUE;i++){
        queues_[i] = nullptr;
        err=xnvme_queue_init(dev_,qdepth,0,&queues_[i]);
         print_sungjin(xnvme_queue_init);
        if(err){
            printf("Error 2 :%d\n",i);
            return 0;
        }
        xnvme_queues_.push(queues_[i]);
    }

    void* buf = xnvme_buf_alloc(dev_, BUF_SIZE);
    if(!buf){
        printf("xnvme_buf_alloc error\n");
        return 0;
    }
    print_sungjin(xnvme_buf_alloc);
    xqueue=xnvme_queues_.front();
    xnvme_queues_.pop();
    xnvme_ctx = xnvme_queue_get_cmd_ctx(xqueue);
     print_sungjin(xnvme_queue_get_cmd_ctx);
    xnvme_ctx->async.cb = async_cb;
    xnvme_ctx->async.cb_arg = reinterpret_cast<void*>(xqueue);
    xnvme_ctx->dev = dev_;

    nlb=(BUF_SIZE>>geo_->ssw)-1;
    nsid=xnvme_dev_get_nsid(dev_);
    print_sungjin(nsid);
    // printf("nsid %lu\n",nsid);
    xnvme_prep_nvm(xnvme_ctx,XNVME_SPEC_NVM_OPC_WRITE,nsid,slba,nlb);
     print_sungjin(xnvme_prep_nvm);

     PlacementID pid;
     pid.rg=rg;
     pid.ph=ph;
    
    xnvme_ctx->cmd.nvm.dtype = 2;
    xnvme_ctx->cmd.nvm.cdw13.dspec = pid.pid;
    
    printf("%u / %u = %u\n",pid.rg,pid.ph,pid.pid);

    err = xnvme_cmd_pass(xnvme_ctx, buf, BUF_SIZE, nullptr, 0);
    if (err) {
      printf("Failed to perform xNVMe IO command\n");
      return err;
    }
    print_sungjin(xnvme_cmd_pass);
  err = xnvme_queue_drain(xqueue);
  print_sungjin(xnvme_queue_drain);
  if (err < 0) printf("Failed to drain xNVMe queue\n");


/////////////////

    for(int i=0;i<MAX_NR_QUEUE;i++){
        err = xnvme_queue_term(queues_[i]);
        if (err) {
        printf("The [%d]th queue is failed\n", i);
        }
    }
  xnvme_dev_close(dev_);
    return 0;
}

