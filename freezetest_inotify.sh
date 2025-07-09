#!/usr/bin/env bash
CGF=/sys/fs/cgroup/freezer/kubepods.slice/\
kubepods-burstable.slice/\
kubepods-burstable-pod81b8e7cc_0436_4d05_80ec_8cf79532f656.slice/\
cri-containerd-9215283e6901d30e8a7826c3fcab4425199f5fc955dac2c168765842f03b34eb.scope
CGU=/sys/fs/cgroup/unified/kubepods.slice/kubepods-burstable.slice/\
kubepods-burstable-pod81b8e7cc_0436_4d05_80ec_8cf79532f656.slice/\
cri-containerd-9215283e6901d30e8a7826c3fcab4425199f5fc955dac2c168765842f03b34eb.scope

# ① freeze 요청 직전 타임스탬프
T_req=$(date +%s%N)

# freeze 요요청
sudo echo FROZEN > $CGF/freezer.state  

# ② cgroup.events 의 frozen 값이 1이 될 때까지 대기
while [ "$(grep '^frozen ' "$CGU/cgroup.events" | cut -d' ' -f2)" != "1" ]; do
    sleep 0.001    # 1 ms 폴링
done

T_done=$(date +%s%N)

printf 'freeze latency = %.3f ms\n' \
       "$(bc <<< "scale=3;($T_done-$T_req)/1000000")"
