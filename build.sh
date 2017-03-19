#
# apt-get install libusb-dev libdbus-1-dev libglib2.0-dev automake libudev-dev libical-dev libreadline-dev libbluetooth-dev libpcap-dev
# apt-get install emacs emacs24-el
# systemctl status gattclient.service
# systemctl enable gattclient.service
# systemctl start gattclient.service
# systemctl status bluetoothctl.service
# systemctl enable bluetoothctl.service
# systemctl start bluetoothctl.service
# gatttool -b 88:C2:55:BC:73:AF --char-write-req --handle=0x0013 --value=0200 --listen
./bootstrap && ./configure --enable-debug --enable-static --enable-pie --enable-test && make
