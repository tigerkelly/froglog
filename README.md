# froglog
Remote logging system that uses flat file as well as a postgresql database.

I created this program because I wanted a generic logging system for my Raspberry Pi projects.  I do a lot of embedded type projects and was tired of writting the same logging system over and over again.

I personally use a Raspberry Pi 4, 4GB card with an SSD drive to hold the flat file and database.  I use the SSD because the SD card would be too slow and databases tend to eat up SD cards quickly.  Put a fan on it to kept is cool and plug it into your network and now you can access it from all your programs and systems.

The froglog program is **NOT** meant to be use across the internet because you do not want people randomly adding log records to it.  It uses UDP packets and you would have to setup firewall settings to allow them to pass, which is a pain.  If you must go across the internet then use a VPN so that you do not have to set firewall settings.

As I said above it uses UDP packets and it is very simple to use.  The packet format of the message is very flexible.
The messge packet is broken up into 2 parts, table name and a message ie.

	'froglog:Startup of froglog program.'

The first colon ':' in the packet seperates the table name from the message, if the packet does not contain a colon then the message is written to the froglog table.  All messages wheather they have a table name or not are always logged to the flat file.  If an invalid table name is given it is saved to the froglog table.  All table names must follow **_PostgreSQL_** naming rules.

The froglog program can create tables as needed by using the program option -A. If the table does not exist it is created if it is a valid table name.  Tables by default are **NOT** created and it is best that you keep it that way.  I would not use the -A option unless you can control who sends messages or control table names used.

Since the message part is free form then you can create a meassage that has the following format to allow you to catagorize messages.

	'froglog:INFO: Startup of froglog program.'
	'froglog:ERROR: Froglog has failed to start, port in use.'
	'froglog:WARN: No port given using default port 12998.'
	'froglog:P1: Priority one message.'
	...
	The message formats are endless.

With the above message formating you can search the flat file for any errors given by the program but you can use the power of SQL to query the froglog table.

	SQL: SELECT * FROM froglog WHERE logmsg LIKE 'ERROR%' AND DATE(ts) = '2021-04-01';

The above SQL will list all errors that occured on the date given.

The log tables are simple, they contian two fields.

	SQL: CREATE TABLE logName (ts timestamptz NOT NULL DEFAULT NOW(), logmsg text);

The froglog program has **GUI** in another repository written in *Java* using *JavaFX*, see **froglog_gui** to access the froglog tables.

About security, the froglog program and the postgreSQL database should not be accessed over the internet unless you use a VPN.  The way I personally setup access to the progreSQL database is to allow only the local users on the same host as the database to access it.  So if you can not SSH into the host then you do not have access to the database but can still send log messages.

## Usage:

	froglog \[-A\] \[-T numTables\] \[-U userName\] \[-D dbName\] \[-M num\] \[-m maxMsg\]
		\[-a ipaddr\] \[-p portNum\] \[-K daysKept\]
	
	-a IPv4 address.
	-p Port number to listen on.
	-K Number of days to keep in database tables.
	-A Turn on auto table create.
	-U DB user name to use. Default froglog
	-D DB name to use. Default froglogdb
	-M Max number of MBs flat log file can be before being archived.  Default 5 MB
	-m Max message size.  Defaults to 2048
	-T Max tables in database. Defaults to 32

You could use the froglog.ini file to set the above options instead of the command line.  The command line overrides the ini file.

	[System]
		version = 1.0.0
		maxArchives = 5
		portNum = 12998
		ipAddr = 192.168.0.30
		daysKept = 30
		autoTableCreate = false
		dbUser = froglog
		dbName = froglogdb
		maxMbNum = 5
		maxMsgSize = 2048
		maxTables = 32
		adminPassword = sha256_Hex_string
	{End]

The keywords are case sensive.

- maxArchives, controls how many archive files are kept.
- portNum, the port number to listen on.
- ipAddr, IP address of interface to listen on.
- daysKept, number of days to keep in any log table.
- autoTableCreate, Whether to allow auto create of tables.
- dbUser, user name used to access froglog database.
- dbName, name of froglog database.
- maxMbNum, max number of MB of flat file before being archived.
- maxMsgSize, max size, in bytes, a message can be.
- maxTables, max number of tables that can be in database.
- adminPassword, A sha256 sum of the password.

You can send UDP packets from most languages including Bash script.

	To send from Bash use the following format.

	echo -n "tableName:Message" > /dev/udp/192.168.0.30/12998

Please search the internet for code to send UDP packets with the language you are using.

## Manual Install:

As said before I used a **Raspberry Pi 4**, 4GB card with a 128GB SSD card and a USB 3.0 to Sata cable.  But this should work with any Linux like OS and hardware.

	sudo apt-get install postgresql postgresql-contrib libcurl4-openssl-dev gmake zip

### Log into psql and create Froglog DB

	sudo -u postgres psql
	At the postgres=# prompt type:
		show data_directory;
	Save the output to be used below.
	To create the froglog database type:
		create database froglogdb;
	To create the froglog user type:
		create user froglog;
	Grant access to DB type:
		grant all privileges on database froglogdb to froglog;
	Give user a passord:
		alter role froglog with password 'LetFroglogin2';
	Create froglog table;
		create table froglog (ts timestamptz NOT NULL DEFAULT NOW(), logmsg text);
	To quit psql type:
		\q

### Create mount point for SSD drive.

	sudo mkdir -p /ssd/db
	sudo chown pi:pi /ssd/db

### Setup SSD drive and mount it.

These commands will wipe the SSD, format and label it.  **NOTE:** You will lose all data on the SSD.

	sudo wipefs -a /dev/sda
	sudo parted --script /dev/sda mklabel gpt mkpart primary ext4 0% 100%
	sudo mkfs.ext4 -F -t ext4 /dev/sda1
	sudo e2label /dev/sda1 FroglogData
	sudo mount -t ext4 -L FroglogData /ssd

I like labeling the drives so that I can mount them by label instead of device.  I do this because the SSD device name (/dev/sda1) can change based on the other USB drives found when booting up.

	sudo mkdir -p /ssd/Froglog
	sudo chown froglog:froglog /ssd/Froglog
	sudo touch /ssd/Froglog/froglog.log
	sudo chown froglog:froglog /ssd/Froglog/froglog.log

### Edit /etc/fstab so that it is mounted automatically.

	sudo vi /etc/fstab

	Add line.
	LABEL=FroglogData	/ssd	ext4	defaults,noatime 0 2

### Move database

How to move a PostgreSQL database was taken from [here](https://www.digitalocean.com/community/tutorials/how-to-move-a-postgresql-data-directory-to-a-new-location-on-ubuntu-16-04).  The postgreSQL DB installed at the time of this writting was version 11, so change commands below to reflect your version.

	
	sudo systemctl stop postgresql
	sudo systemctl status postgresql
	sudo rsync -av /var/lib/postgresql /ssd/db
	sudo mv /var/lib/postgresql/11/main /var/lib/postgresql/11/main.bak

	sudo vi /etc/postgresql/11/main/postgresql.conf
	data_directory = '/ssd/db/postgresql/11/main'
	
	sudo systemctl start postgresql
	sudo systemctl status postgresql

	sudo rm -rf /var/lib/postgresql/11/main.bak

### Add user froglog

	sudo adduser froglog

### Run froglog program as a service.

Change the froglog.service file to reflect the IP address you want to listen on.

	vi froglog.ini
	Change the ipAddr to match your needs.
	If you local eth0 network address is 192.168.0.23 then make
	sure ipAddr matches it.

Copy file froglog.service to /etc/systemd/system/froglog.service

	sudo cp froglog.service /etc/systemd/system/froglog.service
	sudo systemctl enable froglog.service
	sudo systemctl start froglog.service

#### To get status of froglog service.

	sudo journalctl frolog

The Froglog system has a GUI interface written in JavaFX in the repository **froglog_gui**.  This GUI allows you to query the logs tables as well as purge, create and delete them.

The froglog program will create a table called **froglog** if it does not exist.  By default no other tables exist in the database, so you need to create the log tables of the applications you are using.  Use the froglog_gui program to do this or set the option -A to allow tables to be created on the fly.

Please send me an email if you need help and to let me know what projects you are using this logging system with, thanks.
