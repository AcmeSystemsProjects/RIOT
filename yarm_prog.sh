cd /home/andrew/RIOT/drivers/ata8510/firmware
ll firmware.hex 
#
echo  "Put the switch in AVR position !!!!!!"
#
sudo avrdude -c atmelice_isp -p ata8510 -C +ata8510.conf -U eeprom:w:firmware.hex:i


EXTRA_PATH=`pwd`/gcc-arm-none-eabi-5_4-2016q2/bin/
if [ "`which arm-none-eabi-gcc`" != "$EXTRA_PATH/arm-none-eabi-gcc" ]; then
  export PATH=$EXTRA_PATH:$PATH
fi

cd ~/RIOT/examples/default

BOARD=yarm make flash
BOARD=yarm make term
