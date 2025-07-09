CG=/sys/fs/cgroup/freezer/kubepods.slice/kubepods-burstable.slice/kubepods-burstable-pod81b8e7cc_0436_4d05_80ec_8cf79532f656.slice/cri-containerd-9215283e6901d30e8a7826c3fcab4425199f5fc955dac2c168765842f03b34eb.scope            # 측정할 cgroup
start=$(date +%s%N)                       # T_req (ns) : 측정 직전 시간
echo FROZEN > $CG/freezer.state           # freeze 요청

# 0.5 ms 간격으로 상태 체크
while [[ $(<$CG/freezer.state) != FROZEN ]]; do
    sleep 500
done

end=$(date +%s%N)                         # T_done
printf 'freeze latency = %.3f ms\n' \
        $(bc <<< "scale=3;($end-$start)/1000000")
