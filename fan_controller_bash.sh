#!/bin/bash

<<LICENSE
    Copyright (C) 2018  kevinlekiller
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
    https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
LICENSE

# Sends CPU temp through the arduino serial interface, tries to monitor / restart if the arduino disconnects.
# You will need to change the sensors command below if your output does not look exactly like mine:
# temp1:        +53.2 C  (high = +70.0 C)

if [[ $(id -u) != 0 ]]; then
        echo "Requires root"
        exit 1
fi
while [[ true ]]; do
        while [[ $port == "" ]]; do
                port=$(ls /dev/ttyA* 2> /dev/null | head -n1)
                if [[ $port ]]; then
                        echo "Found port $port"
                        break
                fi
                echo "Failed to find Arduino."
                sleep 15
        done
        stty -F "$port" ispeed 9600 ospeed 9600 -ignpar cs8 -cstopb -echo
        while [[ -e $port ]]; do
                temperature="$(sensors | grep -Poi "temp1:\s+\+[\d.]+ C" | grep -Po "[0-9]{1,2}\.\d{1,2}")"
                temperature="$(echo $temperature | awk '{print int($1)}')"
                if [[ $temperature -ge 95 ]]; then
                        temperature=95
                elif [[ $temperature -le 20 ]]; then
                        temperature=20
                fi
                echo "$temperature" > "$port"
                sleep 1
                if [[ ! $(cat $port) ]]; then
                    break
                fi
        done
        echo "Lost arduino serial connection"
        port=""
        sleep 15
done
