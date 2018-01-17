#!/usr/bin/env bash

# 1.03.06.2017

###################################################
#
# Swift openstack wrapper for bash
# John Quaglieri <john@interserver.net>
#
# requires curl / file / md5 / python / bc / sqlite
#
###################################################


#-
# Copyright (c) 2012 InterServer, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of InterServer, Inc nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY INTERSERVER, INC. AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COMPANY OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#/




# to do
# add --delete to rsync
# support fake "folders" in containers
# move binaries to config file
# test all binaries exist
# add in test functions (test md5 / curl etc)
#
# more freebsd support, update sleep
# 
# cat may not be needed in split (however tests were not working initially)
#
###################################################


# binaries
# need to test all these exist

curl="/usr/bin/curl";
md5prog="/usr/bin/md5sum";
FILECOMMAND="/usr/bin/file";
db="/root/.swift/sqlite.db";
bc='/usr/bin/bc';
stat="/usr/bin/stat";
gzip="/bin/gzip";
tar="/bin/tar";
hostprog="/bin/hostname"
splitprog="/admin/swift/include/split";
DDPROG="/bin/dd";

# bw rate limit per second
RATE=8M;

if [ -e /etc/debian_version ]; then
	if [ ! -x /usr/bin/sqlite3 ]; then
		echo 'Installing sqlite3';
		apt-get -y install sqlite3
	fi
fi

sqlite="/usr/bin/sqlite3";

if [ -d /usr/share/doc/centos-release-5 -o `uname -r | cut -d- -f1` = "2.6.18" ]; then
	splitprog="/admin/swift/include/split5";
fi

# for backup system

if [ -x /opt/curl/bin/curl ]; then
	if [ -d /usr/share/doc/centos-release-5 -o `uname -r | cut -d- -f1` = "2.6.18" ]; then
		curl="/opt/curl/bin/curl";
	fi
fi


if [ -x "/root/cpaneldirect/pigz" ]; then
	arch=`uname -m`;
	if [ "$arch" = "x86_64" ]; then
		#echo "Using new split and pigz";
		#splitprog="/root/cpaneldirect/split";
		gzip="/root/cpaneldirect/pigz";
	fi
fi

#ignore ssl  (curl -k)
# $CURLOPTS
ignoressl=1;

# enable / disable debugging
debug=0;


# begin 
# -v is *NOT* needed , -i would be better than for headers anyways, i kept -v on the lines that need it
if [ "$ignoressl" = "1" ]; then
	# progress bar fills up log files
	if [ "$RUN_BY_CRON" = "1" ]; then
		#echo 'RUN_BY_CRON is true no progress bar will be shown';
		CURLOPTS=" -k --limit-rate ${RATE}";
	else
		CURLOPTS=" -k --progress-bar --limit-rate ${RATE}";
	fi
else
	CURLOPTS=" --progress-bar --limit-rate ${RATE}";
fi

# freebsd
# lazy way to do this, will fix, but it was friday and almost time to go
if [ -e /etc/master.passwd ]; then
	if [ ! -e /usr/local/bin/gstat -a ! -e /usr/local/bin/gnustat ]; then
		echo "FreeBSD detected, install coreutils misc/findutils databases/sqlite3 and curl";
		exit;
	fi
	if [ -f /usr/local/bin/gstat ]; then
		stat="/usr/local/bin/gstat";
	elif [ -e /usr/local/bin/gnustat ]; then
		stat="/usr/local/bin/gnustat";
	else
		echo "Can not find coreutils / stat at $LINENO";
		exit;
	fi

	md5prog="/sbin/md5";
	curl="/usr/local/bin/curl";
	sqlite="/usr/local/bin/sqlite3";
	splitprog="/usr/local/bin/gsplit";
	gzip="/usr/bin/gzip";
	tar="/usr/bin/tar";


fi

# auth key from each curl command
function getauthkey()
{

	auth_key=`${curl} -v $CURLOPTS -H "X-Storage-User: ${username}" -H "X-Storage-Pass: ${pass}" $AUTH_URL 2>&1 | grep "^< X-" | grep -v "X-Storage-Token:" | cut -d " " -f2- > /$file`;

}

# check age of auth key
function age() {
   local filename=$1
   local changed=`$stat -c %Y "$filename"`
   local now=`date +%s`
   local elapsed

   let elapsed=now-changed
   echo $elapsed
}


function create_config()
{
	if [ "$HOME" = "" ]; then
		echo "$HOME is not defined";
		exit;
	fi
	mkdir -p $HOME/.swift
	chmod 700 $HOME/.swift

	echo -n "Enter Username (user:name): "
	read config_uname
	echo -n "Enter APIkey/password: "
	read config_pass
	echo -n "Enter auth url (enter for default https://storage-nj.interserver.net:8080/auth/v1.0) "
	read config_authurl

	if [ "$config_uname" = "" ]; then
		echo "username was blank"
		exit;
	elif [ "$config_pass" = "" ]; then
		echo "password was blank"
		exit;
	elif [ "$config_authurl" = "" ]; then
		config_authurl="https://storage-nj.interserver.net:8080/auth/v1.0"
	fi

	if [ ! -e $HOME/.swift/config ]; then
		touch $HOME/.swift/config
		chmod 600 $HOME/.swift/config
		echo "username=\"$config_uname\"" >> $HOME/.swift/config
		echo "pass=\"$config_pass\"" >> $HOME/.swift/config
		echo "AUTH_URL=\"$config_authurl\"" >> $HOME/.swift/config
		exit;
	else
		echo "$HOME/.swift/config already exists";
		exit;
	fi
}

function rundebug() {
	echo "curl: ${curl}";
	echo "curl options: $CURLOPTS "
	echo "Storage url: $STORAGE_URL";
	echo
	echo "Example url: ${curl} $CURLOPTS -o/dev/null -X PUT -H \"X-Object-Manifest: container/filename/\" -H \"X-Auth-Token: ${APIKEY}\" \"$STORAGE_URL/container/filename\" --data-binary ''";
	echo
	/admin/swift/isstat
}

#headers can verify creation
#HTTP/1.1 201 Created will show if created
function makedir()
{

	if [ "$1" = "" ]; then
                echo 'Usage ./ismkdir container';
        else
		CONTAINER="${1}";
		URL=`urlencode "${CONTAINER}"`;
		${curl} $CURLOPTS -X PUT -D - -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/""${URL}"
	fi

}

function delete_after()
{
        if [ "$1" = "" -o "$2" = "" -o "$3" = ""  ]; then
                echo 'Usage ./deleteafter container file XX (number of days)';
        else
                CONTAINER="${1}";
                FILE="${2}";

                TIME=`echo "${3} * 86400" | $bc -l`;
                if [ "$TIME" = "" ]; then
                        echo "Did not get a proper value for time (non integer used) at $LINENO";
			echo "Defaulting to 45 days";
			TIME=3888000;
			sleep 5s;
                fi

		listsplits=`${curl} $CURLOPTS -s -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL"/"${CONTAINER}" 2>&1 | grep "${FILE}/" | grep "/split-"`
		if [ "$listsplits" = "" ]; then
			URL=`urlencode "${CONTAINER}/${FILE}"`;
			delete_after_check=`${curl} $CURLOPTS -X POST -H "X-Delete-After: $TIME" -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/$URL" | grep accepted`;
			if [ ! "${delete_after_check}" = "" ]; then
				echo "Appears we got an error, will retry";
				sleep 2s;
				${curl} $CURLOPTS -X POST -H "X-Delete-After: $TIME" -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/$URL"
			else
				echo 'Accepted command';
			fi
			echo
			
		else
			for SPLITNAME in $listsplits; do
				URL=`urlencode "${CONTAINER}/${SPLITNAME}"`;
				${curl} $CURLOPTS -X POST -H "X-Delete-After: $TIME" -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/$URL"
			done
			# delete the filemanifest too
                        URL=`urlencode "${CONTAINER}/${FILE}"`
                        # need to do delete after and object manifest at the same time
                        ${curl} $CURLOPTS -X POST -H "X-Object-Manifest: ${URL}/" -H "X-Delete-After: $TIME" -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/$URL"

		fi
	fi
}

function backuplvm()
{
	# designed for interserver cloud use
	# it should be done with a snapshot
	if [ "$1" = "" -o "$2" = "" -o "$3" = ""  ]; then
                echo 'Usage ./ddlvm windows49159 (lvm) vps49159 (container) windows49159-april6-2015.img.gz (remote filename)';
		exit;
	else

		if [ ! -x $DDPROG ]; then
		        echo "$DDPROG does not exist";
			exit;
		fi

		if [ -e /etc/master.passwd ]; then
			echo 'No FreeBSD support';
			exit;
		fi

		NAME=$1;
		REMOTEFILENAME=$3;
		CONTAINER=$2;
		
		#SOLUSVM backup
		if [ -d /usr/local/solusvm ]; then
			echo 'Found SolusVM';
			SOLUSVM=`vgdisplay  | grep "VG Name" | awk '{print $3}' | head -n 1`;
			echo "VG name is $SOLUSVM";
			if [ "$SOLUSVM" = "" ]; then
				echo "VG path is blank at $LINENO";
				exit;
			fi
			VZPATH="/dev/${SOLUSVM}";
			if [ -d /dev/${SOLUSVM} ]; then
				echo "Using /dev/${SOLUSVM}";
			else
				echo "/dev/${SOLUSVM} is not a directory";
				exit;
			fi
		else
			VZPATH=/dev/vz;
		fi

		SRIPLVM=`echo "${VZPATH}/${NAME}" | tr -d '\r'`;

		# Check that we exist
	        if [ ! -L ${SRIPLVM} ]; then
	                echo "${SRIPLVM} does not exist";
        	        exit;
        	fi

		check_container_exists ${CONTAINER}

		# split size
		bytes=1024;
		##${DDPROG} bs=4096 conv=noerror if=${SRIPLVM} | gzip -1c | ${splitprog} -d --bytes=${bytes}M --filter="sleep 10s && ${curl} $CURLOPTS -o/dev/null -f -X PUT -T - -H \"X-Auth-Token: ${APIKEY}\" \"$STORAGE_URL/${CONTAINER}/${REMOTEFILENAME}/split-\${FILE}\""
		${DDPROG} bs=4096 conv=noerror if=${SRIPLVM} | gzip -1c | ${splitprog} --verbose -d --bytes=${bytes}M --filter="export WRCONTAINER=${CONTAINER}; export WRREALFILE=${REMOTEFILENAME}; /admin/swift/include/splitwrapper"
		sleep 10s;
		echo "Creating dd manifest file at line $LINENO"
		sleep 10s;
		echo "${curl} $CURLOPTS -o/dev/null -X PUT -H \"X-Object-Manifest: ${CONTAINER}/${REMOTEFILENAME}/\" -H \"X-Auth-Token: ${APIKEY}\" \"$STORAGE_URL/${CONTAINER}/${REMOTEFILENAME}\" --data-binary ''"
		echo
		sleep 2s;

                # get new key
                /bin/rm $file
                getauthkey
                APIKEY=`cat $file | grep ^X-Auth-Token: | cut -d: -f2 | tr -d '\r'`;
                sleep 10s;
                echo "X-Object-Manifest: ${CONTAINER}/${SAVENAME}/"
                echo "X-Auth-Token: ${APIKEY}"
                echo "$STORAGE_URL";
                echo

		${curl} $CURLOPTS -o/dev/null -X PUT -H "X-Object-Manifest: ${CONTAINER}/${REMOTEFILENAME}/" -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/${CONTAINER}/${REMOTEFILENAME}" --data-binary ''
		echo "done ddlvm at line $LINENO"
		echo
		sleep 2s;
		echo 'Fileinfo';
		/admin/swift/fileinfo ${CONTAINER} ${REMOTEFILENAME}
		echo
	fi

}

function backupmysql()
{
	if [ "$1" = "" -o "$2" = "" ]; then
                echo 'Usage ./backupmysql container filename';
                exit;
	fi

	if [ ! -f /root/.my.cnf ]; then
		echo 'No /root/.my.cnf, skipping';
		exit;
	fi

	if [ ! -x /usr/bin/mysqldump ]; then
		echo '/usr/bin/mysqldump not executable or does not exist';
		exit;
	fi

	REMOTEFILENAME=$2;
        CONTAINER=$1;

	check_container_exists ${CONTAINER}
	# split size
        bytes=1024;
	/usr/bin/mysqldump -f --single-transaction --quick --skip-extended-insert --all-databases --add-drop-table --allow-keywords -u root | gzip -1c | ${splitprog} --verbose -d --bytes=${bytes}M --filter="export WRCONTAINER=${CONTAINER}; export WRREALFILE=${REMOTEFILENAME}; /admin/swift/include/splitwrapper"
	sleep 10s;
	echo "Creating manifest at line $LINENO";
	${curl} $CURLOPTS -o/dev/null -X PUT -H "X-Object-Manifest: ${CONTAINER}/${REMOTEFILENAME}/" -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/${CONTAINER}/${REMOTEFILENAME}" --data-binary ''
	sleep 5s;
	${curl} $CURLOPTS -o/dev/null -X PUT -H "X-Object-Manifest: ${CONTAINER}/${REMOTEFILENAME}/" -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/${CONTAINER}/${REMOTEFILENAME}" --data-binary ''
}

function runabackup()
{
	# single file backup or directory which is tarred on the fly
	# delete after 30 days by default
	# auto create dir if needed, based on hostname
	# append the date

	if [ "$1" = "" ]; then
		echo 'Usage ./backup filename';
		echo 'Notes: container will be the server hostname and the file will be called filename-year-month-day';
		exit;
	fi
	
	#get date
	year=$(date +%Y)
	month=$(date +%m)
	day=$(date +%d)


	hostname=`$hostprog`;
	if [ "$hostname" = "" ]; then
		echo "hostname returned a blank value in function runabackup at line $LINENO";
		exit;
	else
		check_container_exists $hostname --force
	fi
	
	JUSTFILE=`basename ${1}`;

	echo "Backup up file ${1} as ${JUSTFILE}-${year}-${month}-${day} to container ${hostname}";

	if [ -f "$1" ]; then 
        	upload ${hostname} ${1} ${JUSTFILE}-${year}-${month}-${day}
	elif [ -d "$1" ]; then
		onthefly ${hostname} ${1} frombackup ${JUSTFILE}-${year}-${month}-${day}
		# delete the fly files after 30 days
		onthefly ${hostname} ${JUSTFILE}-${year}-${month}-${day} deleteafter
	else
		echo "runabackup can only do files or directories";
		exit;
	fi

	echo 'Sleeping 5 seconds to settle upload';
	sleep 5s;
	delete_after ${hostname} ${JUSTFILE}-${year}-${month}-${day} 30
	


}

# test size, if we are 5GB or over display error to use split
function check_size()
{
	# 5368709120 is 5gb in bytes, we are under by 1 byte for swift
	size=5368709119;
	#size=100;

	FILE="${1}";

	if [ "$FILE" = "" ]; then
		echo "File name missing in function check_size at $LINENO";
		exit;
	fi
	
	if [ ! -f $FILE ]; then
		echo "File $FILE does not exist in function check_size at $LINENO";
		exit;
	fi

	# ls is quicker
	if [ -x /bin/ls ]; then
		file_size=`/bin/ls -l $FILE | awk '{print $5}'`;
	elif [ -x /usr/bin/du ]; then
		file_size=`/usr/bin/du $FILE | awk '{print $1}'`;

	else
		echo "No programs to check disk space in check_size at $LINENO";
		exit;
	fi

	if [ "$file_size" -gt "$size" ]; then
		echo "$FILE size is $file_size which is over 5GB. Must use the split function";
		echo "sending split split $2 $FILE $3 $4 $5";
		split $2 $FILE $3 $4 $5
	else
		if [ "$debug" = "1" ]; then
			echo "Found file size $file_size for file $FILE in function check_size";
		fi
	fi
	
}

function display_storage_url()
{
	echo
	echo "Storage URL (pub/private): ${blue}$STORAGE_URL${normal}"
	echo
}

function makepublic()
{
	if [ "$1" = "" ]; then
                echo 'Usage ./mkpub container [optional --remove (to make private)]';
		echo '[optional --dirlist (to allow directory listings)]';
		exit;
	fi


	CONTAINER="${1}";
	if [ "$2" = "--remove" ]; then
		${curl} $CURLOPTS -X PUT -H 'X-Container-Read: \n*' -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/""${CONTAINER}"
		display_storage_url
		exit;
	elif [ "$2" = "--dirlist" ]; then
		${curl} $CURLOPTS -X PUT -H 'X-Container-Read: .r:*,.rlistings' -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/""${CONTAINER}"
		display_storage_url
		exit;
	# default call
	elif [ "$2" = "" ]; then
		${curl} $CURLOPTS -X PUT -H 'X-Container-Read: .r:*' -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/""${CONTAINER}"
        	display_storage_url
	else
		echo 'Invalid call to mkpub';
		display_storage_url
		exit;
	fi

}

function getstats()
{
        if [ "$1" = "" ]; then
                ${curl} -v $CURLOPTS -X HEAD -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL" 2>&1 | grep X-Account | cut -d" " -f2-
                # display api key info
                cat $file
	elif [ "$1" = "/all" ]; then

		bytes=`${curl} -v $CURLOPTS -X HEAD -H "X-Auth-Token: ${APIKEY}" "${STORAGE_URL}" 2>&1 | grep X-Account-Bytes-Used: | cut -d: -f2 | tr -d '\r'`;
                echo "${bytes} / 1000000000" | $bc -l
	else

                CONTAINER=$1;
       	        check_container_exists ${CONTAINER}

		# next three are code reuse, fix on any updates
                # in mb
                if [ "$2" = "mb" ]; then
                        bytes=`${curl} -v $CURLOPTS -X HEAD -H "X-Auth-Token: ${APIKEY}" "${STORAGE_URL}/${CONTAINER}" 2>&1 | grep X-Container-Bytes-Used: | cut -d: -f2 | tr -d '\r'`;
                        # in the future awk 'BEGIN{print int(($bytes / 1000000 )+0.5)}'
                        echo "${bytes} / 1000000" | $bc -l 
		# in gb
		elif [ "$2" = "gb" ]; then
			bytes=`${curl} -v $CURLOPTS -X HEAD -H "X-Auth-Token: ${APIKEY}" "${STORAGE_URL}/${CONTAINER}" 2>&1 | grep X-Container-Bytes-Used: | cut -d: -f2 | tr -d '\r'`;
                        # in the future awk 'BEGIN{print int(($bytes / 1000000 )+0.5)}
			echo "${bytes} / 1000000000" | $bc -l
                else
                        # normal display
                        ${curl} -v $CURLOPTS -X HEAD -H "X-Auth-Token: ${APIKEY}" "${STORAGE_URL}/${CONTAINER}" 2>&1 | grep X-Container | cut -d" " -f2-
                fi
        fi
}

function showfileinfo()
{
	if [ "$1" = "" -o "$2" = "" ]; then
                echo 'Usage ./fileinfo container file';
        else
		CONTAINER=$1;
		check_container_exists ${CONTAINER}
		FILE=$2;
		${curl} -v $CURLOPTS -X HEAD -H "X-Auth-Token: ${APIKEY}" "${STORAGE_URL}/${CONTAINER}/${FILE}" || exit 0
	fi
}

function rcopy
{

	if [ "$1" = "" ]; then
                echo 'Usage ./iscp container file newfile';
        else
                CONTAINER=$1;
		FILE=$2;
		NEWFILE=$3;

		${curl}  $CURLOPTS -X COPY -H "Destination: ${CONTAINER}/${NEWFILE}" -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/${CONTAINER}/${FILE}"
        fi
}


function rmove
{

        if [ "$1" = "" ]; then
                echo 'Usage ./ismv container newcontainer file';
        else
                CONTAINER=$1;
                NEWCONTAINER=$2;
                FILE=$3;

                ${curl}  $CURLOPTS -X COPY -H "Destination: ${NEWCONTAINER}/${FILE}" -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/${CONTAINER}/${FILE}"
		sleep 1s;
		echo
		echo -n 'Delete old file? [y/n]';
		read check
		if [ "$check" = "y" ]; then
			delete ${CONTAINER} ${FILE}
		else
			echo 'Skipping deletion';
		fi
        fi
}

checkforexit18() 
{
	if [ "$1" = "" -o "$2" = "" ]; then
		echo "Did not pass container or filename in function checkforexit18 at LINE $LINENO";
		exit;
	fi
	if [ "$curlexit" = "18" ]; then
		if [ "$debug" = "1" ]; then
			echo "Curl exiting with code 18 retrying";
		fi
		download $1 $2 -c
	else
		if [ "$debug" = "1" ]; then
			echo "Curl exiting with code $curlexit";
		fi
	fi

}

function download()
{
        if [ $# -lt 2 ]; then
                echo 'Usage ./isget <container> <file> [-f] [-out]';
                echo '  -f Optional, force overwrite existing file'
                echo '  -out Optional, output to stdout instead of file'
                return;
        fi
        force=0
        out=0
	continue=0;
        CONTAINER=$1
	if [ "$CONTAINER" = "x" ]; then
		if [ -f /root/.swift/hostname ]; then
        		CONTAINER=`cat /root/.swift/hostname`;
		else
        		CONTAINER=`/bin/hostname`;
		fi
	fi

        shift
        FILE=$1
        shift
        while [ $# -gt 0 ]; do
                if [ "$1" = "-f" ]; then
                        force=1;
                elif [ "$1" = "-out" ]; then
                        out=1;
                elif [ "$1" = "-c" ]; then
			continue=1;
		fi
		
                shift
        done
        if [ -d "${FILE}" ]; then
                echo "Directory with same name exists, can not download";
                return;
        fi
        if [ $out = 0 ]; then
                if [ -f "${FILE}" ]; then
                        if [ $force = 1 ]; then
                                if [ "$debug" = "1" ]; then
                                                echo "Downloading file ${FILE} with force";
                                fi
				${curl} $CURLOPTS -o ${FILE} -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/${CONTAINER}/${FILE}"
				curlexit=`echo $?`;
				checkforexit18 ${CONTAINER} ${FILE}
			elif [ $continue = 1 ]; then
				if [ "$debug" = "1" ]; then
                                                echo "Downloading file ${FILE} with continue";
                                fi
				${curl} $CURLOPTS -C - -o ${FILE} -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/${CONTAINER}/${FILE}"
				curlexit=`echo $?`;
				checkforexit18 ${CONTAINER} ${FILE}
                        else
                                echo "File with same name exists, can not download with out -f or -c";
                                return;
                        fi
		    else
            		${curl} $CURLOPTS -C - -o ${FILE} -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/${CONTAINER}/${FILE}"
			curlexit=`echo $?`;
			checkforexit18 ${CONTAINER} ${FILE}
        	fi
        else
                ${curl} $CURLOPTS -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/${CONTAINER}/${FILE}"
		curlexit=`echo $?`;
		checkforexit18 ${CONTAINER} ${FILE}
        fi
}

#delete file, supports email
function delete()
{

	# curl -D . -X DELETE -H "X-Auth-Token: yourAuthToken" -H "X-Purge-Email: your@email.address"  https://cdn1.clouddrive.com/v1/yourAccountHash/bar/foo.txt
	if [ "$1" = "" -o "$2" = "" ]; then
                echo 'Usage ./isrm container file';
        else
		FILE=$2;
                CONTAINER=$1;
		${curl}  $CURLOPTS -o/dev/null -X DELETE -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/${CONTAINER}/${FILE}"
	fi
}

function rrmdir()
{

	if [ "$1" = "" ]; then
		echo 'Usage ./isdir container [ force ] for none empty containers';
	else

		CONTAINER=$1;
		check_container_exists ${CONTAINER}
		# remove all data
		if [ "$2" = "force" ]; then
			echo -n "DO you really want to remove all data in ${CONTAINER} and remove the container? ";
			read nogoingback
			if [ "$nogoingback" = "y" ]; then
				for filetodelete in `/admin/swift/isls ${CONTAINER}`; do /admin/swift/isrm ${CONTAINER} $filetodelete; done
			else
				echo "Cancelling";
				exit;
			fi
		fi
		${curl} $CURLOPTS  -o/dev/null -X DELETE -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/${CONTAINER}"
        fi

}

function getcontenttype()
{
	FILE=$1;
	if [ ! -e "${FILE}" ]; then
		echo "Failed on getting content type of file ${FILE} at line $LINENO file does not exist";
		exit;
	fi
	${FILECOMMAND} -bi ${FILE}
}

function getmd5()
{
	FILE=$1;
	if [ ! -f "${FILE}" ]; then
		echo "Ran into a problem in getmd5 function, file not found or is a directory";
		exit;
	fi
	if [ -e /etc/master.passwd ]; then
		$md5prog "${FILE}" | awk '{print $4}'
	else
		$md5prog "${FILE}" | awk '{print $1}'
	fi
}

function urlencode()
{

	python -c "import urllib; print urllib.quote('$*')"

}

function getremotemd5()
{
	if [ "$1" = "" -o "$2" = "" ]; then
		echo "Usage ./getmd5 container filename";
		exit;
	fi
	FILE=$2;
	CONTAINER=$1;
	check_container_exists ${CONTAINER}

	# remove final /r
	#encoded_value=$(python -c "import urllib; print urllib.quote('''$value''')")
	URL=`urlencode "${CONTAINER}/${FILE}"`; 
        ${curl} -s $CURLOPTS -I -H "X-Auth-Token: ${APIKEY}" "${STORAGE_URL}/${URL}" | grep ^Etag: | awk '{print $2}' | tr -d '\r'

}

function rsyncfile()
{
	if [ "$1" = "" -o "$2" = "" ]; then
                echo 'Usage ./rsync container file [args]';
		echo 'optional args: --put (upload file) add new file name after --put for rename';
		echo 'optional args: --get (download file)';
		echo 'optional args: --check (checks md5sum for remote / localfile) add new file name after --check for a renamed file [add 24 to just select todays entries in sqlite db]';
		echo 'optional args: --dirput (switch file to directory, and sync directory to remote system - does not support sub directories yet )';
		echo 'optional args: --dirget (switch file to directory, and sync directory to local system) - does not support sub directories yet';
		echo './rsync none none deleteoldsplitover60';
		echo '4th arg in put or dirput add a number to send delete after XX days like ./rsync container filename --put filename 60 or ./rsync container dir --dirput 60';
				
		# end help / function here
		exit;
	fi

	# removed 3/28 can't see why this was here
	# check file size
	# 2 is file, 1 is container 3 is optional
	#check_size $2 $1 $4

	# download
	if [ "$3" = "--get" ]; then
		FILE=$2;
                CONTAINER=$1;
		if [ -d "${FILE}" ]; then
                        echo "File: ${FILE} is a directory, use --dirget";
                        exit;
		#if file does not exist, download and exit
		elif [ ! -f "${FILE}" ]; then
			echo "File: ${FILE} does not exist, downloading";
			download ${CONTAINER} ${FILE}
			exit;
                fi
		localetag=`getmd5 ${FILE}`;
                remoteetag=`getremotemd5 ${CONTAINER} ${FILE}`;
		if [ "$debug" = "1" ]; then
                        echo "Found etagremote: ${remoteetag} and found etaglocal $localetag";
                fi

                if [ "$localetag" = "$remoteetag" ]; then
                        echo "Checksums ${green}match{normal} for container: ${green}${CONTAINER}${normal} file ${green}${FILE}${normal}";
                else
                        download ${CONTAINER} ${FILE} -f
                fi
	# md5 verify
	elif [ "$3" = "--check" ]; then
		FILE=$2;
                CONTAINER=$1;
		if [ ! "$4" = "" ]; then
			REMOTEFILE=$4;
		else
                        REMOTEFILE=`basename $2`;
                        echo "No remote file name passed. We will call this ${cyan}$REMOTEFILE${normal} which matches --put";

		fi
		if [ "$5" = "24" ]; then
			check_size ${FILE} ${CONTAINER} ${REMOTEFILE} --justcheck 24
		else
			check_size ${FILE} ${CONTAINER} ${REMOTEFILE} --justcheck
		fi
		localetag=`getmd5 ${FILE}`;
		remoteetag=`getremotemd5 ${CONTAINER} ${REMOTEFILE}`;

		if [ "$localetag" = "$remoteetag" ]; then
			echo "Checksum $localetag ${green}matches${normal} for ${green}${REMOTEFILE}${normal} ";
		else
			echo "No match found on ${cyan}${REMOTEFILE}${normal}. Local: $localetag and Remote $remoteetag";
		fi

	# fix me add support for directories
	elif [ "$3" = "--dirput" ]; then
		DIRPUTCONTAINER=$1;
		DIRPUTFILE=$2;
		DIRPUTDELETE=$4;

		if [ ! -d "${DIRPUTFILE}" ]; then
			echo "${cyan}Error: ${DIRPUTFILE}${normal} is not a directory or does not exist";
		fi

		check_container_exists ${DIRPUTCONTAINER}
		
		cd ${DIRPUTFILE}

		# directory loop
		for filenames in `find -maxdepth 1 -type f  | cut -d/ -f2`; do
			echo "Found file ${filenames}";
			if [ -f "$filenames" ]; then
				# escaped to allow spaces
				# support for delete after
				if [ "$DIRPUTFILE" = "" ]; then
					rsyncfile ${DIRPUTCONTAINER} "${filenames}" --put
				else
					echo "Sending delete after after $DIRPUTDELETE days";
					rsyncfile ${DIRPUTCONTAINER} "${filenames}" --put "${filenames}" $DIRPUTDELETE
				fi		
			else
				echo "${cyan}Skipping $filenames${normal} directories are not supported yet";
			fi
		done
		echo 'Done with rsync dirput run';
		exit;

	elif [ "$3" = "--dirget" ]; then
		echo
	elif [ "$3" = "--put" ]; then
		# default upload function
		FILE=$2;
        	CONTAINER=$1;
		if [ ! -f "${FILE}" ]; then
			echo "${cyan}File: ${FILE} does not${normal} exist or is a directory";
			return;
		fi

		if [ ! "$4" = "" ]; then
                        REMOTEFILE=$4;
                else
			REMOTEFILE=`basename $2`;
			echo "No remote file name passed. We will call this $REMOTEFILE";
                fi

		localetag=`getmd5 "${FILE}"`;
		remoteetag=`getremotemd5 ${CONTAINER} "${REMOTEFILE}"`;
		#eval remoteetag=$(${curl} -s $CURLOPTS -I -H "X-Auth-Token: ${APIKEY}" $STORAGE_URL/${CONTAINER}/${FILE} | grep "^Etag:" | awk '{print $2}');
		
		if [ "$debug" = "1" ]; then
			echo "Found etagremote: ${remoteetag} and found etaglocal $localetag";
		fi

		if [ "$localetag" = "$remoteetag" ]; then
			echo "${green}Checksums match${normal} for container: ${green}${CONTAINER}${normal} file ${green}${REMOTEFILE}${normal}";
		else
			echo "Uploading container ${CONTAINER} file ${FILE} remotefile ${REMOTEFILE} (if blank to change)";
			upload ${CONTAINER} "${FILE}" ${REMOTEFILE}
			if [ ! "$5" = "" ]; then
				echo "Deleting ${CONTAINER} ${REMOTEFILE} after $5 days";
				delete_after ${CONTAINER} ${REMOTEFILE} $5
				sleep 1s;
			fi
		fi
		echo
	elif [ "$3" = "deleteoldsplitover60" ]; then
		deleteoldsplitover60
	else
		rsyncfile
	fi
}

#upload file, we need to remove slases in the future
function upload()
{
	if [ "$1" = "" -o "$2" = "" ]; then
		echo 'Usage ./isput container file [newfilename]';
		echo 'newfilename is optional';
	else
		REALFILE=$2;
		JUSTFILE=`basename ${2}`;
		CONTAINER=$1;
      	      	if [ ! "$3" = "" ]; then
                        REMOTEFILE=$3;
                else
                        REMOTEFILE=${JUSTFILE};
                fi

		echo "Checking size of ${REALFILE} to container ${CONTAINER} remote file name ${REMOTEFILE}";
		check_size ${REALFILE} ${CONTAINER} ${REMOTEFILE} 
		etag=`getmd5 ${REALFILE}`;
		contenttype=`getcontenttype ${REALFILE}`;
		check_container_exists ${CONTAINER}
		if [ "$debug" = "1" ]; then
			echo "Returned MD5checksum $etag of ${JUSTFILE}";
		fi

		${curl}  $CURLOPTS -o/dev/null -f -X PUT -T "${REALFILE}" -H "ETag: ${etag}" -H "Content-Type: ${contenttype}" -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/${CONTAINER}/${REMOTEFILE}"
	fi
}

function delete_splits()
{

	if [ "$1" = "" -o "$2" = "" ]; then
                echo 'Usage ./isrmsplit container filename (must be full path) | OR | container filename nodb (for non sqlit splits) rmdir';
        else

		FILENAME=$2;
                CONTAINER=$1;
		if [ "$3" = "nodb" ]; then
			REMOTEFILENAME=${FILENAME};
		else
			if [ ! -f $FILENAME ]; then
				echo 'filename must be full path or pass no db (this is for splitdb';
			else
				REMOTEFILENAME=`basename ${FILENAME}`;
			fi
		fi

		check_container_exists ${CONTAINER}

		if [ "$3" = "nodb" ]; then
			listsplits=`${curl} $CURLOPTS -s -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL"/"${CONTAINER}" 2>&1 | grep "${REMOTEFILENAME}/" | grep "/split-"`
		else
			listsplits=`get_splits ${FILENAME} ${REMOTEFILENAME}`;
		fi
		if [ "$listsplits" = "" ]; then
			echo "No split files found for ${FILENAME}";
			return;
		fi

		for SPLITNAME in $listsplits; do
			echo "Removing ${SPLITNAME} in container ${CONTAINER}":
			/admin/swift/isrm ${CONTAINER} ${SPLITNAME}
		done

		# delete from db

		if [ ! "$3" = "nodb" ]; then
			sqdelete ${FILENAME} ${REMOTEFILENAME}
			/admin/swift/isrm ${CONTAINER} ${REMOTEFILENAME}
		fi
 
               /admin/swift/isrm ${CONTAINER} ${REMOTEFILENAME}

		if [ "$4" = "rmdir" ]; then
			/admin/swift/isrmdir ${CONTAINER}
		fi
	fi
}

#split into smaller files, default 1000M
# call FILE FILENAME because split filter uses FILE
function split()
{
	if [ "$1" = "" -o "$2" = "" ]; then
                echo 'Usage ./split container file (optional size in MB, default 1000 (1GB)';
        else

		if [ ! -x ${splitprog} ]; then
			echo "Missing split binary at $LINENO";
			exit;
		fi

		FILENAME=$2;
                CONTAINER=$1;
		check_container_exists ${CONTAINER}

		# pass change of filename
		if [ ! "$3" = "" ]; then
			REMOTEFILENAME=$3;
			CHECKSQFILENAME=$REMOTEFILENAME;
		else
			echo 'Did not pass a remote file name, we will strip / out of the name';
			STRIPFILE=`basename ${FILENAME}`;
			CHECKSQFILENAME=$REMOTEFILENAME;
		fi

		# reduced for splitwrapper
		bytes=1024;

		# just check feature, don't upload
		if [ "$3" = "--check" -o "$4" = "--justcheck" ]; then
			echo 'We are just checking an upload';
			justcheck=1;
		fi

		if [ ! -f ${FILENAME} ]; then
			echo "File ${FILENAME} does not exist at $LINENO";
			exit;
		fi

		# check db md5sums
		# pass 24 for just select from the last of the day
		if [ "$5" = "24" ]; then
			md5check=`check ${FILENAME} $CHECKSQFILENAME 24`;
		else
			md5check=`check ${FILENAME} $CHECKSQFILENAME`;
		fi
		#echo "We returned at $LINENO $md5check";
		# we conocate all md5's into a new string
		buildmd5='';
		# if something returns we have uploaded this in the past
		if [ ! "$md5check" = "" ]; then
			for md5 in $md5check; do 
				addmd5=`echo $md5 | cut -d"|" -f2`;
				buildmd5="${buildmd5}${addmd5}";
			done
			finalmd5=`echo -n $buildmd5 | ${md5prog} | awk '{print $1}'`;
			STRIPFILE=`basename ${FILENAME}`;
			rmd5=`getremotemd5 ${CONTAINER} ${REMOTEFILENAME}`;
			#remotemd5 returns in " "
			if [ "\"$finalmd5\"" = "$rmd5" ]; then
				echo "MD5sum for container $CONTAINER and large object file $FILENAME matches, confirming local checksum";

				localsum=`getmd5 ${FILENAME}`;
				localdbsum=`checkmain ${FILENAME} ${REMOTEFILENAME}`;
				if [ "$localsum" = "$localdbsum" ]; then
					echo "Local md5sum for locate ${FILENAME} remote ${REMOTEFILENAME} matches record in db";
					exit;
				else
					echo -n "Local md5sum for ${FILENAME} REMOTE $REMOTEFILENAME as $localsum did not match record in the DB $localdbsum, ";
					if [ "$justcheck" = "1" ]; then
						echo "exiting";
						exit;
					else
						echo "upload will continue";
					fi
				fi
			else
				echo "MD5sum for container $CONTAINER and large object file $FILENAME do not match, upload will continue";
				# delete remotely and from db
				if [ "$justcheck" = "0" ]; then
					delete_splits ${CONTAINER} ${FILENAME} ${REMOTEFILENAME}
				fi
			fi
		else
			echo "No match found due to no returned md5 check sum. No past upload done.";
		fi

		if [ "$justcheck" = "1" ]; then
			# new debug message can be removed
			echo "Just checking so exiting at $LINENO";
			exit;
		fi
		cat ${FILENAME} | ${splitprog} --verbose -d --bytes=${bytes}M --filter="export WRCONTAINER=${CONTAINER}; export WRREALFILE=${REMOTEFILENAME}; /admin/swift/include/splitwrapper"	
		# calling twice for now
		sleep 10s;
		echo "bulding manifest file for container ${CONTAINER} and filename ${REMOTEFILENAME}"
		${curl} $CURLOPTS -o/dev/null -X PUT -H "X-Object-Manifest: ${CONTAINER}/${REMOTEFILENAME}/" -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/${CONTAINER}/${REMOTEFILENAME}" --data-binary ''
		sleep 10s;
		${curl} $CURLOPTS -o/dev/null -X PUT -H "X-Object-Manifest: ${CONTAINER}/${REMOTEFILENAME}/" -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/${CONTAINER}/${REMOTEFILENAME}" --data-binary ''
		echo "calling ${curl} $CURLOPTS -o/dev/null -X PUT -H \"X-Object-Manifest: ${CONTAINER}/${REMOTEFILENAME}/\" -H \"X-Auth-Token: ${APIKEY}\" \"$STORAGE_URL/${CONTAINER}/${REMOTEFILENAME}\" --data-binary ''"
		build_split_db ${CONTAINER} ${FILENAME} ${REMOTEFILENAME}
		
		echo 'Finished split upload';
		# no exit as function could be nested in things like rsync and dirput
		#exit;	
		sleep 2s;
		echo 'Fileinfo';
                /admin/swift/fileinfo ${CONTAINER} ${REMOTEFILENAME}
		echo

		# exit when we are done to prevent trying to upload with out split
		exit;
	fi

}

function onthefly()
{
	if [ "$1" = "" -o "$2" = "" ]; then
                echo 'Usage ./fly container directory [delete|deleteafter|frombackup add backup name 4th arge] exclude pattern 5th arge optional';
		echo 'All / in the file name will be stripped out of the remotely stored filename';
                exit;
        fi

	FILENAME=$2;
	CONTAINER=$1;
	SAVENAME=`basename ${FILENAME}`;

	check_container_exists ${CONTAINER}

	if [ "$3" = "delete" -o "$3" = "deleteafter" ]; then
		echo "Entering delete mode";
		DELETETIME=30;
		if [ ! "$4" = "" ]; then
			DELETETIME=${4};
		fi
		for flyfiles in `${curl} $CURLOPTS -s -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL"/"${CONTAINER}" 2>&1 | grep "${SAVENAME}/" | grep "/split-"`; do
			if [ "$flyfiles" = "" ]; then
				echo "No on the fly files returned";
			else
				if [ "$3" = "delete" ]; then
					/admin/swift/isrm ${CONTAINER} ${SAVENAME}
					/admin/swift/isrm ${CONTAINER} $flyfiles
				elif [ "$3" = "deleteafter" ]; then
					delete_after ${CONTAINER} ${SAVENAME} ${DELETETIME}
					delete_after ${CONTAINER} $flyfiles ${DELETETIME}
				else
					echo "Unknown delete command passed in function onthefly at line $LINENO";
					exit;
				fi
				
			fi
		done
	else

		# when we call from the backup script we want to append the date so we change the file name
		if [ "$3" = "frombackup" ]; then
			if [ ! "$4" = "" ]; then
				SAVENAME=${4};
				echo "Filename will be called ${SAVENAME}";
			fi
		fi

		if [ -d $FILENAME ]; then
			echo "$FILENAME is a directory, we will be tar/gziping this on the fly to swift";

			#tarargs allows excluding a pattern (one)
			
			TARARGS="";
			if [ ! "$5" = "" ]; then
				TARARGS="--exclude='${5}'"
			else 
				echo "No excludes in tar at line $LINENO";
			fi

			# if $3 = auto we will remove conflicting files (for cron)
			checkif_fly__exists ${CONTAINER} ${SAVENAME} $3
			sleep 4s;
			if [ -e /etc/master.passwd ]; then
				echo 'On the fly: freebsd detected';
				${tar} -zc -f - ${FILENAME} | ${splitprog} --verbose -d --bytes=1024M --filter="export WRCONTAINER=${CONTAINER}; export WRREALFILE=${SAVENAME}; /admin/swift/include/splitwrapper"
			else
				# removed --posix
				if [ ! "$TARARGS" = "" ]; then
					echo "Excluding: $TARARGS";
				fi
				${tar} -c ${FILENAME} $TARARGS | ${gzip} --fast | ${splitprog} --verbose -d --bytes=1024M --filter="export WRCONTAINER=${CONTAINER}; export WRREALFILE=${SAVENAME}; /admin/swift/include/splitwrapper"
			fi
			# it appears this may expire after a long upload
			#${curl} $CURLOPTS -o/dev/null -X PUT -H "X-Object-Manifest: ${CONTAINER}/${SAVENAME}/" -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/${CONTAINER}/${SAVENAME}" --data-binary ''
			
			
			# debug
			echo			
			echo "Settling to create backup manifest at line $LINENO";
			${curl} -v $CURLOPTS -o/dev/null -X PUT -H "X-Object-Manifest: ${CONTAINER}/${SAVENAME}/" -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/${CONTAINER}/${SAVENAME}" --data-binary ''
			sleep 10s;
			# get new key
			/bin/rm $file
			getauthkey
			APIKEY=`cat $file | grep ^X-Auth-Token: | cut -d: -f2 | tr -d '\r'`;
			sleep 10s;
			echo "X-Object-Manifest: ${CONTAINER}/${SAVENAME}/"
			echo "X-Auth-Token: ${APIKEY}"
			echo "$STORAGE_URL";
			echo
			${curl} -v $CURLOPTS -o/dev/null -X PUT -H "X-Object-Manifest: ${CONTAINER}/${SAVENAME}/" -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL/${CONTAINER}/${SAVENAME}" --data-binary ''
			echo "done creating manifest at line $LINENO";
			echo
		else
			echo "$FILENAME is not a directory";
		fi
	fi

}

# can be extended for split
function checkif_fly__exists()
{

	SAVENAME=$2;
        CONTAINER=$1;
	#SAVENAME=`basename ${FILENAME}`;
	#SAVENAME=`echo ${FILENAME} | tr -d '/'`;
	delete=0;

	if [ "$3" = "auto" ]; then
		delete=1;
	fi

	if [ "$1" = "" -o "$2" = "" ]; then
                echo 'Usage ./checkif_fly_exists container directory';
		exit;
        fi

	for files in `${curl} $CURLOPTS -s -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL"/"${CONTAINER}" 2>&1 | grep "${SAVENAME}/"`; do
		if [ "$files" = "" ]; then
			echo "Checked for existing files, none exist";
		else
			if [ "$delete" = "1" ]; then
				/admin/swift/isrm ${CONTAINER} $files
			else
				echo "Checked for existing $CUT file, found: $files - this may cause a conflict";
			fi
		fi
	done
}

function build_split_db()
{

	if [ "$1" = "" -o "$2" = "" ]; then
		echo 'Usage ./build_split_db container file';
		exit;
	fi

	FILENAME=$2;
        CONTAINER=$1;
	if [ "$3" = "" ]; then
		REMOTEFILENAME=$FILENAME;
	else
		REMOTEFILENAME=$3;
	fi
	localmd5=`getmd5 ${FILENAME}`;
	# this won't change in other functions
	SPLITMAIN=$FILENAME;
	#echo 'debug splitfiles command';
	#echo "${curl} $CURLOPTS -s -H \"X-Auth-Token: ${APIKEY}\" \"$STORAGE_URL\"/\"${CONTAINER}\" 2>&1 | grep ^\"${REMOTEFILENAME}/\" | grep \"/split-\""
	for splitfiles in `${curl} $CURLOPTS -s -H "X-Auth-Token: ${APIKEY}" "$STORAGE_URL"/"${CONTAINER}" 2>&1 | grep ^"${REMOTEFILENAME}/" | grep "/split-"`; do
		echo "Found splitfile $splitfiles";
		if [ "$splitfiles" = "" ]; then
			echo "function build_split_db returned blank vlue at $LINENO";
			exit;
		else
			splitsum=`getremotemd5 ${CONTAINER} ${splitfiles}`;
			insert ${SPLITMAIN} $splitfiles $splitsum ${REMOTEFILENAME}
			echo "Adding split file to db $splitfiles $splitsum";
		fi	
	done

	# main checksum
	#FULLFILE from another function
	if [ "$FULLFILE" = "" ]; then
		FULLFILE=${FILENAME};
	fi
	echo "Adding main main to splitdb ${FULLFILE} $localmd5";
	insertmain ${FULLFILE} $localmd5 ${REMOTEFILENAME}
	echo "Adding localfile to split db as main ${FULLFILE} $localmd5 ${REMOTEFILENAME}";
}

#
function check_container_exists()
{
	# quick check to see if we exist
	check=`${curl} $CURLOPTS -s -H "X-Auth-Token: ${APIKEY}" $STORAGE_URL/$1 | grep "The resource could not be found."`;
	if [ ! "$check" = "" ]; then

		if [ "$2" = "--force" ]; then
			echo "Container $1 does not exist, creating";
			makedir $1
		else
			echo "Container $1 does not exist. Use ismkdir to create or run with --force";
			exit;
		fi
	fi


}

function listcontainers()
{
	if [ "$1" = "" ]; then
		# make swift backups eaiser
		if [ -e /root/.swift/_lesspriv ]; then
			if [ -e /root/.swift/hostname ]; then
				HOSTNAME=`cat /root/.swift/hostname`;
			else
				HOSTNAME=`hostname`;
			fi
			${curl} $CURLOPTS -s -H "X-Auth-Token: ${APIKEY}" $STORAGE_URL/$HOSTNAME
			exit;
		fi

                for dir in `${curl} $CURLOPTS -s -H "X-Auth-Token: ${APIKEY}" $STORAGE_URL | sort`; do
			printf "${green}$dir${normal}\n";
		done
	else
		if [ "$debug" = "1" ]; then
			echo "Listing container $1"
		fi
		${curl} $CURLOPTS -s -H "X-Auth-Token: ${APIKEY}" $STORAGE_URL/$1
	fi
}


# end functions

# begin main
file="$HOME/.swift/.auth_key";

if [ ! -x $curl ]; then
        echo "$curl does not exist";
        exit;
fi

if [ ! -x $md5prog ]; then
        echo "$md5prog does not exist";
        exit;
fi

if [ ! -x $FILECOMMAND ]; then
        echo "$FILECOMMAND does not exist";
        exit;
fi

if [ ! -x $bc ]; then
        echo "$bc does not exist";
        exit;
fi

if [ ! -x $DDPROG ]; then
	echo "$DDPROG does not exist but not fatal";
fi

if [ ! -x $hostprog ]; then
	echo "$hostprog does not exist";
	exit;
fi

if [ ! -e $HOME/.swift/config ]; then
        echo "$HOME/.swift/config does not exist";
        create_config
        exit;
else
        . $HOME/.swift/config
fi

if [ "$username" = "" ]; then
        echo 'username value missing';
        exit;
elif [ "$pass" = "" ]; then
        echo 'password value missing';
        exit;
elif [ "$AUTH_URL" = "" ]; then
        echo 'AUTH_URL missing';
        exit;
fi

# used to get incoming commands
script=`basename $0`

#colors
blue=$(tput setaf 4)
green=$(tput setaf 2)
cyan=$(tput setaf 6)
normal=$(tput sgr0)


if [ -e $file ]; then
        if [ $(age "$file") -gt "1000" ]; then
		if [ "$debug" = "1" ]; then
                	echo 'Auth key found but too old, and removed';
		fi
		/bin/rm $file
		getauthkey
        else
		# keep key
		if [ "$debug" = "1" ]; then
			echo 'Auth key found and kept';
		fi
        fi
else
	getauthkey
fi



APIKEY=`cat $file | grep ^X-Auth-Token: | cut -d: -f2 | tr -d '\r'`;
SSL_STORAGE_URL=`cat $file | grep ^X-Storage-Url: | cut -d" " -f2 | tr -d '\r'`;
NON_SSL_STORAGE_URL=`cat $file | grep ^X-Storage-Url: | cut -d" " -f2 | tr -d '\r' | sed s#"https:"#"http:"#g | sed s#":8080"#":80"#g`;
STORAGE_URL="$NON_SSL_STORAGE_URL";
#echo "Using $STORAGE_URL for uploads and $SSL_STORAGE_URL for auth";

if [ "$debug" = "1" ]; then
	echo "Found storage_url $STORAGE_URL"
	echo
	echo "Found APIKEY $APIKEY"
fi

if [ "$APIKEY" = "" ]; then
        echo "APIKEY missing, username/pass may be wrong or remove $file";
        exit;
fi

if [ "$STORAGE_URL" = "" ]; then
        echo 'storage url missing';
        exit;
fi

# sqlite function to store md5sums
. /admin/swift/include/sqlite


if [ "$script" = "qswift" -o "$script" = "c" ]; then
	if [ ! "$1" = "" ]; then
       		script=$1;
       		shift;
	else
		echo 'Incorrect qswift/c call';
		exit;
	fi
fi

case "$script" in
 isstat)
  getstats $1 $2
;;
 fileinfo)
  showfileinfo $1 $2
;;
 isls)
  listcontainers $1
 ;;
split)
 split $1 $2 $3
;;
isput)
  upload $1 $2 $3
 ;;
isrm)
  delete $1 $2
 ;;
ismkdir)
  makedir $1
 ;;
isget)
 download $1 $2 $3
;;
iscp)
 rcopy $1 $2 $3
;;
isrmdir)
 rrmdir $1 $2
;;
ismv)
 rmove $1 $2 $3
;;
rsync)
  rsyncfile $1 $2 $3 $4 $5
;;
mkpub)
 makepublic $1 $2
 ;;
mkdir_p)
 check_container_exists $1 $2 
;;
deleteafter)
 delete_after $1 $2 $3
  ;;
isrmsplit)
 delete_splits $1 $2 $3 $4
 ;;
fly)
 onthefly $1 $2 $3 $4 $5
 ;;
ddlvm)
 backuplvm $1 $2 $3
 ;;
backupmysql)
 backupmysql $1 $2
 ;;
getsig)
 getremotemd5 $1 $2
 ;;
isbackup)
 runabackup $1
 ;;
debug)
 rundebug
 ;;
  *)
	echo 'Unknown call';
  ;;
 esac

