#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#include <pwd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include "libshmlogclient.h"

#define SHM_FILE_PATH "/dev/shm"

static int g_requestExit = 0;

void sig_handle(int sig)
{
    fprintf(stderr, "receive signal %d\n", sig);
    g_requestExit = 1;
}

struct process_info_t {
    pid_t pid;
    uid_t uid;
    char username[16];
    char exe[256];
    char cmdline[256];
};

int get_process_info(pid_t pid, struct process_info_t *info)
{
    char filename[256], content[4096], *line;
    struct stat statbuf;
    struct passwd *pwd;
    int fd, len, uid, i;
    // process exist ?
    snprintf(filename, sizeof(filename), "/proc/%d", pid);
    if ( stat(filename, &statbuf) == -1 && ENOENT == errno ) {
        errno = ESRCH; // no such process
        return -1;
    }
    memset(info, 0, sizeof(struct process_info_t));
    info->pid = pid;
    // get user name
    info->uid = (uid_t)-1;
    memset(info->username, 0, sizeof(info->username));
    do {
        snprintf(filename, sizeof(filename), "/proc/%d/status", pid);
        fd = open(filename, O_RDONLY);
        if ( fd < 0 || (len = read(fd, content, sizeof(content)-1)) < 0 ) {
            fprintf(stderr, "Error: read status file \"%s\", %d:%s\n", filename, errno, strerror(errno));
            break;
        }
        content[len] = '\0';
        close(fd);
        line = strstr(content, "\nUid:");
        if ( NULL == line ) {
            fprintf(stderr, "Error: Uid not found in status file!\n");
            break;
        }
        if ( sscanf(line, "\nUid:\t%d", &uid) != 1 || uid < 0 || uid >= 65535 ) {
            fprintf(stderr, "Error: can not get uid!\n");
            break;
        }
        info->uid = uid;
        pwd = getpwuid(uid);
        if ( NULL == pwd ) {
            snprintf(info->username, sizeof(info->username)-1, "Uid:%d", uid);
        } else {
            strncpy(info->username, pwd->pw_name, sizeof(info->username)-1);
        }
        info->username[sizeof(info->username)-1] = '\0';
    } while ( 0 );
    // gets the full path of the execution file
    memset(info->exe, 0, sizeof(info->exe));
    do {
        snprintf(filename, sizeof(filename), "/proc/%d/exe", pid);
        len = readlink(filename, content, sizeof(content)-1);
        if ( len < 0 ) {
            fprintf(stderr, "Error: read link \"%s\", %d %s\n", filename, errno, strerror(errno));
            break;
        }
        content[len] = '\0';
        strncpy(info->exe, content, sizeof(info->exe)-1);
        info->exe[sizeof(info->exe)-1] = '\0';
    } while ( 0 );
    // get command line
    memset(info->cmdline, 0, sizeof(info->cmdline));
    do {
        snprintf(filename, sizeof(filename), "/proc/%d/cmdline", pid);
        fd = open(filename, O_RDONLY);
        if ( fd < 0 || (len = read(fd, content, sizeof(content)-1)) < 0 ) {
            fprintf(stderr, "Error: read cmdline file \"%s\", %d %s\n", filename, errno, strerror(errno));
            break;
        }
        close(fd);
        for ( i = 0; i < (len-1); i++ ) {
            if ( '\0' == content[i] )
                content[i] = ' ';
        }
        content[len] = '\0';
        strncpy(info->cmdline, content, sizeof(info->cmdline)-1);
        info->cmdline[sizeof(info->cmdline)-1] = '\0';
    } while ( 0 );
    return 0;
}

int list()
{
    char size_str[16];
    struct stat statbuf;
    DIR *dir;
    struct dirent *dent;
    int pid, ret;
    dir = opendir(SHM_FILE_PATH);
    if ( NULL == dir ) {
        return -1;
    }
    while ( (dent = readdir(dir)) != NULL ) {
        if ( DT_REG == dent->d_type ) {
            //fprintf(stderr, "\tfind shm: %s, type=0x%x\n", dent->d_name, dent->d_type);
            if ( sscanf(dent->d_name, SHM_FILE_PREFIX "%d", &pid) == 1 && pid > 0 ) {
                // get shared memory size
                ret = fstatat(dirfd(dir), dent->d_name, &statbuf, 0);
                if ( ret < 0 ) {
                    fprintf(stderr, "Error: get shared memory size failed, %d:%s\n", errno, strerror(errno));
                    size_str[0] = '\0';
                } else {
                    int size = (int)statbuf.st_size;
                    if ( size < 1024 ) {
                        snprintf(size_str, sizeof(size_str), "%d", size);
                    } else if ( size < 1024*10 ) {
                        snprintf(size_str, sizeof(size_str), "%.1fK", size/1024.0F);
                    } else if ( size < 1024*1024 ) {
                        snprintf(size_str, sizeof(size_str), "%dK", size/1024);
                    } else if ( size < 1024*1024*10 ) {
                        snprintf(size_str, sizeof(size_str), "%.1fM", size/1048576.0F);
                    } else {
                        snprintf(size_str, sizeof(size_str), "%dM", size/1048576);
                    }
                }
                // get process info
                struct process_info_t info;
                ret = get_process_info(pid, &info);
                if ( ret == -1 && ENOENT == errno ) {
                    printf("Process not found!\n");
                    continue;
                }
                printf("%d  %s  %s  %s  %s\n", pid, size_str, info.username, info.exe, info.cmdline);
            }
        }
    }
    closedir(dir);
    return 0;
}

int info(pid_t pid)
{
    struct shm_log_client_t client;
    int_headtail headtail;
    int_head head, tail;
    int consumer_pid, ret;

    ret = shmlogclient_init(pid, &client, 1);
    if ( ret < 0 ) {
        fprintf(stderr, "Error: initialize failed! %d:%s\n", errno, strerror(errno));
        return 1;
    }

    printf("pid: %d", pid);
    // get process info of pid
    struct process_info_t info;
    ret = get_process_info(pid, &info);
    if ( ret == -1 && ENOENT == errno ) {
        printf("  NotFound!\n");
    } else {
        printf("  %s  %s  %s\n", info.username, info.exe, info.cmdline);
    }

    printf("nmsg: %d\n", client.hdr->nmsg);
    
    consumer_pid = atomic_load(&client.hdr->consumer_pid);
    printf("consumer: %d", consumer_pid);
    if ( consumer_pid > 0 ) {
        ret = get_process_info(consumer_pid, &info);
        if ( ret == -1 && ENOENT == errno ) {
            printf("  NotFound!\n");
        } else {
            printf("  %s  %s  %s\n", info.username, info.exe, info.cmdline);
        }
    } else {
        printf("\n");
    }

    headtail = atomic_load(&client.hdr->headtail);
    head = GET_HEAD(headtail);
    tail = GET_TAIL(headtail);
    printf("head: %d\n", head);
    printf("tail: %d\n", tail);

    shmlogclient_uninit(&client);

    return 0;
}

int main(int argc, char *argv[])
{
    static const char *usage = "Usage: dtracetail [options]... [pid]\n" \
            "output the last party of dtrace ring buffer.\n\n"          \
            "options:\n"                                                \
            "  -h,--help\n"                                             \
            "  -p,--pid <number>\n"                                     \
            "  -b,--block         Start in blocking mode.\n"            \
            "                     Note: log messages are not lost, but write performance may be reduced!\n" \
            "  -d,--drop          Drop some messages to speed up processing When the buffer will be full.\n" \
            "  -l,--list          List the PID of all the processes that open shmlog and exit.\n" \
            "  -i,--info          Displays shmlog information for the specified PID process and exits.\n" \
            "";
    static struct option opts[] = {
        {"help", 0, NULL, 'h'},
        {"pid", 1, NULL, 'p'},
        {"block", 0, NULL, 'b'},
        {"drop", 0, NULL, 'd'},
        {"list", 0, NULL, 'l'},
        {"info", 0, NULL, 'i'},
        {NULL, 0, NULL, 0}
    };
    pid_t pid = -1;
    int block = 0, drop_in_emergency = 0;
    int o, ret;
    struct shm_log_client_t client;
    size_t lost;
    int64_t total_read, total_lost, total_lost_cnt, total_drop;
    char buf[1024];

    // command line parse
    opterr = 0;
    while ( (o = getopt_long(argc, argv, ":hp:bdli:", opts, NULL)) != EOF ) {
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
            case 'b':
                block = 1;
                break;
            case 'd':
                drop_in_emergency = 1;
                break;
            case 'l':
                return list();
            case 'i':
                if ( sscanf(optarg, "%d", &pid) != 1 || pid <= 0 ) {
                    fprintf(stderr, "Error: invalid pid '%s'!\n", optarg);
                    return 1;
                }
                return info(pid);
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
    ret = shmlogclient_init(pid, &client, !block);
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
