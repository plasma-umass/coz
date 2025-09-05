// pause_detector.c (lateness-only)
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef NS_PER_SEC
#define NS_PER_SEC 1000000000LL
#endif

static void die(const char *msg) { perror(msg); exit(1); }

static inline int64_t nsec_now_raw(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) die("clock_gettime");
    return (int64_t)ts.tv_sec * NS_PER_SEC + ts.tv_nsec;
}

static void pin_cpu(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set); CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0) die("sched_setaffinity");
    int now = sched_getcpu();
    if (now != cpu) {
        fprintf(stderr, "[warn] pinned cpu=%d but running on cpu=%d\n", cpu, now);
    }
}

static void usage(const char *p) {
    fprintf(stderr,
        "Usage:\n"
        "  %s --cpu N [--period-ns 100000] [--seconds 30] [--csv]\n"
        "    cpu        : 고정할 코어 번호\n"
        "    period-ns  : 턴 주기(ns) (기본 100,000ns = 0.1ms)\n"
        "    seconds    : 수행 시간 (기본 30초)\n"
        "    --csv      : CSV 형식 출력\n", p);
}

int main(int argc, char **argv) {
    int cpu = -1;
    int64_t period_ns = 100000;  // 0.1 ms
    int duration_s = 30;
    int csv = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--cpu") && i+1 < argc) cpu = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--period-ns") && i+1 < argc) period_ns = atoll(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i+1 < argc) duration_s = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--csv")) csv = 1;
        else { usage(argv[0]); return 1; }
    }
    if (cpu < 0 || period_ns <= 0 || duration_s <= 0) { usage(argv[0]); return 1; }

    pin_cpu(cpu);

    if (!csv) {
        printf("# lateness-only mode: cpu=%d period=%.3f us duration=%ds\n",
               cpu, period_ns/1000.0, duration_s);
        printf("# columns: t_ms late_ns cpu tick expected_ns\n");
    } else {
        printf("t_ms,late_ns,cpu,tick,expected_ns\n");
    }

    int64_t t0 = nsec_now_raw();
    int64_t end_at = t0 + (int64_t)duration_s * NS_PER_SEC;

    // 턴 1 목표 시각과 카운터
    int64_t expected = t0 + period_ns;
    long long tick = 1;

    while (1) {
        // 목표 시각까지 바쁜대기
        for (;;) {
            int64_t now = nsec_now_raw();
            if (now >= expected) break;
            asm volatile("pause" ::: "memory");
        }
        int64_t now = nsec_now_raw();
        int this_cpu = sched_getcpu();

        // 목표 대비 늦었으면 기록
        int64_t lateness = now - expected;  // >0이면 지연
        if (lateness > 0) {
            double t_ms = (now - t0) / 1e6;
            if (csv)
                printf("%.3f,%lld,%d,%lld,%lld\n",
                       t_ms, (long long)lateness, this_cpu, tick, (long long)expected);
            else
                printf("%10.3f  late_ns=%lld  cpu=%d  tick=%lld  expected=%lld\n",
                       t_ms, (long long)lateness, this_cpu, tick, (long long)expected);
            fflush(stdout);
        }

        // 다음 목표 시각으로 페이즈 보정 (드리프트 방지)
        do {
            expected += period_ns;
            tick++;
        } while (expected <= now);

        if (now >= end_at) break;

        // 코어 이탈 시 경고 후 종료(선택)
        if (this_cpu != cpu) {
            fprintf(stderr, "[warn] migrated to CPU %d (expected %d). Exiting.\n", this_cpu, cpu);
            break;
        }
    }

    return 0;
}
