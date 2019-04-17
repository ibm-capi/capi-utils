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

version=1.0
log_file=/var/log/capi-utils.log

# Load factory partition
function perst_factory() {
  if [ -f /sys/kernel/debug/powerpc/eeh_max_freezes ]; then
    eeh_max_freezes=`cat /sys/kernel/debug/powerpc/eeh_max_freezes`
    echo 100000 > /sys/kernel/debug/powerpc/eeh_max_freezes
  fi

  c=$1
  printf "factory" > /sys/class/cxl/card$c/load_image_on_perst
  printf 1 > /sys/class/cxl/card$c/reset

  if [ -f /sys/kernel/debug/powerpc/eeh_max_freezes ]; then
    echo $eeh_max_freezes > /sys/kernel/debug/powerpc/eeh_max_freezes
  fi
  date +"%T %a %b %d %Y" >> $log_file 
  echo "Flash failure. Resetting to factory" >> $log_file 
  rm -rf "/var/cxl/capi-flash-script.lock"
}

# Test if a card reset is done
function testcard
{
  cardId=$1
  cxlDir="/sys/class/cxl/card$cardId"
  cxlafuFile="/dev/cxl/afu$cardId.0m"
  cxlDirStatus=0  # 0->still here, 1->disapeared so reset is on going, 2-> first dir reappeared 3->back here so reset is done
  
  maxCount=50000
  count=0
  while true
    do
      case $cxlDirStatus in
        0)
          [ $count -eq 0 ] && printf "." && count=$maxCount
          [ ! -d $cxlDir ] && cxlDirStatus=1 && printf "\n$cxlDir deleted\n"
        ;;
        1)
          [ $count -eq 0 ] && printf "." && count=$maxCount
          [ -d $cxlDir ] && cxlDirStatus=2 && printf "\n$cxlDir recreated\n"
        ;;
      	2)
      	  [ $count -eq 0 ] && printf "." && count=$maxCount
      	  [ -e $cxlafuFile ] && cxlDirStatus=3 && printf "$cxlafuFile recreated\n"
        ;;
        3)
          break
        ;;
      esac
      count=$((count-1))
  done
}

export -f testcard  # function exportation in order to allow using it with timeout command below

# Reset a card and control what image gets loaded
function reset_card() {
  # Timeout for reset
  reset_timeout=60

  # eeh_max_freezes: default number of resets allowed per PCI device per
  # hour. Backup/restore this counter, since if card is rest too often,
  # it would be fenced away.
  if [ -f /sys/kernel/debug/powerpc/eeh_max_freezes ]; then
    eeh_max_freezes=`cat /sys/kernel/debug/powerpc/eeh_max_freezes`
    echo 100000 > /sys/kernel/debug/powerpc/eeh_max_freezes
  fi

  [ -n "$3" ] && printf "$3\n" || printf "Preparing to reset card\n"
  [ -n "$4" ] && reset_timeout=$4
  # sleep 5  ## Why ?
  printf "Resetting card $1: "
  c=$1
  printf $2 > /sys/class/cxl/card$c/load_image_on_perst
  ic=`cat /sys/class/cxl/card$c/load_image_on_perst`
  printf "load_image_on_perst is set to \"$ic\". Reset!\n"
  
  cxlDir="/sys/class/cxl/card$c"

  printf 1 > $cxlDir/reset  # reset requested

  if timeout $reset_timeout bash -c "testcard $c"
  then
    ret_status=0
  else
    ret_status=1
  fi
  
  printf "\n"

  if [ -f /sys/kernel/debug/powerpc/eeh_max_freezes ]; then
    echo $eeh_max_freezes > /sys/kernel/debug/powerpc/eeh_max_freezes
  fi

  if [ $ret_status -ne 0 ]; then
    printf "${bold}ERROR:${normal} Reset timeout has occurred\n\n"
    exit 1
  else
# Uncomment the 2 following lines should you want to give access to all users
#	printf "Doing : sudo chmod -R ugo+rw /dev/cxl\n"
#        sudo chmod -R ugo+rw /dev/cxl
    printf "\nNew /dev/cxl/* device will need sudo authorization\n"
    printf "example: sudo snap_maint -v\n"
    printf "Tune /dev/cxl/* privileges if needed\n"
    printf "Reset complete\n\n"
  fi
}

# stop on non-zero response
set -e

# output formatting
( [[ $- == *i* ]] && bold=$(tput bold) ) || bold=""
( [[ $- == *i* ]] && normal=$(tput sgr0) ) || normal=""

# make sure script runs as root
if [[ $EUID -ne 0 ]]; then
  printf "${bold}ERROR:${normal} This script must run as root (${EUID})\n"
  exit 1
fi
