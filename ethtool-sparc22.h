/*
 * This is an excerpt from kernel-source-2.2.19 include/asm-sparc/ethtool.h.
 */

/* $Id: ethtool.h,v 1.1.2.1 2000/01/31 05:02:42 davem Exp $
 * ethtool.h: Defines for SparcLinux ethtool.
 *
 * Copyright (C) 1998 David S. Miller (davem@redhat.com)
 */

#ifndef _SPARC_ETHTOOL_H
#define _SPARC_ETHTOOL_H

/* pre-2.4 value of SIOCETHTOOL */
#define SIOCETHTOOL_22 (SIOCDEVPRIVATE + 0x0f)

/* This should work for both 32 and 64 bit userland. */
struct ethtool_cmd_22 {
	u32	cmd;
	u32	supported;
	u16	speed;
	u8	duplex;
	u8	port;
	u8	phy_address;
	u8	transceiver;
	u8	autoneg;
};

#endif /* _SPARC_ETHTOOL_H */
