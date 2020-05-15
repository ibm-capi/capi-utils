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
isnumber='^[0-9]+$'

# get capi-utils root
[ -h $0 ] && package_root=`ls -l "$0" |sed -e 's|.*-> ||'` || package_root="$0"
package_root=$(dirname $package_root)
source $package_root/capi-utils-common.sh

function confirm {
  # prompt to confirm
  while true; do
    read -p "Do you want to continue to reset all CAPI adapters? [y/n] " yn
    case $yn in
      [Yy]* ) break;;
      [Nn]* ) exit;;
      * ) printf "${bold}ERROR:${normal} Please answer with y or n\n";;
    esac
  done
}

if [ -n "$1" ]; then
        # get number of cards in system
        n=`ls -d /sys/class/cxl/card* | awk -F"/sys/class/cxl/card" '{ print $2 }' | wc -w`
        if ! [[ $1 =~ $isnumber ]] ; then
          echo "error: 1rst argument is not a number. usage: capi-reset 0 user" >&2; exit 1
        fi
        card=$((10#$1))
        if (($card < 0 )) || (( "$card" > "$n-1" )); then
                printf "${bold}ERROR:${normal} Wrong card number ${card}\n"
                exit 1
        fi
        if [ -n "$2" ]; then
            region=$2;
            if [[ "$region" != "factory" && "$region" != "user" ]]; then
                    printf "${bold}ERROR:${normal} Only supports \"factory\" or \"user\".\n"
                    exit 1
                fi
            reset_card $card $region "Resetting CAPI Adapter $card"
        else
            reset_card $card factory "Resetting CAPI Adapter $card"
            #printf "Bad argument\n"
           # exit 1
        fi
else
        confirm
        #Find all the CAPI cards in the system
        cardnums=`ls -d /sys/class/cxl/card* | awk -F"/sys/class/cxl/card" '{ print $2 }'`
        for i in $cardnums; do
                reset_card $i factory "Resetting CAPI Adapter $i"
        done
fi
