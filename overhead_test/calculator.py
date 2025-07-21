#!/usr/bin/env python3
import re, sys, statistics as st

PAT = re.compile(
    r'gap\s+([\d.]+)\s*ms.*?expected\s+([\d.]+)\s*ms',
    re.I | re.S
)

print('칸 idx │   N  │ gap 평균(ms) │  exp (ms)   │ (gap/exp) 평균')
print('───────┼──────┼──────────────┼─────────────┼────────────────')

blocks = sys.stdin.read().split('=' * 34)

for idx, blk in enumerate(blocks, 1):
    gaps, exps, ratios = [], [], []

    for gap_s, exp_s in PAT.findall(blk):
        gap = float(gap_s)
        exp = float(exp_s)

        gaps.append(gap)
        exps.append(exp)
        if exp > 0:
            ratios.append(100 * gap / exp)

    if not gaps:
        continue

    gap_avg = st.fmean(gaps)
    exp_avg = st.fmean(exps)
    ratio_col = f'{st.fmean(ratios):9.3f} %' if ratios else '         -'

    print(f'{idx:7d} │ {len(gaps):4d} │ {gap_avg:12.6f} │ {exp_avg:11.6f} │ {ratio_col:>16}')
