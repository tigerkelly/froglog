/* Program to receive data for logging to a file. */

#include <stdio.h>      // for printf() and fprintf()
#include <sys/socket.h> // for socket(), connect(), sendto(), and recvfrom()
#include <arpa/inet.h>  // for sockaddr_in and inet_addr()
#include <stdlib.h>     // for atoi() and exit()
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>     // for memset()
#include <signal.h>
#include <unistd.h>     // for close()
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>       // for time()
#include <sys/time.h>   // for settimeofday()
#include <math.h>       // for abs()
#include <pthread.h>       // for abs()
#include <ctype.h>       // for abs()
#include <libpq-fe.h>

#include "ini.h"
#include "strutils.h"

#define null NULL		// I am so use to writing null using java :(

#define FROGLOG_LOG		"froglog"
#define FROGLOG_PORT	12998
#define FROGLOG_PWD		"LetFroglogin2"
#define FROGLOG_DB		"froglog.ini"
#define MILLION			1000000
#define KILOBYTE		1024
#define MAX_MB_NUM		5
// #define MAX_FILE_SIZE	(MAX_MB_NUM * MILLION)
#define BASE_PATH		"/ssd/Froglog"
#define LOG_NAME		"froglog.log"
#define DAYS_KEPT		30
#define MAX_TABLES		32
#define MAX_MSG_SIZE	2048
#define MIN_MSG_SIZE	128
#define DEFAULT_ARCHIVES	5

#define MAXRECVSTRING	1024  // Longest string to receive

typedef struct _table_ {
	char *tableName;
	struct _table_ *next;
} Table;

Table *tables = null;

int maxArchives = 0;
int maxTables = 0;
int postgres_version = 0;
int daysKept = DAYS_KEPT;
PGconn *conn = null;

pthread_t purgeThread;
bool purgeStop = false;
bool autoTableCreate = false;

char *dbUser = NULL;
char *userPassword = NULL;
char *dbName = NULL;
int maxMbNum = MAX_MB_NUM;
int maxMsgSize = MAX_MSG_SIZE;

IniFile *ini = NULL;

FILE *out = NULL;
char basePath[1024];
char logName[64];
char *logIp = NULL;
char filePath[PATH_MAX];
// char backupPath1[PATH_MAX];
// char backupPath2[PATH_MAX];
int repeatCount = 0;
int repeatRow = 0;
char *lastMsg = NULL;
char *lastRow = NULL;
char *msgBuf = NULL;
char *tmpMsgBuf = NULL;

void DieWithError(char *errorMessage);  // External error handling function
void archiveLog(void);
void handleSignal(int sig);
void handleArchive(int sig);
void mkDirs(char *path);
void msgToLog(char *tableName, char *msg, ...);
void msgToDb(char *tableName, char *msg, ...);
void buildTableList();
bool findTableName(char *tableName);
void addTableName(char *tableName);
void *purgeOld(void *param);

int main(int argc, char *argv[]) {
	int sock;                         // Socket
	struct sockaddr_in broadcastAddr; // Broadcast Address
	unsigned short portNum = 0;     // Port
	char recvString[MAXRECVSTRING+1]; // Buffer for received string
	int recvStringLen;                // Length of received string
	char *p = NULL;

	postgres_version = PQlibVersion();

	if (signal(SIGINT, handleSignal) == SIG_ERR) {
		printf("Can't catch SIGINT\n");
    }

    if (signal(SIGTERM, handleSignal) == SIG_ERR) {
		printf("Can't catch SIGTERM\n");
    }

    if (signal(SIGUSR1, handleArchive) == SIG_ERR) {
		printf("Can't catch SIGUSR1\n");
    }

	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			switch(argv[i][1]) {
			case 'p':				// port number
				portNum = atoi(argv[i+1]);
				break;
			case 'K':				// number of days to keep
				daysKept = atoi(argv[i+1]);
				break;
			case 'a':				// IPv4 address
				logIp = strdup(argv[i+1]);
				break;
			case 'A':				// Turn on table create.
				autoTableCreate = true;
				break;
			case 'U':				// User name.
				dbUser = strdup(argv[i+1]);
				break;
			case 'P':				// User password.
				userPassword = strdup(argv[i+1]);
				break;
			case 'D':				// database name.
				dbName = strdup(argv[i+1]);
				break;
			case 'M':				// Max flat file size.
				maxMbNum = atoi(argv[i+1]);
				break;
			case 'm':				// Max message size.
				maxMsgSize = atoi(argv[i+1]);
				break;
			case 'T':
				maxTables = atoi(argv[i+1]);
				break;
			default:
				printf("Usage froglog [-A] [-T numTables] [-U userName] [-P password] [-D dbName] [-M num] [-m maxMsg] [-a ipaddr] [-p portNum] [-K daysKept]\n");
				printf("   -a IPv4 address.\n");
				printf("   -p port number to listen on.\n");
				printf("   -K Number of days to keep in database tables.\n");
				printf("   -A Turn on auto table create.\n");
				printf("   -U DB user name to use. Default froglog\n");
				printf("   -P DB user password.\n");
				printf("   -D DB name to use. Default froglogdb\n");
				printf("   -M Max number of MBs flat log file can be before being archived.  Default 5 MB\n");
				printf("   -m Max message size.  Defaults to %d\n", MAX_MSG_SIZE);
				printf("   -T Max tables in database. Defaults to %d\n", MAX_TABLES);
				exit(1);
				break;
			}
		}
	}

	if (portNum == 0)
		portNum = FROGLOG_PORT;
	
	if (daysKept <= 0)
		daysKept = DAYS_KEPT;
	
	if (dbUser == null)
		dbUser = "froglog";
	if (userPassword == null)
		userPassword = FROGLOG_PWD;

	if (dbName == null)
		dbName = "froglogdb";
	
	if (maxMbNum < 1)
		maxMbNum = MAX_MB_NUM;
	
	if (maxTables <= 0)
		maxTables = MAX_TABLES;
	
	if (maxMsgSize <= 0  || maxMsgSize > 4096) {
		maxMsgSize = MAX_MSG_SIZE;
	}

	lastMsg = (char *)calloc(1, maxMsgSize + 64);
	lastRow = (char *)calloc(1, maxMsgSize + 64);
	msgBuf = (char *)calloc(1, maxMsgSize + 64);
	tmpMsgBuf = (char *)calloc(1, maxMsgSize + 64);

	// Create a best-effort datagram socket using UDP
	if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		DieWithError("socket() failed");
	}

	if (logIp == NULL) {
		p = getenv("FROGLOG_IP");
		if (p != NULL)
			logIp = strdup(p);
		else {
			printf("No FROGLOG_IP environment varaible or -a option given.\n");
			exit(1);
		}
	}

	if (pthread_create(&purgeThread, NULL, purgeOld, NULL) == -1) {
		printf("Error: Can not start 'purgeOld' thread.");
		return -1;
	}

	// Construct bind structure
	memset(&broadcastAddr, 0, sizeof(broadcastAddr));   // Zero out structure
	broadcastAddr.sin_family = AF_INET;                 // Internet address family
	broadcastAddr.sin_addr.s_addr = inet_addr(logIp);  // Any incoming interface
	broadcastAddr.sin_port = htons(portNum);      // Broadcast port

	// printf("IP: %s:%d\n", logIp, portNum);

	// The program maybe started before the network is up and assigned an IP address.
	bool bindFlag = false;
	for(int i = 0; i < 20; i++) {
		// Bind to the broadcast port
		int r = bind(sock, (struct sockaddr *) &broadcastAddr, sizeof(broadcastAddr));

		if (r < 0) {
			if (errno != EADDRNOTAVAIL) {
				fprintf(stderr, "Errno: %d, %s\n", errno, strerror(errno));
				DieWithError("bind failed.");
			} else {
				sleep(10);		// sleep and try again.
			}
		} else {
			bindFlag = true;
			break;
		}
	}

	if (bindFlag == false) {
		DieWithError("Cound not bind to IP address after 2 minutes. Aborting..");
	}


	memset(filePath, 0, sizeof(filePath));
	// memset(backupPath1, 0, sizeof(backupPath1));
	// memset(backupPath2, 0, sizeof(backupPath2));

	strcpy(basePath, BASE_PATH);
	p = getenv("FROGLOG_BASE_PATH");
	if (p != NULL) 
		strcpy(basePath, p);
	
	DIR *dir = opendir(basePath);
	if (dir) {
		closedir(dir);
	} else if (ENOENT == errno) {
		mkDirs(basePath);
	}

	strcpy(logName, LOG_NAME);
	p = getenv("FROGLOG_LOG_NAME");
	if (p != NULL) 
		strcpy(logName, p);

	sprintf(filePath, "%s/%s", basePath, logName);
	// sprintf(backupPath1, "%s/%s.1", basePath, logName);
	// sprintf(backupPath2, "%s/%s.2", basePath, logName);

	// fprintf(stderr, "filePath %s\n", filePath);
	out = fopen(filePath, "a");	// append mode
	if (out == NULL) {
		DieWithError("fopen() failed");
	}

	msgToLog(FROGLOG_LOG, "*** Froglog logging started ***");
	msgToLog(FROGLOG_LOG, "Listening on %s:%d", logIp, portNum);
    msgToLog(FROGLOG_LOG, "Version of libpq: %d", postgres_version);

	char connText[256];

	sprintf(connText, "user=%s dbname=%s", dbUser, dbName);
	// printf("connText: %s\n", connText);

	conn = PQconnectdb(connText);

	if (PQstatus(conn) == CONNECTION_BAD) {
		// fprintf(stderr, "Connect to DB failed. %s\n", PQerrorMessage(conn));
		msgToLog(FROGLOG_LOG, "Connect to DB failed. %s", PQerrorMessage(conn));
		conn = null;
		return 1;
	}

	if (conn != null) {
		buildTableList();

		if (findTableName(FROGLOG_LOG) == false) {
			char sql[256];
			sprintf(sql, "CREATE TABLE %s (ts timestamptz NOT NULL DEFAULT NOW(), logmsg text);", FROGLOG_LOG);

			PGresult *res = PQexec(conn, sql);

			if (PQresultStatus(res) != PGRES_COMMAND_OK) {
				// fprintf(stderr, "Can not create table %s\n", FROGLOG_LOG);
				msgToLog(FROGLOG_LOG, "Can not create table %s", FROGLOG_LOG);
				PQclear(res);
				return 1;
			}

			PQclear(res);
		}
	}

	ini = iniCreate(FROGLOG_DB);
	if (ini != NULL) {
		maxArchives = iniGetIntValue(ini, "System", "maxArchives");
	}
	if (maxArchives <= 0 || maxArchives > 16)
		maxArchives = DEFAULT_ARCHIVES;

	char settings[1024];

	sprintf(settings,
			"Settings: Address: %s, Port: %d, Days: %d, Create: %c, User: %s, DB: %s, MaxSize: %d MB, MaxMsg: %d, MaxTables: %d",
			logIp, portNum, daysKept,
			(autoTableCreate == true ? 'T':'F'),
			dbUser, dbName, maxMbNum, maxMsgSize, maxTables);

	msgToLog(FROGLOG_LOG, settings);
	msgToDb(FROGLOG_LOG, settings);

	bool checkTableName = true;

	for ( ;; ) {
		// Receive a single datagram from the server
		memset(recvString, 0, sizeof(recvString));
		if ((recvStringLen = recvfrom(sock, recvString, MAXRECVSTRING, 0, NULL, 0)) < 0) {
			DieWithError("recvfrom() failed");
		}

		recvString[recvStringLen] = '\0';

		p = strrchr(recvString, '\n');
		if (p != NULL)
			*p = '\0';

		char *sp = strdup(recvString);
		char *tableName = null;

		checkTableName = true;

		// Seperate out the table name from the message.
		char *msg = strchr(sp, ':');
		if (msg != null) {
			*msg++ = '\0';		// point to the message part and null treminate tableName.
			tableName = sp;
		} else {
			// No table name given put it in the default table.
			msg = sp;
			tableName = FROGLOG_LOG;
			checkTableName = false;
		}

		if (strcmp(tableName, "TaBleCreAted") == 0 ||
				strcmp(tableName, "TaBleDeleTed") == 0) {
			buildTableList();
			continue;
		 }

		if (checkTableName == true) {
			if (strlen(tableName) > 62)
				tableName[62] = '\0';		// truncate long table names, Postgresql max is 63.

			if (isalpha(tableName[0]) == 0) {
				// Table names must start with a letter.
				msgToLog(FROGLOG_LOG, "Table names must start with a letter. '%s'", tableName);
				tableName = FROGLOG_LOG;
			}

			if (strncasecmp(tableName, "pg_", 3) == 0) {
				// Table names cannot start with pg_
				msgToLog(FROGLOG_LOG, "Table names cannot start with pg_ '%s'", tableName);
				tableName = FROGLOG_LOG;
			}

			if (strchr(tableName, ' ') != null || strchr(tableName, '\t') != null) {
				// Table names cannot contain whitespaces.
				msgToLog(FROGLOG_LOG, "Table names cannot contain whitespaces '%s'", tableName);
				tableName = FROGLOG_LOG;
			}
		}

		msgToLog(tableName, msg);
		if (conn != NULL)
			msgToDb(tableName, msg);

		size_t size = ftell(out);
		if (size >= (MAX_MB_NUM * MILLION)) {
			archiveLog();
		}

		free(sp);
	}

	if (out != NULL)
		fclose(out);

	close(sock);
	exit(0);
}

void *purgeOld(void *param) {

	PGresult *res = null;
	time_t now;
	bool purgeDone = false;
	char sql[265];

	while(purgeStop == false) {

		time(&now);
		struct tm *tm_struct = localtime(&now);

		int hour = tm_struct->tm_hour;
		if (hour == 0) {
			if (purgeDone == false) {
				purgeDone = true;

				sprintf(sql, "select current_date - %d;", daysKept);
				res = PQexec(conn, sql);

				if (PQresultStatus(res) == PGRES_COMMAND_OK) {
					char *purgeDate = strdup(PQgetvalue(res, 0, 0));

					PQclear(res);

					Table *tp = tables;

					while (tp != null) {
						if (tp->tableName != null) {
							msgToDb(tp->tableName, "Purging records from '%s' back.", purgeDate);
							msgToLog(tp->tableName, "Purging records from '%s' back.", purgeDate);
							sprintf(sql, "delete from %s where ts < '%s';", tp->tableName, purgeDate);

							res = PQexec(conn, sql);

							PQclear(res);
						}
						tp = tp->next;
					}

					free(purgeDate);
				}
			}
		} else {
			purgeDone = false;
		}

		sleep(60);
	}

	return NULL;
}

void archiveLog() {
	fprintf(out, "Archive log, changing to new file.\n");
	fclose(out);

	char from[PATH_MAX];
	char to[PATH_MAX];

	sprintf(from, "%s/froglog.log.%d.zip", BASE_PATH, maxArchives);
	if (access(from, F_OK) != -1)
		remove(from);

	for (int i = (maxArchives - 1); i > 1; i--) {
		sprintf(from, "%s/froglog.log.%d.zip", BASE_PATH, i);
		if (access(from, F_OK) != -1) {
			sprintf(to, "%s/froglog.log.%d.zip", BASE_PATH, (i + 1));
			rename(from, to);
		}
	}

	sprintf(from, "%s/froglog.log", BASE_PATH);
	sprintf(to, "%s/froglog.log.1", BASE_PATH);
	rename(from, to);

	char cmd[256];
	sprintf(cmd, "/usr/bin/zip %s/froglog.log.1", BASE_PATH);
	system(cmd);

	out = fopen(filePath, "a");
	if (out == NULL) {
		DieWithError("2. fopen() failed");
	}
}

#if(0)
void archiveLog() {
	fprintf(out, "Archive log, changing to new file.\n");
	fclose(out);

	if( access( backupPath2, F_OK ) != -1 ) {
		remove( backupPath2);
	}
	if( access(backupPath1, F_OK ) != -1 ) {
		rename(backupPath1, backupPath2);
	}

	if( access( backupPath1, F_OK ) != -1 ) {
		remove( backupPath1);
	}

	if( access(filePath, F_OK ) != -1 ) {
		rename(filePath, backupPath1);
	}

	out = fopen(filePath, "a");
	if (out == NULL) {
		DieWithError("2. fopen() failed");
	}
}
#endif

void DieWithError(char *errMsg) {
	fprintf(stderr, "%s\n", errMsg);

	if (conn != null)
		PQfinish(conn);

	exit(1);
}

void handleArchive(int sig) {

	archiveLog();
}

void mkDirs(char *path) {
    char *args[20];
    char *s = strdup(path);

    int n = parse(s, "/", args, 20);

    char f[PATH_MAX];
    f[0] = '\0';
    for (int i = 0; i < n; i++) {
        strcat(f, "/");
        strcat(f, args[i]);

        mkdir(f, 0777);
    }

    free(s);
}

void msgToLog(char *tableName, char *msg, ...) {
	va_list valist;
	struct tm *tm_info;
	struct timespec ts;
	char tbuf[32];
	char sbuf[32];
	char header[128];

	va_start(valist, msg);

	clock_gettime(CLOCK_REALTIME, &ts);
	tm_info = localtime(&ts.tv_sec);

	strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", tm_info);

	sprintf(sbuf, ".%03ld", (ts.tv_nsec / MILLION));

	strcat(tbuf, sbuf);

	sprintf(header, "%s %s - ", tableName, tbuf);

	int n2 = vsprintf(tmpMsgBuf, msg, valist);
	
	if (tmpMsgBuf[n2 - 1] != '\n') {
		tmpMsgBuf[n2++] = '\n';
		tmpMsgBuf[n2] = '\0';
	}

	if (strcmp(lastMsg, tmpMsgBuf) != 0) {

		if (repeatCount > 0) {
			fwrite(header, 1, strlen(header), out);

			char t[128];
			sprintf(t, "Last Message repeats %d time(s).\n", repeatCount);
			fwrite(t, 1, strlen(t), out);
			fflush(out);
		}

		fwrite(header, 1, strlen(header), out);
		fwrite(tmpMsgBuf, 1, strlen(tmpMsgBuf), out);
		fflush(out);

		strcpy(lastMsg, tmpMsgBuf);

		repeatCount = 0;
	} else {
		repeatCount++;
	}

}

void msgToDb(char *tableName, char *msg, ...) {
	va_list valist;
	PGresult *res = null;
	char sql[2048];
	char buf[1024];

	va_start(valist, msg);

	if (findTableName(tableName) == false) {
		if (autoTableCreate == false) {
			msgToLog(FROGLOG_LOG, "Table creation not allowed. %s", tableName);
			return;
		}

		sprintf(sql, "CREATE TABLE %s (ts timestamptz NOT NULL DEFAULT NOW(), logmsg text);", tableName);

		res = PQexec(conn, sql);

		if (PQresultStatus(res) != PGRES_COMMAND_OK) {
			msgToLog(FROGLOG_LOG, "Can not create table %s", tableName);
			PQclear(res);
			return;
		}

		addTableName(tableName);
	}

	PQclear(res);

	int n = vsprintf(buf, msg, valist);

	if (buf[n - 1] == '\n')		// remove newline
		buf[n - 1] = '\0';

	if (strcmp(lastRow, buf) != 0) {
		if (repeatRow > 0) {
			sprintf(sql, "INSERT INTO %s (logmsg) values ('Last row repeats %d time(s).');", tableName, repeatRow);

			res = PQexec(conn, sql);

			if (PQresultStatus(res) != PGRES_COMMAND_OK) {
				msgToLog(FROGLOG_LOG, "Can not insert into table %s", tableName);
				PQclear(res);
				return;
			}
			PQclear(res);
		}

		sprintf(sql, "INSERT INTO %s (logmsg) values ('%s');", tableName, buf);

		res = PQexec(conn, sql);

		if (PQresultStatus(res) != PGRES_COMMAND_OK) {
			msgToLog(FROGLOG_LOG, "Can not insert into table %s", tableName);
			PQclear(res);
			return;
		}
		PQclear(res);

		strcpy(lastRow, buf);

		repeatRow = 0;
	} else {
		repeatRow ++;
	}
}

void buildTableList() {
	Table *tp = null;
	PGresult *res = null;
	char sql[1024];

	strcpy(sql, "SELECT table_name FROM information_schema.tables WHERE table_schema = 'public';"); 

	res = PQexec(conn, sql);

	if (PQresultStatus(res) != PGRES_TUPLES_OK) {
		msgToLog(FROGLOG_LOG, "Can not get table names");
		PQclear(res);
		return;
	}

	int rows = PQntuples(res);

	if (tables != null) {
		free(tables);
		tables = null;
	}

	// fprintf(stderr, "rows %d\n", rows);

	for (int i = 0; i < rows; i++) {
		if (tables == null) {
			tables = (Table *)calloc(1, sizeof(Table));
			tp = tables;
		}

		tp->tableName = strdup(PQgetvalue(res, i, 0));

		if ((i + 1) < rows)
			tp->next = (Table *)calloc(1, sizeof(Table));

		tp = tp->next;
	}

	PQclear(res);

}

void addTableName(char *tableName) {
	Table *tp = tables;

	while (tp->next != null) {
		tp = tp->next;		
	}

	tp->next = (Table *)calloc(1, sizeof(Table));
	tp->next->tableName = strdup(tableName);

	// tp->next->next = (Table *)calloc(1, sizeof(Table));
}

bool findTableName(char *tableName) {
	Table *tp = tables;

	while (tp != null) {
		// fprintf(stderr, "tp->tableName %s\n", tp->tableName);
		if (strcmp(tp->tableName, tableName) == 0)
			return true;
		tp = tp->next;
	}

	return false;
}

void handleSignal(int sig) {

    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);

	PQfinish(conn);

	exit(1);
}
