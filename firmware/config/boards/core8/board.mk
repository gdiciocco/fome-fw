# List of all the board related files.
BOARDCPPSRC =  $(BOARD_DIR)/board_configuration.cpp

# Override DEFAULT_ENGINE_TYPE
SHORT_BOARD_NAME = core8
DDEFS += -DFIRMWARE_ID=\"core8\"
DDEFS += -DDEFAULT_ENGINE_TYPE=MINIMAL_PINS

# Core8 keeps the embedded INI USB drive, but does not use the SD card.
DDEFS += -DEFI_FILE_LOGGING=FALSE -DEFI_USB_MSD=TRUE
# Capture the slow ADC gap performance trace on this diagnostic build.
DDEFS += -DCORE8_ADC_GAP_TRACE
USE_FATFS = no

# Core8 v2.3/v2.4 schematics: PG11 is LED3; PD14 drives Low Side 9.
DDEFS += -DLED_CRITICAL_ERROR_BRAIN_PIN=Gpio::G11
