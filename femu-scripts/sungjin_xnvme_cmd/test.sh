sudo /home/femu/fdp_send_sungjin
sudo /home/femu/FDP_ROCKSDB/db_bench \
     --benchmarks="fillrandom,stats" -subcompactions=8 -histogram -max_background_compactions=4 \
     -max_background_flushes=4 -num=83886080 -stream_option=1 \
     --fs_uri=torfs:xnvme:/dev/nvme0n1?be=thrpool > stream_0


sudo /home/femu/fdp_send_sungjin
sleep 30
sudo /home/femu/FDP_ROCKSDB/db_bench \
     --benchmarks="fillrandom,stats" -subcompactions=8 -max_background_compactions=4 \
     -max_background_flushes=4 -num=83886080 -stream_option=1 \
     --fs_uri=torfs:xnvme:/dev/nvme0n1?be=thrpool > stream_1

sudo /home/femu/fdp_send_sungjin
sleep 30
sudo /home/femu/FDP_ROCKSDB/db_bench \
     --benchmarks="fillrandom,stats" -subcompactions=8 -max_background_compactions=4 \
     -max_background_flushes=4 -num=83886080 -stream_option=2 \
     --fs_uri=torfs:xnvme:/dev/nvme0n1?be=thrpool > stream_2