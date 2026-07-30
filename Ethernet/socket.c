//*****************************************************************************
//
//! \file socket.c
//! \brief SOCKET APIs Implements file.
//! \details SOCKET APIs like as Berkeley Socket APIs.
//! \version 1.0.3
//! \date 2013/10/21
//! \par  Revision history
//!       <2015/02/05> Notice
//!        The version history is not updated after this point.
//!        Download the latest version directly from GitHub. Please visit the our GitHub repository for ioLibrary.
//!        >> https://github.com/Wiznet/ioLibrary_Driver
//!       <2014/05/01> V1.0.3. Refer to M20140501
//!         1. Implicit type casting -> Explicit type casting.
//!         2. replace 0x01 with PACK_REMAINED in recvfrom()
//!         3. Validation a destination ip in connect() & sendto():
//!            It occurs a fatal error on converting unint32 address if uint8* addr parameter is not aligned by 4byte address.
//!            Copy 4 byte addr value into temporary uint32 variable and then compares it.
//!       <2013/12/20> V1.0.2 Refer to M20131220
//!                    Remove Warning.
//!       <2013/11/04> V1.0.1 2nd Release. Refer to "20131104".
//!                    In sendto(), Add to clear timeout interrupt status (Sn_IR_TIMEOUT)
//!       <2013/10/21> 1st Release
//! \author MidnightCow
//! \copyright
//!
//! Copyright (c)  2013, WIZnet Co., LTD.
//! All rights reserved.
//!
//! Redistribution and use in source and binary forms, with or without
//! modification, are permitted provided that the following conditions
//! are met:
//!
//!     * Redistributions of source code must retain the above copyright
//! notice, this list of conditions and the following disclaimer.
//!     * Redistributions in binary form must reproduce the above copyright
//! notice, this list of conditions and the following disclaimer in the
//! documentation and/or other materials provided with the distribution.
//!     * Neither the name of the <ORGANIZATION> nor the names of its
//! contributors may be used to endorse or promote products derived
//! from this software without specific prior written permission.
//!
//! THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//! AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//! IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//! ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
//! LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//! CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
//! SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
//! INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
//! CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
//! ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
//! THE POSSIBILITY OF SUCH DAMAGE.
//
//*****************************************************************************
#include "socket.h"

//M20150401 : Typing Error
//#define SOCK_ANY_PORT_NUM  0xC000;
#define SOCK_ANY_PORT_NUM  0xC000

static uint16_t sock_any_port = SOCK_ANY_PORT_NUM;
static uint8_t sock_io_mode[_WIZCHIP_SOCK_NUM_];
static uint8_t sock_is_sending[_WIZCHIP_SOCK_NUM_];
typedef enum {
    SOCK_HEALTHY = 0,
    SOCK_FAULTED
} sock_health_t;
static sock_health_t sock_health[_WIZCHIP_SOCK_NUM_];

static uint16_t sock_remained_size[_WIZCHIP_SOCK_NUM_] = {0};

//M20150601 : For extern decleation
//static uint8_t  sock_pack_info[_WIZCHIP_SOCK_NUM_] = {0,};
uint8_t  sock_pack_info[_WIZCHIP_SOCK_NUM_] = {0,};
//

#if _WIZCHIP_ == 5200
static uint16_t sock_next_rd[_WIZCHIP_SOCK_NUM_] = {0,};
#endif

//A20150601 : For integrating with W5300
#if _WIZCHIP_ == 5300
uint8_t sock_remained_byte[_WIZCHIP_SOCK_NUM_] = {0,}; // set by wiz_recv_data()
#endif

static uint8_t sock_mode[_WIZCHIP_SOCK_NUM_] = {0,}; // Sn_MR cache (AUD-064)

/* CPU-owned live transmit state. Never read from the WIZCHIP. */
static sock_tx_state_t sock_tx_state[_WIZCHIP_SOCK_NUM_];
static uint16_t sock_tx_pending_len[_WIZCHIP_SOCK_NUM_];

static void sock_state_reset(uint8_t sn) {
    sock_io_mode[sn] = 0;
    sock_is_sending[sn] = 0;
    sock_health[sn] = SOCK_HEALTHY;
    sock_tx_state[sn] = SOCK_TX_IDLE;
    sock_tx_pending_len[sn] = 0U;
    sock_remained_size[sn] = 0;
    sock_pack_info[sn] = PACK_NONE;
    sock_mode[sn] = 0;
#if _WIZCHIP_ == 5200
    sock_next_rd[sn] = 0;
#endif
#if _WIZCHIP_ == 5300
    sock_remained_byte[sn] = 0;
#endif
}

void wizchip_socket_state_reset_one(uint8_t sn) {
    if (sn < _WIZCHIP_SOCK_NUM_) {
        sock_state_reset(sn);
    }
}

void wizchip_socket_state_reset(void) {
    uint8_t sn;

    for (sn = 0; sn < _WIZCHIP_SOCK_NUM_; sn++) {
        sock_state_reset(sn);
    }
}

static void wait_poll_init(wizchip_deadline_t *poll, uint64_t deadline) {
    poll->started_us = wizchip_time_now();
    poll->deadline_us = deadline;
    poll->timeout_us = UINT64_MAX;
    poll->polls = 0U;
}

static int8_t wait_poll_expired(wizchip_deadline_t *poll,
                                uint64_t deadline) {
    if (wizchip_deadline_poll(poll) == SOCKERR_DEADLINE ||
        wizchip_deadline_expired(deadline)) {
        return SOCKERR_DEADLINE;
    }
    return SOCK_OK;
}

/* Open one local operation deadline for the invocation that is about to issue
 * a command.  A missing time source or a zero budget is a local configuration
 * fault: fail bounded here, before any side effect, rather than falling back
 * to poll counting which is not an elapsed-time guarantee. */
static int8_t sock_deadline_begin(uint64_t *deadline, uint32_t budget_us) {
    if (wizchip_deadline_config_valid() == 0) {
        return SOCKERR_TIMEOUT;
    }
    *deadline = wizchip_deadline_abs(budget_us);
    return SOCK_OK;
}

static int8_t wait_cr_accepted(uint8_t sn, uint64_t deadline) {
    wizchip_deadline_t poll;

    wait_poll_init(&poll, deadline);
    while (getSn_CR(sn) != 0U) {
        if (wait_poll_expired(&poll, deadline) == SOCKERR_DEADLINE) {
            sock_health[sn] = SOCK_FAULTED;
            return SOCKERR_TIMEOUT;
        }
    }
    return SOCK_OK;
}

/* Immediate status transition caused by a command, bounded by the same
 * invocation deadline that bounded the command itself. */
static int8_t wait_sr_reached(uint8_t sn, uint8_t wanted, uint64_t deadline) {
    wizchip_deadline_t poll;

    wait_poll_init(&poll, deadline);
    while (getSn_SR(sn) != wanted) {
        if (wait_poll_expired(&poll, deadline) == SOCKERR_DEADLINE) {
            sock_health[sn] = SOCK_FAULTED;
            return SOCKERR_TIMEOUT;
        }
    }
    return SOCK_OK;
}

static int8_t wait_datagram_cr_accepted(uint8_t sn, uint32_t timeout_us,
                                        uint8_t nonblocking) {
    uint64_t deadline;
    wizchip_deadline_t poll;

    if (getSn_CR(sn) == 0U) {
        return SOCK_OK;
    }
    if ((getSn_IR(sn) & Sn_IR_TIMEOUT) != 0U) {
        setSn_IR(sn, Sn_IR_TIMEOUT);
        return SOCKERR_TIMEOUT;
    }
    if (nonblocking != 0U) {
        return SOCK_BUSY;
    }

    deadline = wizchip_deadline_abs(timeout_us);
    wait_poll_init(&poll, deadline);
    while (getSn_CR(sn) != 0U) {
        if ((getSn_IR(sn) & Sn_IR_TIMEOUT) != 0U) {
            setSn_IR(sn, Sn_IR_TIMEOUT);
            return SOCKERR_TIMEOUT;
        }
        if (wait_poll_expired(&poll, deadline) == SOCKERR_DEADLINE) {
            sock_health[sn] = SOCK_FAULTED;
            return SOCKERR_TIMEOUT;
        }
    }
    return SOCK_OK;
}

static uint8_t socket_open_status(uint8_t protocol) {
    switch (protocol & 0x0FU) {
    case Sn_MR_TCP:
        return SOCK_INIT;
    case Sn_MR_UDP:
        return SOCK_UDP;
    case Sn_MR_IPRAW:
        return SOCK_IPRAW;
    case Sn_MR_MACRAW:
        return SOCK_MACRAW;
    default:
        return SOCK_CLOSED;
    }
}

static int8_t read_sn_tx_fsr(uint8_t sn, uint16_t *value) {
#if _WIZCHIP_ == 5500
    int8_t result = getSn_TX_FSR_stable(sn, value);

    return result == SOCK_OK ? 0 : result;
#else
    *value = (uint16_t)getSn_TX_FSR(sn);
    return 0;
#endif
}

static int8_t read_sn_rx_rsr(uint8_t sn, uint16_t *value) {
#if _WIZCHIP_ == 5500
    int8_t result = getSn_RX_RSR_stable(sn, value);

    return result == SOCK_OK ? 0 : result;
#else
    *value = (uint16_t)getSn_RX_RSR(sn);
    return 0;
#endif
}

static uint8_t sock_mode_cached_or_read(uint8_t sn) {
    uint8_t mode = sock_mode[sn];

    if (mode == 0U) {
        mode = (uint8_t)getSn_MR(sn);
    }
    return mode;
}

static int8_t close_internal(uint8_t sn, uint8_t under_lock,
                             uint64_t inherited_deadline);
static int8_t connect_IO_6(uint8_t sn, uint8_t *addr, uint16_t port,
                           uint8_t addrlen);
static int32_t sendto_IO_6(uint8_t sn, uint8_t *buf, uint16_t len,
                           uint8_t *addr, uint16_t port, uint8_t addrlen);
static int32_t recvfrom_IO_6(uint8_t sn, uint8_t *buf, uint16_t len,
                             uint8_t *addr, uint16_t *port,
                             uint8_t *addrlen);


#define CHECK_SOCKNUM()   \
   do{                    \
      if(sn >= _WIZCHIP_SOCK_NUM_) return SOCKERR_SOCKNUM;   \
   }while(0);             \

#define CHECK_SOCKMODE(mode)  \
   do{                     \
      if((sock_mode[sn] & 0x0F) != mode) return SOCKERR_SOCKMODE;  \
   }while(0);              \

#define CHECK_TCPMODE()                                           \
   do{                                                            \
      if((sock_mode[sn] & 0x03) != 0x01) return SOCKERR_SOCKMODE;  \
   }while(0);

#define CHECK_SOCKINIT()   \
   do{                     \
      if((getSn_SR(sn) != SOCK_INIT)) return SOCKERR_SOCKINIT; \
   }while(0);              \

//teddy 240122
#if _WIZCHIP_ == W6100 || _WIZCHIP_ == W6300
#define CHECK_TCPMODE()                                           \
   do{                                                            \
      if((sock_mode[sn] & 0x03) != 0x01) return SOCKERR_SOCKMODE;  \
   }while(0);

#define CHECK_UDPMODE()                                           \
   do{                                                            \
      if((sock_mode[sn] & 0x03) != 0x02) return SOCKERR_SOCKMODE;  \
   }while(0);

#define CHECK_IPMODE()                                            \
   do{                                                            \
      if((sock_mode[sn] & 0x07) != 0x03) return SOCKERR_SOCKMODE;  \
   }while(0);

#define CHECK_DGRAMMODE()                                         \
   do{                                                            \
      if(sock_mode[sn] == Sn_MR_CLOSED) return SOCKERR_SOCKMODE;   \
      if((sock_mode[sn] & 0x03) == 0x01) return SOCKERR_SOCKMODE;  \
   }while(0);

#define CHECK_IPZERO(addr, addrlen)                                  \
   do{                                                               \
      uint16_t ipzero= 0;                                            \
      for(uint8_t i=0; i<addrlen; i++)  ipzero += (uint16_t)addr[i]; \
      if (ipzero == 0) return SOCKERR_IPINVALID;                     \
   }while(0);


#endif


#if (_WIZCHIP_ > W5500)
#define IPV6_AVAILABLE
#endif

#if 1


#define Sn_MR_TCP4           (Sn_MR_TCP)   ///< Refer to @ref Sn_MR_TCP.
#define Sn_MR_UDP4           (Sn_MR_UDP)   ///< Refer to @ref Sn_MR_UDP
#define Sn_MR_IPRAW4         (Sn_MR_IPRAW)   ///< Refer to @ref Sn_MR_IPRAW.   
#define Sn_MR_TCP6           (0x09)
#define Sn_MR_UDP6           (0x0A) //0x1010
#define Sn_MR_IPRAW6         (0x0B) //0x1011
#define Sn_MR_TCPD           (0x0D)
#define Sn_MR_UDPD           (0x0E)



#endif





int8_t socket(uint8_t sn, uint8_t protocol, uint16_t port, uint8_t flag) {
    int8_t ret;
    uint8_t expected_status;
    uint8_t hardware_flag = (uint8_t)(flag & (uint8_t)~SF_IO_NONBLOCK);
    uint8_t status_before_lock;
    uint64_t deadline_abs;
    wizchip_timeout_config_t timeout_config;

#ifdef IPV6_AVAILABLE
    uint8_t taddr[16];
    uint16_t ipzero;
    uint8_t i;
#endif
    CHECK_SOCKNUM();
    switch (protocol & 0x0F) {
#ifdef IPV6_AVAILABLE
    case Sn_MR_TCP4 :
    case Sn_MR_TCP6 :
    case Sn_MR_TCPD :
        break;
#else
    case Sn_MR_TCP :
        break;
#endif
    case Sn_MR_UDP :
#ifdef IPV6_AVAILABLE
    case Sn_MR_UDP6 :
    case Sn_MR_UDPD :
#endif
    case Sn_MR_MACRAW :
    case Sn_MR_IPRAW4 :
#ifdef IPV6_AVAILABLE
    case Sn_MR_IPRAW6 :
#endif
        break;
#if ( _WIZCHIP_ < 5200 )
    case Sn_MR_PPPoE :
        break;
#endif
    default :
        return SOCKERR_SOCKMODE;
    }
#ifndef IPV6_AVAILABLE
    if (sn != 0 && (protocol & 0x0F) == Sn_MR_MACRAW) {
        return SOCKERR_SOCKMODE;
    }
#endif
#if _WIZCHIP_ != 5500
    //M20150601 : For SF_TCP_ALIGN & W5300
    //if((flag & 0x06) != 0) WIZCHIP_SOCK_UNLOCK(sn); return SOCKERR_SOCKFLAG;
    if ((flag & 0x04) != 0) {
        return SOCKERR_SOCKFLAG;
    }
#endif
#if _WIZCHIP_ == 5200
    if (flag & 0x10) {
        return SOCKERR_SOCKFLAG;
    }
#endif

#if _WIZCHIP_ == 5500
    {
        uint8_t valid_flag_mask;

        switch (protocol & 0x0FU) {
        case Sn_MR_TCP:
            valid_flag_mask = SF_TCP_VALID_MASK;
            break;
        case Sn_MR_UDP:
            valid_flag_mask = SF_UDP_VALID_MASK;
            break;
        case Sn_MR_MACRAW:
            valid_flag_mask = SF_MACRAW_VALID_MASK;
            break;
        case Sn_MR_IPRAW:
            valid_flag_mask = SF_IPRAW_VALID_MASK;
            break;
        default:
            return SOCKERR_SOCKMODE;
        }
        if ((flag & (uint8_t)~valid_flag_mask) != 0U) {
            return SOCKERR_SOCKFLAG;
        }
        if ((protocol & 0x0FU) == Sn_MR_UDP &&
            (flag & (SF_IGMP_VER2 | SF_UNI_BLOCK)) != 0U &&
            (flag & SF_MULTI_ENABLE) == 0U) {
            return SOCKERR_SOCKFLAG;
        }
    }
#else
    if (flag != 0) {
        switch (protocol) {

#ifdef IPV6_AVAILABLE
        case Sn_MR_MACRAW:
            if ((flag & (SF_DHA_MANUAL | SF_FORCE_ARP)) != 0) {
                return SOCKERR_SOCKFLAG;
            }
            break;
        case Sn_MR_TCP4:
        case Sn_MR_TCP6:
        case Sn_MR_TCPD:
            if ((flag & (SF_MULTI_ENABLE | SF_UNI_BLOCK)) != 0) {
                return SOCKERR_SOCKFLAG;
            }
            break;
        case Sn_MR_IPRAW4:
        case Sn_MR_IPRAW6:
            if (flag != 0) {
                return SOCKERR_SOCKFLAG;
            }
            break;
#else
        case Sn_MR_TCP:
            //M20150601 :  For SF_TCP_ALIGN & W5300
#if _WIZCHIP_ == 5300
            if ((flag & (SF_TCP_NODELAY | SF_IO_NONBLOCK | SF_TCP_ALIGN)) == 0) {
                return SOCKERR_SOCKFLAG;
            }
#else
            if ((flag & (SF_TCP_NODELAY | SF_IO_NONBLOCK)) == 0) {
                return SOCKERR_SOCKFLAG;
            }
#endif

            break;
        case Sn_MR_UDP:
            if (flag & SF_IGMP_VER2) {
                if ((flag & SF_MULTI_ENABLE) == 0) {
                    return SOCKERR_SOCKFLAG;
                }
            }
#if _WIZCHIP_ == 5500
            if (flag & SF_UNI_BLOCK) {
                if ((flag & SF_MULTI_ENABLE) == 0) {
                    return SOCKERR_SOCKFLAG;
                }
            }
#endif
            break;

#endif

        default:
            break;
        }
    }
#endif
    if (wizchip_get_state() != WIZCHIP_STATE_READY) {
        wizchip_set_last_error(SOCKERR_NOTREADY);
        return SOCKERR_NOTREADY;
    }
    status_before_lock = getSn_SR(sn);

    if (!port) {
        WIZCHIP_GLOBAL_LOCK();
        port = sock_any_port++;
        if (sock_any_port == 0xFFF0) {
            sock_any_port = SOCK_ANY_PORT_NUM;
        }
        WIZCHIP_GLOBAL_UNLOCK();
    }

    ret = (int8_t)sn;
    WIZCHIP_SOCK_LOCK(sn);
    if (sock_health[sn] == SOCK_FAULTED) {
        ret = SOCKERR_NOTREADY;
        goto socket_done;
    }

    /* One deadline for this whole invocation, opened before the first side
     * effect and shared with the internal close below. */
    (void)wizchip_get_timeout_config(&timeout_config);
    ret = sock_deadline_begin(&deadline_abs, timeout_config.command_timeout_us);
    if (ret != SOCK_OK) {
        goto socket_done;
    }

    if (getSn_SR(sn) != SOCK_CLOSED) {
        if (status_before_lock == SOCK_CLOSED) {
            ret = SOCKERR_SOCKSTATUS;
            goto socket_done;
        }
        ret = close_internal(sn, 1U, deadline_abs);
        if (ret != SOCK_OK) {
            goto socket_done;
        }
        ret = (int8_t)sn;
    }

#ifdef IPV6_AVAILABLE
    if ((protocol & 0x0F) == Sn_MR_TCP4 ||
        (protocol & 0x0F) == Sn_MR_TCPD) {
        getSIPR(taddr);
        ipzero = 0U;
        for (i = 0U; i < 4U; i++) {
            ipzero = (uint16_t)(ipzero + taddr[i]);
        }
        if (ipzero == 0U) {
            ret = SOCKERR_SOCKINIT;
            goto socket_done;
        }
    }
    if ((protocol & 0x0F) == Sn_MR_TCP6 ||
        (protocol & 0x0F) == Sn_MR_TCPD) {
        getLLAR(taddr);
        ipzero = 0U;
        for (i = 0U; i < 16U; i++) {
            ipzero = (uint16_t)(ipzero + taddr[i]);
        }
        if (ipzero == 0U) {
            ret = SOCKERR_SOCKINIT;
            goto socket_done;
        }
    }
#else
    if ((protocol & 0x0F) == Sn_MR_TCP) {
        uint32_t taddr;

        getSIPR((uint8_t*)&taddr);
        if (taddr == 0U) {
            ret = SOCKERR_SOCKINIT;
            goto socket_done;
        }
    }
#endif

    //M20150601
#if _WIZCHIP_ == 5300
    setSn_MR(sn, ((uint16_t)(protocol | (flag & 0xF0))) | (((uint16_t)(flag & 0x02)) << 7));
#else
    setSn_MR(sn, (protocol | (hardware_flag & 0xF0)));
#endif
#ifdef IPV6_AVAILABLE
    setSn_MR2(sn, flag & 0x03);
#endif
    setSn_PORTR(sn, port);
    setSn_CR(sn, Sn_CR_OPEN);
    ret = wait_cr_accepted(sn, deadline_abs);
    if (ret != SOCK_OK) {
        goto socket_done;
    }

    expected_status = socket_open_status(protocol);
    ret = wait_sr_reached(sn, expected_status, deadline_abs);
    if (ret != SOCK_OK) {
        goto socket_done;
    }
    ret = (int8_t)sn;
    sock_state_reset(sn);
    sock_mode[sn] = (uint8_t)(protocol | (hardware_flag & 0xF0));
    sock_health[sn] = SOCK_HEALTHY;
#ifndef IPV6_AVAILABLE
    sock_io_mode[sn] = (flag & SF_IO_NONBLOCK) ? 1U : 0U;
#else
    sock_io_mode[sn] = (flag & SF_IO_NONBLOCK) ? 1U : 0U;
#endif
    sock_remained_size[sn] = 0;
    //M20150601 : repalce 0 with PACK_COMPLETED
    //sock_pack_info[sn] = 0;
    sock_pack_info[sn] = PACK_COMPLETED;//PACK_COMPLETED //TODO::need verify:LINAN 20250421
    //
socket_done:
    WIZCHIP_SOCK_UNLOCK(sn);
    return ret;
}

/* inherited_deadline is the caller's invocation deadline, or 0 when this call
 * is itself the invocation that issues the first command. */
static int8_t close_internal(uint8_t sn, uint8_t under_lock,
                             uint64_t inherited_deadline) {
    uint64_t deadline_abs = inherited_deadline;
    wizchip_timeout_config_t timeout_config;
    int8_t ret = SOCK_OK;

    if (sn >= _WIZCHIP_SOCK_NUM_) {
        return SOCKERR_SOCKNUM;
    }
    if (under_lock == 0U) {
        WIZCHIP_SOCK_LOCK(sn);
    }
    if (deadline_abs == 0U) {
        (void)wizchip_get_timeout_config(&timeout_config);
        ret = sock_deadline_begin(&deadline_abs,
                                  timeout_config.command_timeout_us);
        if (ret != SOCK_OK) {
            goto close_clear_state;
        }
    }

    if (sock_health[sn] == SOCK_FAULTED) {
        goto close_clear_state;
    }
    if (getSn_SR(sn) == SOCK_CLOSED) {
        goto close_clear_state;
    }

    //A20160426 : Applied the erratum 1 of W5300
#if   (_WIZCHIP_ == 5300)
    //M20160503 : Wrong socket parameter. s -> sn
    //if( ((getSn_MR(s)& 0x0F) == Sn_MR_TCP) && (getSn_TX_FSR(s) != getSn_TxMAX(s)) )
    if (((sock_mode[sn] & 0x0F) == Sn_MR_TCP) && (getSn_TX_FSR(sn) != getSn_TxMAX(sn))) {
        uint8_t destip[4] = {0, 0, 0, 1};
        // TODO
        // You can wait for completing to sending data;
        // wait about 1 second;
        // if you have completed to send data, skip the code of erratum 1
        // ex> wait_1s();
        //     if (getSn_TX_FSR(s) == getSn_TxMAX(s)) continue;
        //
        //M20160503 : The socket() of close() calls close() itself again. It occures a infinite loop - close()->socket()->close()->socket()-> ~
        //socket(s,Sn_MR_UDP,0x3000,0);
        //sendto(s,destip,1,destip,0x3000); // send the dummy data to an unknown destination(0.0.0.1).
        setSn_MR(sn, Sn_MR_UDP);
        setSn_PORTR(sn, 0x3000);
        setSn_CR(sn, Sn_CR_OPEN);
        while (getSn_CR(sn) != 0);
        while (getSn_SR(sn) != SOCK_UDP);
        sendto(sn, destip, 1, destip, 0x3000); // send the dummy data to an unknown destination(0.0.0.1).
    };
#endif
    setSn_CR(sn, Sn_CR_CLOSE);
    ret = wait_cr_accepted(sn, deadline_abs);
    if (ret != SOCK_OK) {
        goto close_clear_state;
    }
    /* clear all interrupt of SOCKETn. */
    setSn_IR(sn, 0xFF);
    /* The watchdog is deliberately not kicked from this loop: it must remain
     * able to detect a driver that has stopped making local progress. */
    ret = wait_sr_reached(sn, SOCK_CLOSED, deadline_abs);

close_clear_state:
    sock_state_reset(sn);
    sock_remained_size[sn] = 0;
    sock_pack_info[sn] = PACK_NONE;
    sock_mode[sn] = 0U;
    if (ret == SOCKERR_TIMEOUT) {
        sock_health[sn] = SOCK_FAULTED;
    }
    if (under_lock == 0U) {
        WIZCHIP_SOCK_UNLOCK(sn);
    }
    return ret;
}

int8_t close(uint8_t sn) {
    int8_t ret;

    CHECK_SOCKNUM();
    WIZCHIP_SOCK_LOCK(sn);
    ret = close_internal(sn, 1U, 0U);
    WIZCHIP_SOCK_UNLOCK(sn);
    return ret;
}

int8_t listen(uint8_t sn) {
    int8_t ret = SOCK_OK;
    uint64_t deadline_abs;
    wizchip_timeout_config_t timeout_config;

    CHECK_SOCKNUM();
    WIZCHIP_SOCK_LOCK(sn);
    if (sock_health[sn] == SOCK_FAULTED) {
        ret = SOCKERR_IO;
        goto listen_done;
    }
    if (getSn_SR(sn) != SOCK_INIT) {
        ret = SOCKERR_SOCKINIT;
        goto listen_done;
    }
    (void)wizchip_get_timeout_config(&timeout_config);
    ret = sock_deadline_begin(&deadline_abs,
                              timeout_config.command_timeout_us);
    if (ret != SOCK_OK) {
        goto listen_done;
    }
    setSn_CR(sn, Sn_CR_LISTEN);
    ret = wait_cr_accepted(sn, deadline_abs);
    if (ret != SOCK_OK) {
        goto listen_done;
    }
    ret = wait_sr_reached(sn, SOCK_LISTEN, deadline_abs);
    if (ret != SOCK_OK) {
        goto listen_done;
    }
listen_done:
    WIZCHIP_SOCK_UNLOCK(sn);
    return ret;
}
//int8_t connect (uint8_t sn, uint8_t * addr, uint16_t port )
int8_t connect_W5x00(uint8_t sn, uint8_t * addr, uint16_t port) {
    // printf(" W5x00 - connect - addrlen = %d \r\n" , 4 );
    // #ifdef IPV6_AVAILABLE
    // TODO :define how to work, when IPV6_AVAILABLE is defined
    // #endif
    return connect_IO_6(sn, addr, port, 4);
}

int8_t connect_W6x00(uint8_t sn, uint8_t * addr, uint16_t port, uint8_t addrlen) {
    // printf(" W6x00 - connect - addrlen = %d \r\n" , addrlen );
    // #ifdef IPV6_AVAILABLE
    // TODO :define how to work, when IPV6_AVAILABLE is defined
    // #endif
    return connect_IO_6(sn, addr, port, addrlen);
}

static int8_t connect_IO_6(uint8_t sn, uint8_t * addr, uint16_t port, uint8_t addrlen) {
    int8_t ret;
    uint8_t status;
    uint64_t deadline_abs;
    wizchip_timeout_config_t timeout_config;

    if (sn >= _WIZCHIP_SOCK_NUM_) {
        return SOCKERR_SOCKNUM;
    }
    if (addr == 0) {
        return SOCKERR_ARG;
    }
    if (port == 0U) {
        return SOCKERR_PORTZERO;
    }
    if (addrlen != 4U && addrlen != 16U) {
        return SOCKERR_IPINVALID;
    }
#ifdef IPV6_AVAILABLE
    {
        uint16_t ipzero = 0U;
        uint8_t i;

        for (i = 0U; i < addrlen; i++) {
            ipzero = (uint16_t)(ipzero + addr[i]);
        }
        if (ipzero == 0U) {
            return SOCKERR_IPINVALID;
        }
    }
#else
    if (addrlen == 4U) {
        uint32_t taddr;

        taddr = ((uint32_t)addr[0] & 0x000000FFU);
        taddr = (taddr << 8) + ((uint32_t)addr[1] & 0x000000FFU);
        taddr = (taddr << 8) + ((uint32_t)addr[2] & 0x000000FFU);
        taddr = (taddr << 8) + ((uint32_t)addr[3] & 0x000000FFU);
        if (taddr == 0xFFFFFFFFU || taddr == 0U) {
            return SOCKERR_IPINVALID;
        }
    }
#endif

    WIZCHIP_SOCK_LOCK(sn);
    if (wizchip_get_state() != WIZCHIP_STATE_READY) {
        ret = SOCKERR_NOTREADY;
        goto conn_done;
    }
    if (sock_health[sn] == SOCK_FAULTED) {
        ret = SOCKERR_IO;
        goto conn_done;
    }
    if ((sock_mode[sn] & 0x03U) != 0x01U) {
        ret = SOCKERR_SOCKMODE;
        goto conn_done;
    }
    if (getSn_SR(sn) != SOCK_INIT) {
        ret = SOCKERR_SOCKINIT;
        goto conn_done;
    }
#ifdef IPV6_AVAILABLE
    if ((addrlen == 16U && (sock_mode[sn] & 0x08U) == 0U) ||
        (addrlen == 4U && sock_mode[sn] == Sn_MR_TCP6)) {
        ret = SOCKERR_SOCKMODE;
        goto conn_done;
    }
#else
    if (addrlen == 16U) {
        ret = SOCKERR_SOCKMODE;
        goto conn_done;
    }
#endif

    setSn_DPORTR(sn, port);

    if (addrlen == 16) {   // addrlen=16, Sn_MR_TCP6(1001), Sn_MR_TCPD(1101))
#ifdef IPV6_AVAILABLE
        setSn_DIP6R(sn, addr);
        setSn_CR(sn, Sn_CR_CONNECT6);
#endif
    } else {       // addrlen=4, Sn_MR_TCP4(0001), Sn_MR_TCPD(1101)
        setSn_DIPR(sn, addr);
        //setSn_DPORT(sn,port); //TODO::need verify:LINAN 20250421
        setSn_CR(sn, Sn_CR_CONNECT);
    }
    (void)wizchip_get_timeout_config(&timeout_config);
    if (wizchip_deadline_config_valid() == 0) {
        ret = SOCKERR_TIMEOUT;
        goto conn_done;
    }
    deadline_abs = wizchip_deadline_abs(timeout_config.command_timeout_us);
    ret = wait_cr_accepted(sn, deadline_abs);
    if (ret != SOCK_OK) {
        goto conn_done;
    }

    deadline_abs = wizchip_deadline_abs(timeout_config.operation_timeout_us);
    while ((status = getSn_SR(sn)) != SOCK_ESTABLISHED) {
        if ((getSn_IR(sn) & Sn_IR_TIMEOUT) != 0U) {
            setSn_IR(sn, Sn_IR_TIMEOUT);
            ret = SOCKERR_TIMEOUT;
            goto conn_done;
        }
        if (status == SOCK_CLOSED) {
            ret = SOCKERR_SOCKCLOSED;
            goto conn_done;
        }
        if (wizchip_deadline_expired(deadline_abs)) {
            sock_health[sn] = SOCK_FAULTED;
            ret = SOCKERR_IO;
            goto conn_done;
        }
    }

    ret = SOCK_OK;
conn_done:
    WIZCHIP_SOCK_UNLOCK(sn);
    return ret;
}

int8_t disconnect(uint8_t sn) {
    uint8_t tmp;
    int8_t ret = SOCK_OK;
    uint64_t deadline_abs;
    wizchip_timeout_config_t timeout_config;

    if (sn >= _WIZCHIP_SOCK_NUM_) {
        return SOCKERR_SOCKNUM;
    }
    WIZCHIP_SOCK_LOCK(sn);
    if (wizchip_get_state() != WIZCHIP_STATE_READY) {
        ret = SOCKERR_NOTREADY;
        goto disconn_done;
    }
    if (sock_health[sn] == SOCK_FAULTED) {
        ret = SOCKERR_IO;
        goto disconn_done;
    }
    if ((sock_mode[sn] & 0x03U) != 0x01U) {
        ret = SOCKERR_SOCKMODE;
        goto disconn_done;
    }
    tmp = getSn_SR(sn);
    if (tmp != SOCK_CLOSED) {
        if (tmp == SOCK_ESTABLISHED || tmp == SOCK_CLOSE_WAIT) {
            setSn_CR(sn, Sn_CR_DISCON);
            (void)wizchip_get_timeout_config(&timeout_config);
            if (wizchip_deadline_config_valid() == 0) {
                ret = SOCKERR_TIMEOUT;
                goto disconn_done;
            }
            deadline_abs = wizchip_deadline_abs(timeout_config.command_timeout_us);
            ret = wait_cr_accepted(sn, deadline_abs);
            if (ret != SOCK_OK) {
                goto disconn_done;
            }
        }
        sock_is_sending[sn] = 0;
        (void)wizchip_get_timeout_config(&timeout_config);
        if (wizchip_deadline_config_valid() == 0) {
            ret = SOCKERR_TIMEOUT;
            goto disconn_done;
        }
        deadline_abs = wizchip_deadline_abs(timeout_config.operation_timeout_us);
        while (getSn_SR(sn) != SOCK_CLOSED) {
            if ((getSn_IR(sn) & Sn_IR_TIMEOUT) != 0U) {
                setSn_IR(sn, Sn_IR_TIMEOUT);
                sock_health[sn] = SOCK_FAULTED;
                ret = SOCKERR_TIMEOUT;
                goto disconn_done;
            }
            if (wizchip_deadline_expired(deadline_abs)) {
                sock_health[sn] = SOCK_FAULTED;
                ret = SOCKERR_IO;
                goto disconn_done;
            }
        }
    }
disconn_done:
    WIZCHIP_SOCK_UNLOCK(sn);
    return ret;
}


int32_t send(uint8_t sn, uint8_t * buf, uint16_t len) {
    uint8_t tmp = 0;
    uint16_t freesize = 0;
    int32_t ret;
    uint64_t deadline_abs;
    wizchip_deadline_t poll;
    wizchip_timeout_config_t timeout_config;

    if (len == 0U) {
        return 0;
    }
    if (buf == 0) {
        return SOCKERR_ARG;
    }
    if (sn >= _WIZCHIP_SOCK_NUM_) {
        return SOCKERR_SOCKNUM;
    }
    if (wizchip_get_state() != WIZCHIP_STATE_READY) {
        return SOCKERR_NOTREADY;
    }

    WIZCHIP_SOCK_LOCK(sn);
    if (sock_health[sn] == SOCK_FAULTED) {
        ret = SOCKERR_IO;
        goto send_done;
    }
    (void)wizchip_get_timeout_config(&timeout_config);
    if (wizchip_deadline_config_valid() == 0) {
        ret = SOCKERR_TIMEOUT;
        goto send_done;
    }
    /*
        The below codes can be omitted for optmization of speed
    */
    //CHECK_SOCKNUM();
    //CHECK_TCPMODE(Sn_MR_TCP4);
    /************/
    if ((sock_mode[sn] & 0x03U) != 0x01U) {
        ret = SOCKERR_SOCKMODE;
        goto send_done;
    }
#ifndef IPV6_AVAILABLE
    tmp = getSn_SR(sn);
    if (tmp != SOCK_ESTABLISHED && tmp != SOCK_CLOSE_WAIT) {
        ret = SOCKERR_SOCKSTATUS; goto send_done;
    }
    if (sock_is_sending[sn]) {
        deadline_abs = wizchip_deadline_abs(
            timeout_config.operation_timeout_us);
        wait_poll_init(&poll, deadline_abs);
        for (;;) {
            tmp = getSn_IR(sn);
            if (tmp & Sn_IR_SENDOK) {
                setSn_IR(sn, Sn_IR_SENDOK);
// Fixed: removed trailing semicolon from SOCK_ANY_PORT_NUM macro below
            //#if _WZICHIP_ == 5200
#if _WIZCHIP_ == 5200
                if (getSn_TX_RD(sn) != sock_next_rd[sn]) {
                    setSn_CR(sn, Sn_CR_SEND);
                    deadline_abs = wizchip_deadline_abs(
                        timeout_config.command_timeout_us);
                    ret = wait_cr_accepted(sn, deadline_abs);
                    if (ret != SOCK_OK) {
                        goto send_done;
                    }
                    ret = SOCK_BUSY; goto send_done;
                }
#endif
                sock_is_sending[sn] = 0;
                break;
            }
            if (tmp & Sn_IR_TIMEOUT) {
                setSn_IR(sn, Sn_IR_TIMEOUT);
                sock_health[sn] = SOCK_FAULTED;
                ret = SOCKERR_TIMEOUT; goto send_done;
            }
            tmp = getSn_SR(sn);
            if (tmp != SOCK_ESTABLISHED && tmp != SOCK_CLOSE_WAIT) {
                sock_is_sending[sn] = 0;
                ret = (tmp == SOCK_CLOSED) ? SOCKERR_SOCKCLOSED
                                          : SOCKERR_SOCKSTATUS;
                goto send_done;
            }
            if (sock_io_mode[sn]) {
                ret = SOCK_BUSY; goto send_done;
            }
            if (wait_poll_expired(&poll, deadline_abs) ==
                SOCKERR_DEADLINE) {
                sock_health[sn] = SOCK_FAULTED;
                ret = SOCKERR_TIMEOUT; goto send_done;
            }
        }
    }
#endif
    freesize = wizchip_txmax_cache[sn];
    if (len > freesize) {
        len = freesize;    // check size not to exceed MAX size.
    }
    deadline_abs = wizchip_deadline_abs(timeout_config.operation_timeout_us);
    wait_poll_init(&poll, deadline_abs);
    while (1) {
        ret = read_sn_tx_fsr(sn, &freesize);
        if (ret != 0) {
            goto send_done;
        }
        tmp = getSn_SR(sn);
        if ((tmp != SOCK_ESTABLISHED) && (tmp != SOCK_CLOSE_WAIT)) {
            if (tmp == SOCK_CLOSED) {
                (void)close_internal(sn, 1U, 0U);
                ret = SOCKERR_SOCKSTATUS; goto send_done;
            }
            ret = SOCKERR_SOCKSTATUS; goto send_done;
        }
        if (sock_io_mode[sn] && (len > freesize)) {
            ret = SOCK_BUSY; goto send_done;
        }
        if (len <= freesize) {
            break;
        }
        if (wait_poll_expired(&poll, deadline_abs) == SOCKERR_DEADLINE) {
            ret = SOCKERR_TIMEOUT; goto send_done;
        }
    }
    wiz_send_data(sn, buf, len);
#if _WIZCHIP_ == 5200
    sock_next_rd[sn] = getSn_TX_RD(sn) + len;
#endif

#if _WIZCHIP_ == 5300
    setSn_TX_WRSR(sn, len);
#endif
    /* NOTE: The sock_is_sending check block that previously appeared here
       was unreachable dead code on the W5500 path — the bit is always
       cleared at line 544 (SENDOK branch) before control reaches this
       point, confirmed by fourth-pass analysis. */
    setSn_CR(sn, Sn_CR_SEND);
    deadline_abs = wizchip_deadline_abs(timeout_config.command_timeout_us);
    ret = wait_cr_accepted(sn, deadline_abs);
    if (ret != SOCK_OK) {
        sock_health[sn] = SOCK_FAULTED;
        goto send_done;
    }
    sock_is_sending[sn] = 1;

    ret = len;
send_done:
    WIZCHIP_SOCK_UNLOCK(sn);
    return ret;
}
int32_t recv(uint8_t sn, uint8_t * buf, uint16_t len) { //lihan
    uint8_t  tmp = 0;
    uint16_t recvsize = 0;
    int32_t ret;
    uint64_t deadline_abs;
    wizchip_deadline_t poll;
    wizchip_timeout_config_t timeout_config;

    if (len == 0U) {
        return 0;
    }
    if (buf == 0) {
        return SOCKERR_ARG;
    }
    if (sn >= _WIZCHIP_SOCK_NUM_) {
        return SOCKERR_SOCKNUM;
    }
    if (wizchip_get_state() != WIZCHIP_STATE_READY) {
        return SOCKERR_NOTREADY;
    }

    WIZCHIP_SOCK_LOCK(sn);
    if (sock_health[sn] == SOCK_FAULTED) {
        ret = SOCKERR_IO;
        goto recv_done;
    }
    (void)wizchip_get_timeout_config(&timeout_config);
    if (wizchip_deadline_config_valid() == 0) {
        ret = SOCKERR_TIMEOUT;
        goto recv_done;
    }
    /*
        The below codes can be omitted for optmization of speed
    */
    //A20150601 : For integarating with W5300
#if   _WIZCHIP_ == 5300
    uint8_t head[2];
    uint16_t mr;
#endif
    //
    if ((sock_mode[sn] & 0x03U) != 0x01U) {
        ret = SOCKERR_SOCKMODE;
        goto recv_done;
    }

    recvsize = wizchip_rxmax_cache[sn];
    if (recvsize < len) {
        len = recvsize;
    }

    //A20150601 : For Integrating with W5300
#if _WIZCHIP_ == 5300
    //sock_pack_info[sn] = PACK_COMPLETED;    // for clear
    if (sock_remained_size[sn] == 0) {
#endif
        //
        deadline_abs = wizchip_deadline_abs(
            timeout_config.operation_timeout_us);
        wait_poll_init(&poll, deadline_abs);
        while (1) {
            ret = read_sn_rx_rsr(sn, &recvsize);
            if (ret != 0) {
                goto recv_done;
            }
            tmp = getSn_SR(sn);
            if (tmp != SOCK_ESTABLISHED) {
                if (tmp == SOCK_CLOSE_WAIT) {
                    if (recvsize != 0) {
                        break;
                    } else {
                        uint16_t freesize;
                        ret = read_sn_tx_fsr(sn, &freesize);
                        if (ret != 0) {
                            goto recv_done;
                        }
                        if (freesize == wizchip_txmax_cache[sn]) {
                            (void)close_internal(sn, 1U, 0U);
                            ret = SOCKERR_SOCKSTATUS; goto recv_done;
                        }
                    }
                    if (wait_poll_expired(&poll, deadline_abs) ==
                        SOCKERR_DEADLINE) {
                        ret = SOCKERR_TIMEOUT; goto recv_done;
                    }
                    continue;
                } else {
                    (void)close_internal(sn, 1U, 0U);
                    ret = SOCKERR_SOCKSTATUS; goto recv_done;
                }
            }
#ifdef IPV6_AVAILABLE
            if (recvsize != 0) {
                break;
            }
            if (sock_io_mode[sn]) {
                ret = SOCK_BUSY; goto recv_done;
            }
#else
            if (recvsize != 0) {
                break;
            }
            if (sock_io_mode[sn]) {
                ret = SOCK_BUSY; goto recv_done;
            }
#endif
            if (wait_poll_expired(&poll, deadline_abs) ==
                SOCKERR_DEADLINE) {
                ret = SOCKERR_TIMEOUT; goto recv_done;
            }
        };
#if _WIZCHIP_ == 5300
    }
#endif

    //A20150601 : For integrating with W5300
#if _WIZCHIP_ == 5300
    if ((sock_remained_size[sn] == 0) || (getSn_MR(sn) & Sn_MR_ALIGN)) {
        mr = getMR();
        if ((getSn_MR(sn) & Sn_MR_ALIGN) == 0) {
            wiz_recv_data(sn, head, 2);
            if (mr & MR_FS) {
                recvsize = (((uint16_t)head[1]) << 8) | ((uint16_t)head[0]);
            } else {
                recvsize = (((uint16_t)head[0]) << 8) | ((uint16_t)head[1]);
            }
            sock_pack_info[sn] = PACK_FIRST;
        }
        sock_remained_size[sn] = recvsize;
    }
    if (len > sock_remained_size[sn]) {
        len = sock_remained_size[sn];
    }
    recvsize = len;
    if (sock_pack_info[sn] & PACK_FIFOBYTE) {
        *buf = sock_remained_byte[sn];
        buf++;
        sock_pack_info[sn] &= ~(PACK_FIFOBYTE);
        recvsize -= 1;
        sock_remained_size[sn] -= 1;
    }
    if (recvsize != 0) {
        wiz_recv_data(sn, buf, recvsize);
        setSn_CR(sn, Sn_CR_RECV);
        deadline_abs = wizchip_deadline_abs(
            timeout_config.command_timeout_us);
        ret = wait_cr_accepted(sn, deadline_abs);
        if (ret != SOCK_OK) {
            sock_health[sn] = SOCK_FAULTED;
            goto recv_done;
        }
    }
    sock_remained_size[sn] -= recvsize;
    if (sock_remained_size[sn] != 0) {
        sock_pack_info[sn] |= PACK_REMAINED;
        if (recvsize & 0x1) {
            sock_pack_info[sn] |= PACK_FIFOBYTE;
        }
    } else {
        sock_pack_info[sn] = PACK_COMPLETED;
    }
    if (getSn_MR(sn) & Sn_MR_ALIGN) {
        sock_remained_size[sn] = 0;
    }
    //len = recvsize;
#else
    if (recvsize < len) {
        len = recvsize;
    }
    wiz_recv_data(sn, buf, len);
    setSn_CR(sn, Sn_CR_RECV);
    deadline_abs = wizchip_deadline_abs(timeout_config.command_timeout_us);
    ret = wait_cr_accepted(sn, deadline_abs);
    if (ret != SOCK_OK) {
        sock_health[sn] = SOCK_FAULTED;
        goto recv_done;
    }
#endif

    //M20150409 : Explicit Type Casting
    //return len;
    ret = (int32_t)len;
recv_done:
    WIZCHIP_SOCK_UNLOCK(sn);
    return ret;
}


int32_t sendto_W5x00(uint8_t sn, uint8_t * buf, uint16_t len, uint8_t * addr, uint16_t port) {
    if (len == 0U) {
        return 0;
    }
    if (buf == 0) {
        return SOCKERR_ARG;
    }
    if (sn >= _WIZCHIP_SOCK_NUM_) {
        return SOCKERR_SOCKNUM;
    }
    //static int32_t sendto_IO_6(uint8_t sn, uint8_t * buf, uint16_t len, uint8_t * addr, uint16_t port)
    // printf("sendto_W5x00\r\n" ) ;
    return sendto_IO_6(sn,   buf,  len,   addr,  port, 4);
}

int32_t sendto_W6x00(uint8_t sn, uint8_t * buf, uint16_t len, uint8_t * addr, uint16_t port, uint8_t addrlen) {
    if (len == 0U) {
        return 0;
    }
    if (buf == 0) {
        return SOCKERR_ARG;
    }
    if (sn >= _WIZCHIP_SOCK_NUM_) {
        return SOCKERR_SOCKNUM;
    }
    // printf("sendto_W6x00\r\n" ) ;
    //static int32_t sendto_IO_6(uint8_t sn, uint8_t * buf, uint16_t len, uint8_t * addr, uint16_t port)
    return sendto_IO_6(sn,  buf,  len,   addr,  port, addrlen);
}

static int32_t sendto_IO_6(uint8_t sn, uint8_t * buf, uint16_t len, uint8_t * addr, uint16_t port, uint8_t addrlen) {
    uint8_t tmp = 0;
    uint8_t tcmd = Sn_CR_SEND;
    uint8_t nonblocking;
    uint8_t is_macraw;
    (void)tcmd;
    uint16_t freesize = 0;
    uint32_t taddr;
    int32_t ret;
    uint64_t deadline_abs;
    wizchip_deadline_t poll;
    wizchip_timeout_config_t timeout_config;

    if (len == 0U) {
        return 0;
    }
    if (buf == 0) {
        return SOCKERR_ARG;
    }
    if (sn >= _WIZCHIP_SOCK_NUM_) {
        return SOCKERR_SOCKNUM;
    }

    WIZCHIP_SOCK_LOCK(sn);
    /* A locally faulted transmit path reports its own timeout ahead of the
     * generic health gate, and performs no further W5500 access. Only a
     * controller reset and reopen may clear it. */
    if (sock_tx_state[sn] == SOCK_TX_LOCAL_FAULT) {
        ret = SOCKERR_TIMEOUT;
        goto sndto_done;
    }
    if (sock_health[sn] == SOCK_FAULTED) {
        ret = SOCKERR_IO;
        goto sndto_done;
    }
    (void)wizchip_get_timeout_config(&timeout_config);
    if (wizchip_deadline_config_valid() == 0) {
        ret = SOCKERR_TIMEOUT;
        goto sndto_done;
    }
    nonblocking = sock_io_mode[sn];

    /* While a SEND is pending, do not copy the payload or reissue the
     * command. Inspect one interrupt and return immediately. */
    if (sock_tx_state[sn] == SOCK_TX_DGRAM_PENDING) {
        tmp = getSn_IR(sn);
        if ((tmp & Sn_IR_SENDOK) != 0U) {
            setSn_IR(sn, Sn_IR_SENDOK);
            ret = (int32_t)sock_tx_pending_len[sn];
            sock_tx_pending_len[sn] = 0U;
            sock_tx_state[sn] = SOCK_TX_IDLE;
            goto sndto_done;
        }
        if ((tmp & Sn_IR_TIMEOUT) != 0U) {
            setSn_IR(sn, Sn_IR_TIMEOUT);
            sock_tx_pending_len[sn] = 0U;
            sock_tx_state[sn] = SOCK_TX_IDLE;
            ret = SOCKERR_TIMEOUT;
            goto sndto_done;
        }
        ret = SOCK_BUSY;
        goto sndto_done;
    }

    /*
        The below codes can be omitted for optmization of speed
    */
    //CHECK_DGRAMMODE();
    /************/
    tmp = sock_mode[sn];
    is_macraw = ((tmp & 0x0FU) == Sn_MR_MACRAW) ? 1U : 0U;
    switch (tmp & 0x0F) {
    case Sn_MR_UDP:
    case Sn_MR_MACRAW:
    //         break;
    //   #if ( _WIZCHIP_ < 5200 )
    case Sn_MR_IPRAW:
    case Sn_MR_IPRAW6:
        break;
    //   #endif
    default:
        ret = SOCKERR_SOCKMODE; goto sndto_done;
    }
    if (tmp != Sn_MR_MACRAW) {
        if (addrlen == 16) {    // addrlen=16, Sn_MR_UDP6(1010), Sn_MR_UDPD(1110)), IPRAW6(1011)
#ifdef IPV6_AVAILABLE
            if (tmp & 0x08) {
                setSn_DIP6R(sn, addr);
                tcmd = Sn_CR_SEND6;
            } else
#endif
                ret = SOCKERR_SOCKMODE; goto sndto_done;
        } else if (addrlen == 4) { // addrlen=4, Sn_MR_UDP4(0010), Sn_MR_UDPD(1110), IPRAW4(0011)
            if (tmp == Sn_MR_UDP6 || tmp == Sn_MR_IPRAW6) {
                ret = SOCKERR_SOCKMODE; goto sndto_done;
            }
            setSn_DIPR(sn, addr);
            tcmd = Sn_CR_SEND;
        } else {
            ret = SOCKERR_IPINVALID; goto sndto_done;
        }
    }
    if ((tmp & 0x03) == 0x02) { // Sn_MR_UPD4(0010), Sn_MR_UDP6(1010), Sn_MR_UDPD(1110)
        if (port) {
            setSn_DPORTR(sn, port);
        } else {
            ret = SOCKERR_PORTZERO; goto sndto_done;
        }
    }
#ifndef IPV6_AVAILABLE
    if ((tmp & 0x0F) != Sn_MR_MACRAW) {
    //M20140501 : For avoiding fatal error on memory align mismatched
    //if(*((uint32_t*)addr) == 0) return SOCKERR_IPINVALID;
    //{
    //uint32_t taddr;
    taddr = ((uint32_t)addr[0]) & 0x000000FF;
    taddr = (taddr << 8) + ((uint32_t)addr[1] & 0x000000FF);
    taddr = (taddr << 8) + ((uint32_t)addr[2] & 0x000000FF);
    taddr = (taddr << 8) + ((uint32_t)addr[3] & 0x000000FF);
    //}
    //
    //if(*((uint32_t*)addr) == 0) return SOCKERR_IPINVALID;
    if ((taddr == 0) && ((tmp & Sn_MR_MACRAW) != Sn_MR_MACRAW)) {
        ret = SOCKERR_IPINVALID; goto sndto_done;
    }
    if ((port  == 0) && ((tmp & Sn_MR_MACRAW) != Sn_MR_MACRAW)) {
        ret = SOCKERR_PORTZERO; goto sndto_done;
    }
    tmp = getSn_SR(sn);
    //#if ( _WIZCHIP_ < 5200 )
    if ((tmp != SOCK_MACRAW) && (tmp != SOCK_UDP) && (tmp != SOCK_IPRAW)) {
        ret = SOCKERR_SOCKSTATUS; goto sndto_done;
    }
    //#else
    //   if(tmp != SOCK_MACRAW && tmp != SOCK_UDP) return SOCKERR_SOCKSTATUS;
    //#endif

    setSn_DIPR(sn, addr);
    setSn_DPORT(sn, port);
    }
#endif

    freesize = wizchip_txmax_cache[sn];
    if (len > freesize) {
        len = freesize;    // check size not to exceed MAX size.
    }

    if (is_macraw == 0U) {
        deadline_abs = wizchip_deadline_abs(
            timeout_config.operation_timeout_us);
        wait_poll_init(&poll, deadline_abs);
        for (;;) {
            ret = read_sn_tx_fsr(sn, &freesize);
            if (ret != 0) {
                goto sndto_done;
            }
            if (getSn_SR(sn) == SOCK_CLOSED) {
                ret = SOCKERR_SOCKCLOSED; goto sndto_done;
            }
            if (len <= freesize) {
                break;
            }
            if (nonblocking != 0U) {
                ret = SOCK_BUSY; goto sndto_done;
            }
            if (wait_poll_expired(&poll, deadline_abs) ==
                SOCKERR_DEADLINE) {
                ret = SOCKERR_TIMEOUT; goto sndto_done;
            }
        }
    } else if (getSn_SR(sn) == SOCK_CLOSED) {
        ret = SOCKERR_SOCKCLOSED; goto sndto_done;
    }
    /* Clear stale terminal events so the pending poll cannot observe the
     * result of a previous datagram. */
    setSn_IR(sn, (uint8_t)(Sn_IR_SENDOK | Sn_IR_TIMEOUT));
    wiz_send_data(sn, buf, len);

#if _WIZCHIP_ < 5500   //M20150401 : for WIZCHIP Errata #4, #5 (ARP errata)
    getSIPR((uint8_t *)&taddr);
    if (taddr == 0) {
        getSUBR((uint8_t*)&taddr);
        setSUBR((uint8_t*)"\x00\x00\x00\x00");
    } else {
        taddr = 0;
    }
#endif

#ifdef IPV6_AVAILABLE
    setSn_CR(sn, tcmd);
#else
    //A20150601 : For W5300
#if _WIZCHIP_ == 5300
    setSn_TX_WRSR(sn, len);
#endif
    //
    setSn_CR(sn, Sn_CR_SEND);
#endif
    /* Only command acceptance is a local wait. Its expiry is a
     * controller-progress fault rather than a peer-paced wait, so it must not
     * enter the pending state. */
    ret = sock_deadline_begin(&deadline_abs,
                              timeout_config.command_timeout_us);
    if (ret != SOCK_OK) {
        sock_tx_state[sn] = SOCK_TX_LOCAL_FAULT;
        goto sndto_done;
    }
    ret = wait_cr_accepted(sn, deadline_abs);
    if (ret != SOCK_OK) {
        sock_tx_state[sn] = SOCK_TX_LOCAL_FAULT;
        goto sndto_done;
    }

    /* Accepted. Completion is paced by the peer, so record the accepted length
     * and resume across scheduling iterations instead of blocking here. */
    sock_tx_pending_len[sn] = len;
    if (nonblocking != 0U) {
        sock_tx_state[sn] = SOCK_TX_DGRAM_PENDING;
        ret = SOCK_BUSY;
        goto sndto_done;
    }

    deadline_abs = wizchip_deadline_abs(timeout_config.operation_timeout_us);
    wait_poll_init(&poll, deadline_abs);
    for (;;) {
        tmp = getSn_IR(sn);
        if ((tmp & Sn_IR_SENDOK) != 0U) {
            setSn_IR(sn, Sn_IR_SENDOK);
            break;
        }
        //M:20131104
        //else if(tmp & Sn_IR_TIMEOUT) return SOCKERR_TIMEOUT;
        else if (tmp & Sn_IR_TIMEOUT) {
            setSn_IR(sn, Sn_IR_TIMEOUT);
            //M20150409 : Fixed the lost of sign bits by type casting.
            //len = (uint16_t)SOCKERR_TIMEOUT;
            //break;
#if _WIZCHIP_ < 5500   //M20150401 : for WIZCHIP Errata #4, #5 (ARP errata)
            if (taddr) {
                setSUBR((uint8_t*)&taddr);
            }
#endif
            ret = SOCKERR_TIMEOUT; goto sndto_done;
        }
        if (nonblocking != 0U) {
            ret = SOCK_BUSY; goto sndto_done;
        }
        if (wait_poll_expired(&poll, deadline_abs) == SOCKERR_DEADLINE) {
            sock_health[sn] = SOCK_FAULTED;
            ret = SOCKERR_TIMEOUT; goto sndto_done;
        }
        ////////////
    }
#if _WIZCHIP_ < 5500   //M20150401 : for WIZCHIP Errata #4, #5 (ARP errata)
    if (taddr) {
        setSUBR((uint8_t*)&taddr);
    }
#endif
    //M20150409 : Explicit Type Casting
    //ret = len;
    ret = (int32_t)len;
sndto_done:
    WIZCHIP_SOCK_UNLOCK(sn);
    return ret;
}



int32_t recvfrom_W5x00(uint8_t sn, uint8_t * buf, uint16_t len, uint8_t * addr, uint16_t *port) {
    if (len == 0U) {
        return 0;
    }
    if (buf == 0) {
        return SOCKERR_ARG;
    }
    if (sn >= _WIZCHIP_SOCK_NUM_) {
        return SOCKERR_SOCKNUM;
    }
    return recvfrom_IO_6(sn, buf, len, addr, port, (uint8_t*)0);
}

int32_t recvfrom_W6x00(uint8_t sn, uint8_t * buf, uint16_t len, uint8_t * addr, uint16_t *port, uint8_t *addrlen) {
    if (len == 0U) {
        return 0;
    }
    if (buf == 0) {
        return SOCKERR_ARG;
    }
    if (sn >= _WIZCHIP_SOCK_NUM_) {
        return SOCKERR_SOCKNUM;
    }
    // printf("recvfrom_W6x00\r\n" ) ;
    //int32_t recvfrom_IO_6(uint8_t sn, uint8_t * buf, uint16_t len, uint8_t * addr, uint16_t *port)
    return recvfrom_IO_6(sn,  buf,  len,   addr,  port, addrlen);
}
static int32_t recvfrom_IO_6(uint8_t sn, uint8_t * buf, uint16_t len, uint8_t * addr, uint16_t *port, uint8_t *addrlen) {
    (void)addrlen;
#if _WIZCHIP_ == 5300
    uint16_t mr;
    uint16_t mr1;
#else
    uint8_t  mr;
#endif
    //
    uint8_t  head[8];
    uint8_t nonblocking;
    uint16_t pack_len = 0;
    int32_t ret;
    uint64_t deadline_abs;
    wizchip_deadline_t poll;
    wizchip_timeout_config_t timeout_config;

    if (len == 0U) {
        return 0;
    }
    if (buf == 0) {
        return SOCKERR_ARG;
    }
    if (sn >= _WIZCHIP_SOCK_NUM_) {
        return SOCKERR_SOCKNUM;
    }

    WIZCHIP_SOCK_LOCK(sn);
    if (sock_health[sn] == SOCK_FAULTED) {
        ret = SOCKERR_IO;
        goto rcvfr_done;
    }
    (void)wizchip_get_timeout_config(&timeout_config);
    if (wizchip_deadline_config_valid() == 0) {
        ret = SOCKERR_TIMEOUT;
        goto rcvfr_done;
    }
    nonblocking = sock_io_mode[sn];

    /*
        The below codes can be omitted for optmization of speed
    */
    //CHECK_DGRAMMODE();
    /************/
    //CHECK_SOCKMODE(Sn_MR_UDP);
    //A20150601
#if _WIZCHIP_ == 5300
    mr1 = getMR();
#endif

    switch ((mr = sock_mode[sn]) & 0x0F) {
    case Sn_MR_UDP:
    case Sn_MR_IPRAW:
    case Sn_MR_IPRAW6:
    case Sn_MR_MACRAW:
        break;
#if ( _WIZCHIP_ < 5200 )
    case Sn_MR_PPPoE:
        break;
#endif
    default:
        ret = SOCKERR_SOCKMODE; goto rcvfr_done;
    }
    if (sock_remained_size[sn] == 0) {
        deadline_abs = wizchip_deadline_abs(
            timeout_config.operation_timeout_us);
        wait_poll_init(&poll, deadline_abs);
        for (;;) {
            ret = read_sn_rx_rsr(sn, &pack_len);
            if (ret != 0) {
                goto rcvfr_done;
            }
            if (getSn_SR(sn) == SOCK_CLOSED) {
                ret = SOCKERR_SOCKCLOSED; goto rcvfr_done;
            }
#ifndef IPV6_AVAILABLE
            if (pack_len != 0) {
                break;
            }
#else
            if (pack_len != 0) {
                sock_pack_info[sn] = PACK_NONE;
                break;
            }
#endif
            if (nonblocking != 0U) {
                ret = SOCK_BUSY; goto rcvfr_done;
            }
            if (wait_poll_expired(&poll, deadline_abs) ==
                SOCKERR_DEADLINE) {
                ret = SOCKERR_TIMEOUT; goto rcvfr_done;
            }
        }
    }
#ifdef IPV6_AVAILABLE
    /* First read 2 bytes of PACKET INFO in SOCKETn RX buffer*/
    wiz_recv_data(sn, head, 2);
    setSn_CR(sn, Sn_CR_RECV);
    ret = wait_datagram_cr_accepted(
        sn, timeout_config.command_timeout_us, nonblocking);
    if (ret != SOCK_OK) {
        goto rcvfr_done;
    }
    pack_len = head[0] & 0x07;
    pack_len = (pack_len << 8) + head[1];
#endif
    //D20150601 : Move it to bottom
    // sock_pack_info[sn] = PACK_COMPLETED;
#ifndef IPV6_AVAILABLE
    if (addr == 0 || port == 0) {
        ret = SOCKERR_ARG; goto rcvfr_done;
    }
#endif
    switch (mr & 0x07) {
    case Sn_MR_UDP4 :
    case Sn_MR_UDP6:
    case Sn_MR_UDPD:
#ifdef IPV6_AVAILABLE
        if (addr == 0) {
            ret = SOCKERR_ARG; goto rcvfr_done;
        }

        sock_pack_info[sn] = head[0] & 0xF8;

        if (sock_pack_info[sn] & PACK_IPv6) {
            *addrlen = 16 ;
        } else {
            *addrlen = 4 ;
        }
        wiz_recv_data(sn, addr, *addrlen);
        setSn_CR(sn, Sn_CR_RECV);
        ret = wait_datagram_cr_accepted(
            sn, timeout_config.command_timeout_us, nonblocking);
        if (ret != SOCK_OK) {
            goto rcvfr_done;
        }

#else
        if (sock_remained_size[sn] == 0) {
            wiz_recv_data(sn, head, 8);
            setSn_CR(sn, Sn_CR_RECV);
            ret = wait_datagram_cr_accepted(
                sn, timeout_config.command_timeout_us, nonblocking);
            if (ret != SOCK_OK) {
                goto rcvfr_done;
            }
            /*
             * Optimization (AUD-033): header and payload reads
             * each call wiz_recv_data with separate RX pointer
             * commit. Read pointer once, compute local offsets,
             * publish final pointer once to save ~4 SPI frames.
             */
            // read peer's IP address, port number & packet length
            //A20150601 : For W5300
#if _WIZCHIP_ == 5300
            if (mr1 & MR_FS) {
                addr[0] = head[1];
                addr[1] = head[0];
                addr[2] = head[3];
                addr[3] = head[2];
                *port = head[5];
                *port = (*port << 8) + head[4];
                sock_remained_size[sn] = head[7];
                sock_remained_size[sn] = (sock_remained_size[sn] << 8) + head[6];
            } else {
#endif
                addr[0] = head[0];
                addr[1] = head[1];
                addr[2] = head[2];
                addr[3] = head[3];
                *port = head[4];
                *port = (*port << 8) + head[5];
                sock_remained_size[sn] = head[6];
                sock_remained_size[sn] = (sock_remained_size[sn] << 8) + head[7];
#if _WIZCHIP_ == 5300
            }
#endif
            sock_pack_info[sn] = PACK_FIRST;
        }
        if (len < sock_remained_size[sn]) {
            pack_len = len;
        } else {
            pack_len = sock_remained_size[sn];
        }
        //A20150601 : For W5300
        len = pack_len;
#if _WIZCHIP_ == 5300
        if (sock_pack_info[sn] & PACK_FIFOBYTE) {
            *buf++ = sock_remained_byte[sn];
            pack_len -= 1;
            sock_remained_size[sn] -= 1;
            sock_pack_info[sn] &= ~PACK_FIFOBYTE;
        }
#endif
        //
        // Need to packet length check (default 1472)
        //
        wiz_recv_data(sn, buf, pack_len); // data copy.
#endif
        break;
    case Sn_MR_MACRAW :
        if (sock_remained_size[sn] == 0) {
#ifndef IPV6_AVAILABLE
            wiz_recv_data(sn, head, 2);
            setSn_CR(sn, Sn_CR_RECV);
            ret = wait_datagram_cr_accepted(
                sn, timeout_config.command_timeout_us, nonblocking);
            if (ret != SOCK_OK) {
                goto rcvfr_done;
            }
#endif
            // read peer's IP address, port number & packet length
            {
                uint16_t pkt_len;
                sock_remained_size[sn] = head[0];
                pkt_len = ((uint16_t)sock_remained_size[sn] << 8) | head[1];
                if (pkt_len < 2u) {
                    (void)close_internal(sn, 1U, 0U);
                    ret = SOCKFATAL_PACKLEN; goto rcvfr_done;
                }
                sock_remained_size[sn] = pkt_len - 2u;
            }
#if _WIZCHIP_ == W5300
            if (sock_remained_size[sn] & 0x01) {
                sock_remained_size[sn] = sock_remained_size[sn] + 1 - 4;
            } else {
                sock_remained_size[sn] -= 4;
            }
#endif
            if (sock_remained_size[sn] > 1514) {
                (void)close_internal(sn, 1U, 0U);
                ret = SOCKFATAL_PACKLEN; goto rcvfr_done;
            }
            sock_pack_info[sn] = PACK_FIRST;
        }
        if (len < sock_remained_size[sn]) {
            pack_len = len;
        } else {
            pack_len = sock_remained_size[sn];
        }
        wiz_recv_data(sn, buf, pack_len);
        break;
    //#if ( _WIZCHIP_ < 5200 )
    case Sn_MR_IPRAW6:
    case Sn_MR_IPRAW4 :
        if (sock_remained_size[sn] == 0) {
#ifndef IPV6_AVAILABLE
            wiz_recv_data(sn, head, 6);
            setSn_CR(sn, Sn_CR_RECV);
            ret = wait_datagram_cr_accepted(
                sn, timeout_config.command_timeout_us, nonblocking);
            if (ret != SOCK_OK) {
                goto rcvfr_done;
            }
            addr[0] = head[0];
            addr[1] = head[1];
            addr[2] = head[2];
            addr[3] = head[3];
            sock_remained_size[sn] = head[4];
            //M20150401 : For Typing Error
            //sock_remaiend_size[sn] = (sock_remained_size[sn] << 8) + head[5];
            sock_remained_size[sn] = (sock_remained_size[sn] << 8) + head[5];
            sock_pack_info[sn] = PACK_FIRST;
#else
            if (*addr == 0) {
                ret = SOCKERR_ARG; goto rcvfr_done;
            }
            sock_pack_info[sn] = head[0] & 0xF8;
            if (sock_pack_info[sn] & PACK_IPv6) {
                *addrlen = 16;
            } else {
                *addrlen = 4;
            }
            wiz_recv_data(sn, addr, *addrlen);
            setSn_CR(sn, Sn_CR_RECV);
            ret = wait_datagram_cr_accepted(
                sn, timeout_config.command_timeout_us, nonblocking);
            if (ret != SOCK_OK) {
                goto rcvfr_done;
            }

#endif
        }
#ifndef IPV6_AVAILABLE
        /* Progress partial IPRAW receives on every call, not just first chunk */
        if (sock_remained_size[sn] > 0) {
            if (len < sock_remained_size[sn]) {
                pack_len = len;
            } else {
                pack_len = sock_remained_size[sn];
            }
            wiz_recv_data(sn, buf, pack_len);
        }
#endif
        break;
    default:
        wiz_recv_ignore(sn, pack_len); // data copy.
        sock_remained_size[sn] = pack_len;
        break;
    }
#ifdef IPV6_AVAILABLE
    sock_remained_size[sn] = pack_len;
    sock_pack_info[sn] |= PACK_FIRST;
    if ((sock_mode[sn] & 0x03) == 0x02) { // Sn_MR_UDP4(0010), Sn_MR_UDP6(1010), Sn_MR_UDPD(1110)
        /* Read port number of PACKET INFO in SOCKETn RX buffer */
        if (port == 0) {
            ret = SOCKERR_ARG; goto rcvfr_done;
        }
        wiz_recv_data(sn, head, 2);
        *port = (((((uint16_t)head[0])) << 8) + head[1]);
        setSn_CR(sn, Sn_CR_RECV);
        ret = wait_datagram_cr_accepted(
            sn, timeout_config.command_timeout_us, nonblocking);
        if (ret != SOCK_OK) {
            goto rcvfr_done;
        }
    }

    if (len < sock_remained_size[sn]) {
        pack_len = len;
    } else {
        pack_len = sock_remained_size[sn];
    }
    wiz_recv_data(sn, buf, pack_len);
    setSn_CR(sn, Sn_CR_RECV);
    /* wait to process the command... */
    ret = wait_datagram_cr_accepted(
        sn, timeout_config.command_timeout_us, nonblocking);
    if (ret != SOCK_OK) {
        goto rcvfr_done;
    }

    sock_remained_size[sn] -= pack_len;
    if (sock_remained_size[sn] != 0) {
        sock_pack_info[sn] |= PACK_REMAINED;
    } else {
        sock_pack_info[sn] |= PACK_COMPLETED;
    }

#else
    setSn_CR(sn, Sn_CR_RECV);
    /* wait to process the command... */
    ret = wait_datagram_cr_accepted(
        sn, timeout_config.command_timeout_us, nonblocking);
    if (ret != SOCK_OK) {
        goto rcvfr_done;
    }
    sock_remained_size[sn] -= pack_len;
    //M20150601 :
    //if(sock_remained_size[sn] != 0) sock_pack_info[sn] |= 0x01;
    if (sock_remained_size[sn] != 0) {
        sock_pack_info[sn] |= PACK_REMAINED;
#if _WIZCHIP_ == 5300
        if (pack_len & 0x01) {
            sock_pack_info[sn] |= PACK_FIFOBYTE;
        }
#endif
    } else {
        /* Preserve PACK_FIRST for zero-length UDP datagrams: per
           socket.h:596-599, PACK_FIRST + zero return = valid empty
           datagram. PACK_COMPLETED==0, so |= is a no-op. */
        sock_pack_info[sn] |= PACK_COMPLETED;
    }
#if _WIZCHIP_ == 5300
    pack_len = len;
#endif
    //
    //M20150409 : Explicit Type Casting
    //return pack_len;
#endif
    ret = (int32_t)pack_len;
rcvfr_done:
    WIZCHIP_SOCK_UNLOCK(sn);
    return ret;
}


int8_t  ctlsocket(uint8_t sn, ctlsock_type cstype, void* arg) {
    uint8_t tmp = 0;
    int8_t ret;
    CHECK_SOCKNUM();
    if (arg == 0) {
        return SOCKERR_ARG;
    }
    WIZCHIP_SOCK_LOCK(sn);
    /* Answered before the health gate and without any WIZCHIP access: the
     * caller needs this query most when the socket is locally faulted. */
    if (cstype == CS_GET_TX_STATE) {
        *((sock_tx_state_t*)arg) = sock_tx_state[sn];
        ret = SOCK_OK;
        goto ctl_done;
    }
    if (sock_health[sn] == SOCK_FAULTED) {
        ret = SOCKERR_IO;
        goto ctl_done;
    }
    switch (cstype) {
    case CS_SET_IOMODE:
        tmp = *((uint8_t*)arg);
        if (tmp == SOCK_IO_NONBLOCK) {
            sock_io_mode[sn] = 1;
        } else if (tmp == SOCK_IO_BLOCK) {
            sock_io_mode[sn] = 0;
        } else {
            ret = SOCKERR_ARG; goto ctl_done;
        }
        break;
    case CS_GET_IOMODE:
        *((uint8_t*)arg) = sock_io_mode[sn];
        //
        break;
    case CS_GET_MAXTXBUF:
        *((uint16_t*)arg) = wizchip_txmax_cache[sn];
        break;
    case CS_GET_MAXRXBUF:
        *((uint16_t*)arg) = wizchip_rxmax_cache[sn];
        break;
    case CS_CLR_INTERRUPT:
        tmp = *((uint8_t*)arg);
        if (tmp > SIK_ALL) {
            ret = SOCKERR_ARG; goto ctl_done;
        }
        setSn_IR(sn, tmp);
        break;
    case CS_GET_INTERRUPT:
        *((uint8_t*)arg) = getSn_IR(sn);
        break;
#if _WIZCHIP_ != 5100
    case CS_SET_INTMASK:
        tmp = *((uint8_t*)arg);
        if (tmp > SIK_ALL) {
            ret = SOCKERR_ARG; goto ctl_done;
        }
        setSn_IMR(sn, tmp);
        break;
    case CS_GET_INTMASK:
        *((uint8_t*)arg) = getSn_IMR(sn);
        break;
#endif
#ifdef IPV6_AVAILABLE
    case CS_SET_PREFER:
        tmp = *((uint8_t*)arg);
        if ((tmp & 0x03) == 0x01) {
            ret = SOCKERR_ARG; goto ctl_done;
        }
        setSn_PSR(sn, tmp);
        break;
    case CS_GET_PREFER:
        *(uint8_t*) arg = getSn_PSR(sn);
        break;
#endif
    default:
        ret = SOCKERR_ARG; goto ctl_done;
    }
    ret = SOCK_OK;
ctl_done:
    WIZCHIP_SOCK_UNLOCK(sn);
    return ret;
}

int8_t  setsockopt(uint8_t sn, sockopt_type sotype, void* arg) {
    int8_t ret;
#if _WIZCHIP_ != 5100
    uint64_t deadline_abs;
    wizchip_deadline_t poll;
    wizchip_timeout_config_t timeout_config;
#endif
    CHECK_SOCKNUM();
    if (arg == 0) {
#if _WIZCHIP_ != 5100
        if (sotype != SO_KEEPALIVESEND) {
            return SOCKERR_ARG;
        }
#else
        return SOCKERR_ARG;
#endif
    }
    WIZCHIP_SOCK_LOCK(sn);
    if (sock_health[sn] == SOCK_FAULTED) {
        ret = SOCKERR_IO;
        goto ss_done;
    }
    switch (sotype) {
    case SO_TTL:
        setSn_TTL(sn, *(uint8_t*)arg);
        break;
    case SO_TOS:
        setSn_TOS(sn, *(uint8_t*)arg);
        break;
    case SO_MSS:
        setSn_MSSR(sn, *(uint16_t*)arg);
        break;
    case SO_DESTIP:
#ifdef IPV6_AVAILABLE
        if (((wiz_IPAddress *)arg)->len == 16) {
            setSn_DIP6R(sn, ((wiz_IPAddress*)arg)->ip);
        } else
#endif
            setSn_DIPR(sn, (uint8_t*)arg);
        break;
    case SO_DESTPORT:
        setSn_DPORTR(sn, *(uint16_t*)arg);
        break;
#if _WIZCHIP_ != 5100
    case SO_KEEPALIVESEND:
        if ((sock_mode[sn] & 0x03U) != 0x01U) {
            ret = SOCKERR_SOCKMODE; goto ss_done;
        }
#if _WIZCHIP_ > 5200
        if (getSn_KPALVTR(sn) != 0) {
            ret = SOCKERR_SOCKOPT; goto ss_done;
        }
#endif
        (void)wizchip_get_timeout_config(&timeout_config);
        if (wizchip_deadline_config_valid() == 0) {
            ret = SOCKERR_TIMEOUT;
            goto ss_done;
        }
        deadline_abs = wizchip_deadline_abs(
            timeout_config.command_timeout_us);
        wait_poll_init(&poll, deadline_abs);
        setSn_CR(sn, Sn_CR_SEND_KEEP);
        while (getSn_CR(sn) != 0) {
            // M20131220
            //if ((tmp = getSn_IR(sn)) & Sn_IR_TIMEOUT)
            if (getSn_IR(sn) & Sn_IR_TIMEOUT) {
                setSn_IR(sn, Sn_IR_TIMEOUT);
                ret = SOCKERR_TIMEOUT; goto ss_done;
            }
            if (wait_poll_expired(&poll, deadline_abs) ==
                SOCKERR_DEADLINE) {
                sock_health[sn] = SOCK_FAULTED;
                ret = SOCKERR_TIMEOUT; goto ss_done;
            }
        }
        break;
#if _WIZCHIP_ > 5200
    case SO_KEEPALIVEAUTO:
        if ((sock_mode[sn] & 0x03U) != 0x01U) {
            ret = SOCKERR_SOCKMODE; goto ss_done;
        }
        setSn_KPALVTR(sn, *(uint8_t*)arg);
        break;
#endif
#endif
    default:
        ret = SOCKERR_ARG; goto ss_done;
    }
    ret = SOCK_OK;
ss_done:
    WIZCHIP_SOCK_UNLOCK(sn);
    return ret;
}

int8_t getsockopt(uint8_t sn, sockopt_type sotype, void* arg) {
    int8_t ret;
    uint8_t mode;
    CHECK_SOCKNUM();
    if (arg == 0) {
        return SOCKERR_ARG;
    }
    WIZCHIP_SOCK_LOCK(sn);
    if (sock_health[sn] == SOCK_FAULTED && sotype != SO_STATUS) {
        ret = SOCKERR_IO;
        goto gs_done;
    }
    switch (sotype) {
    case SO_FLAG:
        mode = sock_mode_cached_or_read(sn);
#ifdef IPV6_AVAILABLE
        *(uint8_t*)arg = (mode & 0xF0) | (getSn_MR2(sn)) | (uint8_t)(sock_io_mode[sn] << 3);
#else
        *(uint8_t*)arg = (mode & 0xF0) | (uint8_t)(sock_io_mode[sn] << 3);
#endif
        break;
    case SO_TTL:
        *(uint8_t*) arg = getSn_TTL(sn);
        break;
    case SO_TOS:
        *(uint8_t*) arg = getSn_TOS(sn);
        break;
    case SO_MSS:
        *(uint16_t*) arg = getSn_MSSR(sn);
        break;
    case SO_DESTIP:
#ifdef IPV6_AVAILABLE
        if ((sock_mode[sn] & 0x03U) != 0x01U) {
            ret = SOCKERR_SOCKMODE; goto gs_done;
        }
        if (getSn_ESR(sn) & TCPSOCK_MODE) { //IPv6 ?
            getSn_DIP6R(sn, ((wiz_IPAddress*)arg)->ip);
            ((wiz_IPAddress*)arg)->len = 16;
        } else {
            getSn_DIPR(sn, ((wiz_IPAddress*)arg)->ip);
            ((wiz_IPAddress*)arg)->len = 4;
        }
        break;
#else
        getSn_DIPR(sn, (uint8_t*)arg);
        break;
#endif
    case SO_DESTPORT:
        *(uint16_t*) arg = getSn_DPORTR(sn);
        break;
#if  _WIZCHIP_ > 5200
    case SO_KEEPALIVEAUTO:
        if ((sock_mode[sn] & 0x03U) != 0x01U) {
            ret = SOCKERR_SOCKMODE; goto gs_done;
        }
        *(uint8_t*) arg = getSn_KPALVTR(sn);
        break;
#endif
    case SO_SENDBUF:
        if (read_sn_tx_fsr(sn, (uint16_t *)arg) != 0) {
            ret = SOCKERR_IO; goto gs_done;
        }
        break;
    case SO_RECVBUF:
        if (read_sn_rx_rsr(sn, (uint16_t *)arg) != 0) {
            ret = SOCKERR_IO; goto gs_done;
        }
        break;
    case SO_STATUS:
        *(uint8_t*) arg = getSn_SR(sn);
        break;
#ifdef IPV6_AVAILABLE
    case SO_EXTSTATUS:
        if ((sock_mode[sn] & 0x03U) != 0x01U) {
            ret = SOCKERR_SOCKMODE; goto gs_done;
        }
        *(uint8_t*) arg = getSn_ESR(sn) & 0x07;
        break;
    case SO_REMAINSIZE:
        mode = sock_mode_cached_or_read(sn);
        if (mode == SOCK_CLOSED) {
            ret = SOCKERR_SOCKSTATUS; goto gs_done;
        }
        if (mode & 0x01) {
            if (read_sn_rx_rsr(sn, (uint16_t *)arg) != 0) {
                ret = SOCKERR_IO; goto gs_done;
            }
        } else {
            *(uint16_t*)arg = sock_remained_size[sn];
        }
        break;
    case SO_PACKINFO:
        mode = sock_mode_cached_or_read(sn);
        if (mode == SOCK_CLOSED) {
            ret = SOCKERR_SOCKSTATUS; goto gs_done;
        }
        if (mode & 0x01) {
            ret = SOCKERR_SOCKMODE; goto gs_done;
        } else {
            *(uint8_t*)arg = sock_pack_info[sn];
        }
        break;
    case SO_MODE:
        *(uint8_t*) arg = 0x0F & sock_mode_cached_or_read(sn);
        break;
#else
    case SO_REMAINSIZE:
        mode = sock_mode_cached_or_read(sn);
        if ((mode & 0x0F) == Sn_MR_TCP) {
            if (read_sn_rx_rsr(sn, (uint16_t *)arg) != 0) {
                ret = SOCKERR_IO; goto gs_done;
            }
        } else {
            *(uint16_t*)arg = sock_remained_size[sn];
        }
        break;
    case SO_PACKINFO  :
        //CHECK_SOCKMODE(Sn_MR_TCP);
#if _WIZCHIP_ != 5300
        if ((sock_mode_cached_or_read(sn) & 0x0F) == Sn_MR_TCP) {
            ret = SOCKERR_SOCKMODE; goto gs_done;
        }
#endif
        *(uint8_t*)arg = sock_pack_info[sn];
        break;

#endif
    default:
        ret = SOCKERR_SOCKOPT; goto gs_done;
    }
    ret = SOCK_OK;
gs_done:
    WIZCHIP_SOCK_UNLOCK(sn);
    return ret;
}

#ifdef IPV6_AVAILABLE
int16_t peeksockmsg(uint8_t sn, uint8_t* submsg, uint16_t subsize) {
    uint32_t rx_ptr = 0;
    uint16_t available = 0, i = 0, sub_idx = 0;

    if (read_sn_rx_rsr(sn, &available) != 0) {
        return SOCKERR_IO;
    }
    if ((available > 0) && (subsize > 0)) {
        rx_ptr = ((uint32_t)getSn_RX_RD(sn) << 8)  + WIZCHIP_RXBUF_BLOCK(sn);
        sub_idx = 0;
        for (i = 0; i < available; i++) {
            if (WIZCHIP_READ(rx_ptr) == submsg[sub_idx]) {
                sub_idx++;
                if (sub_idx == subsize) {
                    return (i + 1 - sub_idx);
                }
            } else {
                sub_idx = 0;
            }
            rx_ptr = WIZCHIP_OFFSET_INC(rx_ptr, 1);
        }
    }
    return -1;
}


#endif
