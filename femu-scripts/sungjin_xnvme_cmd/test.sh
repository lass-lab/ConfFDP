sudo /home/femu/FDP_ROCKSDB/db_bench \
     --benchmarks=fillrandom -subcompactions=8 -max_background_compactions=4 \
     -max_background_flushes=4 -num=83886080 -stream_option=1 \
     --fs_uri=torfs:xnvme:/dev/nvme0n1?be=thrpool > stream_1
/home/femmu/fdp_send_sungjin
sleep 30
sudo /home/femu/FDP_ROCKSDB/db_bench \
     --benchmarks=fillrandom -subcompactions=8 -max_background_compactions=4 \
     -max_background_flushes=4 -num=83886080 -stream_option=4 \
     --fs_uri=torfs:xnvme:/dev/nvme0n1?be=thrpool > stream_4