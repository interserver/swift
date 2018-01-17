#!/bin/bash

# we do not support incremental

if [ ! -f /etc/cpbackup.conf ]; then
	echo 'Missing /etc/cpbackup.conf';
	exit;
fi

# folders in format of hostname/[daily,weekly,monthly]
hostname=`hostname`;


function run_backup()
{

        dir=$1;
        container="$hostname-$dir";

        echo "Running sync on $dir";
        if [ ! -d "$backupdir/cpbackup/$dir" ]; then
                echo "$backupdir/cpbackup/$dir does not exit";
                return;
        fi
        /admin/swift/mkdir_p $container --force
        cd $backupdir/cpbackup/$dir
	fulldate=`date`;
        echo $fulldate > date.txt
        for i in *.gz; do
                echo
                echo "Working on user $i for $dir";
                /admin/swift/rsync $container $i --put
                /admin/swift/rsync $container $i --check
                echo
        done
	/admin/swift/rsync $container date.txt --put date.txt
}

# check if enabled
if [ `grep ^BACKUPENABLE /etc/cpbackup.conf | cut -d" " -f2` = "yes" ]; then

# check backup type (No incremental)
        if [ ! `grep ^BACKUPTYPE /etc/cpbackup.conf | cut -d" " -f2` = "normal" ]; then
                echo 'Support not available for incremental backups';
                exit;
        fi

        # grab variables
        daily=`grep ^BACKUPRETDAILY /etc/cpbackup.conf | cut -d" " -f2`;
        weekly=`grep ^BACKUPRETWEEKLY /etc/cpbackup.conf | cut -d" " -f2`;
        monthly=`grep ^BACKUPRETMONTHLY /etc/cpbackup.conf | cut -d" " -f2`;
        backupdir=`grep ^BACKUPDIR /etc/cpbackup.conf | cut -d" " -f2`;

        echo "Found backupdir: $backupdir daily: $daily weekly: $weekly monthly: $monthly";

        # error checking
        if [ ! -d "$backupdir/cpbackup" ]; then
                echo "$backupdir/cpbackup does not exist, is $backupdir not mounted?";
                exit;
        fi

        if [ "$daily" = "1" ]; then
                run_backup daily
        fi

        if [ "$weekly" = "1" ]; then
                run_backup weekly
        fi

        if [ "$monthly" = "1" ]; then
                run_backup monthly
        fi
else
        echo 'cpanel backups are not enabled in /etc/cpbackup.conf';
fi

