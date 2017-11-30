#!/bin/bash
#
# Copyright 2016, 2017 International Business Machines
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

# get pwd
pwd="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $pwd/../lib/capi-utils/capi-utils-common.sh

force=0
program=`basename "$0"`
card=-1

# Print usage message helper function
function usage() {
  echo "Usage:  sudo ${program} [OPTIONS]"
  echo "    [-C <card>] card to flash."
  echo "    [-f] force execution without asking."
  echo "         warning: use with care e.g. for automation."
  echo "    [-V] Print program version (${version})"
  echo "    [-h] Print this help message."
  echo "    <path-to-bit-file>"
  echo
  echo "Utility to flash/write bitstreams to CAPI FPGA cards."
  echo "Please ensure that you are using the right bitstream data."
  echo "Using non-functional bitstream data or aborting the process"
  echo "can leave your card in a state where a hardware debugger is"
  echo "required to make the card usable again."
  echo
}

# Parse any options given on the command line
while getopts ":C:fVh" opt; do
  case ${opt} in
      C)
      card=$OPTARG
      ;;
      f)
      force=1
      ;;
      V)
      echo "${version}" >&2
      exit 0
      ;;
      h)
      usage;
      exit 0
      ;;
      \?)
      printf "${bold}ERROR:${normal} Invalid option: -${OPTARG}\n" >&2
      exit 1
      ;;
      :)
      printf "${bold}ERROR:${normal} Option -$OPTARG requires an argument.\n" >&2
      exit 1
      ;;
  esac
done

shift $((OPTIND-1))
# now do something with $@

ulimit -c unlimited

# make sure an input argument is provided
if [ $# -eq 0 ]; then
  printf "${bold}ERROR:${normal} Input argument missing\n"
  printf "Usage: sudo capi-flash-script <path-to-bit-file>\n"
  exit 1
fi

# make sure the input file exists
if [[ ! -e $1 ]]; then
  printf "${bold}ERROR:${normal} $1 not found\n"
  exit 1
fi

# check if CAPI boards exists
capi_check=`lspci -d "1014":"477" | wc -l`
if [ $capi_check -eq 0 ]; then
  printf "${bold}ERROR:${normal} No CAPI devices found\n"
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
  done < "$pwd/../lib/capi-utils/psl-devices"
  i=$[$i+1]
done < <(lspci -d "1014":"477" )

printf "\n"

# card is set via parameter since it is positive
if (($card >= 0)); then
  c=$((10#$card))
  if (( "$c" >= "$n" )); then
    printf "${bold}ERROR:${normal} Wrong card number ${card}\n"
    exit 1
  fi
else
# prompt card to flash to
  while true; do
    read -p "Which card do you want to flash? [0-$(($n - 1))] " c
    if ! [[ $c =~ ^[0-9]+$ ]]; then
      printf "${bold}ERROR:${normal} Invalid input\n"
    else
      c=$((10#$c))
      if (( "$c" >= "$n" )); then
        printf "${bold}ERROR:${normal} Wrong card number\n"
        exit 1
      else
        break
      fi
    fi
  done
fi

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

# card is set via parameter since it is positive
if (($force != 1)); then
  # prompt to confirm
  while true; do
    read -p "Do you want to continue to flash ${bold}$1${normal} to ${bold}card$c${normal}? [y/n] " yn
    case $yn in
      [Yy]* ) break;;
      [Nn]* ) exit;;
      * ) printf "${bold}ERROR:${normal} Please answer with y or n\n";;
    esac
  done
else
  printf "Continue to flash ${bold}$1${normal} to ${bold}card$c${normal}\n"
fi

printf "\n"

# update flash history file
printf "%-29s %-20s %s\n" "$(date)" "$(logname)" $1 > /var/cxl/card$c

# Check if lowlevel flash utility is existing and executable
if [ ! -x $pwd/../lib/capi-utils/capi-flash-${board_vendor[$c]} ]; then
  printf "${bold}ERROR:${normal} Utility capi-flash-${board_vendor[$c]} not found!\n"
  exit 1
fi

# Reset to card/flash registers to known state (factory) 
reset_card $c factory "Preparing card for flashing"

trap 'kill -TERM $PID; perst_factory $c' TERM INT
# flash card with corresponding binary
$pwd/../lib/capi-utils/capi-flash-${board_vendor[$c]} $1 $c &
# || printf "${bold}ERROR:${normal} Something went wrong\n"
PID=$!
wait $PID
trap - TERM INT
wait $PID

# reset card
reset_card $c user

# remind afu to use in host application
# Y.Lu: This is not needed for SNAP. The DEVICE may not be in dedicated mode. Comment off.

# printf "\nMake sure to use ${bold}/dev/cxl/afu$c.0d${normal} in your host application;\n\n"
# printf "#define DEVICE \"/dev/cxl/afu$c.0d\"\n"
# printf "struct cxl_afu_h *afu = cxl_afu_open_dev ((char*) (DEVICE));\n\n"
