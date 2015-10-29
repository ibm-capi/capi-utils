#!/bin/bash
# Usage: sudo capi-flash-script.sh <path-to-bit-file>

# stop on non-zero response
set -e

# output formatting
bold=$(tput bold)
normal=$(tput sgr0)

# make sure script runs as root
if [[ $EUID -ne 0 ]]; then
  printf "${bold}ERROR:${normal} This script must run as root\n"
  exit 1
fi

# make sure an input argument is provided
if [ $# -eq 0 ]; then
  printf "${bold}ERROR:${normal} Input argument missing\n"
  printf "Usage: sudo capi-flash-script.sh <path-to-bit-file>\n"
  exit 1
fi

# make sure the input file exists
if [[ ! -e $1 ]]; then
  printf "${bold}ERROR:${normal} $1 not found\n"
  exit 1
fi

# make cxl dir if not present
mkdir -p /var/cxl/

# mutual exclusion
if ! mkdir /var/cxl/capi-flash-script.lock 2>/dev/null; then
  printf "${bold}ERROR:${normal} Another instance of this script is running\n"
  exit 1
fi
trap 'rm -rf "/var/cxl/capi-flash-script.lock"' 0

# get pwd
pwd="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# get number of cards in system
n=`ls -d /sys/class/cxl/card* | awk -F"/sys/class/cxl/card" '{ print $2 }' | wc -w`

# touch history files if not present
for i in `seq 0 $(($n - 1))`; do
  f="/var/cxl/card$i"
  if [[ ! -f $f ]]; then
    touch $f
  fi
done

# print current date on server for comparison
printf "\n${bold}Current date:${normal}\n$(date)\n\n"

# print table header
printf "${bold}%-7s %-30s %-29s %-20s %s${normal}\n" "#" "Card" "Flashed" "by" "Image"

# print card information and flash history
i=0;
while read d; do
  p[$i]=$(cat /sys/class/cxl/card$i/psl_revision | xargs printf "%.4X");
  f=$(cat /var/cxl/card$i)
  while IFS='' read -r line || [[ -n $line ]]; do
    if [[ ${line:0:4} == ${p[$i]:0:4} ]]; then
      printf "%-7s %-30s %-29s %-20s %s\n" "card$i" "${line:5}" "${f:0:29}" "${f:30:20}" "${f:51}"
    fi
  done < "$pwd/psl-revisions"
  i=$[$i+1]
done < <(lspci -d "1014":"477" )

printf "\n"

# check file type
FILE_EXT=${1##*.}
if [[ ${p[$c]:0:4} == "0000" ]]; then
  if [[ $FILE_EXT != "rbf" ]]; then
    printf "${bold}ERROR: ${normal} Incorrect board type (Altera/Xilinx)\n"
    exit 0
  fi
else
  if [[ $FILE_EXT != "bin" ]]; then
    printf "${bold}ERROR: ${normal} Incorrect board type (Altera/Xilinx)\n"
    exit 0
  fi
fi

# prompt card to flash to
while true; do
  read -p "Which card do you want to flash? [0-$(($n - 1))] " c
  if ! [[ $c =~ ^[0-9]+$ ]]; then
    printf "${bold}ERROR:${normal} Invalid input\n"
  else
    c=$((10#$c))
    if (( "$c" >= "$n" )); then
      printf "${bold}ERROR:${normal} Wrong card number\n"
    else
      break
    fi
  fi
done

printf "\n"

# prompt to confirm
while true; do
  read -p "Do you want to continue to flash ${bold}$1${normal} to ${bold}card$c${normal}? [y/n] " yn
  case $yn in
    [Yy]* ) break;;
    [Nn]* ) exit;;
    * ) printf "${bold}ERROR:${normal} Please answer with y or n\n";;
  esac
done

printf "\n"

# update flash history file
printf "%-29s %-20s %s\n" "$(date)" "$(logname)" $1 > /var/cxl/card$c

# flash card with corresponding binary
$pwd/capi-flash-${p[$c]} $1 $c || printf "${bold}ERROR:${normal} Something went wrong\n"

# reset card
printf "Preparing to reset card\n"
sleep 5
printf "Resetting card\n"
printf user > /sys/class/cxl/card$c/load_image_on_perst
printf 1 > /sys/class/cxl/card$c/reset
sleep 5
while true; do
  if [[ `ls -d /sys/class/cxl/card* | awk -F"/sys/class/cxl/card" '{ print $2 }' | wc -w` == "$n" ]]; then
    break
  fi
  sleep 1
done
printf "Reset complete\n"

# remind afu to use in host application
printf "\nMake sure to use ${bold}/dev/cxl/afu$c.0d${normal} in your host application;\n\n"
printf "#define DEVICE \"/dev/cxl/afu$c.0d\"\n"
printf "struct cxl_afu_h *afu = cxl_afu_open_dev ((char*) (DEVICE));\n\n"
