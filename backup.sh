#!/bin/bash

## create /admin/.info/backup.config
# dirs="folder1 folder2"
# dbs="db1 db2"
# swift=1;
#end


if [ ! -e /admin/.info/backup.config ]; then
	echo "Backup not enabled";
	exit;
fi

. /admin/.info/backup.config

if [ -f /root/.swift/hostname ]; then
	hostname=`cat /root/.swift/hostname`;
	echo "Overriding hostname at $hostname";
else
	hostname=`hostname`;
fi

if [ -e /etc/master.passwd ]; then
	find=/usr/local/bin/gfind;
	date=/usr/local/bin/gdate;
	if [ ! -e /usr/local/bin/gfind ]; then
		echo "Install misc/findutils";
		exit;
	fi
	if [ ! -e /usr/local/bin/gdate ]; then
		echo "Install coreutils";
		exit;
	fi
else
	find=find;
	date=date;
fi

#1 weekly / monthly
#2 folder
function delete()
{
	if [ "$1" = "" -o "$2" = "" ]; then
		echo 'Blank value passed, exiting';
		return;
	fi

	folder="/backup/$1/$2";
	container="$hostname-backup-$1";

	if [ ! -e /backup/$1/$2 ]; then
		echo "/backup/$1/$2 does not exist";
		return;
	fi
}

function delete_weekly_monthly()
{

for del in Weekly Monthly; do
	grep="grep -v "
	for i in $(ls /backup/$del/  | sort -n | tail -n 4); do
        	grep="$grep -e $i"
	done

	oldweeks="$(ls /backup/$del/ | $grep)"
	if [ ! "$oldweeks" = "" ]; then
        	for i in $oldweeks; do
                	echo "Removing Old $del data $i"
                	if [ "$swift" = "1" ]; then
                        	delete $del $i
                	fi
			rm -rf /backup/$del/$i
        	done
	fi
done

}

function getrealfile()
{

		#date is not included 
		passedfile=$1;
                echo "found $passedfile"
                file=`echo $passedfile | cut -d/ -f2`;
                #echo "Found file $file";
                savedfile="$file";


}

function backup()
{

        #Daily/Weekly/Monthly
        backup="$1";

        if [ ! -d /backup/$backup ]; then
                echo "Missing /backup/$backup";
                return;
        fi

        cd /backup/$backup
	
	# put a date so we known when this ran
	fulldate=`date`;
	echo $fulldate > date.txt
	
        container="$hostname-backup-$backup";
	
        /admin/swift/mkdir_p $container --force

        echo "Working on container $container";
	/admin/swift/rsync $container date.txt --put date.txt

if [ "$1" = "Daily" ]; then
    cd /backup/Daily
    todo=`ls | grep -v date.txt | tail -n 1`;
elif [ "$1" = "Weekly" ]; then
    cd /backup/Weekly
    todo=`ls | grep -v date.txt | tail -n 1`;
elif [ "$1" = "Monthly" ]; then
    cd /backup/Monthly
    todo=`ls | grep -v date.txt | tail -n 1`;
fi

		for realfile in `${find} * -name "*.gz" | grep $todo`; do
                #echo "found $realfile"
		getrealfile $realfile
                echo 
		# added in $year-$month-$day
		/admin/swift/rsync $container $realfile --put $savedfile-$year-$month-$day
                /admin/swift/rsync $container $realfile --check $savedfile-$year-$month-$day

		# delete after 45 days
		/admin/swift/deleteafter $container $savedfile-$year-$month-$day 45
                echo
        done

	# old stuff delete 
        olddates='2012 2013 2013 2014 2015';
        for olddate in $olddates; do
                for realold in `/admin/swift/isls $container  | grep "\-${olddate}-"`; do
                        /admin/swift/isrm $container $realold
                done
        done

}

export HOME=/root

year=$(${date} +%Y)
month=$(${date} +%m)
day=$(${date} +%d)
mkdir -p /backup/Monthly
mkdir -p /backup/Weekly
mkdir -p /backup/Daily/$year-$month-$day

for dir in $dirs; do
        dirname="$(echo "$dir" | sed s#"/"#"_"#g)"
        if [ ! -e "/backup/Daily/$year-$month-$day/$dirname.tar.gz" ] || [ -e "/backup/Daily/$year-$month-$day/$dirname.tar.gz.building" ]; then
                echo "Backing Up: $dir"
                touch /backup/Daily/$year-$month-$day/$dirname.tar.gz.building
                tar czf /backup/Daily/$year-$month-$day/$dirname.tar.gz $dir && rm -f /backup/Daily/$year-$month-$day/$dirname.tar.gz.building
        else
                echo "Skipping: $dir"
        fi
done

# skip if blank
# need /root/.my.cnf set
if [ ! "$dbs" = "" ]; then
	for db in $dbs; do
        	mysqldump --single-transaction --quick --skip-extended-insert --add-drop-table --add-drop-database --quote-names -u root $db | gzip -c - > /backup/Daily/$year-$month-$day/$db.sql.gz
        	tar -zcf /backup/Daily/$year-$month-$day/$db.tar.gz /var/lib/mysql/$db
	done

	mysqldump --single-transaction --quick --skip-extended-insert --all-databases --add-drop-table --allow-keywords -u root | gzip -c > /backup/Daily/$year-$month-$day/all_db.sql.gz
fi


# daily delete
if [ -e /admin/.info/backup_evenless ]; then

for i in $(ls /backup/Daily/ | grep -v -e "$year-$month-$day" \
 -e $(${date} -d "1 day ago" +%Y-%m-%d) 
); do
        echo "Removing $i"
        rm -rf /backup/Daily/$i
        if [ "$swift" = "1" ]; then
                delete Daily $i
        fi
done


elif [ -e /admin/.info/backup_less ]; then

for i in $(ls /backup/Daily/ | grep -v -e "$year-$month-$day" \
 -e $(${date} -d "1 day ago" +%Y-%m-%d) \
 -e $(${date} -d "2 day ago" +%Y-%m-%d) 
); do
        echo "Removing $i"
        rm -rf /backup/Daily/$i
        if [ "$swift" = "1" ]; then
                delete Daily $i
        fi
done


else

for i in $(ls /backup/Daily/ | grep -v -e "$year-$month-$day" \
 -e $(${date} -d "1 day ago" +%Y-%m-%d) \
 -e $(${date} -d "2 day ago" +%Y-%m-%d) \
 -e $(${date} -d "3 day ago" +%Y-%m-%d) \
 -e $(${date} -d "4 day ago" +%Y-%m-%d) \
 -e $(${date} -d "5 day ago" +%Y-%m-%d) \
 -e $(${date} -d "6 day ago" +%Y-%m-%d)
); do
        echo "Removing $i"
        rm -rf /backup/Daily/$i
        if [ "$swift" = "1" ]; then
                delete Daily $i
        fi
done

fi


if [ $(${date} +%w) = 0 ]; then
        if [ ! -e "/backup/Weekly/$year-$month-$day" ]; then
                echo "Copying Weekly Backup"
                cp -a /backup/Daily/$year-$month-$day /backup/Weekly/
        fi
fi

if [ "$(ls /backup/Monthly/$year-$month-* 2>/dev/null)" = "" ]; then
                echo "Copying Monthly Backup $year-$month-$day"
                cp -a /backup/Daily/$year-$month-$day /backup/Monthly/
fi

# weekly / monthly delete
delete_weekly_monthly

# swift
if [ "$swift" = "1" ]; then
        if [ -e /backup/Daily ]; then
                backup Daily
        fi

        if [ -e /backup/Weekly ]; then
        backup Weekly
        fi

        if [ -e /backup/Monthly ]; then
        backup Monthly
        fi
fi


