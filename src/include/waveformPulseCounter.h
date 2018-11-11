/* 
  esp8266_waveformPulseCounter - Generates a waveform on a GPIO pin for a set number of pulses.
  
  Based on esp8266_waveform by Earle F. Philhower, III from the esp8266 Arduino framework.
  Copyright (c) 2018 Earle F. Philhower, III.  All rights reserved.
  Copyright (c) 2018 Open MYR, LLC.  All rights reserved.
 
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/


#ifndef __ESP8266_WAVEFORM_PULSECNT_H
#define __ESP8266_WAVEFORM_PULSECNT_H

#include "Arduino.h"

#ifdef __cplusplus
extern "C" {
#endif

//Set which pin will output the waveform. Won't work while the timer nterrupt is running
//or if you're trying to use pin 16.
int setWaveformPulseCountPin(uint32_t pin);

//Start a waveform with the specified duty for the specified number of pulses. A pulse is counted
//every high->low transition
int startWaveform(uint32_t timeHighUS, uint32_t timeLowUS, uint32_t pulses);

//Stop the waveform
int stopWaveform();

//Set a callback function that occurs every time a pulse is counted
void setPerPulseCallback(void (*fn)());

//Set a callback fucntion when the pulse counter reaches 0
void setPulsesDepletedCallback(void (*fn)());

#ifdef __cplusplus
}
#endif

#endif