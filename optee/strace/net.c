/*
 * Copyright (c) 1991, 1992 Paul Kranenburg <pk@cs.few.eur.nl>
 * Copyright (c) 1993 Branko Lankester <branko@hacktic.nl>
 * Copyright (c) 1993, 1994, 1995, 1996 Rick Sladkey <jrs@world.std.com>
 * Copyright (c) 1996-2000 Wichert Akkerman <wichert@cistron.nl>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "defs.h"
#include "msghdr.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <netinet/in.h>
#ifdef HAVE_NETINET_TCP_H
# include <netinet/tcp.h>
#endif
#ifdef HAVE_NETINET_UDP_H
# include <netinet/udp.h>
#endif
#ifdef HAVE_NETINET_SCTP_H
# include <netinet/sctp.h>
#endif
#include <arpa/inet.h>
#include <net/if.h>
#include <asm/types.h>
#ifdef HAVE_NETIPX_IPX_H
# include <netipx/ipx.h>
#else
# include <linux/ipx.h>
#endif

#if defined(HAVE_LINUX_IP_VS_H)
# include <linux/ip_vs.h>
#endif
#include <linux/netlink.h>
#if defined(HAVE_LINUX_NETFILTER_ARP_ARP_TABLES_H)
# include <linux/netfilter_arp/arp_tables.h>
#endif
#if defined(HAVE_LINUX_NETFILTER_BRIDGE_EBTABLES_H)
# include <linux/netfilter_bridge/ebtables.h>
#endif
#if defined(HAVE_LINUX_NETFILTER_IPV4_IP_TABLES_H)
# include <linux/netfilter_ipv4/ip_tables.h>
#endif
#if defined(HAVE_LINUX_NETFILTER_IPV6_IP6_TABLES_H)
# include <linux/netfilter_ipv6/ip6_tables.h>
#endif
#include <linux/if_packet.h>
#include <linux/icmp.h>

#include "xlat/socktypes.h"
#include "xlat/sock_type_flags.h"
#ifndef SOCK_TYPE_MASK
# define SOCK_TYPE_MASK 0xf
#endif

#include "xlat/socketlayers.h"

#include "xlat/inet_protocols.h"

#if !defined NETLINK_SOCK_DIAG && defined NETLINK_INET_DIAG
# define NETLINK_SOCK_DIAG NETLINK_INET_DIAG
#endif
#include "xlat/netlink_protocols.h"

#ifdef HAVE_BLUETOOTH_BLUETOOTH_H
# include <bluetooth/bluetooth.h>
# include "xlat/bt_protocols.h"
#endif

void
print_ifindex(unsigned int ifindex)
{
#ifdef HAVE_IF_INDEXTONAME
	char buf[IFNAMSIZ + 1];

	if (if_indextoname(ifindex, buf)) {
		tprints("if_nametoindex(");
		print_quoted_string(buf, sizeof(buf), QUOTE_0_TERMINATED);
		tprints(")");
		return;
	}
#endif
	tprintf("%u", ifindex);
}

static void
decode_sockbuf(struct tcb *tcp, int fd, long addr, long addrlen)
{

	switch (verbose(tcp) ? getfdproto(tcp, fd) : SOCK_PROTO_UNKNOWN) {
	case SOCK_PROTO_NETLINK:
		decode_netlink(tcp, addr, addrlen);
		break;
	default:
		printstr(tcp, addr, addrlen);
	}
}

/*
 * low bits of the socket type define real socket type,
 * other bits are socket type flags.
 */
static void
tprint_sock_type(unsigned int flags)
{
	const char *str = xlookup(socktypes, flags & SOCK_TYPE_MASK);

	if (str) {
		tprints(str);
		flags &= ~SOCK_TYPE_MASK;
		if (!flags)
			return;
		tprints("|");
	}
	printflags(sock_type_flags, flags, "SOCK_???");
}

SYS_FUNC(socket)
{
	printxval(addrfams, tcp->u_arg[0], "AF_???");
	tprints(", ");
	tprint_sock_type(tcp->u_arg[1]);
	tprints(", ");
	switch (tcp->u_arg[0]) {
	case AF_INET:
	case AF_INET6:
		printxval(inet_protocols, tcp->u_arg[2], "IPPROTO_???");
		break;

	case AF_NETLINK:
		printxval(netlink_protocols, tcp->u_arg[2], "NETLINK_???");
		break;

#ifdef HAVE_BLUETOOTH_BLUETOOTH_H
	case AF_BLUETOOTH:
		printxval(bt_protocols, tcp->u_arg[2], "BTPROTO_???");
		break;
#endif

	default:
		tprintf("%lu", tcp->u_arg[2]);
		break;
	}

	return RVAL_DECODED | RVAL_FD;
}

SYS_FUNC(bind)
{
	printfd(tcp, tcp->u_arg[0]);
	tprints(", ");
	decode_sockaddr(tcp, tcp->u_arg[1], tcp->u_arg[2]);
	tprintf(", %lu", tcp->u_arg[2]);

	return RVAL_DECODED;
}

SYS_FUNC(listen)
{
	printfd(tcp, tcp->u_arg[0]);
	tprints(", ");
	tprintf("%lu", tcp->u_arg[1]);

	return RVAL_DECODED;
}

static int
do_sockname(struct tcb *tcp, int flags_arg)
{
	if (entering(tcp)) {
		printfd(tcp, tcp->u_arg[0]);
		tprints(", ");
		return 0;
	}

	int len;
	if (!tcp->u_arg[2] || !verbose(tcp) || syserror(tcp) ||
	    umove(tcp, tcp->u_arg[2], &len) < 0) {
		printaddr(tcp->u_arg[1]);
		tprints(", ");
		printaddr(tcp->u_arg[2]);
	} else {
		decode_sockaddr(tcp, tcp->u_arg[1], len);
		tprintf(", [%d]", len);
	}

	if (flags_arg >= 0) {
		tprints(", ");
		printflags(sock_type_flags, tcp->u_arg[flags_arg],
			   "SOCK_???");
	}
	return 0;
}

SYS_FUNC(accept)
{
	do_sockname(tcp, -1);
	return RVAL_FD;
}

SYS_FUNC(accept4)
{
	do_sockname(tcp, 3);
	return RVAL_FD;
}

SYS_FUNC(send)
{
	printfd(tcp, tcp->u_arg[0]);
	tprints(", ");
	decode_sockbuf(tcp, tcp->u_arg[0], tcp->u_arg[1], tcp->u_arg[2]);
	tprintf(", %lu, ", tcp->u_arg[2]);
	/* flags */
	printflags(msg_flags, tcp->u_arg[3], "MSG_???");

	return RVAL_DECODED;
}

SYS_FUNC(sendto)
{
	printfd(tcp, tcp->u_arg[0]);
	tprints(", ");
	decode_sockbuf(tcp, tcp->u_arg[0], tcp->u_arg[1], tcp->u_arg[2]);
	tprintf(", %lu, ", tcp->u_arg[2]);
	/* flags */
	printflags(msg_flags, tcp->u_arg[3], "MSG_???");
	/* to address */
	tprints(", ");
	decode_sockaddr(tcp, tcp->u_arg[4], tcp->u_arg[5]);
	/* to length */
	tprintf(", %lu", tcp->u_arg[5]);

	return RVAL_DECODED;
}

SYS_FUNC(sendmsg)
{
	printfd(tcp, tcp->u_arg[0]);
	tprints(", ");
	decode_msghdr(tcp, tcp->u_arg[1], (unsigned long) -1L);
	/* flags */
	tprints(", ");
	printflags(msg_flags, tcp->u_arg[2], "MSG_???");

	return RVAL_DECODED;
}

SYS_FUNC(sendmmsg)
{
	if (entering(tcp)) {
		/* sockfd */
		printfd(tcp, tcp->u_arg[0]);
		tprints(", ");
		if (!verbose(tcp)) {
			printaddr(tcp->u_arg[1]);
			/* vlen */
			tprintf(", %u, ", (unsigned int) tcp->u_arg[2]);
			/* flags */
			printflags(msg_flags, tcp->u_arg[3], "MSG_???");
			return RVAL_DECODED;
		}
	} else {
		decode_mmsgvec(tcp, tcp->u_arg[1], tcp->u_rval, false);
		/* vlen */
		tprintf(", %u, ", (unsigned int) tcp->u_arg[2]);
		/* flags */
		printflags(msg_flags, tcp->u_arg[3], "MSG_???");
	}
	return 0;
}

SYS_FUNC(recv)
{
	if (entering(tcp)) {
		printfd(tcp, tcp->u_arg[0]);
		tprints(", ");
	} else {
		if (syserror(tcp)) {
			printaddr(tcp->u_arg[1]);
		} else {
			decode_sockbuf(tcp, tcp->u_arg[0], tcp->u_arg[1],
				     tcp->u_rval);
		}

		tprintf(", %lu, ", tcp->u_arg[2]);
		printflags(msg_flags, tcp->u_arg[3], "MSG_???");
	}
	return 0;
}

SYS_FUNC(recvfrom)
{
	int fromlen;

	if (entering(tcp)) {
		printfd(tcp, tcp->u_arg[0]);
		tprints(", ");
	} else {
		/* buf */
		if (syserror(tcp)) {
			printaddr(tcp->u_arg[1]);
		} else {
			decode_sockbuf(tcp, tcp->u_arg[0], tcp->u_arg[1],
				     tcp->u_rval);
		}
		/* len */
		tprintf(", %lu, ", tcp->u_arg[2]);
		/* flags */
		printflags(msg_flags, tcp->u_arg[3], "MSG_???");
		tprints(", ");
		if (syserror(tcp) || !tcp->u_arg[4] || !tcp->u_arg[5] ||
		    umove(tcp, tcp->u_arg[5], &fromlen) < 0) {
			/* from address, len */
			printaddr(tcp->u_arg[4]);
			tprints(", ");
			printaddr(tcp->u_arg[5]);
			return 0;
		}
		/* from address */
		decode_sockaddr(tcp, tcp->u_arg[4], fromlen);
		/* from length */
		tprintf(", [%u]", fromlen);
	}
	return 0;
}

SYS_FUNC(recvmsg)
{
	if (entering(tcp)) {
		printfd(tcp, tcp->u_arg[0]);
		tprints(", ");
	} else {
		if (syserror(tcp))
			printaddr(tcp->u_arg[1]);
		else
			decode_msghdr(tcp, tcp->u_arg[1], tcp->u_rval);
		/* flags */
		tprints(", ");
		printflags(msg_flags, tcp->u_arg[2], "MSG_???");
	}
	return 0;
}

SYS_FUNC(recvmmsg)
{
	static char str[sizeof("left") + TIMESPEC_TEXT_BUFSIZE];

	if (entering(tcp)) {
		printfd(tcp, tcp->u_arg[0]);
		tprints(", ");
		if (verbose(tcp)) {
			/* Abusing tcp->auxstr as temp storage.
			 * Will be used and cleared on syscall exit.
			 */
			tcp->auxstr = sprint_timespec(tcp, tcp->u_arg[4]);
		} else {
			printaddr(tcp->u_arg[1]);
			/* vlen */
			tprintf(", %u, ", (unsigned int) tcp->u_arg[2]);
			/* flags */
			printflags(msg_flags, tcp->u_arg[3], "MSG_???");
			tprints(", ");
			print_timespec(tcp, tcp->u_arg[4]);
		}
		return 0;
	} else {
		if (verbose(tcp)) {
			decode_mmsgvec(tcp, tcp->u_arg[1], tcp->u_rval, true);
			/* vlen */
			tprintf(", %u, ", (unsigned int) tcp->u_arg[2]);
			/* flags */
			printflags(msg_flags, tcp->u_arg[3], "MSG_???");
			tprints(", ");
			/* timeout on entrance */
			tprints(tcp->auxstr);
			tcp->auxstr = NULL;
		}
		if (syserror(tcp))
			return 0;
		if (tcp->u_rval == 0) {
			tcp->auxstr = "Timeout";
			return RVAL_STR;
		}
		if (!verbose(tcp))
			return 0;
		/* timeout on exit */
		snprintf(str, sizeof(str), "left %s",
			 sprint_timespec(tcp, tcp->u_arg[4]));
		tcp->auxstr = str;
		return RVAL_STR;
	}
}

#include "xlat/shutdown_modes.h"

SYS_FUNC(shutdown)
{
	printfd(tcp, tcp->u_arg[0]);
	tprints(", ");
	printxval(shutdown_modes, tcp->u_arg[1], "SHUT_???");

	return RVAL_DECODED;
}

SYS_FUNC(getsockname)
{
	return do_sockname(tcp, -1);
}

static void
printpair_fd(struct tcb *tcp, const int i0, const int i1)
{
	tprints("[");
	printfd(tcp, i0);
	tprints(", ");
	printfd(tcp, i1);
	tprints("]");
}

static void
decode_pair_fd(struct tcb *tcp, const long addr)
{
	int pair[2];

	if (umove_or_printaddr(tcp, addr, &pair))
		return;

	printpair_fd(tcp, pair[0], pair[1]);
}

static int
do_pipe(struct tcb *tcp, int flags_arg)
{
	if (exiting(tcp)) {
		decode_pair_fd(tcp, tcp->u_arg[0]);
		if (flags_arg >= 0) {
			tprints(", ");
			printflags(open_mode_flags, tcp->u_arg[flags_arg], "O_???");
		}
	}
	return 0;
}

SYS_FUNC(pipe)
{
#ifdef HAVE_GETRVAL2
	if (exiting(tcp) && !syserror(tcp))
		printpair_fd(tcp, tcp->u_rval, getrval2(tcp));
	return 0;
#else
	return do_pipe(tcp, -1);
#endif
}

SYS_FUNC(pipe2)
{
	return do_pipe(tcp, 1);
}

SYS_FUNC(socketpair)
{
	if (entering(tcp)) {
		printxval(addrfams, tcp->u_arg[0], "AF_???");
		tprints(", ");
		tprint_sock_type(tcp->u_arg[1]);
		tprintf(", %lu", tcp->u_arg[2]);
	} else {
		tprints(", ");
		decode_pair_fd(tcp, tcp->u_arg[3]);
	}
	return 0;
}

#include "xlat/sockoptions.h"
#include "xlat/sockipoptions.h"
#include "xlat/getsockipoptions.h"
#include "xlat/setsockipoptions.h"
#include "xlat/sockipv6options.h"
#include "xlat/getsockipv6options.h"
#include "xlat/setsockipv6options.h"
#include "xlat/sockipxoptions.h"
#include "xlat/sockrawoptions.h"
#include "xlat/sockpacketoptions.h"
#include "xlat/socksctpoptions.h"
#include "xlat/socktcpoptions.h"

static void
print_sockopt_fd_level_name(struct tcb *tcp, int fd, unsigned int level,
			    unsigned int name, bool is_getsockopt)
{
	printfd(tcp, fd);
	tprints(", ");
	printxval(socketlayers, level, "SOL_??");
	tprints(", ");

	switch (level) {
	case SOL_SOCKET:
		printxval(sockoptions, name, "SO_???");
		break;
	case SOL_IP:
		printxvals(name, "IP_???", sockipoptions,
			is_getsockopt ? getsockipoptions : setsockipoptions, NULL);
		break;
	case SOL_IPV6:
		printxvals(name, "IPV6_???", sockipv6options,
			is_getsockopt ? getsockipv6options : setsockipv6options, NULL);
		break;
	case SOL_IPX:
		printxval(sockipxoptions, name, "IPX_???");
		break;
	case SOL_PACKET:
		printxval(sockpacketoptions, name, "PACKET_???");
		break;
	case SOL_TCP:
		printxval(socktcpoptions, name, "TCP_???");
		break;
	case SOL_SCTP:
		printxval(socksctpoptions, name, "SCTP_???");
		break;
	case SOL_RAW:
		printxval(sockrawoptions, name, "RAW_???");
		break;

		/* Other SOL_* protocol levels still need work. */

	default:
		tprintf("%u", name);
	}

	tprints(", ");
}

static void
print_linger(struct tcb *tcp, long addr, int len)
{
	struct linger linger;

	if (len != sizeof(linger) ||
	    umove(tcp, addr, &linger) < 0) {
		printaddr(addr);
		return;
	}

	tprintf("{onoff=%d, linger=%d}",
		linger.l_onoff,
		linger.l_linger);
}

#ifdef SO_PEERCRED
static void
print_ucred(struct tcb *tcp, long addr, int len)
{
	struct ucred uc;

	if (len != sizeof(uc) ||
	    umove(tcp, addr, &uc) < 0) {
		printaddr(addr);
	} else {
		tprintf("{pid=%u, uid=%u, gid=%u}",
			(unsigned) uc.pid,
			(unsigned) uc.uid,
			(unsigned) uc.gid);
	}
}
#endif /* SO_PEERCRED */

#ifdef PACKET_STATISTICS
static void
print_tpacket_stats(struct tcb *tcp, long addr, int len)
{
	struct tpacket_stats stats;

	if (len != sizeof(stats) ||
	    umove(tcp, addr, &stats) < 0) {
		printaddr(addr);
	} else {
		tprintf("{packets=%u, drops=%u}",
			stats.tp_packets,
			stats.tp_drops);
	}
}
#endif /* PACKET_STATISTICS */

#include "xlat/icmpfilterflags.h"

static void
print_icmp_filter(struct tcb *tcp, const long addr, int len)
{
	struct icmp_filter filter = {};

	if (len > (int) sizeof(filter))
		len = sizeof(filter);
	else if (len <= 0) {
		printaddr(addr);
		return;
	}

	if (umoven_or_printaddr(tcp, addr, len, &filter))
		return;

	tprints("~(");
	printflags(icmpfilterflags, ~filter.data, "ICMP_???");
	tprints(")");
}

static void
print_getsockopt(struct tcb *tcp, unsigned int level, unsigned int name,
		 long addr, int len)
{
	if (addr && verbose(tcp))
	switch (level) {
	case SOL_SOCKET:
		switch (name) {
		case SO_LINGER:
			print_linger(tcp, addr, len);
			goto done;
#ifdef SO_PEERCRED
		case SO_PEERCRED:
			print_ucred(tcp, addr, len);
			goto done;
#endif
		}
		break;

	case SOL_PACKET:
		switch (name) {
#ifdef PACKET_STATISTICS
		case PACKET_STATISTICS:
			print_tpacket_stats(tcp, addr, len);
			goto done;
#endif
		}
		break;

	case SOL_RAW:
		switch (name) {
		case ICMP_FILTER:
			print_icmp_filter(tcp, addr, len);
			goto done;
		}
		break;
	}

	/* default arg printing */

	if (verbose(tcp)) {
		if (len == sizeof(int)) {
			printnum_int(tcp, addr, "%d");
		} else {
			printstr(tcp, addr, len);
		}
	} else {
		printaddr(addr);
	}
done:
	tprintf(", [%d]", len);
}

SYS_FUNC(getsockopt)
{
	if (entering(tcp)) {
		print_sockopt_fd_level_name(tcp, tcp->u_arg[0],
					    tcp->u_arg[1], tcp->u_arg[2], true);
	} else {
		int len;

		if (syserror(tcp) || umove(tcp, tcp->u_arg[4], &len) < 0) {
			printaddr(tcp->u_arg[3]);
			tprints(", ");
			printaddr(tcp->u_arg[4]);
		} else {
			print_getsockopt(tcp, tcp->u_arg[1], tcp->u_arg[2],
					 tcp->u_arg[3], len);
		}
	}
	return 0;
}

#ifdef IP_ADD_MEMBERSHIP
static void
print_mreq(struct tcb *tcp, long addr, unsigned int len)
{
	struct ip_mreq mreq;

	if (len < sizeof(mreq)) {
		printstr(tcp, addr, len);
		return;
	}
	if (umove_or_printaddr(tcp, addr, &mreq))
		return;

	tprints("{imr_multiaddr=inet_addr(");
	print_quoted_string(inet_ntoa(mreq.imr_multiaddr),
			    16, QUOTE_0_TERMINATED);
	tprints("), imr_interface=inet_addr(");
	print_quoted_string(inet_ntoa(mreq.imr_interface),
			    16, QUOTE_0_TERMINATED);
	tprints(")}");
}
#endif /* IP_ADD_MEMBERSHIP */

#ifdef IPV6_ADD_MEMBERSHIP
static void
print_mreq6(struct tcb *tcp, long addr, unsigned int len)
{
	struct ipv6_mreq mreq;

	if (len < sizeof(mreq))
		goto fail;

	if (umove_or_printaddr(tcp, addr, &mreq))
		return;

	const struct in6_addr *in6 = &mreq.ipv6mr_multiaddr;
	char address[INET6_ADDRSTRLEN];

	if (!inet_ntop(AF_INET6, in6, address, sizeof(address)))
		goto fail;

	tprints("{ipv6mr_multiaddr=inet_pton(");
	print_quoted_string(address, sizeof(address), QUOTE_0_TERMINATED);
	tprints("), ipv6mr_interface=");
	print_ifindex(mreq.ipv6mr_interface);
	tprints("}");
	return;

fail:
	printstr(tcp, addr, len);
}
#endif /* IPV6_ADD_MEMBERSHIP */

#ifdef MCAST_JOIN_GROUP
static void
print_group_req(struct tcb *tcp, long addr, int len)
{
	struct group_req greq;

	if (len != sizeof(greq) ||
	    umove(tcp, addr, &greq) < 0) {
		printaddr(addr);
		return;
	}

	tprintf("{gr_interface=%u, gr_group=", greq.gr_interface);
	print_sockaddr(tcp, &greq.gr_group, sizeof(greq.gr_group));
	tprintf("}");

}
#endif /* MCAST_JOIN_GROUP */

#ifdef PACKET_RX_RING
static void
print_tpacket_req(struct tcb *tcp, long addr, int len)
{
	struct tpacket_req req;

	if (len != sizeof(req) ||
	    umove(tcp, addr, &req) < 0) {
		printaddr(addr);
	} else {
		tprintf("{block_size=%u, block_nr=%u, "
			"frame_size=%u, frame_nr=%u}",
			req.tp_block_size,
			req.tp_block_nr,
			req.tp_frame_size,
			req.tp_frame_nr);
	}
}
#endif /* PACKET_RX_RING */

#ifdef PACKET_ADD_MEMBERSHIP
# include "xlat/packet_mreq_type.h"

static void
print_packet_mreq(struct tcb *tcp, long addr, int len)
{
	struct packet_mreq mreq;

	if (len != sizeof(mreq) ||
	    umove(tcp, addr, &mreq) < 0) {
		printaddr(addr);
	} else {
		unsigned int i;

		tprintf("{mr_ifindex=%u, mr_type=", mreq.mr_ifindex);
		printxval(packet_mreq_type, mreq.mr_type, "PACKET_MR_???");
		tprintf(", mr_alen=%u, mr_address=", mreq.mr_alen);
		if (mreq.mr_alen > ARRAY_SIZE(mreq.mr_address))
			mreq.mr_alen = ARRAY_SIZE(mreq.mr_address);
		for (i = 0; i < mreq.mr_alen; ++i)
			tprintf("%02x", mreq.mr_address[i]);
		tprints("}");
	}
}
#endif /* PACKET_ADD_MEMBERSHIP */

static void
print_setsockopt(struct tcb *tcp, unsigned int level, unsigned int name,
		 long addr, int len)
{
	if (addr && verbose(tcp))
	switch (level) {
	case SOL_SOCKET:
		switch (name) {
		case SO_LINGER:
			print_linger(tcp, addr, len);
			goto done;
		}
		break;

	case SOL_IP:
		switch (name) {
#ifdef IP_ADD_MEMBERSHIP
		case IP_ADD_MEMBERSHIP:
		case IP_DROP_MEMBERSHIP:
			print_mreq(tcp, addr, len);
			goto done;
#endif /* IP_ADD_MEMBERSHIP */
#ifdef MCAST_JOIN_GROUP
		case MCAST_JOIN_GROUP:
		case MCAST_LEAVE_GROUP:
			print_group_req(tcp, addr, len);
			goto done;
#endif /* MCAST_JOIN_GROUP */
		}
		break;

	case SOL_IPV6:
		switch (name) {
#ifdef IPV6_ADD_MEMBERSHIP
		case IPV6_ADD_MEMBERSHIP:
		case IPV6_DROP_MEMBERSHIP:
# ifdef IPV6_JOIN_ANYCAST
		case IPV6_JOIN_ANYCAST:
# endif
# ifdef IPV6_LEAVE_ANYCAST
		case IPV6_LEAVE_ANYCAST:
# endif
			print_mreq6(tcp, addr, len);
			goto done;
#endif /* IPV6_ADD_MEMBERSHIP */
		}
		break;

	case SOL_PACKET:
		switch (name) {
#ifdef PACKET_RX_RING
		case PACKET_RX_RING:
# ifdef PACKET_TX_RING
		case PACKET_TX_RING:
# endif
			print_tpacket_req(tcp, addr, len);
			goto done;
#endif /* PACKET_RX_RING */
#ifdef PACKET_ADD_MEMBERSHIP
		case PACKET_ADD_MEMBERSHIP:
		case PACKET_DROP_MEMBERSHIP:
			print_packet_mreq(tcp, addr, len);
			goto done;
#endif /* PACKET_ADD_MEMBERSHIP */
		}
		break;

	case SOL_RAW:
		switch (name) {
		case ICMP_FILTER:
			print_icmp_filter(tcp, addr, len);
			goto done;
		}
		break;
	}

	/* default arg printing */

	if (verbose(tcp)) {
		if (len == sizeof(int)) {
			printnum_int(tcp, addr, "%d");
		} else {
			printstr(tcp, addr, len);
		}
	} else {
		printaddr(addr);
	}
done:
	tprintf(", %d", len);
}

SYS_FUNC(setsockopt)
{
	print_sockopt_fd_level_name(tcp, tcp->u_arg[0],
				    tcp->u_arg[1], tcp->u_arg[2], false);
	print_setsockopt(tcp, tcp->u_arg[1], tcp->u_arg[2],
			 tcp->u_arg[3], tcp->u_arg[4]);

	return RVAL_DECODED;
}
