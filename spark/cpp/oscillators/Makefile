# Project Name
TARGET = oscillators

USE_DAISYSP_LGPL = 0

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
