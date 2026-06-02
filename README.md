# DC34_Main_Badge_Fuzzer_SAO
NilbinSec SAO for the DC34 Conference Badge that serves as a fuzzing tool for potential badge challenges.

This SAO is designed as a tool for hackers who want a quick way to fuzz specific parts of the DEF CON 34 main conference badge based on the initial information DEF CON released about the badge prior to the conference. This may be insanely useful, or may be completely useless in solving the annual badge challenge. Moreover, Mode 1 may harm your badge depending on how the badge designer has set up the GPIO pins.

# Modes
The Fuzzer SAO has 3 modes with the initial firmware:

Mode 1: Attack button cycles through GPIO1/2 on the left badge port attempting to interface/trigger the actions active on the pull-up/pull-downs on the GPIO pins for the main board.

Mode 2: Attack button triggers the open-drain wake-up interrupt to the CPU on GPIO4 (right SAO port)

Mode 3: Attack button reports as a slave i2c interface and listens for a response from the badge CPU to identify I2C addresses the badge is interested in by incrementally cycling through I2C addresses 0x00 - 0x7F

# Usage
Mode is set via the left momentary switch (mode select). Press the button the number of times of the mode you want. SAO will confirm by: M1 -- D1 Blink, M2 -- D2 Blink, M3 -- D3 Blink. In each case, the Mode LED light will remain solid afterward indicating the attack is ready. To trigger a mode, press the right momentary switch (Attack/Trigger)

There is a strong possibility that this badge will be able to be reflashed at the convention to solve additional issues/speed up solving badge challenges further into the badge challenge. We will likely release updated firmware.

# Flashing

Download firmware files. If you want to remake the firmware blob, the c file is included. You will need Zak Kemble's standalone AVR-GCC build from https://blog.zakkemble.net/avr-gcc-builds/ , the ATtiny series-1 device pack from http://packs.download.atmel.com/ (file Atmel.ATtiny_DFP.x.x.xxx.atpack — .atpack is just a renamed zip). Make sure both are on path for your CLI. 

To flash: "make flash PORT=COM5" (replace COM5 with your device's COM port)

# Repo Files

This repo includes base firmware, gerbers for the PCBs, schematic, and BOM file. Don't hesitate to reach out if you need something! 

<img width="706" height="652" alt="image" src="https://github.com/user-attachments/assets/96fc672f-7217-4e9b-a069-11640e351118" />



<img width="555" height="663" alt="image" src="https://github.com/user-attachments/assets/9b8afcd0-8e3d-438a-b309-bd02be3308d0" /> <img width="566" height="633" alt="image" src="https://github.com/user-attachments/assets/b8611a0b-e358-4dd9-a149-9161bfe696d6" />


