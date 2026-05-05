usbipd list
usbipd bind --busid 1-4
usbipd attach --wsl --busid <BUSID>
usbipd detach --busid <BUSID>
usbipd.exe attach --wsl --busid 1-4 --auto-attach > /dev/null 2>&1 &
