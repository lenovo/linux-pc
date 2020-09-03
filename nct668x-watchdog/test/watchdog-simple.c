#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/watchdog.h>

#define DEFAULT_PING_RATE 1

int main(void)
{
        int fd = open("/dev/watchdog", O_WRONLY);
        int ret = 0;
        int timeouts = 45;
        int timeoutg = 0;
        int pretimeout = 10;
        int pregtimeout = 0;
        int timeleft = 0;
        struct watchdog_info info;
        int flags = 0;
        unsigned int ping_rate = DEFAULT_PING_RATE;

        ret = ioctl(fd, WDIOC_GETSUPPORT, &info);

        if (ret) {
                printf("WDIOC_GETSUPPORT error '%s'\n", strerror(errno));
                close(fd);
                exit(ret);
        }

        ret = ioctl(fd, WDIOC_GETBOOTSTATUS, &flags);
        if (!ret)
                printf("Last boot is caused by: %s.\n",
                       (flags != 0) ? "Watchdog" : "Power-On-Reset");
        else
                printf("WDIOC_GETBOOTSTATUS error '%s'\n", strerror(errno));

        ioctl(fd, WDIOC_SETTIMEOUT, &timeouts);
        ioctl(fd, WDIOC_GETTIMEOUT, &timeoutg);
        printf("The timeout was is %d seconds\n", timeoutg);

        ioctl(fd, WDIOC_SETPRETIMEOUT, &pretimeout);
        ioctl(fd, WDIOC_GETPRETIMEOUT, &pregtimeout);
        printf("The pretimeout was is %d seconds\n", pregtimeout);

        ret = ioctl(fd, WDIOC_GETSUPPORT, &info);
        if (ret) {
                printf("WDIOC_GETSUPPORT error '%s'\n", strerror(errno));
                close(fd);
                exit(ret);
        }

        printf("watchdog_info:\n");
        printf(" identity:\t\t%s\n", info.identity);
        printf(" firmware_version:\t%u\n", info.firmware_version);
        printf(" options:\t\t%08x\n", info.options);

        if (fd == -1) {
                perror("watchdog");
                exit(EXIT_FAILURE);
        }
        while (1) {
                ioctl(fd, WDIOC_GETTIMELEFT, &timeleft);
                printf("The timeleft was is %d seconds\n", timeleft);

                sleep(1);
        }
        close(fd);
        return ret;
}
