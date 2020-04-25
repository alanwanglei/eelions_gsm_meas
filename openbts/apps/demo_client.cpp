#include <stdio.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <string.h>

int ber_power[] = {40,35,34,33,32,31,30,29,28,27,26,25,24,23,22,21,20};

////////////////////////////////////////////////////////////////////////////////
// type define
////////////////////////////////////////////////////////////////////////////////
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

//////////////////////////////////////////
// global variable
//////////////////////////////////////////
struct sockaddr_in sa;
int sock;
char target[64] = "127.0.0.1";
int port = 23000;

////////////////////////////////////////////////////////////////////////////////
// functions
////////////////////////////////////////////////////////////////////////////////
bool connect_gsmmeas()
{
	memset(&sa, 0, sizeof(sa));

	// create socket
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		printf("opening socket failed\n");
		return false;
	}

	// destination address
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	if (inet_pton(AF_INET, target, &sa.sin_addr) <= 0) {
		printf("convert target failed\n");
	}

	if (connect(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		printf("connect GSMMeas failed\n");
		return false;
	}
	return true;
}

bool send_gsmmeas(GUI_CMD_PKG *pkg, int len)
{
	if(send(sock, (char *)pkg, len, 0) < 0)
	{
		printf("Send config command to GSMMeas failed. len=%d\n", len);
		return false;
	}
	//printf("send GUI: len=%d:%d, type=%d\n", len, pkg->len, pkg->type);
	return true;
}

bool recv_gsmmeas(GUI_CMD_PKG *pkg)
{
	int nread = 0;
	memset(pkg, 0, sizeof(*pkg));
	nread = recv(sock, &(pkg->len), sizeof(pkg->len), 0);
	if (nread <= 0)
	{
		perror("receive GSMMeas failed.\n");
		return false;
	}
	if (nread == 0)
	{
		printf("GSMMeas connection closed\n");
		return false;
	}
	if (nread != (int) sizeof(pkg->len))
	{
		printf("Partial read of length from GSMMeas, expected %d, got %d\n", (int)sizeof(pkg->len), (int)nread);
		return false;
	}
	if (pkg->len > (int)(sizeof(*pkg)-sizeof(pkg->len)))
	{
		printf("receive GSMMeas length is too long. len=%d\n", pkg->len);
		return false;
	}

	nread = recv(sock, ((char*)pkg)+sizeof(pkg->len), pkg->len, 0);
	if(nread != pkg->len)
	{
		printf("receive GSMMeas failed. len=%d, got %d", pkg->len, (int)nread);
		return false;
	}
	//printf("recv GUI: len=%d:%d, type=%d\n", sizeof(pkg->len)+pkg->len, pkg->len, pkg->type);
	return true;
}

bool send_config()
{
	int len;
	GUI_CMD_PKG pkg;

	memset(&pkg, 0, sizeof(pkg));
	
	pkg.len = sizeof(pkg.type)+sizeof(pkg.cmd_config);
	pkg.type = GUI_CMD_TYPE_CONFIG;
	pkg.cmd_config.ncc 	= 1;
	pkg.cmd_config.bcc	= 1;
	pkg.cmd_config.mcc	= 1;
	pkg.cmd_config.mnc	= 1;
	pkg.cmd_config.ci	= 1;
	pkg.cmd_config.lac	= 1000;
	pkg.cmd_config.rac	= 1;
	pkg.cmd_config.band	= 900;
	pkg.cmd_config.arfcn	= 51;

	len = sizeof(pkg.len) + pkg.len;
	if(!send_gsmmeas(&pkg, len)) return false;

	// receive ACK
	if(!recv_gsmmeas(&pkg))	return false;

	return true;
}

bool send_start_ber(int pwr)
{
	int len;
	GUI_CMD_PKG pkg;

	memset(&pkg, 0, sizeof(pkg));
	
	pkg.len = sizeof(pkg.type)+sizeof(pkg.cmd_start_ber);
	pkg.type = GUI_CMD_TYPE_START_BER;
	pkg.cmd_start_ber.power	= pwr;

	len = sizeof(pkg.len) + pkg.len;
	if(!send_gsmmeas(&pkg, len)) return false;

	// receive BER result
	if(!recv_gsmmeas(&pkg)) return false;
	if(pkg.type != GUI_CMD_TYPE_BER_RESULT)
	{
		printf("receive type error. len=%d, type=%d\n", pkg.len, pkg.type);
		return false;
	}

	printf("BER measure: power=%d, ber=%0.10f, rssi=%0.4f\n", pwr, pkg.cmd_ber_result.ber, pkg.cmd_ber_result.dl_rssi);
	return true;
}

int main(int argc, char *argv[])
{
	if(!connect_gsmmeas()) 	return -1;
	if(!send_config())	return -1;
	
	for(int i = 0; i < sizeof(ber_power)/sizeof(ber_power[0]); i ++)
	{
		if(!send_start_ber(ber_power[i])) return -1;
	}
	printf("test finished\n");
	return 0;
}
