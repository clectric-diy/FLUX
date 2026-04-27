# Project Name
TARGET = filters

USE_DAISYSP_LGPL = 0

OPT ?= -Os
SPARK_DIAG_BANNER ?= 0
SPARK_I2C_SCAN_BOOT ?= 0
C_DEFS += -DSPARK_DIAG_BANNER=$(SPARK_DIAG_BANNER) -DSPARK_I2C_SCAN_BOOT=$(SPARK_I2C_SCAN_BOOT)

C_INCLUDES += -I../

# Sources
CPP_SOURCES = filters.cpp \
			  ../daisy_spark.cpp

# Library Locations
LIBDAISY_DIR = ../libDaisy
DAISYSP_DIR = ../DaisySP

# Core location, and generic makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
