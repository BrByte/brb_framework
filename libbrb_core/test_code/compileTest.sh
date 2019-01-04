#!/bin/sh

SRC=`cd "$(dirname "$0")" && pwd`

RCol='\e[0m'    # Text Reset

# Regular           Bold                Underline           High Intensity      BoldHigh Intens     Background          High Intensity Backgrounds

Bla='\e[0;30m';     BBla='\e[1;30m';    UBla='\e[4;30m';    IBla='\e[0;90m';    BIBla='\e[1;90m';   On_Bla='\e[40m';    On_IBla='\e[0;100m';
Red='\e[0;31m';     BRed='\e[1;31m';    URed='\e[4;31m';    IRed='\e[0;91m';    BIRed='\e[1;91m';   On_Red='\e[41m';    On_IRed='\e[0;101m';
Gre='\e[0;32m';     BGre='\e[1;32m';    UGre='\e[4;32m';    IGre='\e[0;92m';    BIGre='\e[1;92m';   On_Gre='\e[42m';    On_IGre='\e[0;102m';
Yel='\e[0;33m';     BYel='\e[1;33m';    UYel='\e[4;33m';    IYel='\e[0;93m';    BIYel='\e[1;93m';   On_Yel='\e[43m';    On_IYel='\e[0;103m';
Blu='\e[0;34m';     BBlu='\e[1;34m';    UBlu='\e[4;34m';    IBlu='\e[0;94m';    BIBlu='\e[1;94m';   On_Blu='\e[44m';    On_IBlu='\e[0;104m';
Pur='\e[0;35m';     BPur='\e[1;35m';    UPur='\e[4;35m';    IPur='\e[0;95m';    BIPur='\e[1;95m';   On_Pur='\e[45m';    On_IPur='\e[0;105m';
Cya='\e[0;36m';     BCya='\e[1;36m';    UCya='\e[4;36m';    ICya='\e[0;96m';    BICya='\e[1;96m';   On_Cya='\e[46m';    On_ICya='\e[0;106m';
Whi='\e[0;37m';     BWhi='\e[1;37m';    UWhi='\e[4;37m';    IWhi='\e[0;97m';    BIWhi='\e[1;97m';   On_Whi='\e[47m';    On_IWhi='\e[0;107m';

CFG_ORIG=$SRC
CFG_INPUTDIR=$SRC
CFG_OUTPUTDIR=$CFG_INPUTDIR/bin/

OPT_OUTPUTDIR='false';
OPT_INPUTDIR='false';
OPT_DEBUG='false';
OPT_BANNER='true';
OPT_COPYONLY='false';
OPT_SCP_INFO='false';
OPT_MAKEOLD='false';

CFG_SCP_HOST='127.0.0.1';
CFG_SCP_PORT='2229';
CFG_SCP_DST='/tmp';

#########################################################################################################
output_dir_adjust() {
	
    if [ $OPT_OUTPUTDIR == 'false' ]; then		
		CFG_OUTPUTDIR=$CFG_INPUTDIR/bin/
	fi
		
	if [ "$CFG_OUTPUTDIR" = /* ]; then
	    echo "absolute CFG_OUTPUTDIR $CFG_OUTPUTDIR"
	else
	    echo "relative CFG_OUTPUTDIR $CFG_OUTPUTDIR"
	  
	  	CFG_OUTPUTDIR=$CFG_ORIG/$CFG_OUTPUTDIR
	fi
}
#########################################################################################################
input_dir_adjust() {
	
    if [ $OPT_INPUTDIR == 'false' ]; then		
		CFG_INPUTDIR=$CFG_ORIG
	fi
		
	if [ "$CFG_INPUTDIR" = /* ]; then
	    echo "absolute CFG_INPUTDIR $CFG_INPUTDIR"
	else
	    echo "relative CFG_INPUTDIR $CFG_INPUTDIR"
	  
	  	CFG_INPUTDIR=$CFG_ORIG/$CFG_INPUTDIR
	fi
}
#########################################################################################################
scp_daemons() {
	scp -P$CFG_SCP_PORT $CFG_OUTPUTDIR/* $CFG_SCP_HOST:/$CFG_SCP_DST
}
#########################################################################################################
compile_daemons() {
	
	local LOCAL_DIRECTORY='';
	local TMPDEBUG="`mktemp -t debug_tmp`"
	
	if [ ! -d $CFG_OUTPUTDIR ]; then
		mkdir -p $CFG_OUTPUTDIR
	fi
	
	echo "compile_daemons at $CFG_INPUTDIR"
	
	cd $CFG_INPUTDIR
	
	for direntry in *
	do
		LOCAL_DIRECTORY=$CFG_INPUTDIR/$direntry
		echo > ${TMPDEBUG};
		
		echo "... check directory $LOCAL_DIRECTORY"
		
		if [ ! -d "$LOCAL_DIRECTORY" ]; then
			echo "... not a directory"
			continue
		fi
		
#		if [ ! -d "$LOCAL_DIRECTORY" ]; then
#			echo "... not a directory"
#			continue
#		fi
		
		echo "... check $direntry"
		
		if [ "${direntry%${direntry#?????}}"x != 'test_x' ]; then
			echo "... skip $direntry"
			continue
		fi
		
#		DAEMON_NAME=${direntry:5}
		DAEMON_NAME=${direntry}
		
		if [ "${DAEMON_NAME}" = "xxx" ]; then
			continue
		fi			
		
		cd $LOCAL_DIRECTORY
		
		echo -e "${Gre}+-----------------------------------------------+ ${RCol}"
		
		echo -e "${Gre}|    Making Clean ${DAEMON_NAME}... ${RCol}"
		
		if [ $OPT_MAKEOLD == 'true' ]; then
			if [ -f 'Makefile.old' ]; then
				#cp Makefile.old Makefile
				awk '{ sub("\r$", ""); print }' Makefile.old > Makefile
			fi
		fi
		
		if [ $OPT_DEBUG == 'true' ]; then
			make clean
		else
			make clean >/dev/null 2>&1
		fi
		
		echo -e "${Gre}|    Make... ${RCol}"
		
		if [ $OPT_DEBUG == 'true' ]; then
			make
		else
			make >> ${TMPDEBUG};
		fi
		
		echo -e "${Gre}|    Copy ${DAEMON_NAME} to $CFG_OUTPUTDIR... ${RCol}"
		
		if [ -f ${LOCAL_DIRECTORY}/${DAEMON_NAME} ]; then
			cp ${LOCAL_DIRECTORY}/${DAEMON_NAME} $CFG_OUTPUTDIR
		else
			echo -e "${Red}ERROR on ${LOCAL_DIRECTORY}/${DAEMON_NAME} ${RCol}"
			echo -e "${Red}+-----------------------------------------------+ ${RCol}"
			cat ${TMPDEBUG}
			echo -e "${Red}+-----------------------------------------------+ ${RCol}"
			exit;	
		fi
		
		echo -e "${Gre}+-----------------------------------------------+ ${RCol}"
		
		echo -e ""
		echo -e ""
		
	done
	
	rm ${TMPDEBUG}
}
#########################################################################################################
print_banner() {
	
	if [ $OPT_BANNER == 'true' ]; then
		printf ' -------------------------------------------------------------------------------------------------------------\n' 1>&2
	    printf '  Running in %s \n' $SRC 1>&2
	    printf '  -i OPT_INPUTDIR (%s): [%s]\n' $OPT_INPUTDIR $CFG_INPUTDIR 1>&2
	    printf '  -o OPT_OUTPUTDIR (%s): [%s]\n' $OPT_OUTPUTDIR $CFG_OUTPUTDIR 1>&2
	    
	    printf '  -c OPT_COPYONLY (%s) \n' $OPT_COPYONLY 1>&2
	    printf '  -d OPT_DEBUG (%s) \n' $OPT_DEBUG 1>&2
	    printf '  -s OPT_SCP_INFO (%s): Host [%s]:[%s] in [%s]\n' $OPT_SCP_INFO $CFG_SCP_HOST $CFG_SCP_PORT $CFG_SCP_DIR 1>&2
	    printf ' -------------------------------------------------------------------------------------------------------------\n' 1>&2
	fi	
}
#########################################################################################################
checkopts() {
	OPTIND=1;
	# GET OPTIONS
	while getopts mbcdi:o:s: OPCAO; do
		case "${OPCAO}" in
			m)	OPT_MAKEOLD='true';
			
			;;
			b)	OPT_BANNER='false';
			
			;;
			c)	OPT_COPYONLY='true';
			
			;;
			d) 	OPT_DEBUG='true';
			
			;;
			i) 	OPT_INPUTDIR='true';
			
				CFG_INPUTDIR=${OPTARG};
			;;
			o) 	OPT_OUTPUTDIR='true';
			
				CFG_OUTPUTDIR=${OPTARG};
			;;
			s) 	OPT_SCP_INFO='true';
			
				CFG_SCP_INFO=${OPTARG}
				set -- "$CFG_SCP_INFO"
				IFS=":"; 
				declare -a ScpArr=`$*`
				
				CFG_SCP_INFO=${ScpArr[@]}
				CFG_SCP_HOST=${ScpArr[0]};
				CFG_SCP_PORT=${ScpArr[1]};
				CFG_SCP_DIR=${ScpArr[2]};
			;;
		esac
	done
	
	#shift $(( OPTIND - 2 ))
	
	input_dir_adjust;
	output_dir_adjust;
	print_banner;
	
	if [ $OPT_COPYONLY == 'false' ]; then
		compile_daemons;
	fi
	
	if [ $OPT_SCP_INFO == 'true' ]; then
		scp_daemons;
	fi
	
}

checkopts $@

cd $CFG_ORIG

echo ""
echo -e "${Gre}|    FINISH   ${RCol}"
echo ""

exit;
