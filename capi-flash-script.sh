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

# get pwd
pwd="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# make cxl dir if not present
mkdir -p /var/cxl/

# get number of cards in system
n=`ls -d /sys/class/cxl/card* | awk -F"/sys/class/cxl/card" '{ print $2 }' | wc -w`

# touch history files if not present
for i in `seq 0 $(($n - 1))`; do
  f="/var/cxl/card$i"
  if [[ ! -f $f ]]; then
    touch $f
  fi
done

# print current data on server for comparison
printf "\n${bold}Current date:${normal}\n$(date)\n\n"

# print table header
printf "${bold}%-7s %-30s %-29s %-20s %s${normal}\n" "#" "Card" "Flashed" "by" "Image"

# print card information and flash history
i=0;
while read d; do
  p[$i]=`setpci -s ${d:0:12} 40C.W`;
  f=$(cat /var/cxl/card$i)
  while IFS='' read -r line || [[ -n $line ]]; do
    if [[ ${line:0:4} == ${p[$i]:0:4} ]]; then
      printf "%-7s %-30s %-29s %-20s %s\n" "card$i" "${line:5}" "${f:0:29}" "${f:30:20}" "${f:51}"
    fi
  done < "$pwd/psl-revisions"
  i=$[$i+1]
done < <(lspci -d "1014":"477" )

printf "\n"

# prompt card to flash to
while true; do
  read -p "Which card do you want to flash? [0-$(($n - 1))] " c
  c=$((10#$c))
  if ! [[ "$c" =~ ^[0-9]+$ ]] || (( "$n" <= "$c" )); then
    echo "${bold}ERROR:${normal} invalid input"
  else
    break
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
echo user > /sys/class/cxl/card$c/load_image_on_perst
echo 1 > /sys/class/cxl/card$c/reset
printf "Sleeping 30 seconds for reset to occur\n"
sleep 30

# remind afu to use in host application
printf "\nMake sure to use ${bold}/dev/cxl/afu$c.0d${normal} in your host application;\n\n"
printf "#define DEVICE /dev/cxl/afu$c.0d\n"
printf "struct cxl_afu_h *afu = cxl_afu_open_dev ((char*) (DEVICE));\n\n"
