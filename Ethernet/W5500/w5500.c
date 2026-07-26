//*****************************************************************************
//
//! \file w5500.c
//! \brief W5500 HAL Interface.
//! \version 1.0.2
//! \date 2013/10/21
//! \par  Revision history
//!       <2015/02/05> Notice
//!        The version history is not updated after this point.
//!        Download the latest version directly from GitHub. Please visit the our GitHub repository for ioLibrary.
//!        >> https://github.com/Wiznet/ioLibrary_Driver
//!       <2014/05/01> V1.0.2
//!         1. Implicit type casting -> Explicit type casting. Refer to M20140501
//!            Fixed the problem on porting into under 32bit MCU
//!            Issued by Mathias ClauBen, wizwiki forum ID Think01 and bobh
//!            Thank for your interesting and serious advices.
//!       <2013/12/20> V1.0.1
//!         1. Remove warning
//!         2. WIZCHIP_READ_BUF WIZCHIP_WRITE_BUF in case _WIZCHIP_IO_MODE_SPI_FDM_
//!            for loop optimized(removed). refer to M20131220
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
//#include <stdio.h>
#include "w5500.h"

#ifndef SOCK_OK
#define SOCK_OK (1)
#endif
#ifndef SOCKERR_IO
#define SOCKERR_IO (-3)
#endif
#ifndef SOCKERR_DEADLINE
#define SOCKERR_DEADLINE (-16)
#endif

#define _W5500_SPI_VDM_OP_          0x00
#define _W5500_SPI_FDM_OP_LEN1_     0x01
#define _W5500_SPI_FDM_OP_LEN2_     0x02
#define _W5500_SPI_FDM_OP_LEN4_     0x03

#ifndef WIZCHIP_SPI_OPTIMIZE
#if defined(__has_attribute) && __has_attribute(optimize)
#define WIZCHIP_SPI_OPTIMIZE __attribute__((optimize("O2")))
#else
#define WIZCHIP_SPI_OPTIMIZE
#endif
#endif

#if   (_WIZCHIP_ == 5500)
////////////////////////////////////////////////////

WIZCHIP_SPI_OPTIMIZE uint8_t  WIZCHIP_READ(uint32_t AddrSel) {
    return wizchip_read8_checked(AddrSel);
}

WIZCHIP_SPI_OPTIMIZE void     WIZCHIP_WRITE(uint32_t AddrSel, uint8_t wb) {
    (void)wizchip_write8_checked(AddrSel, wb);
}

WIZCHIP_SPI_OPTIMIZE void     WIZCHIP_READ_BUF(uint32_t AddrSel, uint8_t* pBuf, uint16_t len) {
    (void)wizchip_read_buf_checked(AddrSel, pBuf, len);
}

WIZCHIP_SPI_OPTIMIZE void     WIZCHIP_WRITE_BUF(uint32_t AddrSel, uint8_t* pBuf, uint16_t len) {
    (void)wizchip_write_buf_checked(AddrSel, pBuf, len);
}

static int8_t wizchip_set_buf_size(uint8_t sn, uint8_t size_kb,
                                   uint32_t addr, uint16_t *cache) {
    uint8_t readback = 0;
    int8_t result;

    if (sn >= _WIZCHIP_SOCK_NUM_ ||
        (size_kb != 0u && size_kb != 2u && size_kb != 4u &&
         size_kb != 8u && size_kb != 16u)) {
        return -1;
    }

    WIZCHIP_GLOBAL_LOCK();
    result = wizchip_write8_checked(addr, size_kb);
    if (result == 0) {
        result = wizchip_read8_checked_out(addr, &readback);
    }
    if (result == 0 && readback != size_kb) {
        result = -1;
    }
    if (result == 0) {
        cache[sn] = (uint16_t)size_kb * 1024u;
    }
    WIZCHIP_GLOBAL_UNLOCK();

    return result;
}

int8_t wizchip_set_tx_buf_size(uint8_t sn, uint8_t size_kb) {
    return wizchip_set_buf_size(sn, size_kb, Sn_TXBUF_SIZE(sn),
                                wizchip_txmax_cache);
}

int8_t wizchip_set_rx_buf_size(uint8_t sn, uint8_t size_kb) {
    return wizchip_set_buf_size(sn, size_kb, Sn_RXBUF_SIZE(sn),
                                wizchip_rxmax_cache);
}


int8_t getSn_TX_FSR_stable(uint8_t sn, uint16_t *fsr_out) {
    uint16_t first;
    uint16_t second;
    uint16_t third;
    uint64_t deadline;

    if (!fsr_out) {
        return SOCKERR_IO;
    }

    deadline = wizchip_deadline_abs(_WIZCHIP_POLL_MAX_);
    if (deadline == 0u) {
        deadline = _WIZCHIP_POLL_MAX_;
    }

    for (;;) {
        if (wizchip_read16_5500(Sn_TX_FSR(sn), &first) != 0u ||
            wizchip_read16_5500(Sn_TX_FSR(sn), &second) != 0u) {
            return SOCKERR_IO;
        }
        if (first == second) {
            *fsr_out = second;
            return SOCK_OK;
        }
        if (wizchip_read16_5500(Sn_TX_FSR(sn), &third) != 0u) {
            return SOCKERR_IO;
        }
        if (second == third) {
            *fsr_out = third;
            return SOCK_OK;
        }
        if (wizchip_deadline_expired(deadline)) {
            return SOCKERR_DEADLINE;
        }
    }
}

uint16_t getSn_TX_FSR(uint8_t sn) {
    uint16_t value = 0;

    (void)getSn_TX_FSR_stable(sn, &value);
    return value;
}

int8_t getSn_RX_RSR_stable(uint8_t sn, uint16_t *rsr_out) {
    uint16_t first;
    uint16_t second;
    uint16_t third;
    uint64_t deadline;

    if (!rsr_out) {
        return SOCKERR_IO;
    }

    deadline = wizchip_deadline_abs(_WIZCHIP_POLL_MAX_);
    if (deadline == 0u) {
        deadline = _WIZCHIP_POLL_MAX_;
    }

    for (;;) {
        if (wizchip_read16_5500(Sn_RX_RSR(sn), &first) != 0u ||
            wizchip_read16_5500(Sn_RX_RSR(sn), &second) != 0u) {
            return SOCKERR_IO;
        }
        if (first == second) {
            *rsr_out = second;
            return SOCK_OK;
        }
        if (wizchip_read16_5500(Sn_RX_RSR(sn), &third) != 0u) {
            return SOCKERR_IO;
        }
        if (second == third) {
            *rsr_out = third;
            return SOCK_OK;
        }
        if (wizchip_deadline_expired(deadline)) {
            return SOCKERR_DEADLINE;
        }
    }
}

uint16_t getSn_RX_RSR(uint8_t sn) {
    uint16_t value = 0;

    (void)getSn_RX_RSR_stable(sn, &value);
    return value;
}

void wiz_send_data(uint8_t sn, uint8_t *wizdata, uint16_t len) {
    uint16_t ptr = 0;
    uint32_t addrsel = 0;

    if (sn >= _WIZCHIP_SOCK_NUM_ || len == 0 || wizdata == 0) {
        return;
    }
    ptr = getSn_TX_WR(sn);
    //M20140501 : implict type casting -> explict type casting
    //addrsel = (ptr << 8) + (WIZCHIP_TXBUF_BLOCK(sn) << 3);
    addrsel = ((uint32_t)ptr << 8) + (WIZCHIP_TXBUF_BLOCK(sn) << 3);
    //
    WIZCHIP_WRITE_BUF(addrsel, wizdata, len);

    ptr += len;
    setSn_TX_WR(sn, ptr);
}

void wiz_recv_data(uint8_t sn, uint8_t *wizdata, uint16_t len) {
    uint16_t ptr = 0;
    uint32_t addrsel = 0;

    if (sn >= _WIZCHIP_SOCK_NUM_ || len == 0 || wizdata == 0) {
        return;
    }
    ptr = getSn_RX_RD(sn);
    //M20140501 : implict type casting -> explict type casting
    //addrsel = ((ptr << 8) + (WIZCHIP_RXBUF_BLOCK(sn) << 3);
    addrsel = ((uint32_t)ptr << 8) + (WIZCHIP_RXBUF_BLOCK(sn) << 3);
    //
    WIZCHIP_READ_BUF(addrsel, wizdata, len);
    ptr += len;

    setSn_RX_RD(sn, ptr);
}


void wiz_recv_ignore(uint8_t sn, uint16_t len) {
    uint16_t ptr = 0;

    if (sn >= _WIZCHIP_SOCK_NUM_ || len == 0) {
        return;
    }
    ptr = getSn_RX_RD(sn);
    ptr += len;
    setSn_RX_RD(sn, ptr);
}

uint8_t (wizchip_read16_5500)(uint32_t addr, uint16_t *out) {
    uint8_t tmp[2];

    if (!out) {
        return (uint8_t)-1;
    }
    if (wizchip_read_buf_checked(addr, tmp, 2) != 0) {
        return (uint8_t)-1;
    }
    *out = ((uint16_t)tmp[0] << 8) | tmp[1];
    return 0;
}

uint16_t wizchip_read16_5500_value(uint32_t addr) {
    uint16_t value = 0;

    (void)wizchip_read16_5500(addr, &value);
    return value;
}

int8_t wizchip_write16_5500(uint32_t addr, uint16_t value) {
    uint8_t tmp[2];
    tmp[0] = (uint8_t)(value >> 8);
    tmp[1] = (uint8_t)value;
    return wizchip_write_buf_checked(addr, tmp, 2);
}

#endif
