#----------------------------- User configuration -----------------------------

# Common settings
#------------------------

# How you're loading your module. Used to determine how to find the target module. (AsRtld/Module/Kip)
LOAD_KIND := Module

# Program you're targetting. Used to determine where to deploy your files.
# TowerFall (Switch) application title id.
PROGRAM_ID := 0100a1c00359c000

# Optional path to copy the final ELF to, for convenience.
ELF_EXTRACT :=

# Python command to use. Must be Python 3.4+.
PYTHON := python3

# JSON to use to make .npdm (application, since we inject into a game)
NPDM_JSON := application.json

# Additional C/C++ flags to use.
C_FLAGS := -ffp-contract=off -Wno-error
CXX_FLAGS := -ffp-contract=off

# AsRtld settings
#------------------------

# Path to the SD card. Used to mount and deploy files on SD, likely with hekate UMS.
MOUNT_PATH := /mnt/k

# Module settings
#------------------------

# Settings for deploying over FTP. Used by the deploy-ftp.py script.
# Private settings (TFNX_SERVER, FTP_IP, RYU_PATH) come from ~/.config/tfnx/local.mk, outside the repo.
-include $(HOME)/.config/tfnx/local.mk
FTP_IP ?=
FTP_PORT := 5000
FTP_USERNAME := anonymous
FTP_PASSWORD :=

# Settings for deploying to Ryu. Used by the deploy-ryu.sh script.
# NOTE: this path has a space ("Application Support"). deploy-ryu.sh now quotes
# all path expansions, so `./exlaunch.sh deploy-ryu` works directly.
RYU_PATH ?= $(HOME)/Library/Application Support/Ryujinx

# The Rust staticlib (switch-online/ggrs-spike), linked in for the GGRS test.
EXTRA_LDFLAGS := -nodefaultlibs
EXTRA_LIBS := -L$(TFX_NET_RUSTLIB) -lggrs_spike -lgcc
