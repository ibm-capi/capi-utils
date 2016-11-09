#!/bin/bash
#
# Copyright 2016 International Business Machines
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Usage: sudo capi-flash-script.sh <path-to-bit-file>

ulimit -c unlimited

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
  p[$i]=$(cat /sys/class/cxl/card$i/device/subsystem_device)
  # check for legacy device
  if [[ ${p[$i]:0:6} == "0x04af" ]]; then
    p[$i]=$(cat /sys/class/cxl/card$i/psl_revision | xargs printf "0x%.4X")
  fi
  f=$(cat /var/cxl/card$i)
  while IFS='' read -r line || [[ -n $line ]]; do
    if [[ ${line:0:6} == ${p[$i]:0:6} ]]; then
      parse_info=($line)
      board_vendor[$i]=${parse_info[1]}
      fpga_type[$i]=${parse_info[2]}
      printf "%-7s %-30s %-29s %-20s %s\n" "card$i" "${line:6}" "${f:0:29}" "${f:30:20}" "${f:51}"
    fi
  done < "$pwd/psl-devices"
  i=$[$i+1]
done < <(lspci -d "1014":"477" )

printf "\n"

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

# check file type
FILE_EXT=${1##*.}
if [[ ${fpga_type[$c]} == "Altera" ]]; then
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
$pwd/capi-flash-${board_vendor[$c]} $1 $c || printf "${bold}ERROR:${normal} Something went wrong\n"

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
