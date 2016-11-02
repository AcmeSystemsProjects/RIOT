# name of your application
APPLICATION = gnrc_minimal

# If no BOARD is found in the environment, use this default:
BOARD ?= native

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/../..

BOARD_INSUFFICIENT_MEMORY := chronos msb-430 msb-430h

## Uncomment to support the XBee module and nomac
#USEMODULE += xbee
#USEMODULE += gnrc_nomac

## set default UART to use in case none was defined
#XBEE_UART ?= "UART_NUMOF-1"

## export UART to params file
#CFLAGS += -DXBEE_UART=$(XBEE_UART)

## add current directory to the include path. Putting it in CFLAGS will make
## it go to the beginning, before the standard includes.
## That way xbee_params.h get's included and auto configuration can pick it up.
#CFLAGS += -I$(CURDIR)

# Include packages that pull up and auto-init the link layer.
# NOTE: 6LoWPAN will be included if IEEE802.15.4 devices are present
USEMODULE += gnrc_netdev_default
USEMODULE += auto_init_gnrc_netif
# Specify the minimum networking modules for IPv6
USEMODULE += gnrc_ipv6
USEMODULE += gnrc_ndp
# Additional networking modules that can be dropped if not needed
USEMODULE += gnrc_icmpv6_echo

# Header's compressions
#USEMODULE += gnrc_sixlowpan_iphc

# Use minimal standard PRNG
USEMODULE += prng_minstd

# Add also the shell, some shell commands
USEMODULE += shell
USEMODULE += shell_commands
USEMODULE += ps
USEMODULE += netstats_l2
USEMODULE += netstats_ipv6
USEMODULE += netstats_rpl

CFLAGS += -DGNRC_PKTBUF_SIZE=512 -DGNRC_IPV6_NETIF_ADDR_NUMOF=4 -DGNRC_IPV6_NC_SIZE=1

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

include $(RIOTBASE)/Makefile.include

# Set a custom channel if needed
ifeq (,$(filter cc110x,$(USEMODULE)))     # radio is cc110x sub-GHz
  DEFAULT_CHANNEL ?= 0
  CFLAGS += -DCC110X_DEFAULT_CHANNEL=$(DEFAULT_CHANNEL)
else
  ifeq (,$(filter at86rf212b,$(USEMODULE)))   # radio is IEEE 802.15.4 sub-GHz
    DEFAULT_CHANNEL ?= 5
    CFLAGS += -DIEEE802154_DEFAULT_SUBGHZ_CHANNEL=$(DEFAULT_CHANNEL)
  else                                      # radio is IEEE 802.15.4 2.4 GHz
    DEFAULT_CHANNEL ?= 26
    CFLAGS += -DIEEE802154_DEFAULT_CHANNEL=$(DEFAULT_CHANNEL)
  endif
endif


#
#> ping6 fe80::ff:fe00:547a
#_send: data=[
#	 41 9c 0a 00 00 7a 54 00 fe ff 00 00 02 26 0f
#-----
#	 41
#-----
#	
#	 60 00 00 00 00 0c 3a 40 fe 80 00 00 00 00 00 00
#	 00 00 00 ff fe 00 0f 26 fe 80 00 00 00 00 00 00
#	 00 00 00 ff fe 00 54 7a
#-----
#	 80 00 76 16 00 55 00 01
#	 55 55 55 55
#-----
#	
#], len=68
#
#
#_send: data=[
#	 41 9c 29 00 00 7a 54 00 fe ff 00 00 02 26 0f
#-----
#	 7a
#	 33 3a
#-----
#	 80 00 6e 08 00 59 00 03 59 59 59 59
#-----
#	
#], len=30
#
