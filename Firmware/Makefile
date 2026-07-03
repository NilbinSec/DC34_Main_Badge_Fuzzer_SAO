# DC34 DEF CON SAO Fuzzer firmware -- ATtiny1614

MCU     := attiny1614
F_CPU   := 10000000UL
TARGET  := dc34_fuzzer
SRC     := main.c

# --- Toolchain ---------------------------------------------------------
CC      := avr-gcc
OBJCOPY := avr-objcopy
SIZE    := avr-size

# --- avrdude / programmer ---------------------------------------------
# Override on the command line, e.g. `make flash PORT=COM7 BAUD=230400`
AVRDUDE      := avrdude
PROG         := serialupdi
PORT         ?= COM5
BAUD         ?= 115200

CFLAGS  := -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -std=c11 \
           -Wall -Wextra -Wno-unused-function \
           -ffunction-sections -fdata-sections -flto
LDFLAGS := -mmcu=$(MCU) -Wl,--gc-sections -flto

.PHONY: all size flash erase verify clean

all: $(TARGET).hex size

$(TARGET).elf: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

size: $(TARGET).elf
	$(SIZE) $<

flash: $(TARGET).hex
	$(AVRDUDE) -c $(PROG) -p $(MCU) -P $(PORT) -b $(BAUD) -U flash:w:$<:i

verify: $(TARGET).hex
	$(AVRDUDE) -c $(PROG) -p $(MCU) -P $(PORT) -b $(BAUD) -U flash:v:$<:i

erase:
	$(AVRDUDE) -c $(PROG) -p $(MCU) -P $(PORT) -b $(BAUD) -e

clean:
	-rm -f $(TARGET).elf $(TARGET).hex
