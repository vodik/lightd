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
     -t, --timeout=TIME     the timeout in seconds to dim after
     -d, --dim-by=LEVEL     how much to dim by

`lightd` is a simple daemon that managed the backlight in userspace and
can do things like automatically dims the screen after a period of
inactivity. It listens to evdev events and monitors for inactivity.

**NOTE**: For ``xf86-input-synaptic`` users, the module had to be
configured not to grab the device.

    Option "GrabEventDevice" "false"

Otherwise ``lightd`` won't be able to wake on activity on the touchpad,
the driver likes to be greedy by default. I should ship a configlet to
deal with this.

### TODO

- config file for lightd
