#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "libshmlogclient.h"

static int g_requestExit = 0;

void sig_handle(int sig)
{
    fprintf(stderr, "receive signal %d\n", sig);
    g_requestExit = 1;
}

int main(int argc, char *argv[])
{
    static const char *usage = "Usage: dtracetail [options]... [pid]\n"\
            "output the last party of dtrace ring buffer.\n\n"         \
            "options:\n\t-h,--help\n\t-p,--pid <number>\n\t-n,--nonblock\n\t-d,--drop\n";
    static struct option opts[] = {
        {"help", 0, NULL, 'h'},
        {"pid", 1, NULL, 'p'},
        {"nonblock", 0, NULL, 'n'},
        {"drop", 0, NULL, 'd'},
        {NULL, 0, NULL, 0}
    };
    pid_t pid = -1;
    int nonblock = 0, drop_in_emergency = 0;
    int o, ret;
    struct shm_log_client_t client;
    size_t lost;
    int64_t total_read, total_lost, total_lost_cnt, total_drop;
    char buf[1024];

    // command line parse
    opterr = 0;
    while ( (o = getopt_long(argc, argv, ":hp:nd", opts, NULL)) != EOF ) {
        switch ( o ) {
            case 'h':
                puts(usage);
                return 0;
            case 'p':
                if ( sscanf(optarg, "%d", &pid) != 1 || pid <= 0 ) {
                    fprintf(stderr, "Error: invalid pid '%s'!\n", optarg);
                    return 1;
                }
                break;
            case 'n':
                nonblock = 1;
                break;
            case 'd':
                drop_in_emergency = 1;
                break;
            case ':':
                fprintf(stderr, "Error: missing option argument for option '%s'!\n", argv[optind-1]);
                return 1;
            case '?':
                fprintf(stderr, "Error: unknown options '%s'!\n", argv[optind-1]);
                return 1;
            default:
                fprintf(stderr, "Warning: unknown options '%c'!\n", o);
        }
    }
    if ( optind < argc ) {
        pid_t pid2;
        if ( sscanf(argv[optind], "%d", &pid2) != 1 || pid2 <= 0 ) {
            fprintf(stderr, "Error: invalid pid '%s'!\n", argv[optind]);
            return 1;
        }
        if ( pid > 0 ) {
            fprintf(stderr, "Warning: Previous pid '%d' are overwritten!\n", pid);
        }
        pid = pid2;
        if ( (optind+1) < argc ) {
            fprintf(stderr, "Error: unknown argument '%s'!\n", argv[optind+1]);
            return 1;
        }
    }
    if ( pid <= 0 ) {
        fprintf(stderr, "pid is not specify!\n");
        return 1;
    }
    fprintf(stderr, "pid = %d\n", pid);

    // open shm
    ret = shmlogclient_init(pid, &client, nonblock);
    if ( ret < 0 ) {
        fprintf(stderr, "Error: initialize failed! %d:%s\n", errno, strerror(errno));
        return 1;
    }

    // install signal handle
    signal(SIGINT, sig_handle);

    // core loop
    total_read = 0;
    total_lost = 0;
    total_lost_cnt = 0;
    total_drop = 0;
    while ( !g_requestExit ) {

        ret = shmlogclient_read(&client, buf, sizeof(buf), &lost, 1000*500);
        if ( ret < 0 ) {
            if ( ETIMEDOUT == errno ) {
                usleep(1000*10);
            } else  {
                fprintf(stderr, "Error: read message! %d:%s\n", errno, strerror(errno));
                break;
            }
        } else {
            // stat.
            total_read++;
            total_lost += lost;
            if ( lost > 0 ) {
                total_lost_cnt++;
            }
            // output message or drop message to speed up
            if ( drop_in_emergency && (client.remain * 3) > (client.hdr->nmsg * 2) ) {
                // drop some message to speed up processing
                int drop_cnt = 0;
                while ( (client.remain * 3) >= (client.hdr->nmsg) ) {
                    ret = shmlogclient_zerocopy_read(&client, NULL, NULL, &lost, 0);
                    if ( ret < 0 ) {
                        if ( ETIMEDOUT == errno ) {
                            break;
                        } else {
                            fprintf(stderr, "Error: read message! %d:%s\n", errno, strerror(errno));
                            break;
                        }
                    }
                    shmlogclient_zerocopy_free(&client, ret);
                    drop_cnt++;
                    total_read++;
                    total_lost += lost;
                    if ( lost > 0 ) {
                        total_lost_cnt++;
                    }
                }
                if ( ret < 0 && ETIMEDOUT != errno )
                    break;
                fprintf(stderr, "Warning: drop %d messages!\n", drop_cnt);
                total_drop += drop_cnt;
            } else {
                // output message
                if ( ret > 0 ) {
                    fwrite(buf, 1, ret, stdout);
                }
                fwrite("\n", 1, 1, stdout);
            }
        }
    }

    // finish
    shmlogclient_uninit(&client);
    fprintf(stderr, "total read %ld messages, total lost %ld messages in %ld times, total drop %ld messages\n", total_read, total_lost, total_lost_cnt, total_drop);
    return 0;
}
