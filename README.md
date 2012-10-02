## backlight-utils

Some utilities I've written to managing my Thinkpad's backlight.

### bset

```
usage: bset [value]
```

`bset` is a simple utility to control the backlight. It is suid so
any normal user can control the brightness.

### dimmer

```
usage: dimmer [options]
Options:
 -h, --help             display this help and exit
 -v, --version          display version
 -t, --timeout=TIME     the timeout in seconds to dim after
 -d, --dim-by=LEVEL     how much to dim by
```

`dimmer` is a simple daemon that automatically dims the screen. It
listens to evdev events and monitors for inactivity.

**NOTE**: For ``xf86-input-synaptic`` users, please configure synaptics
with:

```
Option "GrabEventDevice" "false"
```

Otherwise ``dimmer`` won't be able to watch for inactivity. The driver
likes to be greedy with events by default.
