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

char *g_logfile = "/OpenBTS/log/GSMMeas.log";

//////////////////////////////////////
// socket defines
//    _o: for OpenBTS
//    _g: for GUI
//////////////////////////////////////
struct sockaddr_in sa_o, sa_g;
int sock_o = -1, sock_gl = -1, sock_g = -1;
char target_o[64] = "127.0.0.1", target_g[64] = "127.0.0.1";
int port_o = 49300, port_g = 34567;

static char *progname = (char*) "";

const int bufsz = 100000;
char cmdbuf[bufsz];
char resbuf[bufsz];

bool isConnGUI = true; // true: listening socket for GUI. false: read config from files

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

typedef enum
{
	GUI_CMD_TYPE_CONFIG		= 1,
	GUI_CMD_TYPE_START_BER		= 2,
	GUI_CMD_TYPE_ACK		= 101,
	GUI_CMD_TYPE_BER_RESULT		= 102,
} GUI_CMD_ENUM;

typedef struct
{
	int		ncc;
	int		bcc;
	int		mcc;
	int		mnc;
	int		ci;
	int		lac;
	int 		rac;
	int		band;
	int		arfcn;
} GUI_CMD_T_CONFIG;

typedef struct
{
	int		power;
} GUI_CMD_T_START_BER;

typedef struct
{
	int		ret;
} GUI_CMD_T_ACK;

typedef struct
{
	float 		ber;
	float		dl_rssi;
} GUI_CMD_T_BER_RESULT;

typedef struct
{
	int			len;
	GUI_CMD_ENUM		type;
	union
	{
	GUI_CMD_T_CONFIG	cmd_config;
	GUI_CMD_T_START_BER	cmd_start_ber;
	GUI_CMD_T_ACK		cmd_ack;
	GUI_CMD_T_BER_RESULT	cmd_ber_result;
	};
} GUI_CMD_PKG;

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

/////////////////////////////////////////////////////////////
// function define
/////////////////////////////////////////////////////////////
void proc_meas_ber(GUI_CMD_T_START_BER *cmd, GUI_CMD_T_BER_RESULT *res);


void print2log_clear()
{
	FILE *fp;
	fp = fopen(g_logfile, "wt");
	if(fp != NULL) fclose(fp);
}

void print2log(char *str)
{
	FILE *fp;
	fp = fopen(g_logfile, "at");
	if(fp == NULL) return;
	fwrite(str, strlen(str), 1, fp);
	fclose(fp);
}

bool create_gui()
{
	sock_gl = socket(AF_INET, SOCK_STREAM, 0);
	if(sock_gl == -1)
	{
		printf("create listening socket failed.\n");
		return false;
	}

	sa_g.sin_family = AF_INET;
	sa_g.sin_addr.s_addr = htonl(INADDR_ANY);
	sa_g.sin_port = htons(23000);
	bind(sock_gl, (struct sockaddr *)&sa_g, sizeof(sa_g));

	if(listen(sock_gl, 1) != 0)
	{
		printf("listen GUI socket failed. err=%d\n", errno);
		return false;
	}
	return true;
}

bool connect_gui()
{
	sock_g = accept(sock_gl, (struct sockaddr*)NULL, NULL);
	if(sock_g == -1)
	{
		printf("accept GUI socket failed. err=%d\n", errno);
		return false;
	}
	return true;
}

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

bool get_guicmd(GUI_CMD_PKG *cmd)
{
	int nread = 0;

	nread = recv(sock_g, &(cmd->len), sizeof(cmd->len), 0);
	if (nread <= 0)
	{
		perror("receive GUI command failed.\n");
		return false;
	}
	if (nread == 0)
	{
		printf("GUI connection closed\n");
		return false;
	}
	if (nread != (int) sizeof(cmd->len))
	{
		printf("Partial read of length from GUI, expected %d, got %d\n", (int)sizeof(cmd->len), (int)nread);
		return false;
	}
	if (cmd->len > (int)(sizeof(*cmd)-sizeof(cmd->len)))
	{
		printf("receive GUI command is too long. len=%d\n", cmd->len);
		return false;
	}

	nread = recv(sock_g, ((char*)cmd)+sizeof(cmd->len), cmd->len, 0);
	if(nread != cmd->len)
	{
		printf("receive GUI command failed. len=%d, got %d", cmd->len, (int)nread);
		return false;
	}
	//printf("recv GUI: len=%d:%d, type=%d\n", sizeof(cmd->len)+cmd->len, cmd->len, cmd->type);
	return true;

}

bool send_gui(GUI_CMD_PKG *cmd)
{
	int len;
	if(sock_g == -1 && cmd == NULL) return false;

	cmd->len = sizeof(cmd->type);
	switch(cmd->type)
	{
	case GUI_CMD_TYPE_CONFIG:	cmd->len += sizeof(GUI_CMD_T_CONFIG); break;
	case GUI_CMD_TYPE_START_BER:	cmd->len += sizeof(GUI_CMD_T_START_BER); break;
	case GUI_CMD_TYPE_ACK:		cmd->len += sizeof(GUI_CMD_T_ACK); break;
	case GUI_CMD_TYPE_BER_RESULT:	cmd->len += sizeof(GUI_CMD_T_BER_RESULT); break;
	default: printf("send GUI command error. type=%d\n", cmd->type); return false;
	}
	len = sizeof(cmd->len) + cmd->len;

	if (send(sock_g, cmd, len, 0) < 0)
	{
		printf("send command to GUI failed. type=%d, len=%d\n", cmd->type, len);
		return false;
	}
	//printf("send GUI: len=%d:%d, type=%d\n", len, cmd->len, cmd->type);
	return true;
}

bool proc_guicmd(GUI_CMD_PKG *cmd)
{
	GUI_CMD_PKG res;

	memset(&res, 0, sizeof(res));

	switch(cmd->type)
	{
	case GUI_CMD_TYPE_CONFIG:
		res.type = GUI_CMD_TYPE_ACK;
		res.cmd_ack.ret		= 0;
		break;
	case GUI_CMD_TYPE_START_BER:
		proc_meas_ber(&cmd->cmd_start_ber, &res.cmd_ber_result);
		res.type = GUI_CMD_TYPE_BER_RESULT;
		break;
	default:
		return false;
	}

	return send_gui(&res);
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

void proc_meas_ber(GUI_CMD_T_START_BER *cmd, GUI_CMD_T_BER_RESULT *res)
{
	char * str;
	int tid;
	float ul_ber, dl_ber, dl_rssi, ul_pwr;
	char search_str[1024];

	printf("BER measure with power %d\n", cmd->power);

	// restart BTS
//	DO_CMD("power 80 80");
//	DO_CMD("shutdown 1");
	sprintf(cmdbuf, "power %d %d", 80-cmd->power, 80-cmd->power);
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
		printf("Found call tid=%d, txpower=%d\n", tid, cmd->power);
		break;
	}

	// hold call
	print2log("################################\n");
	for(int i = 0; i < 20; i ++)
	{
		sleep(1);

		// get BER
		DO_CMD("chans -a -l -tab");
		print2log("--------------------------------\n");
		print2log(resbuf);
		print2log("\n");
	}

	sprintf(search_str, "TCH/F\tT%d\tLinkEstablished\tfalse\t", tid);
	str = strstr(resbuf, search_str);
	if(str == NULL) {
		printf("Error: Can't get channel informations: txpower=%d\n", cmd->power);
		printf("Retry BER measure with power %d\n", cmd->power);
		continue;
	}
	str = str + strlen(search_str);
	sscanf(str, "%*f\t%*f\t%*f\t%*f\t%*f\t%f\t%*f\t%f\t%*f\t%f\t%f\t", &ul_ber, &dl_rssi, &ul_pwr, &dl_ber);
	
	res->ber 	= dl_ber;
	res->dl_rssi 	= dl_rssi;
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
	printf("BER informations: PWR=%3d, UL_BER=%2.10f%%, DL_BER=%2.10f%%, UL_PWR=%2.1f, DL_RSSI=%2.1f\n", cmd->power, ul_ber, dl_ber, ul_pwr, dl_rssi);
}

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
        printf("You will need to restart GSMMeas after it restarts.\n");
        return false;
    }
    if (strcmp("shutdown", cmd) == 0)
    {
        printf("OpenBTS has been shut down or restarted.\n");
        printf("You will need to restart GSMMeas after it restarts.\n");
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
	print2log_clear();

	std::string sCommand("");
	progname = argv[0];
	argc--; argv++;			// Skip program name.
	while(argc > 0) {
		if (argv[0][0] == '-') {
			switch(argv[0][1]) {
				case 'l': // read local config file, don't create socket for GUI interface
					isConnGUI = false;
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

	// create socket for GUI interface
	if(isConnGUI) create_gui();

	// connect OpenBTS
	if(!connect_openbts()) exit(1);

	if(isConnGUI)
	{
		while(1)
		{
			// connect GUI
			if(!connect_gui()) continue;

			// command process
			while(1)
			{
				GUI_CMD_PKG cmd;
				memset(&cmd, 0, sizeof(cmd));
				if(!get_guicmd(&cmd))
				{
					printf("get GUI command failed. close GUI socket...");
					close(sock_g);
					sock_g = -1;
					break;
				}
				proc_guicmd(&cmd);
			}
		}
	}
	else
	{
		printf("Initial ...\n");
		proc_init();

		printf("BER measure start\n");
		for(int txpwr = 80; txpwr > 0; txpwr -= 5) {
			GUI_CMD_PKG cmd, res;
			cmd.cmd_start_ber.power = txpwr;
			proc_meas_ber(&cmd.cmd_start_ber, &res.cmd_ber_result);
		}
		printf("BER measure finished\n");
	}

        while (1)
        {
                char *cmd = read_cfg_file();
                if (!cmd) continue;
                        if (cmd[0] == '\0') continue;

printf("COMMAND: %s\n", cmd);
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
                            if (sd)
                                break;
                            continue;
                        //}
                    }
                }
//                free(cmd);
        }

	close(sock_o);
}
