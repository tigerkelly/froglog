#!/bin/bash
# Generate a key used by froglog to enable table create.

if [ $# -ne 2 ]; then
	echo "Usage: sql.sh outputFile sql_text"
	exit 1
fi

# echo "/usr/bin/psql -d froglogdb -U froglog -H -q -o $1 -c '$2'"
/usr/bin/psql -d froglogdb -U froglog -H -q -o $1 -c "$2"

exit 0
