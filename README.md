## backlight-utils

Some utilities I've written to managing my Thinkpad's backlight.

### bset

    usage: bset [options] [value]
    Options:
     -h, --help             display this help and exit
     -v, --version          display version
     -i, --inc              increment the backlight
     -d, --dec              decrement the backlight

`bset` is a simple utility to control the backlight. It is suid so
any normal user can control the brightness.

### lightd

    usage: lightd [options]
    Options:
     -h, --help             display this help and exit
     -v, --version          display version
     -D, --dimmer           dim the screen when inactivity detected
     -d, --dim=VALUE        the amount to dim the screen by
     -t, --timeout=VALUE    set the timeout till the screen is dimmed

`lightd` is a simple daemon that managed the backlight in userspace and
can do things like automatically dims the screen after a period of
inactivity. It listens to udev for device and power statue events and
evdev to read input devices.

**NOTE**: For `xf86-input-synaptic` users, the module had to be
configured not to grab the device.

    Option "GrabEventDevice" "false"

Otherwise `lightd` won't be able to wake on activity on the touchpad,
the driver likes to be greedy by default. I should ship a configlet to
deal with this.

### TODO

- config file for lightd
