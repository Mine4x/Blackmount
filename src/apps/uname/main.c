#include <unistd.h>
#include <stdio.h>
#include <string.h>
#define VERSION "1.0.0"

int main(int argc, char **argv)
{
    struct utsname buf;
    if (uname(&buf) != 0)
        return -1;

    if (argc < 2)
    {
        printf("%s\n", buf.sysname);
        return 0;
    }

    int show_sysname = 0, show_nodename = 0, show_release = 0;
    int show_version = 0, show_machine = 0, show_domainname = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--help") == 0)
        {
            printf("Usage: %s [OPTION]\n", argv[0]);
            printf("Print system information.\n\n");
            printf("  -a, --all             print all information\n");
            printf("  -s, --kernel-name      print the kernel name\n");
            printf("  -n, --nodename         print the network node hostname\n");
            printf("  -r, --kernel-release   print the kernel release\n");
            printf("  -v, --kernel-version   print the kernel version\n");
            printf("  -m, --machine          print the machine hardware name\n");
            printf("      --version          print program version\n");
            printf("      --help             display this help and exit\n");
            return 0;
        }
        else if (strcmp(argv[i], "--version") == 0)
        {
            printf("BM Uname v%s\n", VERSION);
            return 0;
        }
        else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0)
        {
            show_sysname = show_nodename = show_release = 1;
            show_version = show_machine = show_domainname = 1;
        }
        else if (strcmp(argv[i], "--kernel-name") == 0)
            show_sysname = 1;
        else if (strcmp(argv[i], "--nodename") == 0)
            show_nodename = 1;
        else if (strcmp(argv[i], "--kernel-release") == 0)
            show_release = 1;
        else if (strcmp(argv[i], "--kernel-version") == 0)
            show_version = 1;
        else if (strcmp(argv[i], "--machine") == 0)
            show_machine = 1;
        else if (argv[i][0] == '-' && argv[i][1] != '-')
        {
            for (int j = 1; argv[i][j] != '\0'; j++)
            {
                switch (argv[i][j])
                {
                    case 's': show_sysname    = 1; break;
                    case 'n': show_nodename   = 1; break;
                    case 'r': show_release    = 1; break;
                    case 'v': show_version    = 1; break;
                    case 'm': show_machine    = 1; break;
                    case 'a':
                        show_sysname = show_nodename = show_release = 1;
                        show_version = show_machine = show_domainname = 1;
                        break;
                    default:
                        printf("%s: invalid option -- '%c'\n", argv[0], argv[i][j]);
                        return 1;
                }
            }
        }
        else
        {
            printf("%s: unrecognized option '%s'\n", argv[0], argv[i]);
            return 1;
        }
    }

    if (show_sysname)    printf("%s ", buf.sysname);
    if (show_nodename)   printf("%s ", buf.nodename);
    if (show_release)    printf("%s ", buf.release);
    if (show_version)    printf("%s ", buf.version);
    if (show_machine)    printf("%s ", buf.machine);
    if (show_domainname) printf("%s ", buf.domainname);

    printf("\n");

    return 0;
}