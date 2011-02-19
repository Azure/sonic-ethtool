/*
 * ethtool.c: Linux ethernet device configuration tool.
 *
 * Copyright (C) 1998 David S. Miller (davem@dm.cobaltmicro.com)
 * Portions Copyright 2001 Sun Microsystems
 * Kernel 2.4 update Copyright 2001 Jeff Garzik <jgarzik@mandrakesoft.com>
 * Wake-on-LAN,natsemi,misc support by Tim Hockin <thockin@sun.com>
 * Portions Copyright 2002 Intel
 * do_test support by Eli Kupermann <eli.kupermann@intel.com>
 * ETHTOOL_PHYS_ID support by Chris Leech <christopher.leech@intel.com>
 * e1000 support by Scott Feldman <scott.feldman@intel.com>
 * e100 support by Wen Tao <wen-hwa.tao@intel.com>
 * ixgb support by Nicholas Nunley <Nicholas.d.nunley@intel.com>
 * amd8111e support by Reeja John <reeja.john@amd.com>
 * long arguments by Andi Kleen.
 * SMSC LAN911x support by Steve Glendinning <steve.glendinning@smsc.com>
 * Various features by Ben Hutchings <bhutchings@solarflare.com>;
 *	Copyright 2009, 2010 Solarflare Communications
 *
 * TODO:
 *   * show settings for all devices
 */

#ifdef HAVE_CONFIG_H
#  include "ethtool-config.h"
#endif

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <net/if.h>
#include <sys/utsname.h>
#include <limits.h>
#include <ctype.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <linux/sockios.h>
#include "ethtool-util.h"


#ifndef SIOCETHTOOL
#define SIOCETHTOOL     0x8946
#endif
#ifndef MAX_ADDR_LEN
#define MAX_ADDR_LEN	32
#endif
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifndef HAVE_NETIF_MSG
enum {
	NETIF_MSG_DRV		= 0x0001,
	NETIF_MSG_PROBE		= 0x0002,
	NETIF_MSG_LINK		= 0x0004,
	NETIF_MSG_TIMER		= 0x0008,
	NETIF_MSG_IFDOWN	= 0x0010,
	NETIF_MSG_IFUP		= 0x0020,
	NETIF_MSG_RX_ERR	= 0x0040,
	NETIF_MSG_TX_ERR	= 0x0080,
	NETIF_MSG_TX_QUEUED	= 0x0100,
	NETIF_MSG_INTR		= 0x0200,
	NETIF_MSG_TX_DONE	= 0x0400,
	NETIF_MSG_RX_STATUS	= 0x0800,
	NETIF_MSG_PKTDATA	= 0x1000,
	NETIF_MSG_HW		= 0x2000,
	NETIF_MSG_WOL		= 0x4000,
};
#endif

static int parse_wolopts(char *optstr, u32 *data);
static char *unparse_wolopts(int wolopts);
static void get_mac_addr(char *src, unsigned char *dest);
static int do_gdrv(int fd, struct ifreq *ifr);
static int do_gset(int fd, struct ifreq *ifr);
static int do_sset(int fd, struct ifreq *ifr);
static int do_gregs(int fd, struct ifreq *ifr);
static int do_nway_rst(int fd, struct ifreq *ifr);
static int do_geeprom(int fd, struct ifreq *ifr);
static int do_seeprom(int fd, struct ifreq *ifr);
static int do_test(int fd, struct ifreq *ifr);
static int do_phys_id(int fd, struct ifreq *ifr);
static int do_gpause(int fd, struct ifreq *ifr);
static int do_spause(int fd, struct ifreq *ifr);
static int do_gring(int fd, struct ifreq *ifr);
static int do_sring(int fd, struct ifreq *ifr);
static int do_gcoalesce(int fd, struct ifreq *ifr);
static int do_scoalesce(int fd, struct ifreq *ifr);
static int do_goffload(int fd, struct ifreq *ifr);
static int do_soffload(int fd, struct ifreq *ifr);
static int do_gstats(int fd, struct ifreq *ifr);
static int rxflow_str_to_type(const char *str);
static int parse_rxfhashopts(char *optstr, u32 *data);
static char *unparse_rxfhashopts(u64 opts);
static void parse_rxntupleopts(int argc, char **argp, int first_arg);
static int dump_rxfhash(int fhash, u64 val);
static int do_srxclass(int fd, struct ifreq *ifr);
static int do_grxclass(int fd, struct ifreq *ifr);
static int do_grxfhindir(int fd, struct ifreq *ifr);
static int do_srxfhindir(int fd, struct ifreq *ifr);
static int do_srxntuple(int fd, struct ifreq *ifr);
static int do_grxntuple(int fd, struct ifreq *ifr);
static int do_flash(int fd, struct ifreq *ifr);
static int do_permaddr(int fd, struct ifreq *ifr);

static int send_ioctl(int fd, struct ifreq *ifr);

static enum {
	MODE_HELP = -1,
	MODE_GSET=0,
	MODE_SSET,
	MODE_GDRV,
	MODE_GREGS,
	MODE_NWAY_RST,
	MODE_GEEPROM,
	MODE_SEEPROM,
	MODE_TEST,
	MODE_PHYS_ID,
	MODE_GPAUSE,
	MODE_SPAUSE,
	MODE_GCOALESCE,
	MODE_SCOALESCE,
	MODE_GRING,
	MODE_SRING,
	MODE_GOFFLOAD,
	MODE_SOFFLOAD,
	MODE_GSTATS,
	MODE_GNFC,
	MODE_SNFC,
	MODE_GRXFHINDIR,
	MODE_SRXFHINDIR,
	MODE_SNTUPLE,
	MODE_GNTUPLE,
	MODE_FLASHDEV,
	MODE_PERMADDR,
} mode = MODE_GSET;

static struct option {
    char *srt, *lng;
    int Mode;
    char *help;
    char *opthelp;
} args[] = {
    { "-s", "--change", MODE_SSET, "Change generic options",
		"		[ speed %d ]\n"
		"		[ duplex half|full ]\n"
		"		[ port tp|aui|bnc|mii|fibre ]\n"
		"		[ autoneg on|off ]\n"
		"		[ advertise %x ]\n"
		"		[ phyad %d ]\n"
		"		[ xcvr internal|external ]\n"
		"		[ wol p|u|m|b|a|g|s|d... ]\n"
		"		[ sopass %x:%x:%x:%x:%x:%x ]\n"
		"		[ msglvl %d | msglvl type on|off ... ]\n" },
    { "-a", "--show-pause", MODE_GPAUSE, "Show pause options" },
    { "-A", "--pause", MODE_SPAUSE, "Set pause options",
      "		[ autoneg on|off ]\n"
      "		[ rx on|off ]\n"
      "		[ tx on|off ]\n" },
    { "-c", "--show-coalesce", MODE_GCOALESCE, "Show coalesce options" },
    { "-C", "--coalesce", MODE_SCOALESCE, "Set coalesce options",
		"		[adaptive-rx on|off]\n"
		"		[adaptive-tx on|off]\n"
		"		[rx-usecs N]\n"
		"		[rx-frames N]\n"
		"		[rx-usecs-irq N]\n"
		"		[rx-frames-irq N]\n"
		"		[tx-usecs N]\n"
		"		[tx-frames N]\n"
		"		[tx-usecs-irq N]\n"
		"		[tx-frames-irq N]\n"
		"		[stats-block-usecs N]\n"
		"		[pkt-rate-low N]\n"
		"		[rx-usecs-low N]\n"
		"		[rx-frames-low N]\n"
		"		[tx-usecs-low N]\n"
		"		[tx-frames-low N]\n"
		"		[pkt-rate-high N]\n"
		"		[rx-usecs-high N]\n"
		"		[rx-frames-high N]\n"
		"		[tx-usecs-high N]\n"
		"		[tx-frames-high N]\n"
	        "		[sample-interval N]\n" },
    { "-g", "--show-ring", MODE_GRING, "Query RX/TX ring parameters" },
    { "-G", "--set-ring", MODE_SRING, "Set RX/TX ring parameters",
		"		[ rx N ]\n"
		"		[ rx-mini N ]\n"
		"		[ rx-jumbo N ]\n"
	        "		[ tx N ]\n" },
    { "-k", "--show-offload", MODE_GOFFLOAD, "Get protocol offload information" },
    { "-K", "--offload", MODE_SOFFLOAD, "Set protocol offload",
		"		[ rx on|off ]\n"
		"		[ tx on|off ]\n"
		"		[ sg on|off ]\n"
	        "		[ tso on|off ]\n"
	        "		[ ufo on|off ]\n"
		"		[ gso on|off ]\n"
		"		[ gro on|off ]\n"
		"		[ lro on|off ]\n"
		"		[ rxvlan on|off ]\n"
		"		[ txvlan on|off ]\n"
		"		[ ntuple on|off ]\n"
		"		[ rxhash on|off ]\n"
    },
    { "-i", "--driver", MODE_GDRV, "Show driver information" },
    { "-d", "--register-dump", MODE_GREGS, "Do a register dump",
		"		[ raw on|off ]\n"
		"		[ file FILENAME ]\n" },
    { "-e", "--eeprom-dump", MODE_GEEPROM, "Do a EEPROM dump",
		"		[ raw on|off ]\n"
		"		[ offset N ]\n"
		"		[ length N ]\n" },
    { "-E", "--change-eeprom", MODE_SEEPROM, "Change bytes in device EEPROM",
		"		[ magic N ]\n"
		"		[ offset N ]\n"
		"		[ length N ]\n"
		"		[ value N ]\n" },
    { "-r", "--negotiate", MODE_NWAY_RST, "Restart N-WAY negotation" },
    { "-p", "--identify", MODE_PHYS_ID, "Show visible port identification (e.g. blinking)",
                "               [ TIME-IN-SECONDS ]\n" },
    { "-t", "--test", MODE_TEST, "Execute adapter self test",
                "               [ online | offline ]\n" },
    { "-S", "--statistics", MODE_GSTATS, "Show adapter statistics" },
    { "-n", "--show-nfc", MODE_GNFC, "Show Rx network flow classification"
		"options",
		"		[ rx-flow-hash tcp4|udp4|ah4|sctp4|"
		"tcp6|udp6|ah6|sctp6 ]\n" },
    { "-f", "--flash", MODE_FLASHDEV, "FILENAME " "Flash firmware image "
    		"from the specified file to a region on the device",
		"               [ REGION-NUMBER-TO-FLASH ]\n" },
    { "-N", "--config-nfc", MODE_SNFC, "Configure Rx network flow "
		"classification options",
		"		[ rx-flow-hash tcp4|udp4|ah4|sctp4|"
		"tcp6|udp6|ah6|sctp6 m|v|t|s|d|f|n|r... ]\n" },
    { "-x", "--show-rxfh-indir", MODE_GRXFHINDIR, "Show Rx flow hash "
		"indirection" },
    { "-X", "--set-rxfh-indir", MODE_SRXFHINDIR, "Set Rx flow hash indirection",
		"		equal N | weight W0 W1 ...\n" },
    { "-U", "--config-ntuple", MODE_SNTUPLE, "Configure Rx ntuple filters "
		"and actions",
		"		{ flow-type tcp4|udp4|sctp4\n"
		"		  [ src-ip ADDR [src-ip-mask MASK] ]\n"
		"		  [ dst-ip ADDR [dst-ip-mask MASK] ]\n"
		"		  [ src-port PORT [src-port-mask MASK] ]\n"
		"		  [ dst-port PORT [dst-port-mask MASK] ]\n"
		"		| flow-type ether\n"
		"		  [ src MAC-ADDR [src-mask MASK] ]\n"
		"		  [ dst MAC-ADDR [dst-mask MASK] ]\n"
		"		  [ proto N [proto-mask MASK] ] }\n"
		"		[ vlan VLAN-TAG [vlan-mask MASK] ]\n"
		"		[ user-def DATA [user-def-mask MASK] ]\n"
		"		action N\n" },
    { "-u", "--show-ntuple", MODE_GNTUPLE,
		"Get Rx ntuple filters and actions\n" },
    { "-P", "--show-permaddr", MODE_PERMADDR,
		"Show permanent hardware address" },
    { "-h", "--help", MODE_HELP, "Show this help" },
    {}
};


static void show_usage(int badarg) __attribute__((noreturn));

static void show_usage(int badarg)
{
	int i;
	if (badarg != 0) {
		fprintf(stderr,
			"ethtool: bad command line argument(s)\n"
			"For more information run ethtool -h\n"
		);
	}
	else {
		/* ethtool -h */
		fprintf(stdout, PACKAGE " version " VERSION "\n");
		fprintf(stdout,
		"Usage:\n"
		"ethtool DEVNAME\tDisplay standard information about device\n");
		for (i = 0; args[i].srt; i++) {
			fprintf(stdout, "        ethtool %s|%s %s\t%s\n%s",
				args[i].srt, args[i].lng,
				strstr(args[i].srt, "-h") ? "\t" : "DEVNAME",
				args[i].help,
				args[i].opthelp ? args[i].opthelp : "");
		}
	}
	exit(badarg);
}

static char *devname = NULL;

static int goffload_changed = 0;
static int off_csum_rx_wanted = -1;
static int off_csum_tx_wanted = -1;
static int off_sg_wanted = -1;
static int off_tso_wanted = -1;
static int off_ufo_wanted = -1;
static int off_gso_wanted = -1;
static u32 off_flags_wanted = 0;
static u32 off_flags_mask = 0;
static int off_gro_wanted = -1;

static struct ethtool_pauseparam epause;
static int gpause_changed = 0;
static int pause_autoneg_wanted = -1;
static int pause_rx_wanted = -1;
static int pause_tx_wanted = -1;

static struct ethtool_ringparam ering;
static int gring_changed = 0;
static s32 ring_rx_wanted = -1;
static s32 ring_rx_mini_wanted = -1;
static s32 ring_rx_jumbo_wanted = -1;
static s32 ring_tx_wanted = -1;

static struct ethtool_coalesce ecoal;
static int gcoalesce_changed = 0;
static s32 coal_stats_wanted = -1;
static int coal_adaptive_rx_wanted = -1;
static int coal_adaptive_tx_wanted = -1;
static s32 coal_sample_rate_wanted = -1;
static s32 coal_pkt_rate_low_wanted = -1;
static s32 coal_pkt_rate_high_wanted = -1;
static s32 coal_rx_usec_wanted = -1;
static s32 coal_rx_frames_wanted = -1;
static s32 coal_rx_usec_irq_wanted = -1;
static s32 coal_rx_frames_irq_wanted = -1;
static s32 coal_tx_usec_wanted = -1;
static s32 coal_tx_frames_wanted = -1;
static s32 coal_tx_usec_irq_wanted = -1;
static s32 coal_tx_frames_irq_wanted = -1;
static s32 coal_rx_usec_low_wanted = -1;
static s32 coal_rx_frames_low_wanted = -1;
static s32 coal_tx_usec_low_wanted = -1;
static s32 coal_tx_frames_low_wanted = -1;
static s32 coal_rx_usec_high_wanted = -1;
static s32 coal_rx_frames_high_wanted = -1;
static s32 coal_tx_usec_high_wanted = -1;
static s32 coal_tx_frames_high_wanted = -1;

static int speed_wanted = -1;
static int duplex_wanted = -1;
static int port_wanted = -1;
static int autoneg_wanted = -1;
static int phyad_wanted = -1;
static int xcvr_wanted = -1;
static int advertising_wanted = -1;
static int gset_changed = 0; /* did anything in GSET change? */
static u32  wol_wanted = 0;
static int wol_change = 0;
static u8 sopass_wanted[SOPASS_MAX];
static int sopass_change = 0;
static int gwol_changed = 0; /* did anything in GWOL change? */
static int phys_id_time = 0;
static int gregs_changed = 0;
static int gregs_dump_raw = 0;
static int gregs_dump_hex = 0;
static char *gregs_dump_file = NULL;
static int geeprom_changed = 0;
static int geeprom_dump_raw = 0;
static s32 geeprom_offset = 0;
static s32 geeprom_length = -1;
static int seeprom_changed = 0;
static s32 seeprom_magic = 0;
static s32 seeprom_length = -1;
static s32 seeprom_offset = 0;
static s32 seeprom_value = EOF;
static int rx_fhash_get = 0;
static int rx_fhash_set = 0;
static u32 rx_fhash_val = 0;
static int rx_fhash_changed = 0;
static int rxfhindir_equal = 0;
static char **rxfhindir_weight = NULL;
static int sntuple_changed = 0;
static struct ethtool_rx_ntuple_flow_spec ntuple_fs;
static int ntuple_ip4src_seen = 0;
static int ntuple_ip4src_mask_seen = 0;
static int ntuple_ip4dst_seen = 0;
static int ntuple_ip4dst_mask_seen = 0;
static int ntuple_psrc_seen = 0;
static int ntuple_psrc_mask_seen = 0;
static int ntuple_pdst_seen = 0;
static int ntuple_pdst_mask_seen = 0;
static int ntuple_ether_dst_seen = 0;
static int ntuple_ether_dst_mask_seen = 0;
static int ntuple_ether_src_seen = 0;
static int ntuple_ether_src_mask_seen = 0;
static int ntuple_ether_proto_seen = 0;
static int ntuple_ether_proto_mask_seen = 0;
static int ntuple_vlan_tag_seen = 0;
static int ntuple_vlan_tag_mask_seen = 0;
static int ntuple_user_def_seen = 0;
static int ntuple_user_def_mask_seen = 0;
static char *flash_file = NULL;
static int flash = -1;
static int flash_region = -1;

static int msglvl_changed;
static u32 msglvl_wanted = 0;
static u32 msglvl_mask = 0;

static enum {
	ONLINE=0,
	OFFLINE,
} test_type = OFFLINE;

typedef enum {
	CMDL_NONE,
	CMDL_BOOL,
	CMDL_S32,
	CMDL_U16,
	CMDL_U32,
	CMDL_U64,
	CMDL_BE16,
	CMDL_IP4,
	CMDL_STR,
	CMDL_FLAG,
	CMDL_MAC,
} cmdline_type_t;

struct cmdline_info {
	const char *name;
	cmdline_type_t type;
	/* Points to int (BOOL), s32, u16, u32 (U32/FLAG/IP4), u64,
	 * char * (STR) or u8[6] (MAC).  For FLAG, the value accumulates
	 * all flags to be set. */
	void *wanted_val;
	void *ioctl_val;
	/* For FLAG, the flag value to be set/cleared */
	u32 flag_val;
	/* For FLAG, points to u32 and accumulates all flags seen.
	 * For anything else, points to int and is set if the option is
	 * seen. */
	void *seen_val;
};

static struct cmdline_info cmdline_gregs[] = {
	{ "raw", CMDL_BOOL, &gregs_dump_raw, NULL },
	{ "hex", CMDL_BOOL, &gregs_dump_hex, NULL },
	{ "file", CMDL_STR, &gregs_dump_file, NULL },
};

static struct cmdline_info cmdline_geeprom[] = {
	{ "offset", CMDL_S32, &geeprom_offset, NULL },
	{ "length", CMDL_S32, &geeprom_length, NULL },
	{ "raw", CMDL_BOOL, &geeprom_dump_raw, NULL },
};

static struct cmdline_info cmdline_seeprom[] = {
	{ "magic", CMDL_S32, &seeprom_magic, NULL },
	{ "offset", CMDL_S32, &seeprom_offset, NULL },
	{ "length", CMDL_S32, &seeprom_length, NULL },
	{ "value", CMDL_S32, &seeprom_value, NULL },
};

static struct cmdline_info cmdline_offload[] = {
	{ "rx", CMDL_BOOL, &off_csum_rx_wanted, NULL },
	{ "tx", CMDL_BOOL, &off_csum_tx_wanted, NULL },
	{ "sg", CMDL_BOOL, &off_sg_wanted, NULL },
	{ "tso", CMDL_BOOL, &off_tso_wanted, NULL },
	{ "ufo", CMDL_BOOL, &off_ufo_wanted, NULL },
	{ "gso", CMDL_BOOL, &off_gso_wanted, NULL },
	{ "lro", CMDL_FLAG, &off_flags_wanted, NULL,
	  ETH_FLAG_LRO, &off_flags_mask },
	{ "gro", CMDL_BOOL, &off_gro_wanted, NULL },
	{ "rxvlan", CMDL_FLAG, &off_flags_wanted, NULL,
	  ETH_FLAG_RXVLAN, &off_flags_mask },
	{ "txvlan", CMDL_FLAG, &off_flags_wanted, NULL,
	  ETH_FLAG_TXVLAN, &off_flags_mask },
	{ "ntuple", CMDL_FLAG, &off_flags_wanted, NULL,
	  ETH_FLAG_NTUPLE, &off_flags_mask },
	{ "rxhash", CMDL_FLAG, &off_flags_wanted, NULL,
	  ETH_FLAG_RXHASH, &off_flags_mask },
};

static struct cmdline_info cmdline_pause[] = {
	{ "autoneg", CMDL_BOOL, &pause_autoneg_wanted, &epause.autoneg },
	{ "rx", CMDL_BOOL, &pause_rx_wanted, &epause.rx_pause },
	{ "tx", CMDL_BOOL, &pause_tx_wanted, &epause.tx_pause },
};

static struct cmdline_info cmdline_ring[] = {
	{ "rx", CMDL_S32, &ring_rx_wanted, &ering.rx_pending },
	{ "rx-mini", CMDL_S32, &ring_rx_mini_wanted, &ering.rx_mini_pending },
	{ "rx-jumbo", CMDL_S32, &ring_rx_jumbo_wanted, &ering.rx_jumbo_pending },
	{ "tx", CMDL_S32, &ring_tx_wanted, &ering.tx_pending },
};

static struct cmdline_info cmdline_coalesce[] = {
	{ "adaptive-rx", CMDL_BOOL, &coal_adaptive_rx_wanted, &ecoal.use_adaptive_rx_coalesce },
	{ "adaptive-tx", CMDL_BOOL, &coal_adaptive_tx_wanted, &ecoal.use_adaptive_tx_coalesce },
	{ "sample-interval", CMDL_S32, &coal_sample_rate_wanted, &ecoal.rate_sample_interval },
	{ "stats-block-usecs", CMDL_S32, &coal_stats_wanted, &ecoal.stats_block_coalesce_usecs },
	{ "pkt-rate-low", CMDL_S32, &coal_pkt_rate_low_wanted, &ecoal.pkt_rate_low },
	{ "pkt-rate-high", CMDL_S32, &coal_pkt_rate_high_wanted, &ecoal.pkt_rate_high },
	{ "rx-usecs", CMDL_S32, &coal_rx_usec_wanted, &ecoal.rx_coalesce_usecs },
	{ "rx-frames", CMDL_S32, &coal_rx_frames_wanted, &ecoal.rx_max_coalesced_frames },
	{ "rx-usecs-irq", CMDL_S32, &coal_rx_usec_irq_wanted, &ecoal.rx_coalesce_usecs_irq },
	{ "rx-frames-irq", CMDL_S32, &coal_rx_frames_irq_wanted, &ecoal.rx_max_coalesced_frames_irq },
	{ "tx-usecs", CMDL_S32, &coal_tx_usec_wanted, &ecoal.tx_coalesce_usecs },
	{ "tx-frames", CMDL_S32, &coal_tx_frames_wanted, &ecoal.tx_max_coalesced_frames },
	{ "tx-usecs-irq", CMDL_S32, &coal_tx_usec_irq_wanted, &ecoal.tx_coalesce_usecs_irq },
	{ "tx-frames-irq", CMDL_S32, &coal_tx_frames_irq_wanted, &ecoal.tx_max_coalesced_frames_irq },
	{ "rx-usecs-low", CMDL_S32, &coal_rx_usec_low_wanted, &ecoal.rx_coalesce_usecs_low },
	{ "rx-frames-low", CMDL_S32, &coal_rx_frames_low_wanted, &ecoal.rx_max_coalesced_frames_low },
	{ "tx-usecs-low", CMDL_S32, &coal_tx_usec_low_wanted, &ecoal.tx_coalesce_usecs_low },
	{ "tx-frames-low", CMDL_S32, &coal_tx_frames_low_wanted, &ecoal.tx_max_coalesced_frames_low },
	{ "rx-usecs-high", CMDL_S32, &coal_rx_usec_high_wanted, &ecoal.rx_coalesce_usecs_high },
	{ "rx-frames-high", CMDL_S32, &coal_rx_frames_high_wanted, &ecoal.rx_max_coalesced_frames_high },
	{ "tx-usecs-high", CMDL_S32, &coal_tx_usec_high_wanted, &ecoal.tx_coalesce_usecs_high },
	{ "tx-frames-high", CMDL_S32, &coal_tx_frames_high_wanted, &ecoal.tx_max_coalesced_frames_high },
};

static struct cmdline_info cmdline_ntuple_tcp_ip4[] = {
	{ "src-ip", CMDL_IP4, &ntuple_fs.h_u.tcp_ip4_spec.ip4src, NULL,
	  0, &ntuple_ip4src_seen },
	{ "src-ip-mask", CMDL_IP4, &ntuple_fs.m_u.tcp_ip4_spec.ip4src, NULL,
	  0, &ntuple_ip4src_mask_seen },
	{ "dst-ip", CMDL_IP4, &ntuple_fs.h_u.tcp_ip4_spec.ip4dst, NULL,
	  0, &ntuple_ip4dst_seen },
	{ "dst-ip-mask", CMDL_IP4, &ntuple_fs.m_u.tcp_ip4_spec.ip4dst, NULL,
	  0, &ntuple_ip4dst_mask_seen },
	{ "src-port", CMDL_BE16, &ntuple_fs.h_u.tcp_ip4_spec.psrc, NULL,
	  0, &ntuple_psrc_seen },
	{ "src-port-mask", CMDL_BE16, &ntuple_fs.m_u.tcp_ip4_spec.psrc, NULL,
	  0, &ntuple_psrc_mask_seen },
	{ "dst-port", CMDL_BE16, &ntuple_fs.h_u.tcp_ip4_spec.pdst, NULL,
	  0, &ntuple_pdst_seen },
	{ "dst-port-mask", CMDL_BE16, &ntuple_fs.m_u.tcp_ip4_spec.pdst, NULL,
	  0, &ntuple_pdst_mask_seen },
	{ "vlan", CMDL_U16, &ntuple_fs.vlan_tag, NULL,
	  0, &ntuple_vlan_tag_seen },
	{ "vlan-mask", CMDL_U16, &ntuple_fs.vlan_tag_mask, NULL,
	  0, &ntuple_vlan_tag_mask_seen },
	{ "user-def", CMDL_U64, &ntuple_fs.data, NULL,
	  0, &ntuple_user_def_seen },
	{ "user-def-mask", CMDL_U64, &ntuple_fs.data_mask, NULL,
	  0, &ntuple_user_def_mask_seen },
	{ "action", CMDL_S32, &ntuple_fs.action, NULL },
};

static struct cmdline_info cmdline_ntuple_ether[] = {
	{ "dst", CMDL_MAC, ntuple_fs.h_u.ether_spec.h_dest, NULL,
	  0, &ntuple_ether_dst_seen },
	{ "dst-mask", CMDL_MAC, ntuple_fs.m_u.ether_spec.h_dest, NULL,
	  0, &ntuple_ether_dst_mask_seen },
	{ "src", CMDL_MAC, ntuple_fs.h_u.ether_spec.h_source, NULL,
	  0, &ntuple_ether_src_seen },
	{ "src-mask", CMDL_MAC, ntuple_fs.m_u.ether_spec.h_source, NULL,
	  0, &ntuple_ether_src_mask_seen },
	{ "proto", CMDL_BE16, &ntuple_fs.h_u.ether_spec.h_proto, NULL,
	  0, &ntuple_ether_proto_seen },
	{ "proto-mask", CMDL_BE16, &ntuple_fs.m_u.ether_spec.h_proto, NULL,
	  0, &ntuple_ether_proto_mask_seen },
	{ "vlan", CMDL_U16, &ntuple_fs.vlan_tag, NULL,
	  0, &ntuple_vlan_tag_seen },
	{ "vlan-mask", CMDL_U16, &ntuple_fs.vlan_tag_mask, NULL,
	  0, &ntuple_vlan_tag_mask_seen },
	{ "user-def", CMDL_U64, &ntuple_fs.data, NULL,
	  0, &ntuple_user_def_seen },
	{ "user-def-mask", CMDL_U64, &ntuple_fs.data_mask, NULL,
	  0, &ntuple_user_def_mask_seen },
	{ "action", CMDL_S32, &ntuple_fs.action, NULL },
};

static struct cmdline_info cmdline_msglvl[] = {
	{ "drv", CMDL_FLAG, &msglvl_wanted, NULL,
	  NETIF_MSG_DRV, &msglvl_mask },
	{ "probe", CMDL_FLAG, &msglvl_wanted, NULL,
	  NETIF_MSG_PROBE, &msglvl_mask },
	{ "link", CMDL_FLAG, &msglvl_wanted, NULL,
	  NETIF_MSG_LINK, &msglvl_mask },
	{ "timer", CMDL_FLAG, &msglvl_wanted, NULL,
	  NETIF_MSG_TIMER, &msglvl_mask },
	{ "ifdown", CMDL_FLAG, &msglvl_wanted, NULL,
	  NETIF_MSG_IFDOWN, &msglvl_mask },
	{ "ifup", CMDL_FLAG, &msglvl_wanted, NULL,
	  NETIF_MSG_IFUP, &msglvl_mask },
	{ "rx_err", CMDL_FLAG, &msglvl_wanted, NULL,
	  NETIF_MSG_RX_ERR, &msglvl_mask },
	{ "tx_err", CMDL_FLAG, &msglvl_wanted, NULL,
	  NETIF_MSG_TX_ERR, &msglvl_mask },
	{ "tx_queued", CMDL_FLAG, &msglvl_wanted, NULL,
	  NETIF_MSG_TX_QUEUED, &msglvl_mask },
	{ "intr", CMDL_FLAG, &msglvl_wanted, NULL,
	  NETIF_MSG_INTR, &msglvl_mask },
	{ "tx_done", CMDL_FLAG, &msglvl_wanted, NULL,
	  NETIF_MSG_TX_DONE, &msglvl_mask },
	{ "rx_status", CMDL_FLAG, &msglvl_wanted, NULL,
	  NETIF_MSG_RX_STATUS, &msglvl_mask },
	{ "pktdata", CMDL_FLAG, &msglvl_wanted, NULL,
	  NETIF_MSG_PKTDATA, &msglvl_mask },
	{ "hw", CMDL_FLAG, &msglvl_wanted, NULL,
	  NETIF_MSG_HW, &msglvl_mask },
	{ "wol", CMDL_FLAG, &msglvl_wanted, NULL,
	  NETIF_MSG_WOL, &msglvl_mask },
};

static long long
get_int_range(char *str, int base, long long min, long long max)
{
	long long v;
	char *endp;

	if (!str)
		show_usage(1);
	errno = 0;
	v = strtoll(str, &endp, base);
	if (errno || *endp || v < min || v > max)
		show_usage(1);
	return v;
}

static unsigned long long
get_uint_range(char *str, int base, unsigned long long max)
{
	unsigned long long v;
	char *endp;

	if (!str)
		show_usage(1);
	errno = 0;
	v = strtoull(str, &endp, base);
	if ( errno || *endp || v > max)
		show_usage(1);
	return v;
}

static int get_int(char *str, int base)
{
	return get_int_range(str, base, INT_MIN, INT_MAX);
}

static u32 get_u32(char *str, int base)
{
	return get_uint_range(str, base, 0xffffffff);
}

static void parse_generic_cmdline(int argc, char **argp,
				  int first_arg, int *changed,
				  struct cmdline_info *info,
				  unsigned int n_info)
{
	int i, idx;
	int found;

	for (i = first_arg; i < argc; i++) {
		found = 0;
		for (idx = 0; idx < n_info; idx++) {
			if (!strcmp(info[idx].name, argp[i])) {
				found = 1;
				*changed = 1;
				if (info[idx].type != CMDL_FLAG &&
				    info[idx].seen_val)
					*(int *)info[idx].seen_val = 1;
				i += 1;
				if (i >= argc)
					show_usage(1);
				switch (info[idx].type) {
				case CMDL_BOOL: {
					int *p = info[idx].wanted_val;
					if (!strcmp(argp[i], "on"))
						*p = 1;
					else if (!strcmp(argp[i], "off"))
						*p = 0;
					else
						show_usage(1);
					break;
				}
				case CMDL_S32: {
					s32 *p = info[idx].wanted_val;
					*p = get_int_range(argp[i], 0,
							   -0x80000000LL,
							   0x7fffffff);
					break;
				}
				case CMDL_U16: {
					u16 *p = info[idx].wanted_val;
					*p = get_uint_range(argp[i], 0, 0xffff);
					break;
				}
				case CMDL_U32: {
					u32 *p = info[idx].wanted_val;
					*p = get_uint_range(argp[i], 0,
							    0xffffffff);
					break;
				}
				case CMDL_U64: {
					u64 *p = info[idx].wanted_val;
					*p = get_uint_range(
						argp[i], 0,
						0xffffffffffffffffLL);
					break;
				}
				case CMDL_BE16: {
					u16 *p = info[idx].wanted_val;
					*p = cpu_to_be16(
						get_uint_range(argp[i], 0,
							       0xffff));
					break;
				}
				case CMDL_IP4: {
					u32 *p = info[idx].wanted_val;
					struct in_addr in;
					if (!inet_aton(argp[i], &in))
						show_usage(1);
					*p = in.s_addr;
					break;
				}
				case CMDL_MAC:
					get_mac_addr(argp[i],
						     info[idx].wanted_val);
					break;
				case CMDL_FLAG: {
					u32 *p;
					p = info[idx].seen_val;
					*p |= info[idx].flag_val;
					if (!strcmp(argp[i], "on")) {
						p = info[idx].wanted_val;
						*p |= info[idx].flag_val;
					} else if (strcmp(argp[i], "off")) {
						show_usage(1);
					}
					break;
				}
				case CMDL_STR: {
					char **s = info[idx].wanted_val;
					*s = strdup(argp[i]);
					break;
				}
				default:
					show_usage(1);
				}
				break;
			}
		}
		if( !found)
			show_usage(1);
	}
}

static void
print_flags(const struct cmdline_info *info, unsigned int n_info, u32 value)
{
	const char *sep = "";

	while (n_info) {
		if (info->type == CMDL_FLAG && value & info->flag_val) {
			printf("%s%s", sep, info->name);
			sep = " ";
			value &= ~info->flag_val;
		}
		++info;
		--n_info;
	}

	/* Print any unrecognised flags in hex */
	if (value)
		printf("%s%#x", sep, value);
}

static int rxflow_str_to_type(const char *str)
{
	int flow_type = 0;

	if (!strcmp(str, "tcp4"))
		flow_type = TCP_V4_FLOW;
	else if (!strcmp(str, "udp4"))
		flow_type = UDP_V4_FLOW;
	else if (!strcmp(str, "ah4"))
		flow_type = AH_ESP_V4_FLOW;
	else if (!strcmp(str, "sctp4"))
		flow_type = SCTP_V4_FLOW;
	else if (!strcmp(str, "tcp6"))
		flow_type = TCP_V6_FLOW;
	else if (!strcmp(str, "udp6"))
		flow_type = UDP_V6_FLOW;
	else if (!strcmp(str, "ah6"))
		flow_type = AH_ESP_V6_FLOW;
	else if (!strcmp(str, "sctp6"))
		flow_type = SCTP_V6_FLOW;
	else if (!strcmp(str, "ether"))
		flow_type = ETHER_FLOW;

	return flow_type;
}

static void parse_cmdline(int argc, char **argp)
{
	int i, k;

	for (i = 1; i < argc; i++) {
		switch (i) {
		case 1:
			for (k = 0; args[k].srt; k++)
				if (!strcmp(argp[i], args[k].srt) ||
				    !strcmp(argp[i], args[k].lng)) {
					mode = args[k].Mode;
					break;
				}
			if (mode == MODE_HELP ||
			    (!args[k].srt && argp[i][0] == '-'))
				show_usage(0);
			else
				devname = argp[i];
			break;
		case 2:
			if ((mode == MODE_SSET) ||
			    (mode == MODE_GDRV) ||
			    (mode == MODE_GREGS)||
			    (mode == MODE_NWAY_RST) ||
			    (mode == MODE_TEST) ||
			    (mode == MODE_GEEPROM) ||
			    (mode == MODE_SEEPROM) ||
			    (mode == MODE_GPAUSE) ||
			    (mode == MODE_SPAUSE) ||
			    (mode == MODE_GCOALESCE) ||
			    (mode == MODE_SCOALESCE) ||
			    (mode == MODE_GRING) ||
			    (mode == MODE_SRING) ||
			    (mode == MODE_GOFFLOAD) ||
			    (mode == MODE_SOFFLOAD) ||
			    (mode == MODE_GSTATS) ||
			    (mode == MODE_GNFC) ||
			    (mode == MODE_SNFC) ||
			    (mode == MODE_GRXFHINDIR) ||
			    (mode == MODE_SRXFHINDIR) ||
			    (mode == MODE_SNTUPLE) ||
			    (mode == MODE_GNTUPLE) ||
			    (mode == MODE_PHYS_ID) ||
			    (mode == MODE_FLASHDEV) ||
			    (mode == MODE_PERMADDR)) {
				devname = argp[i];
				break;
			}
			/* fallthrough */
		case 3:
			if (mode == MODE_TEST) {
				if (!strcmp(argp[i], "online")) {
					test_type = ONLINE;
				} else if (!strcmp(argp[i], "offline")) {
					test_type = OFFLINE;
				} else {
					show_usage(1);
				}
				break;
			} else if (mode == MODE_PHYS_ID) {
				phys_id_time = get_int(argp[i],0);
				break;
			} else if (mode == MODE_FLASHDEV) {
				flash_file = argp[i];
				flash = 1;
				break;
			}
			/* fallthrough */
		default:
			if (mode == MODE_GREGS) {
				parse_generic_cmdline(argc, argp, i,
					&gregs_changed,
					cmdline_gregs,
					ARRAY_SIZE(cmdline_gregs));
				i = argc;
				break;
			}
			if (mode == MODE_GEEPROM) {
				parse_generic_cmdline(argc, argp, i,
					&geeprom_changed,
					cmdline_geeprom,
					ARRAY_SIZE(cmdline_geeprom));
				i = argc;
				break;
			}
			if (mode == MODE_SEEPROM) {
				parse_generic_cmdline(argc, argp, i,
					&seeprom_changed,
					cmdline_seeprom,
					ARRAY_SIZE(cmdline_seeprom));
				i = argc;
				break;
			}
			if (mode == MODE_SPAUSE) {
				parse_generic_cmdline(argc, argp, i,
					&gpause_changed,
			      		cmdline_pause,
			      		ARRAY_SIZE(cmdline_pause));
				i = argc;
				break;
			}
			if (mode == MODE_SRING) {
				parse_generic_cmdline(argc, argp, i,
					&gring_changed,
			      		cmdline_ring,
			      		ARRAY_SIZE(cmdline_ring));
				i = argc;
				break;
			}
			if (mode == MODE_SCOALESCE) {
				parse_generic_cmdline(argc, argp, i,
					&gcoalesce_changed,
			      		cmdline_coalesce,
			      		ARRAY_SIZE(cmdline_coalesce));
				i = argc;
				break;
			}
			if (mode == MODE_SOFFLOAD) {
				parse_generic_cmdline(argc, argp, i,
					&goffload_changed,
			      		cmdline_offload,
			      		ARRAY_SIZE(cmdline_offload));
				i = argc;
				break;
			}
			if (mode == MODE_SNTUPLE) {
				if (!strcmp(argp[i], "flow-type")) {
					i += 1;
					if (i >= argc) {
						show_usage(1);
						break;
					}
					parse_rxntupleopts(argc, argp, i);
					i = argc;
					break;
				} else {
					show_usage(1);
				}
				break;
			}
			if (mode == MODE_GNFC) {
				if (!strcmp(argp[i], "rx-flow-hash")) {
					i += 1;
					if (i >= argc) {
						show_usage(1);
						break;
					}
					rx_fhash_get =
						rxflow_str_to_type(argp[i]);
					if (!rx_fhash_get)
						show_usage(1);
				} else
					show_usage(1);
				break;
			}
			if (mode == MODE_FLASHDEV) {
				flash_region = strtol(argp[i], NULL, 0);
				if ((flash_region < 0))
					show_usage(1);
				break;
			}
			if (mode == MODE_SNFC) {
				if (!strcmp(argp[i], "rx-flow-hash")) {
					i += 1;
					if (i >= argc) {
						show_usage(1);
						break;
					}
					rx_fhash_set =
						rxflow_str_to_type(argp[i]);
					if (!rx_fhash_set) {
						show_usage(1);
						break;
					}
					i += 1;
					if (i >= argc) {
						show_usage(1);
						break;
					}
					if (parse_rxfhashopts(argp[i],
						&rx_fhash_val) < 0)
						show_usage(1);
					else
						rx_fhash_changed = 1;
				} else
					show_usage(1);
				break;
			}
			if (mode == MODE_SRXFHINDIR) {
				if (!strcmp(argp[i], "equal")) {
					if (argc != i + 2) {
						show_usage(1);
						break;
					}
					i += 1;
					rxfhindir_equal =
						get_int_range(argp[i], 0, 1,
							      INT_MAX);
					i += 1;
				} else if (!strcmp(argp[i], "weight")) {
					i += 1;
					if (i >= argc) {
						show_usage(1);
						break;
					}
					rxfhindir_weight = argp + i;
					i = argc;
				} else {
					show_usage(1);
				}
				break;
			}
			if (mode != MODE_SSET)
				show_usage(1);
			if (!strcmp(argp[i], "speed")) {
				gset_changed = 1;
				i += 1;
				if (i >= argc)
					show_usage(1);
				speed_wanted = get_int(argp[i],10);
				break;
			} else if (!strcmp(argp[i], "duplex")) {
				gset_changed = 1;
				i += 1;
				if (i >= argc)
					show_usage(1);
				if (!strcmp(argp[i], "half"))
					duplex_wanted = DUPLEX_HALF;
				else if (!strcmp(argp[i], "full"))
					duplex_wanted = DUPLEX_FULL;
				else
					show_usage(1);
				break;
			} else if (!strcmp(argp[i], "port")) {
				gset_changed = 1;
				i += 1;
				if (i >= argc)
					show_usage(1);
				if (!strcmp(argp[i], "tp"))
					port_wanted = PORT_TP;
				else if (!strcmp(argp[i], "aui"))
					port_wanted = PORT_AUI;
				else if (!strcmp(argp[i], "bnc"))
					port_wanted = PORT_BNC;
				else if (!strcmp(argp[i], "mii"))
					port_wanted = PORT_MII;
				else if (!strcmp(argp[i], "fibre"))
					port_wanted = PORT_FIBRE;
				else
					show_usage(1);
				break;
			} else if (!strcmp(argp[i], "autoneg")) {
				i += 1;
				if (i >= argc)
					show_usage(1);
				if (!strcmp(argp[i], "on")) {
					gset_changed = 1;
					autoneg_wanted = AUTONEG_ENABLE;
				} else if (!strcmp(argp[i], "off")) {
					gset_changed = 1;
					autoneg_wanted = AUTONEG_DISABLE;
				} else {
					show_usage(1);
				}
				break;
			} else if (!strcmp(argp[i], "advertise")) {
				gset_changed = 1;
				i += 1;
				if (i >= argc)
					show_usage(1);
				advertising_wanted = get_int(argp[i], 16);
				break;
			} else if (!strcmp(argp[i], "phyad")) {
				gset_changed = 1;
				i += 1;
				if (i >= argc)
					show_usage(1);
				phyad_wanted = get_int(argp[i], 0);
				break;
			} else if (!strcmp(argp[i], "xcvr")) {
				gset_changed = 1;
				i += 1;
				if (i >= argc)
					show_usage(1);
				if (!strcmp(argp[i], "internal"))
					xcvr_wanted = XCVR_INTERNAL;
				else if (!strcmp(argp[i], "external"))
					xcvr_wanted = XCVR_EXTERNAL;
				else
					show_usage(1);
				break;
			} else if (!strcmp(argp[i], "wol")) {
				gwol_changed = 1;
				i++;
				if (i >= argc)
					show_usage(1);
				if (parse_wolopts(argp[i], &wol_wanted) < 0)
					show_usage(1);
				wol_change = 1;
				break;
			} else if (!strcmp(argp[i], "sopass")) {
				gwol_changed = 1;
				i++;
				if (i >= argc)
					show_usage(1);
				get_mac_addr(argp[i], sopass_wanted);
				sopass_change = 1;
				break;
			} else if (!strcmp(argp[i], "msglvl")) {
				i++;
				if (i >= argc)
					show_usage(1);
				if (isdigit((unsigned char)argp[i][0])) {
					msglvl_changed = 1;
					msglvl_mask = ~0;
					msglvl_wanted =
						get_uint_range(argp[i], 0,
							       0xffffffff);
				} else {
					parse_generic_cmdline(
						argc, argp, i,
						&msglvl_changed,
						cmdline_msglvl,
						ARRAY_SIZE(cmdline_msglvl));
					i = argc;
				}
				break;
			}
			show_usage(1);
		}
	}

	if ((autoneg_wanted == AUTONEG_ENABLE) && (advertising_wanted < 0)) {
		if (speed_wanted == SPEED_10 && duplex_wanted == DUPLEX_HALF)
			advertising_wanted = ADVERTISED_10baseT_Half;
		else if (speed_wanted == SPEED_10 &&
			 duplex_wanted == DUPLEX_FULL)
			advertising_wanted = ADVERTISED_10baseT_Full;
		else if (speed_wanted == SPEED_100 &&
			 duplex_wanted == DUPLEX_HALF)
			advertising_wanted = ADVERTISED_100baseT_Half;
		else if (speed_wanted == SPEED_100 &&
			 duplex_wanted == DUPLEX_FULL)
			advertising_wanted = ADVERTISED_100baseT_Full;
		else if (speed_wanted == SPEED_1000 &&
			 duplex_wanted == DUPLEX_HALF)
			advertising_wanted = ADVERTISED_1000baseT_Half;
		else if (speed_wanted == SPEED_1000 &&
			 duplex_wanted == DUPLEX_FULL)
			advertising_wanted = ADVERTISED_1000baseT_Full;
		else if (speed_wanted == SPEED_2500 &&
			 duplex_wanted == DUPLEX_FULL)
			advertising_wanted = ADVERTISED_2500baseX_Full;
		else if (speed_wanted == SPEED_10000 &&
			 duplex_wanted == DUPLEX_FULL)
			advertising_wanted = ADVERTISED_10000baseT_Full;
		else
			/* auto negotiate without forcing,
			 * all supported speed will be assigned in do_sset()
			 */
			advertising_wanted = 0;

	}

	if (devname == NULL)
		show_usage(1);
	if (strlen(devname) >= IFNAMSIZ)
		show_usage(1);
}

static void dump_supported(struct ethtool_cmd *ep)
{
	u_int32_t mask = ep->supported;
	int did1;

	fprintf(stdout, "	Supported ports: [ ");
	if (mask & SUPPORTED_TP)
		fprintf(stdout, "TP ");
	if (mask & SUPPORTED_AUI)
		fprintf(stdout, "AUI ");
	if (mask & SUPPORTED_BNC)
		fprintf(stdout, "BNC ");
	if (mask & SUPPORTED_MII)
		fprintf(stdout, "MII ");
	if (mask & SUPPORTED_FIBRE)
		fprintf(stdout, "FIBRE ");
	fprintf(stdout, "]\n");

	fprintf(stdout, "	Supported link modes:   ");
	did1 = 0;
	if (mask & SUPPORTED_10baseT_Half) {
		did1++; fprintf(stdout, "10baseT/Half ");
	}
	if (mask & SUPPORTED_10baseT_Full) {
		did1++; fprintf(stdout, "10baseT/Full ");
	}
	if (did1 && (mask & (SUPPORTED_100baseT_Half|SUPPORTED_100baseT_Full))) {
		fprintf(stdout, "\n");
		fprintf(stdout, "	                        ");
	}
	if (mask & SUPPORTED_100baseT_Half) {
		did1++; fprintf(stdout, "100baseT/Half ");
	}
	if (mask & SUPPORTED_100baseT_Full) {
		did1++; fprintf(stdout, "100baseT/Full ");
	}
	if (did1 && (mask & (SUPPORTED_1000baseT_Half|SUPPORTED_1000baseT_Full))) {
		fprintf(stdout, "\n");
		fprintf(stdout, "	                        ");
	}
	if (mask & SUPPORTED_1000baseT_Half) {
		did1++; fprintf(stdout, "1000baseT/Half ");
	}
	if (mask & SUPPORTED_1000baseT_Full) {
		did1++; fprintf(stdout, "1000baseT/Full ");
	}
	if (did1 && (mask & SUPPORTED_2500baseX_Full)) {
		fprintf(stdout, "\n");
		fprintf(stdout, "	                        ");
	}
	if (mask & SUPPORTED_2500baseX_Full) {
		did1++; fprintf(stdout, "2500baseX/Full ");
	}
	if (did1 && (mask & SUPPORTED_10000baseT_Full)) {
		fprintf(stdout, "\n");
		fprintf(stdout, "	                        ");
	}
	if (mask & SUPPORTED_10000baseT_Full) {
		did1++; fprintf(stdout, "10000baseT/Full ");
	}
	fprintf(stdout, "\n");

	fprintf(stdout, "	Supports auto-negotiation: ");
	if (mask & SUPPORTED_Autoneg)
		fprintf(stdout, "Yes\n");
	else
		fprintf(stdout, "No\n");
}

static void dump_advertised(struct ethtool_cmd *ep,
			    const char *prefix, u_int32_t mask)
{
	int indent = strlen(prefix) + 14;
	int did1;

	fprintf(stdout, "	%s link modes:  ", prefix);
	did1 = 0;
	if (mask & ADVERTISED_10baseT_Half) {
		did1++; fprintf(stdout, "10baseT/Half ");
	}
	if (mask & ADVERTISED_10baseT_Full) {
		did1++; fprintf(stdout, "10baseT/Full ");
	}
	if (did1 && (mask & (ADVERTISED_100baseT_Half|ADVERTISED_100baseT_Full))) {
		fprintf(stdout, "\n");
		fprintf(stdout, "	%*s", indent, "");
	}
	if (mask & ADVERTISED_100baseT_Half) {
		did1++; fprintf(stdout, "100baseT/Half ");
	}
	if (mask & ADVERTISED_100baseT_Full) {
		did1++; fprintf(stdout, "100baseT/Full ");
	}
	if (did1 && (mask & (ADVERTISED_1000baseT_Half|ADVERTISED_1000baseT_Full))) {
		fprintf(stdout, "\n");
		fprintf(stdout, "	%*s", indent, "");
	}
	if (mask & ADVERTISED_1000baseT_Half) {
		did1++; fprintf(stdout, "1000baseT/Half ");
	}
	if (mask & ADVERTISED_1000baseT_Full) {
		did1++; fprintf(stdout, "1000baseT/Full ");
	}
	if (did1 && (mask & ADVERTISED_2500baseX_Full)) {
		fprintf(stdout, "\n");
		fprintf(stdout, "	%*s", indent, "");
	}
	if (mask & ADVERTISED_2500baseX_Full) {
		did1++; fprintf(stdout, "2500baseX/Full ");
	}
	if (did1 && (mask & ADVERTISED_10000baseT_Full)) {
		fprintf(stdout, "\n");
		fprintf(stdout, "	%*s", indent, "");
	}
	if (mask & ADVERTISED_10000baseT_Full) {
		did1++; fprintf(stdout, "10000baseT/Full ");
	}
	if (did1 == 0)
		 fprintf(stdout, "Not reported");
	fprintf(stdout, "\n");

	fprintf(stdout, "	%s pause frame use: ", prefix);
	if (mask & ADVERTISED_Pause) {
		fprintf(stdout, "Symmetric");
		if (mask & ADVERTISED_Asym_Pause)
			fprintf(stdout, " Receive-only");
		fprintf(stdout, "\n");
	} else {
		if (mask & ADVERTISED_Asym_Pause)
			fprintf(stdout, "Transmit-only\n");
		else
			fprintf(stdout, "No\n");
	}

	fprintf(stdout, "	%s auto-negotiation: ", prefix);
	if (mask & ADVERTISED_Autoneg)
		fprintf(stdout, "Yes\n");
	else
		fprintf(stdout, "No\n");
}

static int dump_ecmd(struct ethtool_cmd *ep)
{
	u32 speed;

	dump_supported(ep);
	dump_advertised(ep, "Advertised", ep->advertising);
	if (ep->lp_advertising)
		dump_advertised(ep, "Link partner advertised",
				ep->lp_advertising);

	fprintf(stdout, "	Speed: ");
	speed = ethtool_cmd_speed(ep);
	if (speed == 0 || speed == (u16)(-1) || speed == (u32)(-1))
		fprintf(stdout, "Unknown!\n");
	else
		fprintf(stdout, "%uMb/s\n", speed);

	fprintf(stdout, "	Duplex: ");
	switch (ep->duplex) {
	case DUPLEX_HALF:
		fprintf(stdout, "Half\n");
		break;
	case DUPLEX_FULL:
		fprintf(stdout, "Full\n");
		break;
	default:
		fprintf(stdout, "Unknown! (%i)\n", ep->duplex);
		break;
	};

	fprintf(stdout, "	Port: ");
	switch (ep->port) {
	case PORT_TP:
		fprintf(stdout, "Twisted Pair\n");
		break;
	case PORT_AUI:
		fprintf(stdout, "AUI\n");
		break;
	case PORT_BNC:
		fprintf(stdout, "BNC\n");
		break;
	case PORT_MII:
		fprintf(stdout, "MII\n");
		break;
	case PORT_FIBRE:
		fprintf(stdout, "FIBRE\n");
		break;
	case PORT_DA:
		fprintf(stdout, "Direct Attach Copper\n");
		break;
	case PORT_NONE:
		fprintf(stdout, "None\n");
		break;
	case PORT_OTHER:
		fprintf(stdout, "Other\n");
		break;
	default:
		fprintf(stdout, "Unknown! (%i)\n", ep->port);
		break;
	};

	fprintf(stdout, "	PHYAD: %d\n", ep->phy_address);
	fprintf(stdout, "	Transceiver: ");
	switch (ep->transceiver) {
	case XCVR_INTERNAL:
		fprintf(stdout, "internal\n");
		break;
	case XCVR_EXTERNAL:
		fprintf(stdout, "external\n");
		break;
	default:
		fprintf(stdout, "Unknown!\n");
		break;
	};

	fprintf(stdout, "	Auto-negotiation: %s\n",
		(ep->autoneg == AUTONEG_DISABLE) ?
		"off" : "on");

	if (ep->port == PORT_TP) {
		fprintf(stdout, "	MDI-X: ");
		switch (ep->eth_tp_mdix) {
		case ETH_TP_MDI:
			fprintf(stdout, "off\n");
			break;
		case ETH_TP_MDI_X:
			fprintf(stdout, "on\n");
			break;
		default:
			fprintf(stdout, "Unknown\n");
			break;
		}
	}

	return 0;
}

static int dump_drvinfo(struct ethtool_drvinfo *info)
{
	fprintf(stdout,
		"driver: %s\n"
		"version: %s\n"
		"firmware-version: %s\n"
		"bus-info: %s\n",
		info->driver,
		info->version,
		info->fw_version,
		info->bus_info);

	return 0;
}

static int dump_wol(struct ethtool_wolinfo *wol)
{
	fprintf(stdout, "	Supports Wake-on: %s\n",
		unparse_wolopts(wol->supported));
	fprintf(stdout, "	Wake-on: %s\n",
		unparse_wolopts(wol->wolopts));
	if (wol->supported & WAKE_MAGICSECURE) {
		int i;
		int delim = 0;
		fprintf(stdout, "        SecureOn password: ");
		for (i = 0; i < SOPASS_MAX; i++) {
			fprintf(stdout, "%s%02x", delim?":":"", wol->sopass[i]);
			delim=1;
		}
		fprintf(stdout, "\n");
	}

	return 0;
}

static int parse_wolopts(char *optstr, u32 *data)
{
	*data = 0;
	while (*optstr) {
		switch (*optstr) {
			case 'p':
				*data |= WAKE_PHY;
				break;
			case 'u':
				*data |= WAKE_UCAST;
				break;
			case 'm':
				*data |= WAKE_MCAST;
				break;
			case 'b':
				*data |= WAKE_BCAST;
				break;
			case 'a':
				*data |= WAKE_ARP;
				break;
			case 'g':
				*data |= WAKE_MAGIC;
				break;
			case 's':
				*data |= WAKE_MAGICSECURE;
				break;
			case 'd':
				*data = 0;
				break;
			default:
				return -1;
		}
		optstr++;
	}
	return 0;
}

static char *unparse_wolopts(int wolopts)
{
	static char buf[16];
	char *p = buf;

	memset(buf, 0, sizeof(buf));

	if (wolopts) {
		if (wolopts & WAKE_PHY)
			*p++ = 'p';
		if (wolopts & WAKE_UCAST)
			*p++ = 'u';
		if (wolopts & WAKE_MCAST)
			*p++ = 'm';
		if (wolopts & WAKE_BCAST)
			*p++ = 'b';
		if (wolopts & WAKE_ARP)
			*p++ = 'a';
		if (wolopts & WAKE_MAGIC)
			*p++ = 'g';
		if (wolopts & WAKE_MAGICSECURE)
			*p++ = 's';
	} else {
		*p = 'd';
	}

	return buf;
}

static void get_mac_addr(char *src, unsigned char *dest)
{
	int count;
	int i;
	int buf[ETH_ALEN];

	count = sscanf(src, "%2x:%2x:%2x:%2x:%2x:%2x",
		&buf[0], &buf[1], &buf[2], &buf[3], &buf[4], &buf[5]);
	if (count != ETH_ALEN)
		show_usage(1);

	for (i = 0; i < count; i++) {
		dest[i] = buf[i];
	}
}

static int parse_rxfhashopts(char *optstr, u32 *data)
{
	*data = 0;
	while (*optstr) {
		switch (*optstr) {
			case 'm':
				*data |= RXH_L2DA;
				break;
			case 'v':
				*data |= RXH_VLAN;
				break;
			case 't':
				*data |= RXH_L3_PROTO;
				break;
			case 's':
				*data |= RXH_IP_SRC;
				break;
			case 'd':
				*data |= RXH_IP_DST;
				break;
			case 'f':
				*data |= RXH_L4_B_0_1;
				break;
			case 'n':
				*data |= RXH_L4_B_2_3;
				break;
			case 'r':
				*data |= RXH_DISCARD;
				break;
			default:
				return -1;
		}
		optstr++;
	}
	return 0;
}

static char *unparse_rxfhashopts(u64 opts)
{
	static char buf[300];

	memset(buf, 0, sizeof(buf));

	if (opts) {
		if (opts & RXH_L2DA) {
			strcat(buf, "L2DA\n");
		}
		if (opts & RXH_VLAN) {
			strcat(buf, "VLAN tag\n");
		}
		if (opts & RXH_L3_PROTO) {
			strcat(buf, "L3 proto\n");
		}
		if (opts & RXH_IP_SRC) {
			strcat(buf, "IP SA\n");
		}
		if (opts & RXH_IP_DST) {
			strcat(buf, "IP DA\n");
		}
		if (opts & RXH_L4_B_0_1) {
			strcat(buf, "L4 bytes 0 & 1 [TCP/UDP src port]\n");
		}
		if (opts & RXH_L4_B_2_3) {
			strcat(buf, "L4 bytes 2 & 3 [TCP/UDP dst port]\n");
		}
	} else {
		sprintf(buf, "None");
	}

	return buf;
}

static void parse_rxntupleopts(int argc, char **argp, int i)
{
	ntuple_fs.flow_type = rxflow_str_to_type(argp[i]);

	switch (ntuple_fs.flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case SCTP_V4_FLOW:
		parse_generic_cmdline(argc, argp, i + 1,
				      &sntuple_changed,
				      cmdline_ntuple_tcp_ip4,
				      ARRAY_SIZE(cmdline_ntuple_tcp_ip4));
		if (!ntuple_ip4src_seen)
			ntuple_fs.m_u.tcp_ip4_spec.ip4src = 0xffffffff;
		if (!ntuple_ip4dst_seen)
			ntuple_fs.m_u.tcp_ip4_spec.ip4dst = 0xffffffff;
		if (!ntuple_psrc_seen)
			ntuple_fs.m_u.tcp_ip4_spec.psrc = 0xffff;
		if (!ntuple_pdst_seen)
			ntuple_fs.m_u.tcp_ip4_spec.pdst = 0xffff;
		ntuple_fs.m_u.tcp_ip4_spec.tos = 0xff;
		break;
	case ETHER_FLOW:
		parse_generic_cmdline(argc, argp, i + 1,
				      &sntuple_changed,
				      cmdline_ntuple_ether,
				      ARRAY_SIZE(cmdline_ntuple_ether));
		if (!ntuple_ether_dst_seen)
			memset(ntuple_fs.m_u.ether_spec.h_dest, 0xff, ETH_ALEN);
		if (!ntuple_ether_src_seen)
			memset(ntuple_fs.m_u.ether_spec.h_source, 0xff,
			       ETH_ALEN);
		if (!ntuple_ether_proto_seen)
			ntuple_fs.m_u.ether_spec.h_proto = 0xffff;
		break;
	default:
		fprintf(stderr, "Unsupported flow type \"%s\"\n", argp[i]);
		exit(106);
		break;
	}

	if (!ntuple_vlan_tag_seen)
		ntuple_fs.vlan_tag_mask = 0xffff;
	if (!ntuple_user_def_seen)
		ntuple_fs.data_mask = 0xffffffffffffffffULL;

	if ((ntuple_ip4src_mask_seen && !ntuple_ip4src_seen) ||
	    (ntuple_ip4dst_mask_seen && !ntuple_ip4dst_seen) ||
	    (ntuple_psrc_mask_seen && !ntuple_psrc_seen) ||
	    (ntuple_pdst_mask_seen && !ntuple_pdst_seen) ||
	    (ntuple_ether_dst_mask_seen && !ntuple_ether_dst_seen) ||
	    (ntuple_ether_src_mask_seen && !ntuple_ether_src_seen) ||
	    (ntuple_ether_proto_mask_seen && !ntuple_ether_proto_seen) ||
	    (ntuple_vlan_tag_mask_seen && !ntuple_vlan_tag_seen) ||
	    (ntuple_user_def_mask_seen && !ntuple_user_def_seen)) {
		fprintf(stderr, "Cannot specify mask without value\n");
		exit(107);
	}
}

static struct {
	const char *name;
	int (*func)(struct ethtool_drvinfo *info, struct ethtool_regs *regs);

} driver_list[] = {
	{ "8139cp", realtek_dump_regs },
	{ "8139too", realtek_dump_regs },
	{ "r8169", realtek_dump_regs },
	{ "de2104x", de2104x_dump_regs },
	{ "e1000", e1000_dump_regs },
	{ "e1000e", e1000_dump_regs },
	{ "igb", igb_dump_regs },
	{ "ixgb", ixgb_dump_regs },
	{ "ixgbe", ixgbe_dump_regs },
	{ "natsemi", natsemi_dump_regs },
	{ "e100", e100_dump_regs },
	{ "amd8111e", amd8111e_dump_regs },
	{ "pcnet32", pcnet32_dump_regs },
	{ "fec_8xx", fec_8xx_dump_regs },
	{ "ibm_emac", ibm_emac_dump_regs },
	{ "tg3", tg3_dump_regs },
	{ "skge", skge_dump_regs },
	{ "sky2", sky2_dump_regs },
        { "vioc", vioc_dump_regs },
        { "smsc911x", smsc911x_dump_regs },
        { "at76c50x-usb", at76c50x_usb_dump_regs },
        { "sfc", sfc_dump_regs },
	{ "st_mac100", st_mac100_dump_regs },
	{ "st_gmac", st_gmac_dump_regs },
};

static int dump_regs(struct ethtool_drvinfo *info, struct ethtool_regs *regs)
{
	int i;

	if (gregs_dump_raw) {
		fwrite(regs->data, regs->len, 1, stdout);
		return 0;
	}

	if (gregs_dump_file) {
		FILE *f = fopen(gregs_dump_file, "r");
		struct stat st;

		if (!f || fstat(fileno(f), &st) < 0) {
			fprintf(stderr, "Can't open '%s': %s\n",
				gregs_dump_file, strerror(errno));
			return -1;
		}

		regs = realloc(regs, sizeof(*regs) + st.st_size);
		regs->len = st.st_size;
		fread(regs->data, regs->len, 1, f);
		fclose(f);
	}

	if (!gregs_dump_hex)
		for (i = 0; i < ARRAY_SIZE(driver_list); i++)
			if (!strncmp(driver_list[i].name, info->driver,
				     ETHTOOL_BUSINFO_LEN))
				return driver_list[i].func(info, regs);

	fprintf(stdout, "Offset\tValues\n");
	fprintf(stdout, "--------\t-----");
	for (i = 0; i < regs->len; i++) {
		if (i%16 == 0)
			fprintf(stdout, "\n%03x:\t", i);
		fprintf(stdout, " %02x", regs->data[i]);
	}
	fprintf(stdout, "\n\n");
	return 0;
}

static int dump_eeprom(struct ethtool_drvinfo *info, struct ethtool_eeprom *ee)
{
	int i;

	if (geeprom_dump_raw) {
		fwrite(ee->data, 1, ee->len, stdout);
		return 0;
	}

	if (!strncmp("natsemi", info->driver, ETHTOOL_BUSINFO_LEN)) {
		return natsemi_dump_eeprom(info, ee);
	} else if (!strncmp("tg3", info->driver, ETHTOOL_BUSINFO_LEN)) {
		return tg3_dump_eeprom(info, ee);
	}

	fprintf(stdout, "Offset\t\tValues\n");
	fprintf(stdout, "------\t\t------");
	for (i = 0; i < ee->len; i++) {
		if(!(i%16)) fprintf(stdout, "\n0x%04x\t\t", i + ee->offset);
		fprintf(stdout, "%02x ", ee->data[i]);
	}
	fprintf(stdout, "\n");
	return 0;
}

static int dump_test(struct ethtool_drvinfo *info, struct ethtool_test *test,
		      struct ethtool_gstrings *strings)
{
	int i, rc;

	rc = test->flags & ETH_TEST_FL_FAILED;
	fprintf(stdout, "The test result is %s\n", rc ? "FAIL" : "PASS");

	if (info->testinfo_len)
		fprintf(stdout, "The test extra info:\n");

	for (i = 0; i < info->testinfo_len; i++) {
		fprintf(stdout, "%s\t %d\n",
			(char *)(strings->data + i * ETH_GSTRING_LEN),
			(u32) test->data[i]);
	}

	fprintf(stdout, "\n");
	return rc;
}

static int dump_pause(void)
{
	fprintf(stdout,
		"Autonegotiate:	%s\n"
		"RX:		%s\n"
		"TX:		%s\n",
		epause.autoneg ? "on" : "off",
		epause.rx_pause ? "on" : "off",
		epause.tx_pause ? "on" : "off");

	fprintf(stdout, "\n");
	return 0;
}

static int dump_ring(void)
{
	fprintf(stdout,
		"Pre-set maximums:\n"
		"RX:		%u\n"
		"RX Mini:	%u\n"
		"RX Jumbo:	%u\n"
		"TX:		%u\n",
		ering.rx_max_pending,
		ering.rx_mini_max_pending,
		ering.rx_jumbo_max_pending,
		ering.tx_max_pending);

	fprintf(stdout,
		"Current hardware settings:\n"
		"RX:		%u\n"
		"RX Mini:	%u\n"
		"RX Jumbo:	%u\n"
		"TX:		%u\n",
		ering.rx_pending,
		ering.rx_mini_pending,
		ering.rx_jumbo_pending,
		ering.tx_pending);

	fprintf(stdout, "\n");
	return 0;
}

static int dump_coalesce(void)
{
	fprintf(stdout, "Adaptive RX: %s  TX: %s\n",
		ecoal.use_adaptive_rx_coalesce ? "on" : "off",
		ecoal.use_adaptive_tx_coalesce ? "on" : "off");

	fprintf(stdout,
		"stats-block-usecs: %u\n"
		"sample-interval: %u\n"
		"pkt-rate-low: %u\n"
		"pkt-rate-high: %u\n"
		"\n"
		"rx-usecs: %u\n"
		"rx-frames: %u\n"
		"rx-usecs-irq: %u\n"
		"rx-frames-irq: %u\n"
		"\n"
		"tx-usecs: %u\n"
		"tx-frames: %u\n"
		"tx-usecs-irq: %u\n"
		"tx-frames-irq: %u\n"
		"\n"
		"rx-usecs-low: %u\n"
		"rx-frame-low: %u\n"
		"tx-usecs-low: %u\n"
		"tx-frame-low: %u\n"
		"\n"
		"rx-usecs-high: %u\n"
		"rx-frame-high: %u\n"
		"tx-usecs-high: %u\n"
		"tx-frame-high: %u\n"
		"\n",
		ecoal.stats_block_coalesce_usecs,
		ecoal.rate_sample_interval,
		ecoal.pkt_rate_low,
		ecoal.pkt_rate_high,

		ecoal.rx_coalesce_usecs,
		ecoal.rx_max_coalesced_frames,
		ecoal.rx_coalesce_usecs_irq,
		ecoal.rx_max_coalesced_frames_irq,

		ecoal.tx_coalesce_usecs,
		ecoal.tx_max_coalesced_frames,
		ecoal.tx_coalesce_usecs_irq,
		ecoal.tx_max_coalesced_frames_irq,

		ecoal.rx_coalesce_usecs_low,
		ecoal.rx_max_coalesced_frames_low,
		ecoal.tx_coalesce_usecs_low,
		ecoal.tx_max_coalesced_frames_low,

		ecoal.rx_coalesce_usecs_high,
		ecoal.rx_max_coalesced_frames_high,
		ecoal.tx_coalesce_usecs_high,
		ecoal.tx_max_coalesced_frames_high);

	return 0;
}

static int dump_offload(int rx, int tx, int sg, int tso, int ufo, int gso,
			int gro, int lro, int rxvlan, int txvlan, int ntuple,
			int rxhash)
{
	fprintf(stdout,
		"rx-checksumming: %s\n"
		"tx-checksumming: %s\n"
		"scatter-gather: %s\n"
		"tcp-segmentation-offload: %s\n"
		"udp-fragmentation-offload: %s\n"
		"generic-segmentation-offload: %s\n"
		"generic-receive-offload: %s\n"
		"large-receive-offload: %s\n"
		"rx-vlan-offload: %s\n"
		"tx-vlan-offload: %s\n"
		"ntuple-filters: %s\n"
		"receive-hashing: %s\n",
		rx ? "on" : "off",
		tx ? "on" : "off",
		sg ? "on" : "off",
		tso ? "on" : "off",
		ufo ? "on" : "off",
		gso ? "on" : "off",
		gro ? "on" : "off",
		lro ? "on" : "off",
		rxvlan ? "on" : "off",
		txvlan ? "on" : "off",
		ntuple ? "on" : "off",
		rxhash ? "on" : "off");

	return 0;
}

static int dump_rxfhash(int fhash, u64 val)
{
	switch (fhash) {
	case TCP_V4_FLOW:
		fprintf(stdout, "TCP over IPV4 flows");
		break;
	case UDP_V4_FLOW:
		fprintf(stdout, "UDP over IPV4 flows");
		break;
	case SCTP_V4_FLOW:
		fprintf(stdout, "SCTP over IPV4 flows");
		break;
	case AH_ESP_V4_FLOW:
		fprintf(stdout, "IPSEC AH over IPV4 flows");
		break;
	case TCP_V6_FLOW:
		fprintf(stdout, "TCP over IPV6 flows");
		break;
	case UDP_V6_FLOW:
		fprintf(stdout, "UDP over IPV6 flows");
		break;
	case SCTP_V6_FLOW:
		fprintf(stdout, "SCTP over IPV6 flows");
		break;
	case AH_ESP_V6_FLOW:
		fprintf(stdout, "IPSEC AH over IPV6 flows");
		break;
	default:
		break;
	}

	if (val & RXH_DISCARD) {
		fprintf(stdout, " - All matching flows discarded on RX\n");
		return 0;
	}
	fprintf(stdout, " use these fields for computing Hash flow key:\n");

	fprintf(stdout, "%s\n", unparse_rxfhashopts(val));

	return 0;
}

static int doit(void)
{
	struct ifreq ifr;
	int fd;

	/* Setup our control structures. */
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, devname);

	/* Open control socket. */
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("Cannot get control socket");
		return 70;
	}

	/* all of these are expected to populate ifr->ifr_data as needed */
	if (mode == MODE_GDRV) {
		return do_gdrv(fd, &ifr);
	} else if (mode == MODE_GSET) {
		return do_gset(fd, &ifr);
	} else if (mode == MODE_SSET) {
		return do_sset(fd, &ifr);
	} else if (mode == MODE_GREGS) {
		return do_gregs(fd, &ifr);
	} else if (mode == MODE_NWAY_RST) {
		return do_nway_rst(fd, &ifr);
	} else if (mode == MODE_GEEPROM) {
		return do_geeprom(fd, &ifr);
	} else if (mode == MODE_SEEPROM) {
		return do_seeprom(fd, &ifr);
	} else if (mode == MODE_TEST) {
		return do_test(fd, &ifr);
	} else if (mode == MODE_PHYS_ID) {
		return do_phys_id(fd, &ifr);
	} else if (mode == MODE_GPAUSE) {
		return do_gpause(fd, &ifr);
	} else if (mode == MODE_SPAUSE) {
		return do_spause(fd, &ifr);
	} else if (mode == MODE_GCOALESCE) {
		return do_gcoalesce(fd, &ifr);
	} else if (mode == MODE_SCOALESCE) {
		return do_scoalesce(fd, &ifr);
	} else if (mode == MODE_GRING) {
		return do_gring(fd, &ifr);
	} else if (mode == MODE_SRING) {
		return do_sring(fd, &ifr);
	} else if (mode == MODE_GOFFLOAD) {
		return do_goffload(fd, &ifr);
	} else if (mode == MODE_SOFFLOAD) {
		return do_soffload(fd, &ifr);
	} else if (mode == MODE_GSTATS) {
		return do_gstats(fd, &ifr);
	} else if (mode == MODE_GNFC) {
		return do_grxclass(fd, &ifr);
	} else if (mode == MODE_SNFC) {
		return do_srxclass(fd, &ifr);
	} else if (mode == MODE_GRXFHINDIR) {
		return do_grxfhindir(fd, &ifr);
	} else if (mode == MODE_SRXFHINDIR) {
		return do_srxfhindir(fd, &ifr);
	} else if (mode == MODE_SNTUPLE) {
		return do_srxntuple(fd, &ifr);
	} else if (mode == MODE_GNTUPLE) {
		return do_grxntuple(fd, &ifr);
	} else if (mode == MODE_FLASHDEV) {
		return do_flash(fd, &ifr);
	} else if (mode == MODE_PERMADDR) {
		return do_permaddr(fd, &ifr);
	}

	return 69;
}

static int do_gdrv(int fd, struct ifreq *ifr)
{
	int err;
	struct ethtool_drvinfo drvinfo;

	drvinfo.cmd = ETHTOOL_GDRVINFO;
	ifr->ifr_data = (caddr_t)&drvinfo;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot get driver information");
		return 71;
	}
	return dump_drvinfo(&drvinfo);
}

static int do_gpause(int fd, struct ifreq *ifr)
{
	int err;

	fprintf(stdout, "Pause parameters for %s:\n", devname);

	epause.cmd = ETHTOOL_GPAUSEPARAM;
	ifr->ifr_data = (caddr_t)&epause;
	err = send_ioctl(fd, ifr);
	if (err == 0) {
		err = dump_pause();
		if (err)
			return err;
	} else {
		perror("Cannot get device pause settings");
		return 76;
	}

	return 0;
}

static void do_generic_set1(struct cmdline_info *info, int *changed_out)
{
	int wanted, *v1, *v2;

	v1 = info->wanted_val;
	wanted = *v1;

	if (wanted < 0)
		return;

	v2 = info->ioctl_val;
	if (wanted == *v2) {
		fprintf(stderr, "%s unmodified, ignoring\n", info->name);
	} else {
		*v2 = wanted;
		*changed_out = 1;
	}
}

static void do_generic_set(struct cmdline_info *info,
			   unsigned int n_info,
			   int *changed_out)
{
	unsigned int i;

	for (i = 0; i < n_info; i++)
		do_generic_set1(&info[i], changed_out);
}

static int do_spause(int fd, struct ifreq *ifr)
{
	int err, changed = 0;

	epause.cmd = ETHTOOL_GPAUSEPARAM;
	ifr->ifr_data = (caddr_t)&epause;
	err = send_ioctl(fd, ifr);
	if (err) {
		perror("Cannot get device pause settings");
		return 77;
	}

	do_generic_set(cmdline_pause, ARRAY_SIZE(cmdline_pause), &changed);

	if (!changed) {
		fprintf(stderr, "no pause parameters changed, aborting\n");
		return 78;
	}

	epause.cmd = ETHTOOL_SPAUSEPARAM;
	ifr->ifr_data = (caddr_t)&epause;
	err = send_ioctl(fd, ifr);
	if (err) {
		perror("Cannot set device pause parameters");
		return 79;
	}

	return 0;
}

static int do_sring(int fd, struct ifreq *ifr)
{
	int err, changed = 0;

	ering.cmd = ETHTOOL_GRINGPARAM;
	ifr->ifr_data = (caddr_t)&ering;
	err = send_ioctl(fd, ifr);
	if (err) {
		perror("Cannot get device ring settings");
		return 76;
	}

	do_generic_set(cmdline_ring, ARRAY_SIZE(cmdline_ring), &changed);

	if (!changed) {
		fprintf(stderr, "no ring parameters changed, aborting\n");
		return 80;
	}

	ering.cmd = ETHTOOL_SRINGPARAM;
	ifr->ifr_data = (caddr_t)&ering;
	err = send_ioctl(fd, ifr);
	if (err) {
		perror("Cannot set device ring parameters");
		return 81;
	}

	return 0;
}

static int do_gring(int fd, struct ifreq *ifr)
{
	int err;

	fprintf(stdout, "Ring parameters for %s:\n", devname);

	ering.cmd = ETHTOOL_GRINGPARAM;
	ifr->ifr_data = (caddr_t)&ering;
	err = send_ioctl(fd, ifr);
	if (err == 0) {
		err = dump_ring();
		if (err)
			return err;
	} else {
		perror("Cannot get device ring settings");
		return 76;
	}

	return 0;
}

static int do_gcoalesce(int fd, struct ifreq *ifr)
{
	int err;

	fprintf(stdout, "Coalesce parameters for %s:\n", devname);

	ecoal.cmd = ETHTOOL_GCOALESCE;
	ifr->ifr_data = (caddr_t)&ecoal;
	err = send_ioctl(fd, ifr);
	if (err == 0) {
		err = dump_coalesce();
		if (err)
			return err;
	} else {
		perror("Cannot get device coalesce settings");
		return 82;
	}

	return 0;
}

static int do_scoalesce(int fd, struct ifreq *ifr)
{
	int err, changed = 0;

	ecoal.cmd = ETHTOOL_GCOALESCE;
	ifr->ifr_data = (caddr_t)&ecoal;
	err = send_ioctl(fd, ifr);
	if (err) {
		perror("Cannot get device coalesce settings");
		return 76;
	}

	do_generic_set(cmdline_coalesce, ARRAY_SIZE(cmdline_coalesce),
		       &changed);

	if (!changed) {
		fprintf(stderr, "no coalesce parameters changed, aborting\n");
		return 80;
	}

	ecoal.cmd = ETHTOOL_SCOALESCE;
	ifr->ifr_data = (caddr_t)&ecoal;
	err = send_ioctl(fd, ifr);
	if (err) {
		perror("Cannot set device coalesce parameters");
		return 81;
	}

	return 0;
}

static int do_goffload(int fd, struct ifreq *ifr)
{
	struct ethtool_value eval;
	int err, allfail = 1, rx = 0, tx = 0, sg = 0;
	int tso = 0, ufo = 0, gso = 0, gro = 0, lro = 0, rxvlan = 0, txvlan = 0,
	    ntuple = 0, rxhash = 0;

	fprintf(stdout, "Offload parameters for %s:\n", devname);

	eval.cmd = ETHTOOL_GRXCSUM;
	ifr->ifr_data = (caddr_t)&eval;
	err = send_ioctl(fd, ifr);
	if (err)
		perror("Cannot get device rx csum settings");
	else {
		rx = eval.data;
		allfail = 0;
	}

	eval.cmd = ETHTOOL_GTXCSUM;
	ifr->ifr_data = (caddr_t)&eval;
	err = send_ioctl(fd, ifr);
	if (err)
		perror("Cannot get device tx csum settings");
	else {
		tx = eval.data;
		allfail = 0;
	}

	eval.cmd = ETHTOOL_GSG;
	ifr->ifr_data = (caddr_t)&eval;
	err = send_ioctl(fd, ifr);
	if (err)
		perror("Cannot get device scatter-gather settings");
	else {
		sg = eval.data;
		allfail = 0;
	}

	eval.cmd = ETHTOOL_GTSO;
	ifr->ifr_data = (caddr_t)&eval;
	err = send_ioctl(fd, ifr);
	if (err)
		perror("Cannot get device tcp segmentation offload settings");
	else {
		tso = eval.data;
		allfail = 0;
	}

	eval.cmd = ETHTOOL_GUFO;
	ifr->ifr_data = (caddr_t)&eval;
	err = ioctl(fd, SIOCETHTOOL, ifr);
	if (err)
		perror("Cannot get device udp large send offload settings");
	else {
		ufo = eval.data;
		allfail = 0;
	}

	eval.cmd = ETHTOOL_GGSO;
	ifr->ifr_data = (caddr_t)&eval;
	err = ioctl(fd, SIOCETHTOOL, ifr);
	if (err)
		perror("Cannot get device generic segmentation offload settings");
	else {
		gso = eval.data;
		allfail = 0;
	}

	eval.cmd = ETHTOOL_GFLAGS;
	ifr->ifr_data = (caddr_t)&eval;
	err = ioctl(fd, SIOCETHTOOL, ifr);
	if (err) {
		perror("Cannot get device flags");
	} else {
		lro = (eval.data & ETH_FLAG_LRO) != 0;
		rxvlan = (eval.data & ETH_FLAG_RXVLAN) != 0;
		txvlan = (eval.data & ETH_FLAG_TXVLAN) != 0;
		ntuple = (eval.data & ETH_FLAG_NTUPLE) != 0;
		rxhash = (eval.data & ETH_FLAG_RXHASH) != 0;
		allfail = 0;
	}

	eval.cmd = ETHTOOL_GGRO;
	ifr->ifr_data = (caddr_t)&eval;
	err = ioctl(fd, SIOCETHTOOL, ifr);
	if (err)
		perror("Cannot get device GRO settings");
	else {
		gro = eval.data;
		allfail = 0;
	}

	if (allfail) {
		fprintf(stdout, "no offload info available\n");
		return 83;
	}

	return dump_offload(rx, tx, sg, tso, ufo, gso, gro, lro, rxvlan, txvlan,
			    ntuple, rxhash);
}

static int do_soffload(int fd, struct ifreq *ifr)
{
	struct ethtool_value eval;
	int err, changed = 0;

	if (off_csum_rx_wanted >= 0) {
		changed = 1;
		eval.cmd = ETHTOOL_SRXCSUM;
		eval.data = (off_csum_rx_wanted == 1);
		ifr->ifr_data = (caddr_t)&eval;
		err = send_ioctl(fd, ifr);
		if (err) {
			perror("Cannot set device rx csum settings");
			return 84;
		}
	}

	if (off_csum_tx_wanted >= 0) {
		changed = 1;
		eval.cmd = ETHTOOL_STXCSUM;
		eval.data = (off_csum_tx_wanted == 1);
		ifr->ifr_data = (caddr_t)&eval;
		err = send_ioctl(fd, ifr);
		if (err) {
			perror("Cannot set device tx csum settings");
			return 85;
		}
	}

	if (off_sg_wanted >= 0) {
		changed = 1;
		eval.cmd = ETHTOOL_SSG;
		eval.data = (off_sg_wanted == 1);
		ifr->ifr_data = (caddr_t)&eval;
		err = send_ioctl(fd, ifr);
		if (err) {
			perror("Cannot set device scatter-gather settings");
			return 86;
		}
	}

	if (off_tso_wanted >= 0) {
		changed = 1;
		eval.cmd = ETHTOOL_STSO;
		eval.data = (off_tso_wanted == 1);
		ifr->ifr_data = (caddr_t)&eval;
		err = send_ioctl(fd, ifr);
		if (err) {
			perror("Cannot set device tcp segmentation offload settings");
			return 88;
		}
	}
	if (off_ufo_wanted >= 0) {
		changed = 1;
		eval.cmd = ETHTOOL_SUFO;
		eval.data = (off_ufo_wanted == 1);
		ifr->ifr_data = (caddr_t)&eval;
		err = ioctl(fd, SIOCETHTOOL, ifr);
		if (err) {
			perror("Cannot set device udp large send offload settings");
			return 89;
		}
	}
	if (off_gso_wanted >= 0) {
		changed = 1;
		eval.cmd = ETHTOOL_SGSO;
		eval.data = (off_gso_wanted == 1);
		ifr->ifr_data = (caddr_t)&eval;
		err = ioctl(fd, SIOCETHTOOL, ifr);
		if (err) {
			perror("Cannot set device generic segmentation offload settings");
			return 90;
		}
	}
	if (off_flags_mask) {
		changed = 1;
		eval.cmd = ETHTOOL_GFLAGS;
		eval.data = 0;
		ifr->ifr_data = (caddr_t)&eval;
		err = ioctl(fd, SIOCETHTOOL, ifr);
		if (err) {
			perror("Cannot get device flag settings");
			return 91;
		}

		eval.cmd = ETHTOOL_SFLAGS;
		eval.data = ((eval.data & ~off_flags_mask) |
			     off_flags_wanted);

		err = ioctl(fd, SIOCETHTOOL, ifr);
		if (err) {
			perror("Cannot set device flag settings");
			return 92;
		}
	}
	if (off_gro_wanted >= 0) {
		changed = 1;
		eval.cmd = ETHTOOL_SGRO;
		eval.data = (off_gro_wanted == 1);
		ifr->ifr_data = (caddr_t)&eval;
		err = ioctl(fd, SIOCETHTOOL, ifr);
		if (err) {
			perror("Cannot set device GRO settings");
			return 93;
		}
	}

	if (!changed) {
		fprintf(stdout, "no offload settings changed\n");
	}

	return 0;
}

static int do_gset(int fd, struct ifreq *ifr)
{
	int err;
	struct ethtool_cmd ecmd;
	struct ethtool_wolinfo wolinfo;
	struct ethtool_value edata;
	int allfail = 1;

	fprintf(stdout, "Settings for %s:\n", devname);

	ecmd.cmd = ETHTOOL_GSET;
	ifr->ifr_data = (caddr_t)&ecmd;
	err = send_ioctl(fd, ifr);
	if (err == 0) {
		err = dump_ecmd(&ecmd);
		if (err)
			return err;
		allfail = 0;
	} else if (errno != EOPNOTSUPP) {
		perror("Cannot get device settings");
	}

	wolinfo.cmd = ETHTOOL_GWOL;
	ifr->ifr_data = (caddr_t)&wolinfo;
	err = send_ioctl(fd, ifr);
	if (err == 0) {
		err = dump_wol(&wolinfo);
		if (err)
			return err;
		allfail = 0;
	} else if (errno != EOPNOTSUPP) {
		perror("Cannot get wake-on-lan settings");
	}

	edata.cmd = ETHTOOL_GMSGLVL;
	ifr->ifr_data = (caddr_t)&edata;
	err = send_ioctl(fd, ifr);
	if (err == 0) {
		fprintf(stdout, "	Current message level: 0x%08x (%d)\n"
			"			       ",
			edata.data, edata.data);
		print_flags(cmdline_msglvl, ARRAY_SIZE(cmdline_msglvl),
			    edata.data);
		fprintf(stdout, "\n");
		allfail = 0;
	} else if (errno != EOPNOTSUPP) {
		perror("Cannot get message level");
	}

	edata.cmd = ETHTOOL_GLINK;
	ifr->ifr_data = (caddr_t)&edata;
	err = send_ioctl(fd, ifr);
	if (err == 0) {
		fprintf(stdout, "	Link detected: %s\n",
			edata.data ? "yes":"no");
		allfail = 0;
	} else if (errno != EOPNOTSUPP) {
		perror("Cannot get link status");
	}

	if (allfail) {
		fprintf(stdout, "No data available\n");
		return 75;
	}
	return 0;
}

static int do_sset(int fd, struct ifreq *ifr)
{
	int err;

	if (gset_changed) {
		struct ethtool_cmd ecmd;

		ecmd.cmd = ETHTOOL_GSET;
		ifr->ifr_data = (caddr_t)&ecmd;
		err = send_ioctl(fd, ifr);
		if (err < 0) {
			perror("Cannot get current device settings");
		} else {
			/* Change everything the user specified. */
			if (speed_wanted != -1)
				ethtool_cmd_speed_set(&ecmd, speed_wanted);
			if (duplex_wanted != -1)
				ecmd.duplex = duplex_wanted;
			if (port_wanted != -1)
				ecmd.port = port_wanted;
			if (autoneg_wanted != -1)
				ecmd.autoneg = autoneg_wanted;
			if (phyad_wanted != -1)
				ecmd.phy_address = phyad_wanted;
			if (xcvr_wanted != -1)
				ecmd.transceiver = xcvr_wanted;
			if (advertising_wanted != -1) {
				if (advertising_wanted == 0)
					ecmd.advertising = ecmd.supported &
						(ADVERTISED_10baseT_Half |
						 ADVERTISED_10baseT_Full |
						 ADVERTISED_100baseT_Half |
						 ADVERTISED_100baseT_Full |
						 ADVERTISED_1000baseT_Half |
						 ADVERTISED_1000baseT_Full |
						 ADVERTISED_2500baseX_Full |
						 ADVERTISED_10000baseT_Full);
				else
					ecmd.advertising = advertising_wanted;
			}

			/* Try to perform the update. */
			ecmd.cmd = ETHTOOL_SSET;
			ifr->ifr_data = (caddr_t)&ecmd;
			err = send_ioctl(fd, ifr);
			if (err < 0)
				perror("Cannot set new settings");
		}
		if (err < 0) {
			if (speed_wanted != -1)
				fprintf(stderr, "  not setting speed\n");
			if (duplex_wanted != -1)
				fprintf(stderr, "  not setting duplex\n");
			if (port_wanted != -1)
				fprintf(stderr, "  not setting port\n");
			if (autoneg_wanted != -1)
				fprintf(stderr, "  not setting autoneg\n");
			if (phyad_wanted != -1)
				fprintf(stderr, "  not setting phy_address\n");
			if (xcvr_wanted != -1)
				fprintf(stderr, "  not setting transceiver\n");
		}
	}

	if (gwol_changed) {
		struct ethtool_wolinfo wol;

		wol.cmd = ETHTOOL_GWOL;
		ifr->ifr_data = (caddr_t)&wol;
		err = send_ioctl(fd, ifr);
		if (err < 0) {
			perror("Cannot get current wake-on-lan settings");
		} else {
			/* Change everything the user specified. */
			if (wol_change) {
				wol.wolopts = wol_wanted;
			}
			if (sopass_change) {
				int i;
				for (i = 0; i < SOPASS_MAX; i++) {
					wol.sopass[i] = sopass_wanted[i];
				}
			}

			/* Try to perform the update. */
			wol.cmd = ETHTOOL_SWOL;
			ifr->ifr_data = (caddr_t)&wol;
			err = send_ioctl(fd, ifr);
			if (err < 0)
				perror("Cannot set new wake-on-lan settings");
		}
		if (err < 0) {
			if (wol_change)
				fprintf(stderr, "  not setting wol\n");
			if (sopass_change)
				fprintf(stderr, "  not setting sopass\n");
		}
	}

	if (msglvl_changed) {
		struct ethtool_value edata;

		edata.cmd = ETHTOOL_GMSGLVL;
		ifr->ifr_data = (caddr_t)&edata;
		err = send_ioctl(fd, ifr);
		if (err < 0) {
			perror("Cannot get msglvl");
		} else {
			edata.cmd = ETHTOOL_SMSGLVL;
			edata.data = ((edata.data & ~msglvl_mask) |
				      msglvl_wanted);
			ifr->ifr_data = (caddr_t)&edata;
			err = send_ioctl(fd, ifr);
			if (err < 0)
				perror("Cannot set new msglvl");
		}
	}

	return 0;
}

static int do_gregs(int fd, struct ifreq *ifr)
{
	int err;
	struct ethtool_drvinfo drvinfo;
	struct ethtool_regs *regs;

	drvinfo.cmd = ETHTOOL_GDRVINFO;
	ifr->ifr_data = (caddr_t)&drvinfo;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot get driver information");
		return 72;
	}

	regs = calloc(1, sizeof(*regs)+drvinfo.regdump_len);
	if (!regs) {
		perror("Cannot allocate memory for register dump");
		return 73;
	}
	regs->cmd = ETHTOOL_GREGS;
	regs->len = drvinfo.regdump_len;
	ifr->ifr_data = (caddr_t)regs;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot get register dump");
		free(regs);
		return 74;
	}
	if(dump_regs(&drvinfo, regs) < 0) {
		perror("Cannot dump registers");
		free(regs);
		return 75;
	}
	free(regs);

	return 0;
}

static int do_nway_rst(int fd, struct ifreq *ifr)
{
	struct ethtool_value edata;
	int err;

	edata.cmd = ETHTOOL_NWAY_RST;
	ifr->ifr_data = (caddr_t)&edata;
	err = send_ioctl(fd, ifr);
	if (err < 0)
		perror("Cannot restart autonegotiation");

	return err;
}

static int do_geeprom(int fd, struct ifreq *ifr)
{
	int err;
	struct ethtool_drvinfo drvinfo;
	struct ethtool_eeprom *eeprom;

	drvinfo.cmd = ETHTOOL_GDRVINFO;
	ifr->ifr_data = (caddr_t)&drvinfo;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot get driver information");
		return 74;
	}

	if (geeprom_length <= 0)
		geeprom_length = drvinfo.eedump_len;

	if (drvinfo.eedump_len < geeprom_offset + geeprom_length)
		geeprom_length = drvinfo.eedump_len - geeprom_offset;

	eeprom = calloc(1, sizeof(*eeprom)+geeprom_length);
	if (!eeprom) {
		perror("Cannot allocate memory for EEPROM data");
		return 75;
	}
	eeprom->cmd = ETHTOOL_GEEPROM;
	eeprom->len = geeprom_length;
	eeprom->offset = geeprom_offset;
	ifr->ifr_data = (caddr_t)eeprom;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot get EEPROM data");
		free(eeprom);
		return 74;
	}
	err = dump_eeprom(&drvinfo, eeprom);
	free(eeprom);

	return err;
}

static int do_seeprom(int fd, struct ifreq *ifr)
{
	int err;
	struct ethtool_drvinfo drvinfo;
	struct ethtool_eeprom *eeprom;

	drvinfo.cmd = ETHTOOL_GDRVINFO;
	ifr->ifr_data = (caddr_t)&drvinfo;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot get driver information");
		return 74;
	}

	if (seeprom_value != EOF)
		seeprom_length = 1;

	if (seeprom_length <= 0)
		seeprom_length = drvinfo.eedump_len;

	if (drvinfo.eedump_len < seeprom_offset + seeprom_length)
		seeprom_length = drvinfo.eedump_len - seeprom_offset;

	eeprom = calloc(1, sizeof(*eeprom)+seeprom_length);
	if (!eeprom) {
		perror("Cannot allocate memory for EEPROM data");
		return 75;
	}

	eeprom->cmd = ETHTOOL_SEEPROM;
	eeprom->len = seeprom_length;
	eeprom->offset = seeprom_offset;
	eeprom->magic = seeprom_magic;
	eeprom->data[0] = seeprom_value;

	/* Multi-byte write: read input from stdin */
	if (seeprom_value == EOF)
		eeprom->len = fread(eeprom->data, 1, eeprom->len, stdin);

	ifr->ifr_data = (caddr_t)eeprom;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot set EEPROM data");
		err = 87;
	}
	free(eeprom);

	return err;
}

static int do_test(int fd, struct ifreq *ifr)
{
	int err;
	struct ethtool_drvinfo drvinfo;
	struct ethtool_test *test;
	struct ethtool_gstrings *strings;

	drvinfo.cmd = ETHTOOL_GDRVINFO;
	ifr->ifr_data = (caddr_t)&drvinfo;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot get driver information");
		return 72;
	}

	test = calloc(1, sizeof(*test) + drvinfo.testinfo_len * sizeof(u64));
	if (!test) {
		perror("Cannot allocate memory for test info");
		return 73;
	}
	memset (test->data, 0, drvinfo.testinfo_len * sizeof(u64));
	test->cmd = ETHTOOL_TEST;
	test->len = drvinfo.testinfo_len;
	if (test_type == OFFLINE)
		test->flags = ETH_TEST_FL_OFFLINE;
	else
		test->flags = 0;
	ifr->ifr_data = (caddr_t)test;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot test");
		free (test);
		return 74;
	}

	strings = calloc(1, sizeof(*strings) +
			    drvinfo.testinfo_len * ETH_GSTRING_LEN);
	if (!strings) {
		perror("Cannot allocate memory for strings");
		free(test);
		return 73;
	}
	memset (strings->data, 0, drvinfo.testinfo_len * ETH_GSTRING_LEN);
	strings->cmd = ETHTOOL_GSTRINGS;
	strings->string_set = ETH_SS_TEST;
	strings->len = drvinfo.testinfo_len;
	ifr->ifr_data = (caddr_t)strings;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot get strings");
		free (test);
		free (strings);
		return 74;
	}
	err = dump_test(&drvinfo, test, strings);
	free(test);
	free(strings);

	return err;
}

static int do_phys_id(int fd, struct ifreq *ifr)
{
	int err;
	struct ethtool_value edata;

	edata.cmd = ETHTOOL_PHYS_ID;
	edata.data = phys_id_time;
	ifr->ifr_data = (caddr_t)&edata;
	err = send_ioctl(fd, ifr);
	if (err < 0)
		perror("Cannot identify NIC");

	return err;
}

static int do_gstats(int fd, struct ifreq *ifr)
{
	struct ethtool_drvinfo drvinfo;
	struct ethtool_gstrings *strings;
	struct ethtool_stats *stats;
	unsigned int n_stats, sz_str, sz_stats, i;
	int err;

	drvinfo.cmd = ETHTOOL_GDRVINFO;
	ifr->ifr_data = (caddr_t)&drvinfo;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot get driver information");
		return 71;
	}

	n_stats = drvinfo.n_stats;
	if (n_stats < 1) {
		fprintf(stderr, "no stats available\n");
		return 94;
	}

	sz_str = n_stats * ETH_GSTRING_LEN;
	sz_stats = n_stats * sizeof(u64);

	strings = calloc(1, sz_str + sizeof(struct ethtool_gstrings));
	stats = calloc(1, sz_stats + sizeof(struct ethtool_stats));
	if (!strings || !stats) {
		fprintf(stderr, "no memory available\n");
		return 95;
	}

	strings->cmd = ETHTOOL_GSTRINGS;
	strings->string_set = ETH_SS_STATS;
	strings->len = n_stats;
	ifr->ifr_data = (caddr_t) strings;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot get stats strings information");
		free(strings);
		free(stats);
		return 96;
	}

	stats->cmd = ETHTOOL_GSTATS;
	stats->n_stats = n_stats;
	ifr->ifr_data = (caddr_t) stats;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot get stats information");
		free(strings);
		free(stats);
		return 97;
	}

	/* todo - pretty-print the strings per-driver */
	fprintf(stdout, "NIC statistics:\n");
	for (i = 0; i < n_stats; i++) {
		fprintf(stdout, "     %.*s: %llu\n",
			ETH_GSTRING_LEN,
			&strings->data[i * ETH_GSTRING_LEN],
			stats->data[i]);
	}
	free(strings);
	free(stats);

	return 0;
}


static int do_srxclass(int fd, struct ifreq *ifr)
{
	int err;

	if (rx_fhash_changed) {
		struct ethtool_rxnfc nfccmd;

		nfccmd.cmd = ETHTOOL_SRXFH;
		nfccmd.flow_type = rx_fhash_set;
		nfccmd.data = rx_fhash_val;

		ifr->ifr_data = (caddr_t)&nfccmd;
		err = ioctl(fd, SIOCETHTOOL, ifr);
		if (err < 0)
			perror("Cannot change RX network flow hashing options");

	}

	return 0;
}

static int do_grxclass(int fd, struct ifreq *ifr)
{
	int err;

	if (rx_fhash_get) {
		struct ethtool_rxnfc nfccmd;

		nfccmd.cmd = ETHTOOL_GRXFH;
		nfccmd.flow_type = rx_fhash_get;
		ifr->ifr_data = (caddr_t)&nfccmd;
		err = ioctl(fd, SIOCETHTOOL, ifr);
		if (err < 0)
			perror("Cannot get RX network flow hashing options");
		else
			dump_rxfhash(rx_fhash_get, nfccmd.data);
	}

	return 0;
}

static int do_grxfhindir(int fd, struct ifreq *ifr)
{
	struct ethtool_rxnfc ring_count;
	struct ethtool_rxfh_indir indir_head;
	struct ethtool_rxfh_indir *indir;
	u32 i;
	int err;

	ring_count.cmd = ETHTOOL_GRXRINGS;
	ifr->ifr_data = (caddr_t) &ring_count;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot get RX ring count");
		return 102;
	}

	indir_head.cmd = ETHTOOL_GRXFHINDIR;
	indir_head.size = 0;
	ifr->ifr_data = (caddr_t) &indir_head;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot get RX flow hash indirection table size");
		return 103;
	}

	indir = malloc(sizeof(*indir) +
		       indir_head.size * sizeof(*indir->ring_index));
	indir->cmd = ETHTOOL_GRXFHINDIR;
	indir->size = indir_head.size;
	ifr->ifr_data = (caddr_t) indir;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot get RX flow hash indirection table");
		return 103;
	}

	printf("RX flow hash indirection table for %s with %llu RX ring(s):\n",
	       devname, ring_count.data);
	for (i = 0; i < indir->size; i++) {
		if (i % 8 == 0)
			printf("%5u: ", i);
		printf(" %5u", indir->ring_index[i]);
		if (i % 8 == 7)
			fputc('\n', stdout);
	}
	return 0;
}

static int do_srxfhindir(int fd, struct ifreq *ifr)
{
	struct ethtool_rxfh_indir indir_head;
	struct ethtool_rxfh_indir *indir;
	u32 i;
	int err;

	if (!rxfhindir_equal && !rxfhindir_weight)
		show_usage(1);

	indir_head.cmd = ETHTOOL_GRXFHINDIR;
	indir_head.size = 0;
	ifr->ifr_data = (caddr_t) &indir_head;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot get RX flow hash indirection table size");
		return 104;
	}

	indir = malloc(sizeof(*indir) +
		       indir_head.size * sizeof(*indir->ring_index));
	indir->cmd = ETHTOOL_SRXFHINDIR;
	indir->size = indir_head.size;

	if (rxfhindir_equal) {
		for (i = 0; i < indir->size; i++)
			indir->ring_index[i] = i % rxfhindir_equal;
	} else {
		u32 j, weight, sum = 0, partial = 0;

		for (j = 0; rxfhindir_weight[j]; j++) {
			weight = get_u32(rxfhindir_weight[j], 0);
			sum += weight;
		}

		if (sum == 0) {
			fprintf(stderr,
				"At least one weight must be non-zero\n");
			exit(1);
		}

		if (sum > indir->size) {
			fprintf(stderr,
				"Total weight exceeds the size of the "
				"indirection table\n");
			exit(1);
		}

		j = -1;
		for (i = 0; i < indir->size; i++) {
			while (i >= indir->size * partial / sum) {
				j += 1;
				weight = get_u32(rxfhindir_weight[j], 0);
				partial += weight;
			}
			indir->ring_index[i] = j;
		}
	}

	ifr->ifr_data = (caddr_t) indir;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot set RX flow hash indirection table");
		return 105;
	}

	return 0;
}

static int do_flash(int fd, struct ifreq *ifr)
{
	struct ethtool_flash efl;
	int err;

	if (flash < 0) {
		fprintf(stdout, "Missing filename argument\n");
		show_usage(1);
		return 98;
	}

	if (strlen(flash_file) > ETHTOOL_FLASH_MAX_FILENAME - 1) {
		fprintf(stdout, "Filename too long\n");
		return 99;
	}

	efl.cmd = ETHTOOL_FLASHDEV;
	strcpy(efl.data, flash_file);

	if (flash_region < 0)
		efl.region = ETHTOOL_FLASH_ALL_REGIONS;
	else
		efl.region = flash_region;

	ifr->ifr_data = (caddr_t)&efl;
	err = send_ioctl(fd, ifr);
	if (err < 0)
		perror("Flashing failed");

	return err;
}

static int do_permaddr(int fd, struct ifreq *ifr)
{
	int i, err;
	struct ethtool_perm_addr *epaddr;

	epaddr = malloc(sizeof(struct ethtool_perm_addr) + MAX_ADDR_LEN);
	epaddr->cmd = ETHTOOL_GPERMADDR;
	epaddr->size = MAX_ADDR_LEN;
	ifr->ifr_data = (caddr_t)epaddr;

	err = send_ioctl(fd, ifr);
	if (err < 0)
		perror("Cannot read permanent address");
	else {
		printf("Permanent address:");
		for (i = 0; i < epaddr->size; i++)
			printf("%c%02x", (i == 0) ? ' ' : ':',
			       epaddr->data[i]);
		printf("\n");
	}
	free(epaddr);

	return err;
}

static int do_srxntuple(int fd, struct ifreq *ifr)
{
	int err;

	if (sntuple_changed) {
		struct ethtool_rx_ntuple ntuplecmd;

		ntuplecmd.cmd = ETHTOOL_SRXNTUPLE;
		memcpy(&ntuplecmd.fs, &ntuple_fs,
		       sizeof(struct ethtool_rx_ntuple_flow_spec));

		ifr->ifr_data = (caddr_t)&ntuplecmd;
		err = ioctl(fd, SIOCETHTOOL, ifr);
		if (err < 0)
			perror("Cannot add new RX n-tuple filter");
	} else {
		show_usage(1);
	}

	return 0;
}

static int do_grxntuple(int fd, struct ifreq *ifr)
{
	struct ethtool_sset_info *sset_info;
	struct ethtool_gstrings *strings;
	int sz_str, n_strings, err, i;

	sset_info = malloc(sizeof(struct ethtool_sset_info) + sizeof(u32));
	sset_info->cmd = ETHTOOL_GSSET_INFO;
	sset_info->sset_mask = (1ULL << ETH_SS_NTUPLE_FILTERS);
	ifr->ifr_data = (caddr_t)sset_info;
	err = send_ioctl(fd, ifr);

	if ((err < 0) ||
	    (!(sset_info->sset_mask & (1ULL << ETH_SS_NTUPLE_FILTERS)))) {
		perror("Cannot get driver strings info");
		return 100;
	}

	n_strings = sset_info->data[0];
	free(sset_info);
	sz_str = n_strings * ETH_GSTRING_LEN;

	strings = calloc(1, sz_str + sizeof(struct ethtool_gstrings));
	if (!strings) {
		fprintf(stderr, "no memory available\n");
		return 95;
	}

	strings->cmd = ETHTOOL_GRXNTUPLE;
	strings->string_set = ETH_SS_NTUPLE_FILTERS;
	strings->len = n_strings;
	ifr->ifr_data = (caddr_t) strings;
	err = send_ioctl(fd, ifr);
	if (err < 0) {
		perror("Cannot get Rx n-tuple information");
		free(strings);
		return 101;
	}

	n_strings = strings->len;
	fprintf(stdout, "Rx n-tuple filters:\n");
	for (i = 0; i < n_strings; i++)
		fprintf(stdout, "%s", &strings->data[i * ETH_GSTRING_LEN]);

	free(strings);

	return 0;
}

static int send_ioctl(int fd, struct ifreq *ifr)
{
	return ioctl(fd, SIOCETHTOOL, ifr);
}

int main(int argc, char **argp, char **envp)
{
	parse_cmdline(argc, argp);
	return doit();
}
