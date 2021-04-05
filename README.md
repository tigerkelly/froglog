# froglog
Remote logging system that uses flat file as well as a postgresql database.

I created this program because I wanted a generic logging system for my Raspberry Pi projects.  I do a lot of embedded type projects and was tired of writting the same logging system over and over again.

I personally use a Raspberry Pi 4, 4GB card with an SSD drive to hold the flat file and database.  I use the SSD because the SD card would be too slow and databases tend to eat up SD cards quickly.  Put a fan on iti to kept is cool and plug it into your network and now you can access it from all your programs.

The froglog program is **NOT** meant to be use across the internet because you do not want people randomly adding log records to it.  It uses UDP packets and you would have to setup firewall settings to allow them to pass, which is a pain.  If you must go across the internet then use a VPN so that you do not have to set firewall settings.

As I said above it uses UDP packets and it is very simple to use.  The packet format of the message is very flexible.
The messge packet is broken up into 2 parts, table name and a message ie.

	'froglog:Startup of froglog program.'

The first colon ':' in the packet seperates the table name from the message, if the packet does not contain a colon then the message is written to the froglog table.  All messages wheather they have a table name or not are always logged to the flat file.  If an invalid table name is given it is saved to the froglog table.  All table names must follow *PostgreSQL* naming rules.

The froglog program can create tables as needed by using the program option -A. If the table does not exist it is created if it is a valid table name.  Tables by default are **NOT** created and it is best that you keep it that way.  I would not use the -A option unless you can control who sends messages or control table names used.

Since the message part is free form then you can create a meassage that has the following format to allow you to catagorize messages.
	'*froglog*:INFO - Startup of froglog program.'
	'*froglog*:ERROR - Froglog has failed to start, port in use.'
	'*froglog*:WARN - No port given using default port 12998.'
	'*froglog*:P1: Priority one message.'
	...
	The message formats are endless.

With the above message formating you can search the flat file for any errors given by the program but you can use the power of SQL to query the froglog table.

	**SQL:** SELECT * from froglog where logmsg like 'ERROR%' and date(ts) = '2021-04-01';

The above SQL will list all errors that occured on the date given.

The log tables are simple, they contian two fields.

	**SQL:** Create table logName (ts timestamptz NOT NULL DEFAULT NOW(), logmsg text);

The froglog program has **GUI** in another repository written in *Java* using *JavaFX*, see this GUI to access the froglog tables.

About security, the froglog program and the postgreSQL database should not be accessed over the internet unless you use a VPN.  The way I personally setup access to the progreSQL database is to allow only the local users on the same host as the database to access it.  So if you can not SSH into the host then you do not have access to the database.
