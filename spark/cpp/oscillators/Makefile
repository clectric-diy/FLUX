# Project Name
TARGET = oscillators

USE_DAISYSP_LGPL = 1

# Size vs speed: -Os usually saves several KB in this image vs libDaisy's default -O2.
# For stepping in gdb you can set DEBUG=1 (enables extra debug) or override OPT, e.g. OPT=-O2.
OPT ?= -Os

# These trim USB log strings in daisy_spark + oscillators (hundreds+ bytes of .rodata):
# 0 = skip PrintBanner; 1 = show startup banner
SPARK_DIAG_BANNER ?= 0
# 0 = skip delayed I2C bus scan to serial; 1 = run scan 2s after boot
SPARK_I2C_SCAN_BOOT ?= 0
C_DEFS += -DSPARK_DIAG_BANNER=$(SPARK_DIAG_BANNER) -DSPARK_I2C_SCAN_BOOT=$(SPARK_I2C_SCAN_BOOT)

# Uncomment to ship without serial debug (largest savings; vsnprintf/float print paths):
# C_DEFS += -DSPARK_DEBUG_ENABLE=0

C_INCLUDES += -I../

# Sources
CPP_SOURCES = oscillators.cpp \
			  ../daisy_spark.cpp

# Library Locations
LIBDAISY_DIR = ../libDaisy
DAISYSP_DIR = ../DaisySP

# Core location, and generic makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
