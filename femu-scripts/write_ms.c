// #include <libzbd/zbd.h>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
// #include <linux/blkzoned.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <cstring>
#include <errno.h>


// long long ZONE_SIZE;
// long long LBA;
#define TRANSLATE_PAGE(sz) (sz<<12)


int main(int argc, char** argv){
    if(argc<3){
        printf("./a.out (io_n) (pos)\n");
        return 0;
    }
    int io_n=atoi(argv[1]);
    // int zidx=atoi(argv[2]);
    size_t pos=atol(argv[2]);
    // zbd_info info;
    // struct zbd_zone z;
    // uint64_t wp = 0;
    int ret;
    int write_f = open("/dev/nvme0n1",O_WRONLY | O_DIRECT);

    // unsigned int nr_zones;
    // void * zones;
    // ZONE_SIZE=info.zone_size;
    // LBA=info.pblock_size;

    // unsigned int erase_unit=LBA<<6;

    // printf("info zsz : %llu ,LBA %llu\n",ZONE_SIZE,LBA);
    // int ret = zbd_list_zones(write_f,0,info.zone_size*info.nr_zones,ZBD_RO_ALL,
    //         (struct zbd_zone**)&zones,&nr_zones);

    // ret=zbd_report_zones(read_f, 0, ZONE_SIZE, ZBD_RO_ALL, &z, &nr_zones);
    // printf("report zone ret %d ,WP AT %llu, Wfd :%d rfd %d\n",ret,zbd_zone_wp(&z),write_f,read_f);
    ////////////////
    // ret = posix_memalign((void**)&sparse_buffer, sysconf(_SC_PAGESIZE),
    //                 sparse_buffer_sz);

    int dataf=open("./data",O_RDWR);
    if(dataf<0){
        printf("data open fail\n");
    }


    // size_t buf_size=1024*1024;
    char* buf=nullptr;
    ret = posix_memalign((void**)&buf, sysconf(_SC_PAGESIZE),
                TRANSLATE_PAGE(1));
    if(ret){
        printf("posix align error@@ %d\n",ret);
        return 0;
    }
    ret=read(dataf,buf,TRANSLATE_PAGE(1));
    if(ret<=0){
        printf("data read fail\n");
    }

    // ret=zbd_reset_zones(write_f, 0, ZONE_SIZE);
    // if(ret){
    //     printf("reset failed %d\n",ret);
    // }else{
    //     printf("reset success\n");
    // }

    ////////////////
    for(int i=0;i<io_n;i++){
        ret=pwrite(write_f,buf,TRANSLATE_PAGE(1),TRANSLATE_PAGE((1*pos))+ TRANSLATE_PAGE(1)*i);
        if(ret<0){
            printf("pwrite fail %d\n",i);
        }else{
            printf("pwrite sucess %d\n",i);
        }
    }



    // sleep(1);
    // nr_zones =4;
    // ret=zbd_report_zones(read_f, 0, ZONE_SIZE, ZBD_RO_ALL, &z, &nr_zones);
    // printf("after write, zone wp : %llu :: expected :: %lu ret %d\n",zbd_zone_wp(&z),io_size,ret);


    // printf("partial reset do it !! zidx %llu , n %llu\n",range.sector,range.nr_sectors);
    // ioctl(f,BLKPARTIALRESETZONE,&range);

    // ret=zbd_reset_zones(write_f, 0, ZONE_SIZE);
    // if(ret){
    //     printf("reset failed %d\n",ret);
    // }
    // zbd_report_zones(f, 0, ZONE_SIZE, ZBD_RO_ALL, &z, &report,NULL);
    // printf("after partial reset, zone wp : %llu",zbd_zone_wp(&z));
    close(write_f);
    return 0;

}
