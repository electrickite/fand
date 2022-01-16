# fand

A simple daemon to control PWM (Pulse Width Modulation) cooling fans on FreeBSD
systems. fand was originally written for use on arm64 single board computers,
but should work on any cooling fan driven by a `pwmc(4)` device.

## Installation

Compile and install fand with the following commands:

    # make
    # make install

Uninstall with:

    # make uninstall

## Usage and options

See thea `fand(8)` man page for command usage and options.

To determine the correct settings for `-K` (Kelvin) and `-m` (multiplier), it
can be helpful to compare the output from `sysctl` for the temperature sensor
and the temperature reported by using the `fand -s` option.

## rc.d Service

fand provides an rc.d service. It can be configured using the following
rc variables:

  * `fand_enable` - Enable/disable the service
  * `fand_sensor` - The sysctl variable containing the system temperature
  * `fand_device` - The pwmc fan controller device
  * `fand_temp` - Temperature (°C) at which the fan is enabled
  * `fand_off_temp` - Temperature (°C) at which the fan is disabled
  * `fand_period` - The period in nanoseconds of the PWM channel
  * `fand_duty` - The PWM channel duty cycle in nanoseconds or percentage)
  * `fand_kelvin` - The sysctl temperature sensor is in degrees Kelvin
  * `fand_multiplier` - A multiplier to apply to the reported temperature
  * `fand_flags` - Additional command options to pass to fand

Note that both `fand_sensor` and `fand_device` are required to start the
service.

### rc.d Example

The following rc.d configuration will use the "hw.temp.CPU" sensor and the
pwmc1.0 controller to run the fan at half duty cycle when the temperature
reaches 45C. The fan will be disabled when the temperature falls below 35C.
When reading from the sensor, fand will treat the value as tenths of a degree
Kelvin.

    fand_enable="YES"
    fand_sensor="hw.temp.CPU"
    fand_device="pwmc1.0"
    fand_temp="45"
    fand_off_temp="35"
    fand_period="50000"
    fand_duty="25000"
    fand_kelvin="YES"
    fand_multiplier="0.1"

## License and Copyright

Copyright (c) 2022 Corey Hinshaw <corey@electrickite.org>

fand is released under the BSD 2-Clause license. See the LICENSE file included
with the project source code.
