# esp8266_waveformPulseCounter

Generates a waveform that runs for a set number of pulses. Uses timer1 interrupt. Don't mix with projects that use the waveform generator that comes with the ESP8266 Arduino framework (including Servo, Tone, etc).

## Attribution

Based on esp8266_waveform by Earle F. Philhower, III from the esp8266 Arduino framework.

Copyright (c) 2018 Earle F. Philhower, III.  All rights reserved.
  
Copyright (c) 2018 Open MYR, LLC.  All rights reserved.

## Modifications from original code
The code has been modified such that:
* Waveforms can no longer time out
* Waveforms run for a set number of pulses before going defunct
* Only one output pin is supported at a time, configurable by the user
* GPIO16 is not supported at all
* Generic timer callback function has been removed
* A callback function for pulse count depletion has been added
* A callback function for high->low pulse transitions have been added
	
## Licensing
This software is licensed under LGPL version 2.1.