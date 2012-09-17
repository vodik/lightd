## dimmer

A simple daemon that automatically dims the screen. It listens to evdev
events and monitors for inactivity.

### Notes:

For ``xf86-input-synaptic`` users, please configure synaptics with:

```
Option "GrabEventDevice" "false"
```

Otherwise ``dimmer`` won't be able to watch for inactivity. The driver
likes to be greedy with events by default.
