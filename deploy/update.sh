#!/bin/sh
set -e
cd /opt/voidwarzone/src
git pull -q
cmake --preset linux-server > /dev/null
cmake --build --preset linux-server --target vw_master vw_server vw_bot -j 1
B=build/linux-server/bin
install -m 755 $B/vw_master $B/vw_server $B/vw_bot /opt/voidwarzone/
rm -rf /opt/voidwarzone/data
cp -r data /opt/voidwarzone/data
cp deploy/vw-master.service deploy/vw-server.service /etc/systemd/system/
systemctl daemon-reload
systemctl restart vw-master vw-server
systemctl is-active vw-master vw-server
