/* 
  esp8266_waveformPulseCounter - Generates a waveform on a GPIO pin for a set number of pulses.
  
  Based on esp8266_waveform by Earle F. Philhower, III from the esp8266 Arduino framework.
  The code has been modified such that:
    - Waveforms can no longer time out
	- Waveforms run for a set number of pulses before going defunct
	- Only one output pin is supported at a time, configurable by the user
	- GPIO16 is not supported at all
	- Generic timer callback function has been removed
	- A callback function for pulse count depletion has been added
	- A callback function for high->low pulse transitions have been added
	
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


#include "include/waveformPulseCounter.h"

// Need speed, not size, here
#pragma GCC optimize ("O3")

// Maximum delay between IRQs
#define MAXIRQUS (10000)

// If the cycles from now to an event are below this value, perform it anyway since IRQs take longer than this
#define CYCLES_FLUFF (100)

// Macro to get count of predefined array elements
#define countof(a) ((size_t)(sizeof(a)/sizeof(a[0])))

// Set/clear *any* GPIO
#define SetGPIOPin(a) do { if (a < 16) { GPOS |= (1<<a); } else { GP16O |= 1; } } while (0)
#define ClearGPIOPin(a) do { if (a < 16) { GPOC |= (1<<a); } else { GP16O &= ~1; } } while (0)
// Set/clear GPIO 0-15
#define SetGPIO(a) do { GPOS = a; } while (0)
#define ClearGPIO(a) do { GPOC = a; } while (0)

      // Waveform generator can create tones, PWM, and servos
  typedef struct {
    uint32_t nextServiceCycle;        // ESP cycle timer when a transition required
    uint16_t gpioMask;                // Mask instead of value to speed IRQ loop
    unsigned state              : 1;  // Current state of this pin
    unsigned nextTimeHighCycles : 31; // Copy over low->high to keep smooth waveform
    unsigned enabled            : 1;  // Is this GPIO generating a waveform?
    unsigned nextTimeLowCycles  : 31; // Copy over high->low to keep smooth waveform
    uint32_t pulsesToGo;
  } Waveform;
  
  // These can be accessed in interrupts, so ensure to bracket access with SEI/CLI
  static Waveform waveform = {0, 0, 0, 0, 0, 0, 0};

  static void (*perPulseCB)() = NULL;
  static void (*pulsesDepletedCB)() = NULL;
  
  // Helper functions
  static inline ICACHE_RAM_ATTR uint32_t MicrosecondsToCycles(uint32_t microseconds) {
    return clockCyclesPerMicrosecond() * microseconds;
  }
  
  static inline ICACHE_RAM_ATTR uint32_t min_u32(uint32_t a, uint32_t b) {
    if (a < b) {
      return a;
    }
    return b;
  }
  
  static inline ICACHE_RAM_ATTR void ReloadTimer(uint32_t a) {
    // Below a threshold you actually miss the edge IRQ, so ensure enough time
    if (a > 32) {
      timer1_write(a);
    } else {
      timer1_write(32);
    }
  }
  
  static inline ICACHE_RAM_ATTR uint32_t GetCycleCount() {
    uint32_t ccount;
    __asm__ __volatile__("esync; rsr %0,ccount":"=a"(ccount));
    return ccount;
  }
  
  // Interrupt on/off control
  static ICACHE_RAM_ATTR void timer1Interrupt();
  static uint8_t timerRunning = false;
  static uint32_t lastCycleCount = 0; // Last ESP cycle counter on running the interrupt routine
  
  static void initTimer() {
    timer1_disable();
    timer1_isr_init();
    timer1_attachInterrupt(timer1Interrupt);
    lastCycleCount = GetCycleCount();
    timer1_enable(TIM_DIV1, TIM_EDGE, TIM_SINGLE);
    timerRunning = true;
  }
  
  static void deinitTimer() {
    timer1_attachInterrupt(NULL);
    timer1_disable();
    timer1_isr_init();
    timerRunning = false;
  }
  
int setWaveformPulseCountPin(uint32_t pin)
{
    if(timerRunning || pin == 16)
    {
        return false;
    }
    else
    {
        Waveform *wave = (Waveform*) & (waveform);
        wave->gpioMask = 1<<pin;
        return true;
    }
}

  // Start up a waveform on a pin, or change the current one.  Will change to the new
  // waveform smoothly on next low->high transition.  For immediate change, stopWaveform()
  // first, then it will immediately begin.
  int startWaveform(uint32_t timeHighUS, uint32_t timeLowUS, uint32_t pulses) {
    Waveform *wave = (Waveform*) & (waveform);
  
    // To safely update the packed bitfields we need to stop interrupts while setting them as we could
    // get an IRQ in the middle of a multi-instruction mask-and-set required to change them which would
    // then cause an IRQ update of these values (.enabled only, for now) to be lost.
    ets_intr_lock();
  
    wave->nextTimeHighCycles = MicrosecondsToCycles(timeHighUS) - 70;  // Take out some time for IRQ codepath
    wave->nextTimeLowCycles = MicrosecondsToCycles(timeLowUS) - 70;  // Take out some time for IRQ codepath
    wave->pulsesToGo = pulses;
    if (!wave->enabled) {
      wave->state = 0;
      // Actually set the pin high or low in the IRQ service to guarantee times
      wave->nextServiceCycle = GetCycleCount() + MicrosecondsToCycles(1);
      wave->enabled = 1;
      if (!timerRunning) {
        initTimer();
      }
      ReloadTimer(MicrosecondsToCycles(1)); // Cause an interrupt post-haste
    }
  
    // Re-enable interrupts here since we're done with the update
    ets_intr_unlock();
  
    return true;
  }
  
  // Stops a waveform on a pin
  int stopWaveform() {
    // Can't possibly need to stop anything if there is no timer active
    if (!timerRunning) {
      return false;
    }
  
    if (!waveform.enabled) {
      return false; // Skip fast to next one, can't need to stop this one since it's not running
    }
    // Note that there is no interrupt unsafety here.  The IRQ can only ever change .enabled from 1->0
    // We're also doing that, so even if an IRQ occurred it would still stay as 0.
    waveform.enabled = 0;
    deinitTimer();
    return true;
  }
  
  static ICACHE_RAM_ATTR void timer1Interrupt() {
    uint32_t nextEventCycles;
    #if F_CPU == 160000000
    uint8_t cnt = 20;
    #else
    uint8_t cnt = 10;
    #endif
    
    do {
      nextEventCycles = MicrosecondsToCycles(MAXIRQUS);
      Waveform *wave = &waveform;
      uint32_t now;
  
      // If it's not on, ignore!
      if (!wave->enabled) {
        return;
      }
  
      // Check for toggles
      now = GetCycleCount();
      int32_t cyclesToGo = wave->nextServiceCycle - now;
      if (cyclesToGo < 0) {
        wave->state = !wave->state;
        if (wave->state) {
          SetGPIO(wave->gpioMask);
          wave->nextServiceCycle = now + wave->nextTimeHighCycles;
          nextEventCycles = min_u32(nextEventCycles, wave->nextTimeHighCycles);
        } else {
          ClearGPIO(wave->gpioMask);
          wave->nextServiceCycle = now + wave->nextTimeLowCycles;
          nextEventCycles = min_u32(nextEventCycles, wave->nextTimeLowCycles);
          wave->pulsesToGo = wave->pulsesToGo - 1;
          if(perPulseCB)
            perPulseCB();
        }
      } else {
        uint32_t deltaCycles = wave->nextServiceCycle - now;
        nextEventCycles = min_u32(nextEventCycles, deltaCycles);
      }
    } while (--cnt && (nextEventCycles < MicrosecondsToCycles(4)));
  
    uint32_t curCycleCount = GetCycleCount();
    lastCycleCount = curCycleCount;
  
    if(!waveform.pulsesToGo)
    {
        stopWaveform();
        if(pulsesDepletedCB)
            pulsesDepletedCB();
    }
  
    #if F_CPU == 160000000
    if (nextEventCycles <= 5 * MicrosecondsToCycles(1)) {
      nextEventCycles = MicrosecondsToCycles(1) / 2;
    } else {
      nextEventCycles -= 5 * MicrosecondsToCycles(1);
    }
    nextEventCycles = nextEventCycles >> 1;
    #else
    if (nextEventCycles <= 6 * MicrosecondsToCycles(1)) {
      nextEventCycles = MicrosecondsToCycles(1) / 2;
    } else {
      nextEventCycles -= 6 * MicrosecondsToCycles(1);
    }
    #endif
  
    ReloadTimer(nextEventCycles);
  }

  // Set a callback.  Pass in NULL to stop it
void setPerPulseCallback(void (*fn)()) {
  perPulseCB = fn;
  if (!timerRunning && fn) {
    initTimer();
  } else if (timerRunning && !fn) {
    if (!waveform.enabled) {
      deinitTimer();
    }
  }
  ReloadTimer(MicrosecondsToCycles(1)); // Cause an interrupt post-haste
}

  // Set a callback.  Pass in NULL to stop it
void setPulsesDepletedCallback(void (*fn)()) {
  pulsesDepletedCB = fn;
  if (!timerRunning && fn) {
    initTimer();
  } else if (timerRunning && !fn) {
    if (!waveform.enabled) {
      deinitTimer();
    }
  }
  ReloadTimer(MicrosecondsToCycles(1)); // Cause an interrupt post-haste
}