////////////////////////////////////////////////////////////////////////////////
// This process will connect OpenBTS and GUI.
// Send GUI command to OpenBTS and send OpenBTS results to GUI
////////////////////////////////////////////////////////////////////////////////

#include <config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <string>
#include <cstring>

#define HAVE_LIBREADLINE


#ifdef HAVE_LIBREADLINE
#  include <readline/readline.h>
#  include <readline/history.h>
#endif

#include "Globals.h"

//////////////////////////////////////
// socket defines
//    _o: for OpenBTS
//    _g: for GUI
//////////////////////////////////////
struct sockaddr_in sa_o, sa_g;
int sock_o = -1, sock_g = -1;
char target_o[64] = "127.0.0.1", target_g[64] = "127.0.0.1";
int port_o = 49300, port_g = 34567;

static char *progname = (char*) "";

const int bufsz = 100000;
char cmdbuf[bufsz];
char resbuf[bufsz];

//////////////////////////////////////
// function define
//////////////////////////////////////
bool doCmd(int fd, char *cmd);
bool connect_openbts();
bool connect_gui();

#define DO_CMD(cmd) \
	if(doCmd(sock_o, cmd) == false) { \
		printf("Error: command \"%s\" failed.\n", cmd); \
		exit(1); }


// wanglei add
typedef enum
{
	CFG_CMD_POWEROFF   = 0,
	CFG_CMD_BAND,
	CFG_CMD_MCC,
	CFG_CMD_MNC,
	CFG_CMD_CI,
	CFG_CMD_LAC,
	CFG_CMD_RAC,
	CFG_CMD_NCC,
	CFG_CMD_BCC,
	CFG_CMD_MSPWRMAX,
	CFG_CMD_POWERON,
} CFG_CMD_ENUM;

typedef struct
{
	int		ncc;
	int		bcc;
	int		mcc;
	int 		mnc;
	int		ci;
	int		lac;
	int		rac;
	int		band;
	int		start;
} config_stru;

bool connect_openbts()
{
	memset(&sa_o, 0, sizeof(sa_o));

	// close socket if opened before
	if(sock_o < 0)
	{
		close(sock_o);
		sock_o = -1;
	}

	// create socket
	sock_o = socket(AF_INET, SOCK_STREAM, 0);
	if (sock_o < 0) {
		perror("opening OpenBTS socket");
		return false;
	}

	// destination address
	sa_o.sin_family = AF_INET;
	sa_o.sin_port = htons(port_o);
	if (inet_pton(AF_INET, target_o, &sa_o.sin_addr) <= 0) {
		perror("unable to convert OpenBTS target to an IP address");
	}

	if (connect(sock_o, (struct sockaddr *)&sa_o, sizeof(sa_o)) < 0) {
		perror("connect OpenBTS socket");
		fprintf(stderr, "Is OpenBTS running?\n");
		return false;
	}

	return true;
}

char *up_trim(char *str)
{
	int i, index = 0;
	for(i = 0; ; i++){
		if(str[i] == ' ') continue;
		if(str[i] == 0) break;
		str[index] = tolower(str[i]);
		index ++;
	}
	str[index] = 0;
	return str;
}

void print_cfg(config_stru cfg)
{
//	printf("CFG:%d,%d,%d,%d,%d,%d,%d,%d,%d\n", cfg.ncc, cfg.bcc, cfg.mcc, cfg.mnc, cfg.ci, cfg.lac, cfg.rac, cfg.band, cfg.start);
}

void parse_cfg(config_stru *cfg, char *str)
{
	int val;
	char *str_val;
	
	up_trim(str);
	str_val = strchr(str, '=');
	if(str_val == NULL) return;
	str_val++;
	val = atoi(str_val);
	if(strncmp(str, "ncc", 3) == 0) {
		cfg->ncc = val;
	}
	else if(strncmp(str, "bcc", 3) == 0) {
		cfg->bcc = val;
	}
	else if(strncmp(str, "mcc", 3) == 0) {
		cfg->mcc = val;
	}
	else if(strncmp(str, "mnc", 3) == 0) {
		cfg->mnc = val;
	}
	else if(strncmp(str, "ci", 2) == 0) {
		cfg->ci = val;
	}
	else if(strncmp(str, "lac", 3) == 0) {
		cfg->lac = val;
	}
	else if(strncmp(str, "rac", 3) == 0) {
		cfg->rac = val;
	}
	else if(strncmp(str, "band", 4) == 0) {
		cfg->band = val;
	}
	else if(strncmp(str, "start", 5) == 0) {
		cfg->start = val;
	}
}

char *read_cfg_file()
{
	static int do_cmd_index = 0;
	static char str[1024];
	static config_stru cfg;

	if(do_cmd_index == 0) {
		// read file
		FILE *fp;
		fp = fopen("/OpenBTS/log/config", "rt");
		if(fp == NULL)
		{
			sleep(1);
			return NULL;
		}
		memset(&cfg, 0, sizeof(cfg));
		while(1) {
			memset(str, 0, sizeof(str));
			if(NULL == fgets(str, sizeof(str)-1, fp)) break;
			parse_cfg(&cfg, str);
		}
		fclose(fp);
	}

	// check start field
	memset(str, 0, sizeof(str));
	if(cfg.start) {
		print_cfg(cfg);
		switch(do_cmd_index){
		case CFG_CMD_POWEROFF:
			sprintf(str, "power 80 80");
			break;
		case CFG_CMD_BAND:
			sprintf(str, "config GSM.Radio.Band 900");
			break;
		case CFG_CMD_MCC:
			sprintf(str, "config GSM.Identity.MCC %d", cfg.mcc);
			break;
		case CFG_CMD_MNC:
			sprintf(str, "config GSM.Identity.MNC %d", cfg.mnc);
			break;
		case CFG_CMD_CI:
			sprintf(str, "config GSM.Identity.CI %d", cfg.ci);
			break;
		case CFG_CMD_LAC:
			sprintf(str, "config GSM.Identity.LAC %d", cfg.lac);
			break;
		case CFG_CMD_RAC:
			sprintf(str, "config GPRS.RAC %d", cfg.rac);
			break;
		case CFG_CMD_BCC:
			sprintf(str, "config GSM.Identity.BSIC.BCC %d", cfg.bcc);
			break;
		case CFG_CMD_NCC:
			sprintf(str, "config GSM.Identity.BSIC.NCC %d", cfg.ncc);
			break;
		case CFG_CMD_MSPWRMAX:
			sprintf(str, "config GSM.MS.Power.Max 39");
			break;
		case CFG_CMD_POWERON:
			sprintf(str, "power 0 10");
			break;
		default:
			printf("Delete config file /OpenBTS/log/config");
			remove("/OpenBTS/log/config");
			cfg.start = 0;
			do_cmd_index = 0;
			return NULL;
		}

		do_cmd_index ++;
	}

	return str;
}

void proc_init()
{
	DO_CMD("config GSM.Radio.RSSITarget -25");
	sleep(1);
	DO_CMD("power 80 80");
	sleep(1);
}

void proc_meas_ber(int txpwr)
{
	char * str;
	int tid;
	float ul_ber, dl_ber, dl_rssi, ul_pwr;
	char search_str[1024];

	printf("BER measure with power %d\n", 80-txpwr);

	// restart BTS
//	DO_CMD("power 80 80");
//	DO_CMD("shutdown 1");
	sprintf(cmdbuf, "power %d %d", txpwr, txpwr);
	DO_CMD(cmdbuf);

while(1) {
	// wait for mobile call
	while(1) {
		memset(resbuf, 0, sizeof(resbuf));
		DO_CMD("calls");
		str = strstr(resbuf, "TranEntry( tid=");
		if(str == NULL || strstr(str, "to=2600") == NULL) {
			sleep(1);
			continue;
		}
		str = strchr(str, '=');
		if(str == NULL) {
			printf("Error: string error 1\n");
			exit(1);
		}

		tid = atoi(str+1);
		printf("Found call tid=%d, txpower=%d\n", tid, 80-txpwr);
		break;
	}

	// hold call about 5s
	sleep(20);

	// get BER
	DO_CMD("chans -a -l -tab");
	sprintf(search_str, "TCH/F\tT%d\tLinkEstablished\tfalse\t", tid);
	str = strstr(resbuf, search_str);
	if(str == NULL) {
		printf("Error: Can't get channel informations: txpower=%d\n", 80-txpwr);
		printf("Retry BER measure with power %d\n", 80-txpwr);
		continue;
	}
	str = str + strlen(search_str) + 1;
	sscanf(str, "%*f\t%*f\t%*f\t%*f\t%*f\t%f\t%*f\t%f\t%*f\t%f\t%*f\t%f\t", &ul_ber, &dl_rssi, &ul_pwr, &dl_ber);
break;
}

	// end call
	sprintf(cmdbuf, "endcall %d", tid);
	DO_CMD(cmdbuf);
	while(1) {
		sleep(1);
		DO_CMD("calls");
		if(strstr(resbuf, "0 transactions in table")) break;
	}
	printf("BER informations: PWR=%3d, UL_BER=%2.10f%%, DL_BER=%2.10f%%, UL_PWR=%2.1f, DL_RSSI=%2.1f\n", 80-txpwr, ul_ber, dl_ber, ul_pwr, dl_rssi);
}
// wanglei add end

static void banner()
{
	static int bannerPrinted = false;
	if (bannerPrinted) return;
	bannerPrinted = true;
	printf("Run GSM measure\n");
}

bool doCmd(int fd, char *cmd) // return false to abort/exit
{
	int nread = 0;

	int len = strlen(cmd);
	int svlen = len;
	len = htonl(len);
	if (send(fd, &len, sizeof(len), 0) < 0) {
		perror("sending stream");
		return false;
	}
	len = svlen;
	if (send(fd, cmd, strlen(cmd), 0) < 0) {
		perror("sending stream");
		return false;
	}
	nread = recv(fd, &len, sizeof(len), 0);
	if (nread < 0) {
		perror("receiving stream");
		return false;
	}
	if (nread == 0) {
		printf("Remote connection closed\n");
		exit(1);
	}
	if (nread != (int) sizeof(len)) {
		printf("Partial read of length from server, expected %d, got %d\n", sizeof(len), (int)len);
		exit(1);
	}
	len = ntohl(len);
	if (len >= bufsz-1) {
		printf("Response of %d bytes is too long\n", len);
		exit(1);
	}
	int off = 0;
	svlen = len;
	while(len != 0) {
		nread = recv(fd,&resbuf[off],len,0);
		if (nread < 0) {
			perror("receiving stream");
			return false;
		}
		if (nread == 0) {
			printf("Remote connection closed\n");
			exit(1);
		}
		off += nread;
		len -= nread;
	}
	nread = svlen;

	if (nread<0) {
		perror("receiving response");
		return false;
	}
	resbuf[nread] = '\0';
    if (strcmp("restart", cmd) == 0)
    {
        printf("OpenBTS has been shut down or restarted.\n");
        printf("You will need to restart OpenBTSCLI after it restarts.\n");
        return false;
    }
    if (strcmp("shutdown", cmd) == 0)
    {
        printf("OpenBTS has been shut down or restarted.\n");
        printf("You will need to restart OpenBTSCLI after it restarts.\n");
        return false;
    }
//    printf("%s\n",resbuf);
    if (nread==(bufsz-1)) {
        printf("(response truncated at %d characters)\n",nread);
    }
	return true;
}

int main(int argc, char *argv[])
{
	bool isBTSDo = false;	// If set, execute one command without prompting, then exit.
	std::string sCommand("");
	progname = argv[0];
	argc--; argv++;			// Skip program name.
	while(argc > 0) {
		if (argv[0][0] == '-') {
			switch(argv[0][1]) {
				case 'd': // OpenBTSDo interface
					isBTSDo = true;
					break;
				case 'p': // TCP Port number
					argc--, argv++;
					port_o = atoi(argv[0]);
					printf("TCP %d\n", port_o);
					break;
				case 't': // target
					argc--, argv++;
					snprintf(target_o, sizeof(target_o)-1, "%s", argv[0]);
					break;
				default:
					perror("Invalid option");
					exit(1);	// NOTREACHED but makes the compiler happy.
			}
			argc--;
			argv++;
		} else {
			perror("Invalid argument");
			exit(1);	// NOTREACHED but makes the compiler happy.
		}
	}

	if (sCommand.c_str()[0] == '\0') {
		banner();
		printf("Connecting to %s:%d...\n", target_o, port_o);
	}

	char prompt[16] = "OpenBTS> ";

	// connect OpenBTS
	if(!connect_openbts()) exit(1);

#ifdef HAVE_LIBREADLINE
	char *history_name = 0;
	if (!isBTSDo)
	{
	    // start console
	    using_history();

	    static const char * const history_file_name = "/.openbts_history";
	    char *home_dir = getenv("HOME");

	    if(home_dir) {
		    size_t home_dir_len = strlen(home_dir);
		    size_t history_file_len = strlen(history_file_name);
		    size_t history_len = home_dir_len + history_file_len + 1;
		    if(history_len > home_dir_len) {
			    if(!(history_name = (char *)malloc(history_len))) {
				    perror("malloc failed");
				    exit(2);
			    }
			    memcpy(history_name, home_dir, home_dir_len);
			    memcpy(history_name + home_dir_len, history_file_name,
			       history_file_len + 1);
			    read_history(history_name);
		    }
	    }
	}
#endif

	printf("Initial ...\n");
	proc_init();

	printf("BER measure start\n");
	for(int txpwr = 0; txpwr < 75; txpwr += 5) {
		proc_meas_ber(txpwr);
	}
	printf("BER measure finished\n");

	if (!isBTSDo)
	    printf("Remote Interface Ready.\n");


        if (sCommand.c_str()[0] != '\0') {
                doCmd(sock_o, (char *)sCommand.c_str());
        } else
            while (1)
            {
#ifdef HAVE_LIBREADLINE
//                char *cmd = readline(isBTSDo ? NULL : prompt);
                char *cmd = read_cfg_file();
                if (!cmd) continue;
                        if (cmd[0] == '\0') continue;
                if (!isBTSDo)
                    if (*cmd) add_history(cmd);
#endif
printf("COMMAND: %s\n", cmd);
                if (!isBTSDo)
                {
                    // local quit?
                    if (strcmp(cmd,"quit")==0) {
                        printf("closing remote console\n");
                        break;
                    }
                            // shutdown via upstart
                    if (strcmp(cmd,"shutdown")==0) {
                        printf("terminating openbts\n");
                        if (getuid() == 0)
                            system("stop openbts");
                        else
                        {
                            printf("If prompted, enter the password you use for sudo\n");
                            system("sudo stop openbts");
                        }
                        break;
                    }
                    // shell escape?
                    if (cmd[0]=='!') {
                        int i = system(cmd+1);
                        if (i < 0)
                        {
                            perror("system");
                        }
                        continue;
                    }
                }
                char *pCmd = cmd;
                while(isspace(*pCmd)) pCmd++; // skip leading whitespace
                if (*pCmd)
                {
                    if (doCmd(sock_o, cmd) == false)
                    {
                        bool sd = false;
                        if (strcmp(cmd,"shutdown")==0)
                            sd = true;
                        else if (strcmp(cmd,"restart")==0)
                            sd = true;
                        free(cmd);
                        //{
                            if (isBTSDo)
                                break;
                            if (sd)
                                break;
                            continue;
                        //}
                    }
                }
//                free(cmd);
                if (isBTSDo)
                    break;
            }

#ifdef HAVE_LIBREADLINE
	if (!isBTSDo)
	{
	    if(history_name)
	    {
		    int e = write_history(history_name);
		    if(e) {
			    fprintf(stderr, "error: history: %s\n", strerror(e));
		    }
		    free(history_name);
		    history_name = 0;
	    }
	}
#endif


	close(sock_o);
}
