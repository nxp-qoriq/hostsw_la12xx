/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2023 NXP
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <l3locking_ioctl.h>

#ifdef DEBUG_CLI
#define pr_debug(...) printf(__VA_ARGS__)
#else
#define pr_debug(...)
#endif

static void
l3_locking_usage(const char *prgname)
{
    printf("%s \n"
           "  --enable: Enable or disable l3 cache locking\n"
           "      When enabled: you need to specify below parameters:\n"
           "       --ways: number of ways to be locked.\n"
           "       --base[0-3]: base address of memory to be locked.\n"
           "                    number of base addresses is decided by 'ways'\n"
           "      When disabled: you need to specify below parameters:\n"
           "       --ways: number of ways to be unlocked.\n",
           prgname);
}


int main(int argc, char *argv[])
{
    int opt, ret, fd;
    int option_index;
    int enable = -1, ways = -1;
    unsigned long base0 = 0, base1 = 0, base2 = 0, base3 = 0;
    struct cache_locking_data args;

    struct option lgopts[] = {
        { "enable", required_argument, NULL, 0},
        { "ways", required_argument, NULL, 1},
        { "base0", required_argument, NULL, 2},
        { "base1", required_argument, NULL, 3},
        { "base2", required_argument, NULL, 4},
        { "base3", required_argument, NULL, 5},
        {NULL, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "", lgopts, &option_index)) != EOF) {
        if((enable == 0) && (ways != -1))
            break;
        switch (opt) {
            case 0:
                enable = atoi(optarg);
                break;
            case 1:
                ways = atoi(optarg);
                break;
            case 2:
                base0 = strtol(optarg, NULL, 16);
                break;
            case 3:
                base1 = strtol(optarg, NULL, 16);
                break;
            case 4:
                base2 = strtol(optarg, NULL, 16);
                break;
            case 5:
                base3 = strtol(optarg, NULL, 16);
                break;
            default:
                l3_locking_usage(argv[0]);
                return -1;
        }
    }

    if((enable == -1) || (ways == -1)) {
        l3_locking_usage(argv[0]);
        return -1;
    }


    fd = open("/dev/l3locking", O_RDONLY);
    if (fd == -1) {
        printf("error open \"l3locking\" device.\n");
        return -1;
    }

    pr_debug("enable:%d, ways:%d, base0:0x%lx, base1:0x%lx, base2:0x%lx, base3:0x%lx\n",
            enable, ways, base0, base1, base2, base3);

    args.enable = enable;
    args.ways = ways;
    args.base0 = base0;
    args.base1 = base1;
    args.base2 = base2;
    args.base3 = base3;

    ret = ioctl(fd, CACHE_LOCKING_IOC, &args);
    if(ret != 0) {
        printf("ioctl failed, %d!\n", ret);
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}
