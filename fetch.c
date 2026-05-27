#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

#define RED   "\033[1;31m"
#define GRN   "\033[1;32m"
#define YEL   "\033[1;33m"
#define BLU   "\033[1;34m"
#define MAG   "\033[1;35m"
#define CYN   "\033[1;36m"
#define WHT   "\033[1;37m"
#define BWHT  "\033[1;97m"
#define RST   "\033[0m"

static void get_os_release(const char *key, char *out, size_t outlen) {
    FILE *f = fopen("/etc/os-release", "r");
    if (!f) { snprintf(out, outlen, "Unknown"); return; }
    char line[256];
    size_t klen = strlen(key);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
            char *val = line + klen + 1;
            val[strcspn(val, "\n")] = '\0';
            /* strip surrounding double or single quotes */
            if (val[0] == '"' || val[0] == '\'') {
                char q = val[0]; val++;
                size_t l = strlen(val);
                if (l && val[l-1] == q) val[l-1] = '\0';
            }
            snprintf(out, outlen, "%s", val);
            fclose(f); return;
        }
    }
    fclose(f);
    snprintf(out, outlen, "Unknown");
}

static void capture(const char *cmd, char *out, size_t outlen) {
    FILE *fp = popen(cmd, "r");
    if (!fp) { snprintf(out, outlen, "N/A"); return; }
    if (!fgets(out, outlen, fp)) snprintf(out, outlen, "N/A");
    out[strcspn(out, "\n")] = '\0';
    pclose(fp);
}

int main(void) {
    char os_id[64], pretty_name[128];
    get_os_release("ID",          os_id,       sizeof(os_id));
    get_os_release("PRETTY_NAME", pretty_name, sizeof(pretty_name));

    char kernel[128], arch[64], hostname[256];
    capture("uname -r", kernel,   sizeof(kernel));
    capture("uname -m", arch,     sizeof(arch));
    capture("uname -n", hostname, sizeof(hostname));

    char username[256];
    struct passwd *pw = getpwuid(getuid());
    if (pw) snprintf(username, sizeof(username), "%s", pw->pw_name);
    else    snprintf(username, sizeof(username), "user");

    char shell[128] = "unknown";
    const char *sh = getenv("SHELL");
    if (sh) {
        const char *base = strrchr(sh, '/');
        snprintf(shell, sizeof(shell), "%s", base ? base+1 : sh);
    }

    char uptime_str[64] = "N/A";
    FILE *uf = fopen("/proc/uptime", "r");
    if (uf) {
        char buf[64] = {0}; double secs = 0;
        if (fgets(buf, sizeof(buf), uf)) sscanf(buf, "%lf", &secs);
        fclose(uf);
        long h = (long)secs/3600, m = ((long)secs%3600)/60;
        if (h > 0) snprintf(uptime_str, sizeof(uptime_str), "%ldh %ldm", h, m);
        else       snprintf(uptime_str, sizeof(uptime_str), "%ldm", m);
    }

    char mem_str[64] = "N/A";
    long mem_total=0, mem_avail=0;
    FILE *mf = fopen("/proc/meminfo", "r");
    if (mf) {
        char line[128]; long val;
        while (fgets(line, sizeof(line), mf)) {
            if (sscanf(line, "MemTotal: %ld kB", &val)==1)     mem_total=val;
            if (sscanf(line, "MemAvailable: %ld kB", &val)==1) mem_avail=val;
        }
        fclose(mf);
        snprintf(mem_str, sizeof(mem_str), "%ld / %ld MiB",
                 (mem_total-mem_avail)/1024, mem_total/1024);
    }

    char cpu[256] = "Unknown";
    capture("grep 'model name' /proc/cpuinfo | head -1 | cut -d: -f2 | sed 's/^ *//'",
            cpu, sizeof(cpu));
    const char *trim[] = {" CPU", "(R)", "(TM)"};
    for (int i=0; i<3; i++) {
        char *p;
        while ((p = strstr(cpu, trim[i])))
            memmove(p, p+strlen(trim[i]), strlen(p+strlen(trim[i]))+1);
    }

    char disk[64] = "N/A";
    capture("df -h / | awk 'NR==2{print $3 \" / \" $2}'", disk, sizeof(disk));

    /* packages: try managers in order, use first with count > 0 */
    char pkgs[128] = "N/A";
    char pkgtmp[64];
    struct { const char *cmd; const char *label; } pms[] = {
        { "dpkg-query -f '.\\n' -W 2>/dev/null | wc -l",           "dpkg"    },
        { "rpm -qa --nodigest 2>/dev/null | wc -l",                 "rpm"     },
        { "pacman -Qq 2>/dev/null | wc -l",                         "pacman"  },
        { "apk info 2>/dev/null | wc -l",                           "apk"     },
        { "xbps-query -l 2>/dev/null | wc -l",                      "xbps"    },
        { "qlist -I 2>/dev/null | wc -l",                           "portage" },
        { "eix --installed 2>/dev/null | grep -c '^\\[I\\]'",       "eix"     },
    };
    for (int p = 0; p < (int)(sizeof(pms)/sizeof(pms[0])); p++) {
        capture(pms[p].cmd, pkgtmp, sizeof(pkgtmp));
        long n = atol(pkgtmp);
        if (n > 0) {
            snprintf(pkgs, sizeof(pkgs), "%ld (%s)", n, pms[p].label);
            break;
        }
    }

    /* accent colour by distro */
    const char *accent =
        strstr(os_id,"ubuntu")  ? YEL :
        strstr(os_id,"arch")    ? CYN :
        strstr(os_id,"debian")  ? RED :
        strstr(os_id,"fedora")  ? BLU :
        strstr(os_id,"gentoo")  ? MAG :
        strstr(os_id,"mint")    ? GRN :
        strstr(os_id,"manjaro") ? GRN :
        CYN;

    /* separator */
    int seplen = (int)strlen(username) + 1 + (int)strlen(hostname);
    char sep[384] = {0};
    int si = 0;
    for (int s = 0; s < seplen && si < (int)sizeof(sep)-3; s++) {
        sep[si++] = '\xe2'; sep[si++] = '\x94'; sep[si++] = '\x80';
    }

    const char *bar =
        RED"█"YEL"█"GRN"█"BLU"█"MAG"█"CYN"█"WHT"█"RST" "
        RED"█"YEL"█"GRN"█"BLU"█"MAG"█"CYN"█"WHT"█"RST;

    printf("\n");
    printf("  "BWHT"%s"RST"@"BWHT"%s"RST"\n", username, hostname);
    printf("  %s%s%s\n", accent, sep, RST);
    printf("  %sOS%s      %s\n",     accent, RST, pretty_name);
    printf("  %sKernel%s  %s\n",     accent, RST, kernel);
    printf("  %sArch%s    %s\n",     accent, RST, arch);
    printf("  %sShell%s   %s\n",     accent, RST, shell);
    printf("  %sUptime%s  %s\n",     accent, RST, uptime_str);
    printf("  %sCPU%s     %s\n",     accent, RST, cpu);
    printf("  %sMemory%s  %s\n",     accent, RST, mem_str);
    printf("  %sDisk%s    %s\n",     accent, RST, disk);
    printf("  %s%s%s\n", accent, sep, RST);
    return 0;
}
