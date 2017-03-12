#
# apt-get install libusb-dev libdbus-1-dev libglib2.0-dev automake libudev-dev libical-dev libreadline-dev libbluetooth-dev
# apt-get install emacs emacs24-el
./bootstrap && ./configure --enable-debug --enable-static --enable-pie --enable-test && make
