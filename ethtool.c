/*
 * ethtool.c: Linux ethernet device configuration tool.
 *
 * Copyright (C) 1998 David S. Miller (davem@dm.cobaltmicro.com)
 * Kernel 2.4 update Copyright 2001 Jeff Garzik <jgarzik@mandrakesoft.com>
 * Wake-on-LAN support by Tim Hockin <thockin@sun.com>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <net/if.h>
#include <errno.h>

typedef __uint32_t u32;		/* hack, so we may include kernel's ethtool.h */
typedef __uint16_t u16;		/* ditto */
typedef __uint8_t u8;		/* ditto */
#include "ethtool-copy.h"
#ifdef PRE24_COMPAT
# ifdef __sparc__
#  include "ethtool-sparc22.h"
# endif
# include <sys/utsname.h>
#endif
#include <linux/sockios.h>
#ifndef SIOCETHTOOL
# define SIOCETHTOOL	0x8946	/* if compiling on libc with kernel 2.2 headers */
#endif

static int parse_wolopts(char *optstr, int *data);
static char *unparse_wolopts(int wolopts);
static int parse_sopass(char *src, unsigned char *dest);
static int do_gdrv(int fd, struct ifreq *ifr);
static int do_gset(int fd, struct ifreq *ifr);
static int do_sset(int fd, struct ifreq *ifr);
static int send_ioctl(int fd, struct ifreq *ifr);
static int check_for_pre24_kernel();

/* Syntax:
 *
 *	ethtool DEVNAME
 *	ethtool -i DEVNAME
 *	ethtool -s DEVNAME [ speed {10,100,1000} ] \
 *		[ duplex {half,full} ] \
 *		[ port {tp,aui,mii,fibre} ] \
 *		[ autoneg {on,off} ] \
 *		[ phyad %d ] \
 *		[ xcvr {internal,external} ] \
 *		[ wol [pumbagsd]+ ] \
 *		[ sopass %x:%x:%x:%x:%x:%x ]
 */

static char *devname = NULL;
static enum { MODE_GSET=0, MODE_SSET, MODE_GDRV } mode = MODE_GSET;
static int speed_wanted = -1;
static int duplex_wanted = -1;
static int port_wanted = -1;
static int autoneg_wanted = -1;
static int phyad_wanted = -1;
static int xcvr_wanted = -1;
static int gset_changed = 0; /* did anything in GSET change? */
static u32  wol_wanted = 0;
static int wol_change = 0;
static u8 sopass_wanted[SOPASS_MAX];
static int sopass_change = 0;
static int gwol_changed = 0; /* did anything in GWOL change? */
static int is_pre24_kernel = 0;

static void show_usage(int badarg)
{
	fprintf(stderr, PACKAGE " version " VERSION "\n");
	fprintf(stderr, 
		"Usage:\n"
		"	ethtool DEVNAME\n"
		"	ethtool -i DEVNAME\n"
		"	ethtool -s DEVNAME \\\n"
		"		[ speed {10,100,1000} ] \\\n"
		"		[ duplex {half,full} ]	\\\n"
		"		[ port {tp,aui,mii,fibre} ] \\\n"
		"		[ autoneg {on,off} ] \\\n"
		"		[ phyad %%d ] \\\n"
		"		[ xcvr {internal,external} ] \\\n"
		"		[ wol [pumbagsd]+ ] \\\n"
		"		[ sopass %%x:%%x:%%x:%%x:%%x:%%x ] \n"
	);
	exit(badarg);
}

static void parse_cmdline(int argc, char **argp)
{
	int i;

	for(i = 1; i < argc; i++) {
		switch(i) {
		case 1:
			if(!strcmp(argp[i], "-s"))
				mode = MODE_SSET;
			else if(!strcmp(argp[i], "-i"))
				mode = MODE_GDRV;
			else if(!strcmp(argp[i], "-h"))
				show_usage(0);
			else
				devname = argp[i];
			break;
		case 2:
			if ((mode == MODE_SSET) ||
			    (mode == MODE_GDRV)) {
				devname = argp[i];
				break;
			}
			/* fallthrough */
		default:
			if (mode != MODE_SSET)
				show_usage(1);
			if(!strcmp(argp[i], "speed")) {
				gset_changed = 1;
				i += 1;
				if(i >= argc)
					show_usage(1);
				if(!strcmp(argp[i], "10"))
					speed_wanted = SPEED_10;
				else if(!strcmp(argp[i], "100"))
					speed_wanted = SPEED_100;
				else if(!strcmp(argp[i], "1000"))
					speed_wanted = SPEED_1000;
				else
					show_usage(1);
				break;
			} else if(!strcmp(argp[i], "duplex")) {
				gset_changed = 1;
				i += 1;
				if(i >= argc)
					show_usage(1);
				if(!strcmp(argp[i], "half"))
					duplex_wanted = DUPLEX_HALF;
				else if(!strcmp(argp[i], "full"))
					duplex_wanted = DUPLEX_FULL;
				else
					show_usage(1);
				break;
			} else if(!strcmp(argp[i], "port")) {
				gset_changed = 1;
				i += 1;
				if(i >= argc)
					show_usage(1);
				if(!strcmp(argp[i], "tp"))
					port_wanted = PORT_TP;
				else if(!strcmp(argp[i], "aui"))
					port_wanted = PORT_AUI;
				else if(!strcmp(argp[i], "mii"))
					port_wanted = PORT_MII;
				else if(!strcmp(argp[i], "fibre"))
					port_wanted = PORT_FIBRE;
				else
					show_usage(1);
				break;
			} else if(!strcmp(argp[i], "autoneg")) {
				gset_changed = 1;
				i += 1;
				if(i >= argc)
					show_usage(1);
				if(!strcmp(argp[i], "on"))
					autoneg_wanted = AUTONEG_ENABLE;
				else if(!strcmp(argp[i], "off"))
					autoneg_wanted = AUTONEG_DISABLE;
				else
					show_usage(1);
				break;
			} else if(!strcmp(argp[i], "phyad")) {
				gset_changed = 1;
				i += 1;
				if(i >= argc)
					show_usage(1);
				phyad_wanted = strtol(argp[i], NULL, 10);
				if(phyad_wanted < 0)
					show_usage(1);
				break;
			} else if(!strcmp(argp[i], "xcvr")) {
				gset_changed = 1;
				i += 1;
				if(i >= argc)
					show_usage(1);
				if(!strcmp(argp[i], "internal"))
					xcvr_wanted = XCVR_INTERNAL;
				else if(!strcmp(argp[i], "external"))
					xcvr_wanted = XCVR_EXTERNAL;
				else
					show_usage(1);
				break;
			} else if(!strcmp(argp[i], "wol")) {
				gwol_changed = 1;
				i++;
				if (i >= argc)
					show_usage(1);
				if (parse_wolopts(argp[i], &wol_wanted) < 0)
					show_usage(1);
				wol_change = 1;
				break;
			} else if(!strcmp(argp[i], "sopass")) {
				gwol_changed = 1;
				i++;
				if (i >= argc)
					show_usage(1);
				if (parse_sopass(argp[i], sopass_wanted) < 0)
					show_usage(1);
				sopass_change = 1;
				break;
			}
			show_usage(1);
		}
	}
	if(devname == NULL)
		show_usage(1);
}

static void dump_supported(struct ethtool_cmd *ep)
{
	u_int32_t mask = ep->supported;
	int did1;

	fprintf(stdout, "	Supported ports: [ ");
	if(mask & SUPPORTED_TP)
		fprintf(stdout, "TP ");
	if(mask & SUPPORTED_AUI)
		fprintf(stdout, "AUI ");
	if(mask & SUPPORTED_MII)
		fprintf(stdout, "MII ");
	if(mask & SUPPORTED_FIBRE)
		fprintf(stdout, "FIBRE ");
	fprintf(stdout, "]\n");

	fprintf(stdout, "	Supported link modes:   ");
	did1 = 0;
	if(mask & SUPPORTED_10baseT_Half) {
		did1++; fprintf(stdout, "10baseT/Half ");
	}
	if(mask & SUPPORTED_10baseT_Full) {
		did1++; fprintf(stdout, "10baseT/Full ");
	}
	if(did1 && mask & (SUPPORTED_100baseT_Half|SUPPORTED_100baseT_Full)) {
		fprintf(stdout, "\n");
		fprintf(stdout, "	                        ");
	}
	if(mask & SUPPORTED_100baseT_Half) {
		did1++; fprintf(stdout, "100baseT/Half ");
	}
	if(mask & SUPPORTED_100baseT_Full) {
		did1++; fprintf(stdout, "100baseT/Full ");
	}
	if(did1 && mask & (SUPPORTED_1000baseT_Half|SUPPORTED_1000baseT_Full)) {
		fprintf(stdout, "\n");
		fprintf(stdout, "	                        ");
	}
	if(mask & SUPPORTED_1000baseT_Half) {
		did1++; fprintf(stdout, "1000baseT/Half ");
	}
	if(mask & SUPPORTED_1000baseT_Full) {
		did1++; fprintf(stdout, "1000baseT/Full ");
	}
	fprintf(stdout, "\n");

	fprintf(stdout, "	Supports auto-negotiation: ");
	if(mask & SUPPORTED_Autoneg)
		fprintf(stdout, "Yes\n");
	else
		fprintf(stdout, "No\n");
}

static int dump_ecmd(struct ethtool_cmd *ep)
{
	fprintf(stdout, "Settings for %s:\n\n", devname);
	dump_supported(ep);

	fprintf(stdout, "	Speed: ");
	switch(ep->speed) {
	case SPEED_10:
		fprintf(stdout, "10Mb/s\n");
		break;
	case SPEED_100:
		fprintf(stdout, "100Mb/s\n");
		break;
	case SPEED_1000:
		fprintf(stdout, "1000Mb/s\n");
		break;
	default:
		fprintf(stdout, "Unknown!\n");
		break;
	};

	fprintf(stdout, "	Duplex: ");
	switch(ep->duplex) {
	case DUPLEX_HALF:
		fprintf(stdout, "Half\n");
		break;
	case DUPLEX_FULL:
		fprintf(stdout, "Full\n");
		break;
	default:
		fprintf(stdout, "Unknown!\n");
		break;
	};

	fprintf(stdout, "	Port: ");
	switch(ep->port) {
	case PORT_TP:
		fprintf(stdout, "Twisted Pair\n");
		break;
	case PORT_AUI:
		fprintf(stdout, "AUI\n");
		break;
	case PORT_MII:
		fprintf(stdout, "MII\n");
		break;
	case PORT_FIBRE:
		fprintf(stdout, "FIBRE\n");
		break;
	default:
		fprintf(stdout, "Unknown!\n");
		break;
	};

	fprintf(stdout, "	PHYAD: %d\n", ep->phy_address);
	fprintf(stdout, "	Transceiver: ");
	switch(ep->transceiver) {
	case XCVR_INTERNAL:
		fprintf(stdout, "internal\n");
		break;
	case XCVR_EXTERNAL:
		fprintf(stdout, "externel\n");
		break;
	default:
		fprintf(stdout, "Unknown!\n");
		break;
	};

	fprintf(stdout, "	Auto-negotiation: %s\n",
		(ep->autoneg == AUTONEG_DISABLE) ?
		"off" : "on");
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
		fprintf(stdout, "        SecureOn Password: ");
		for (i = 0; i < SOPASS_MAX; i++) {
			fprintf(stdout, "%s%02x", delim?":":"", wol->sopass[i]);
			delim=1;
		}
		fprintf(stdout, "\n");
	}
	
	return 0;
}

static int parse_wolopts(char *optstr, int *data)
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

static int parse_sopass(char *src, unsigned char *dest)
{
	int count;
	int i;
	int buf[SOPASS_MAX];

	count = sscanf(src, "%2x:%2x:%2x:%2x:%2x:%2x",
		&buf[0], &buf[1], &buf[2], &buf[3], &buf[4], &buf[5]);
	if (count != SOPASS_MAX) {
		return -1;
	}

	for (i = 0; i < count; i++) {
		dest[i] = buf[i];
	}
	return 0;
}
			
static int doit(void)
{
	struct ifreq ifr;
	int fd;
	char buf[1024];

	/* Setup our control structures. */
	memset(buf, 0, sizeof(buf));
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, devname);
	ifr.ifr_data = (caddr_t) &buf;

	/* Open control socket. */
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(fd < 0) {
		perror("Cannot get control socket");
		return 70;
	}

	if (mode == MODE_GDRV) {
		return do_gdrv(fd, &ifr);
	} else if (mode == MODE_GSET) {
		return do_gset(fd, &ifr);
	} else if (mode == MODE_SSET) {
		return do_sset(fd, &ifr);
	}

	return 69;
}

static int do_gdrv(int fd, struct ifreq *ifr)
{
	int err;
	struct ethtool_cmd *ecmd = (struct ethtool_cmd *)ifr->ifr_data;

	ecmd->cmd = ETHTOOL_GDRVINFO;
	err = send_ioctl(fd, ifr);
	if(err < 0) {
		perror("Cannot get driver information");
		return 71;
	}
	return dump_drvinfo((struct ethtool_drvinfo *)ifr->ifr_data);
}

static int do_gset(int fd, struct ifreq *ifr)
{
	int err;
	struct ethtool_cmd *ecmd = (struct ethtool_cmd *)ifr->ifr_data;

	ecmd->cmd = ETHTOOL_GSET;
	err = send_ioctl(fd, ifr);
	if(err < 0) {
		perror("Cannot get device settings");
		return 71;
	}
	err = dump_ecmd(ecmd);
	if (err) {
		return err;
	}

	ecmd->cmd = ETHTOOL_GWOL;
	err = send_ioctl(fd, ifr);
	if(err < 0) {
		perror("Cannot get wake-on-lan settings");
		return (err == EOPNOTSUPP ? 0 : 72);
	}
	err = dump_wol((struct ethtool_wolinfo *)ifr->ifr_data);
	return 0;
}

static int do_sset(int fd, struct ifreq *ifr)
{
	int err;
	struct ethtool_cmd *ecmd = (struct ethtool_cmd *)ifr->ifr_data;
	struct ethtool_wolinfo *wol = (struct ethtool_wolinfo *)ifr->ifr_data;

	if (gset_changed) {
		ecmd->cmd = ETHTOOL_GSET;
		err = send_ioctl(fd, ifr);
		if(err < 0) {
			perror("Cannot get current device settings");
			return 71;
		}

		/* Change everything the user specified. */
		if(speed_wanted != -1)
			ecmd->speed = speed_wanted;
		if(duplex_wanted != -1)
			ecmd->duplex = duplex_wanted;
		if(port_wanted != -1)
			ecmd->port = port_wanted;
		if(autoneg_wanted != -1)
			ecmd->autoneg = autoneg_wanted;
		if(phyad_wanted != -1)
			ecmd->phy_address = phyad_wanted;
		if(xcvr_wanted != -1)
			ecmd->transceiver = xcvr_wanted;
	
		/* Try to perform the update. */
		ecmd->cmd = ETHTOOL_SSET;
		err = send_ioctl(fd, ifr);
		if(err < 0) {
			perror("Cannot update new settings");
			return 72;
		}
	}

	if (gwol_changed) {
		ecmd->cmd = ETHTOOL_GWOL;
		err = send_ioctl(fd, ifr);
		if (err < 0) {
			perror("Cannot get current wake-on-lan settings");
			return 73;
		}

		/* Change everything the user specified. */
		if (wol_change) {
			wol->wolopts = wol_wanted;
		}
		if (sopass_change) {
			int i;
			for (i = 0; i < SOPASS_MAX; i++) {
				wol->sopass[i] = sopass_wanted[i];
			}
		}

		/* Try to perform the update. */
		ecmd->cmd = ETHTOOL_SWOL;
		err = send_ioctl(fd, ifr);
		if(err < 0) {
			perror("Cannot update new wake-on-lan settings");
			return 74;
		}
	}

	return 0;
}

static int send_ioctl(int fd, struct ifreq *ifr)
{
	int err;
	if (! is_pre24_kernel) {
		err = ioctl(fd, SIOCETHTOOL, ifr);
	}
	else {
#if defined(__sparc__) && defined(PRE24_COMPAT)
		/* this part is working only on sparc HME ethernet driver */
		struct ethtool_cmd_22 old_ecmd;
		struct ethtool_cmd *ecmd = (struct ethtool_cmd *)ifr->ifr_data;
		/* fill in old pre-2.4 ethtool struct */
		old_ecmd.cmd = ecmd->cmd;
		old_ecmd.supported = ecmd->supported;
		old_ecmd.speed = ecmd->speed;
		old_ecmd.duplex = ecmd->duplex;
		old_ecmd.port = ecmd->port;
		old_ecmd.phy_address = ecmd->phy_address;
		old_ecmd.transceiver = ecmd->transceiver;
		old_ecmd.autoneg = ecmd->autoneg;
		/* issue the ioctl */
		ifr->ifr_data = (caddr_t) &old_ecmd;
		err = ioctl( fd, SIOCETHTOOL_22, ifr );
		ifr->ifr_data = (caddr_t) ecmd;
		/* copy back results from ioctl (on get cmd) */
		if (ecmd->cmd == ETHTOOL_GSET) {
			ecmd->supported = old_ecmd.supported;
			ecmd->speed = old_ecmd.speed;
			ecmd->duplex = old_ecmd.duplex;
			ecmd->port = old_ecmd.port;
			ecmd->phy_address = old_ecmd.phy_address;
			ecmd->transceiver = old_ecmd.transceiver;
			ecmd->autoneg = old_ecmd.autoneg;
		}
#else
		err = -1;
		errno = EOPNOTSUPP;
#endif
	}
	return err;
}

#ifdef PRE24_COMPAT
static int check_for_pre24_kernel()
{
	struct utsname sysinfo;
	int rmaj, rmin, rpl;
	if (uname( &sysinfo ) < 0) {
		perror( "Cannot get system informations:" );
		return 68;
	}
	if (sscanf( sysinfo.release, "%d.%d.%d", &rmaj, &rmin, &rpl ) != 3) {
		fprintf( stderr, "Cannot parse kernel revision: %s\n", sysinfo.release );
		return 68;
	}
	if (rmaj < 2 || rmaj == 2 && rmin < 4)
		is_pre24_kernel = 1;
	return 0;
}
#endif

int main(int argc, char **argp, char **envp)
{
	int err;
#ifdef PRE24_COMPAT
	err = check_for_pre24_kernel();
	if (err != 0)
		return err;
#endif
	parse_cmdline(argc, argp);
	return doit();
}

