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
# The build's extras (docs/PLAN-MEGAMOD-2026-10-30.md "Distribution"): 1 "TF.EX Switch + extras" (the megamod: TowerBall, 8 players, three
# teams, local play), 0 "TF.EX Switch" (TF.EX and the FortRise loader only). tools/build-probe.sh sets it (XPLAY_EXTRAS=0: deploy-core/).
XPLAY_EXTRAS ?= 1
CXX_FLAGS += -DXPLAY_EXTRAS=$(XPLAY_EXTRAS)
# TF.NX's version is TF.EX's plus this build number: 0.29.0.0 = TF.NX's first release for TF.EX 0.29.0, 0.29.0.1 the next one between
# TF.EX versions (main.cpp TFNX_VERSION; the boot line, tools/release.sh). Raise it per release; back to 0 when TF.EX's version changes.
TFNX_BUILD := 2
CXX_FLAGS += -DTFNX_BUILD=\"$(TFNX_BUILD)\"
# Private settings (TFNX_SERVER, FTP_IP, RYU_PATH) come from ~/.config/tfnx/local.mk, outside the repo.
# The matchmaking server built in (first-run default, TF.EX's LOCAL preset): TF.EX's official one unless local.mk names another.
-include $(HOME)/.config/tfnx/local.mk
TFNX_SERVER ?= wss://tfex-server.balatro-vs-matchmaking.eu
CXX_FLAGS += -DTFNX_SERVER=\"$(TFNX_SERVER)\"

# AsRtld settings
#------------------------

# Path to the SD card. Used to mount and deploy files on SD, likely with hekate UMS.
MOUNT_PATH := /mnt/k

# Module settings
#------------------------

# Settings for deploying over FTP. Used by the deploy-ftp.py script.
# FTP_IP comes from ~/.config/tfnx/local.mk (included above), outside the repo.
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
# TF.EX's ggrs_ffi (ggrs-ffi-switch, docs/ws-ggrs-ffi.md), in the same staticlib: its C API is kept in the module (GC roots)
# although nothing calls it yet, so the link and build-probe's unresolved-symbol checks cover it.
GGRS_FFI_KEEP := \
  -Wl,--undefined=ggrs_ffi_ping_measurement_start \
  -Wl,--undefined=ggrs_ffi_ping_measurement_stats \
  -Wl,--undefined=ggrs_ffi_ping_measurement_stop \
  -Wl,--undefined=netplay_add_spectator \
  -Wl,--undefined=netplay_advance_frame \
  -Wl,--undefined=netplay_advance_game_state \
  -Wl,--undefined=netplay_current_frame \
  -Wl,--undefined=netplay_events \
  -Wl,--undefined=netplay_events_free \
  -Wl,--undefined=netplay_frames_ahead \
  -Wl,--undefined=netplay_frames_behind \
  -Wl,--undefined=netplay_free_game_state \
  -Wl,--undefined=netplay_get_requests \
  -Wl,--undefined=netplay_init \
  -Wl,--undefined=netplay_inputs_free \
  -Wl,--undefined=netplay_is_disconnected \
  -Wl,--undefined=netplay_is_synchronized \
  -Wl,--undefined=netplay_load_game_state \
  -Wl,--undefined=netplay_local_player_handle \
  -Wl,--undefined=netplay_network_stats \
  -Wl,--undefined=netplay_poll \
  -Wl,--undefined=netplay_remote_player_handle \
  -Wl,--undefined=netplay_remote_player_handle_at \
  -Wl,--undefined=netplay_remote_player_handle_count \
  -Wl,--undefined=netplay_requests_free \
  -Wl,--undefined=netplay_reset \
  -Wl,--undefined=netplay_save_game_state \
  -Wl,--undefined=netplay_set_test_inputs \
  -Wl,--undefined=netplay_switch_confirmed_frame \
  -Wl,--undefined=netplay_switch_log \
  -Wl,--undefined=netplay_switch_pending_requests \
  -Wl,--undefined=netplay_switch_pump \
  -Wl,--undefined=netplay_switch_socket_bind \
  -Wl,--undefined=netplay_switch_socket_closed \
  -Wl,--undefined=netplay_switch_socket_drive \
  -Wl,--undefined=netplay_switch_socket_keepalive \
  -Wl,--undefined=netplay_switch_socket_log \
  -Wl,--undefined=netplay_switch_socket_next_open \
  -Wl,--undefined=netplay_switch_socket_release \
  -Wl,--undefined=netplay_switch_socket_signal_in \
  -Wl,--undefined=netplay_switch_socket_signal_out \
  -Wl,--undefined=netplay_switch_socket_stun \
  -Wl,--undefined=netplay_switch_socket_udp_in \
  -Wl,--undefined=netplay_switch_socket_udp_out \
  -Wl,--undefined=netplay_switch_socket_wants_close \
  -Wl,--undefined=status_info_free
EXTRA_LDFLAGS += $(GGRS_FFI_KEEP)
EXTRA_LIBS := -L$(CURDIR)/rustlib -lggrs_spike -lgcc
