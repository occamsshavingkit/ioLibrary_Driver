//****************************************************************************/
//!
//! \file wizchip_conf.c
//! \brief WIZCHIP Config Header File.
//! \version 1.0.1
//! \date 2013/10/21
//! \par  Revision history
//!       <2015/02/05> Notice
//!        The version history is not updated after this point.
//!        Download the latest version directly from GitHub. Please visit the our GitHub repository for ioLibrary.
//!        >> https://github.com/Wiznet/ioLibrary_Driver
//!       <2014/05/01> V1.0.1  Refer to M20140501
//!        1. Explicit type casting in wizchip_bus_readdata() & wizchip_bus_writedata()
//            Issued by Mathias ClauBen.
//!           uint32_t type converts into ptrdiff_t first. And then recoverting it into uint8_t*
//!           For remove the warning when pointer type size is not 32bit.
//!           If ptrdiff_t doesn't support in your complier, You should must replace ptrdiff_t into your suitable pointer type.
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
//*****************************************************************************/
//A20140501 : for use the type - ptrdiff_t
#include <stddef.h>
//

#include "wizchip_conf.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "socket.h"

#undef reg_wizchip_time_cbfunc
#undef wizchip_read8_checked

void wizchip_cris_enter(void);
void wizchip_cris_exit(void);
void wizchip_cs_select(void);
void wizchip_cs_deselect(void);
iodata_t wizchip_bus_readdata(uint32_t AddrSel);
void wizchip_bus_writedata(uint32_t AddrSel, iodata_t wb);
void wizchip_bus_read_buf(uint32_t AddrSel, iodata_t* buf, int16_t len,
                          uint8_t addrinc);
void wizchip_bus_write_buf(uint32_t AddrSel, iodata_t* buf, int16_t len,
                           uint8_t addrinc);
uint8_t wizchip_spi_readbyte(void);
void wizchip_spi_writebyte(uint8_t wb);
void wizchip_spi_readburst(uint8_t* pBuf, uint16_t len);
void wizchip_spi_writeburst(uint8_t* pBuf, uint16_t len);
void wizchip_qspi_read(uint8_t opcode, uint16_t addr, uint8_t* pBuf,
                       uint16_t len);
void wizchip_qspi_write(uint8_t opcode, uint16_t addr, uint8_t* pBuf,
                        uint16_t len);
void reg_wizchip_busbuf_cbfunc(
    void (*busbuf_rb)(uint32_t AddrSel, iodata_t* pBuf, int16_t len,
                      uint8_t addrinc),
    void (*busbuf_wb)(uint32_t AddrSel, iodata_t* pBuf, int16_t len,
                      uint8_t addrinc));

/////////////
//M20150401 : Remove ; in the default callback function such as wizchip_cris_enter(), wizchip_cs_select() and etc.
/////////////

/**
    @brief Default function to enable interrupt.
    @note This function help not to access wrong address. If you do not describe this function or register any functions,
    null function is called.
*/
//void 	  wizchip_cris_enter(void)           {};
void 	  wizchip_cris_enter(void)           {}

/**
    @brief Default no-op socket lock functions.
    @note Multi-task deployments must register real implementations
    via reg_wizchip_lock_cbfunc().
*/
static void wizchip_sock_lock_default(uint8_t sn)     { (void)sn; }
static void wizchip_sock_unlock_default(uint8_t sn)   { (void)sn; }
static void wizchip_global_lock_default(void)          {}
static void wizchip_global_unlock_default(void)        {}
static uint8_t wizchip_spi_busy_default(void)           { return 0; }
static int8_t wizchip_spi_error_default(void)            { return 0; }
static void wizchip_spi_clear_default(void)              {}
static void wizchip_wdt_kick_default(void)              {}
static void wizchip_phy_callback_default(uint8_t lu)   { (void)lu; }

static void (*wizchip_wdt_kick_cb)(void)       = wizchip_wdt_kick_default;
static void (*wizchip_phy_link_cb)(uint8_t)    = wizchip_phy_callback_default;
static int8_t wizchip_previous_link = -1;

void __attribute__((weak)) wizchip_wdt_kick(void)                      { if (wizchip_wdt_kick_cb) wizchip_wdt_kick_cb(); }
void __attribute__((weak)) reg_wizchip_wdt_cbfunc(void (*kick)(void))  { wizchip_wdt_kick_cb = kick; }
void __attribute__((weak)) wizchip_phy_link_callback(uint8_t link_up)  { if (wizchip_phy_link_cb) wizchip_phy_link_cb(link_up); }
void __attribute__((weak)) reg_wizchip_phy_cbfunc(void (*cb)(uint8_t)) { wizchip_phy_link_cb = cb; }

/**
    @brief Default function to disable interrupt.
    @note This function help not to access wrong address. If you do not describe this function or register any functions,
    null function is called.
*/
//void 	  wizchip_cris_exit(void)          {};
void 	  wizchip_cris_exit(void)          {}

/**
    @brief Default function to select chip.
    @note This function help not to access wrong address. If you do not describe this function or register any functions,
    null function is called.
*/
//void 	wizchip_cs_select(void)            {};
void 	wizchip_cs_select(void)            {}

/**
    @brief Default function to deselect chip.
    @note This function help not to access wrong address. If you do not describe this function or register any functions,
    null function is called.
*/
//void 	wizchip_cs_deselect(void)          {};
void 	wizchip_cs_deselect(void)          {}

/**
    @brief Default function to read in direct or indirect interface.
    @note This function help not to access wrong address. If you do not describe this function or register any functions,
    null function is called.
*/
//M20150601 : Rename the function for integrating with W5300
//uint8_t wizchip_bus_readbyte(uint32_t AddrSel) { return * ((volatile uint8_t *)((ptrdiff_t) AddrSel)); }
iodata_t wizchip_bus_readdata(uint32_t AddrSel) {
    return * ((volatile iodata_t *)((ptrdiff_t) AddrSel));
}

/**
    @brief Default function to write in direct or indirect interface.
    @note This function help not to access wrong address. If you do not describe this function or register any functions,
    null function is called.
*/
//M20150601 : Rename the function for integrating with W5300
//void 	wizchip_bus_writebyte(uint32_t AddrSel, uint8_t wb)  { *((volatile uint8_t*)((ptrdiff_t)AddrSel)) = wb; }
void 	wizchip_bus_writedata(uint32_t AddrSel, iodata_t wb)  {
    *((volatile iodata_t*)((ptrdiff_t)AddrSel)) = wb;
}
#if 1
// 20231103 taylor
/**
    @brief Default function to read @ref iodata_t buffer by using BUS interface
    @details @ref wizchip_bus_read_buf() provides the default read @ref iodata_t data as many as <i>len</i> from BUS of @ref _WIZCHIP_.
    @param AddrSel It specifies the address of register to be read.
    @param buf It specifies your buffer pointer to be saved the read data from @ref _WIZCHIP_.
    @param len It specifies the data length to be read from @ref _WIZCHIP_.
    @param addrinc It specifies whether the address is increased by every read operation or not.\n
          0 : Not Increased \n
          1 : Increased
    @return void
    @note It can be overwritten with your function or register your functions by calling @ref reg_wizchip_bus_cbfunc().
    @sa wizchip_bus_write_buf()
*/
void wizchip_bus_read_buf(uint32_t AddrSel, iodata_t* buf, int16_t len, uint8_t addrinc) {
    uint16_t i;
    if (addrinc) {
        addrinc = sizeof(iodata_t);
    }
    for (i = 0; i < len; i++) {
        *buf++ = WIZCHIP.IF.BUS._read_data(AddrSel);
        AddrSel += (uint32_t) addrinc;
    }
}

/**
    @brief Default function to write @ref iodata_t buffer by using BUS interface.
    @details @ref wizchip_bus_write_buf() provides the default write @ref iodata_t data as many as <i>len</i> to BUS of @ref _WIZCHIP_.
    @param AddrSel It specifies the address of register to be written.
    @param buf It specifies your buffer pointer to be written to @ref _WIZCHIP_.
    @param len It specifies the data length to be written to @ref _WIZCHIP_.
    @param addrinc It specifies whether the address is increased by every write operation or not.\n
          0 : Not Increased \n
          1 : Increased
    @return void
    @note It can be overwritten with your function or register your functions by calling @ref reg_wizchip_bus_cbfunc().
    @sa wizchip_bus_read_buf()
*/
void wizchip_bus_write_buf(uint32_t AddrSel, iodata_t* buf, int16_t len, uint8_t addrinc) {
    uint16_t i;
    if (addrinc) {
        addrinc = sizeof(iodata_t);
    }
    for (i = 0; i < len ; i++) {
        WIZCHIP.IF.BUS._write_data(AddrSel, *buf++);
        AddrSel += (uint32_t)addrinc;
    }

}
#endif

/**
    @brief Default function to read in SPI interface.
    @note This function help not to access wrong address. If you do not describe this function or register any functions,
    null function is called.
*/
//uint8_t wizchip_spi_readbyte(void)        {return 0;};
uint8_t wizchip_spi_readbyte(void)        {
    return 0;
}

/**
    @brief Default function to write in SPI interface.
    @note This function help not to access wrong address. If you do not describe this function or register any functions,
    null function is called.
*/
//void 	wizchip_spi_writebyte(uint8_t wb) {};
void 	wizchip_spi_writebyte(uint8_t wb) {}

/**
    @brief Default function to burst read in SPI interface.
    @note This function help not to access wrong address. If you do not describe this function or register any functions,
    null function is called.
*/
//void 	wizchip_spi_readburst(uint8_t* pBuf, uint16_t len) 	{};
#if 1
// 20231018 taylor
void 	wizchip_spi_readburst(uint8_t* pBuf, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        *pBuf++ = WIZCHIP.IF.SPI._read_byte();
    }
}
#else
void 	wizchip_spi_readburst(uint8_t* pBuf, uint16_t len) 	{}
#endif

/**
    @brief Default function to burst write in SPI interface.
    @note This function help not to access wrong address. If you do not describe this function or register any functions,
    null function is called.
*/
//void 	wizchip_spi_writeburst(uint8_t* pBuf, uint16_t len) {};
#if 1
// 20231018 taylor
void 	wizchip_spi_writeburst(uint8_t* pBuf, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        WIZCHIP.IF.SPI._write_byte(*pBuf++);
    }
}
#else
void 	wizchip_spi_writeburst(uint8_t* pBuf, uint16_t len) {}
#endif
#if 1   //teddy 240122

/**
    @brief Default function to read in QSPI interface.
    @note This function help not to access wrong address. If you do not describe this function or register any functions,
    null function is called.
*/
void wizchip_qspi_read(uint8_t opcode, uint16_t addr, uint8_t* pBuf, uint16_t len) {}

/**
    @brief Default function to write in QSPI interface.
    @note This function help not to access wrong address. If you do not describe this function or register any functions,
    null function is called.
*/
void wizchip_qspi_write(uint8_t opcode, uint16_t addr, uint8_t* pBuf, uint16_t len) {}

#endif
/**
    @\ref _WIZCHIP instance
*/
//
//M20150401 : For a compiler didnot support a member of structure
//            Replace the assignment of struct members with the assingment of array
//
/*
    _WIZCHIP  WIZCHIP =
      {
      .id                  = _WIZCHIP_ID_,
      .if_mode             = _WIZCHIP_IO_MODE_,
      .CRIS._enter         = wizchip_cris_enter,
      .CRIS._exit          = wizchip_cris_exit,
      .CS._select          = wizchip_cs_select,
      .CS._deselect        = wizchip_cs_deselect,
      .IF.BUS._read_byte   = wizchip_bus_readbyte,
      .IF.BUS._write_byte  = wizchip_bus_writebyte
    //    .IF.SPI._read_byte   = wizchip_spi_readbyte,
    //    .IF.SPI._write_byte  = wizchip_spi_writebyte
      };
*/
_WIZCHIP  WIZCHIP = {
    _WIZCHIP_IO_MODE_,
    _WIZCHIP_ID_,
    {
        wizchip_cris_enter,
        wizchip_cris_exit
    },
    {
        wizchip_cs_select,
        wizchip_cs_deselect
    },
#if (_WIZCHIP_IO_MODE_ & _WIZCHIP_IO_MODE_SPI_)
    {
        .SPI = {
            ._read_byte   = wizchip_spi_readbyte,
            ._write_byte  = wizchip_spi_writebyte,
            ._read_burst  = 0,
            ._write_burst = 0
        }
    }
#else
    {
        {
            //M20150601 : Rename the function
            //wizchip_bus_readbyte,
            //wizchip_bus_writebyte
            wizchip_bus_readdata,
            wizchip_bus_writedata
        },

    }
#endif
    ,
    {
        ._check_busy = wizchip_spi_busy_default,
        ._check_error = wizchip_spi_error_default,
        ._clear = wizchip_spi_clear_default
    },
    {
        wizchip_sock_lock_default,
        wizchip_sock_unlock_default,
        wizchip_global_lock_default,
        wizchip_global_unlock_default
    }
};


static uint8_t    _DNS_[4];      // DNS server ip address
#if (_WIZCHIP_ == W5100 || _WIZCHIP_ == W5100S || _WIZCHIP_ == W5200 || _WIZCHIP_ == W5300 || _WIZCHIP_ == W5500)
static dhcp_mode  _DHCP_;        // DHCP mode
//teddy 240122
#elif ((_WIZCHIP_ == 6100) || (_WIZCHIP_ == 6300))
static uint8_t      _DNS6_[16];    ///< DSN server IPv6 address
static ipconf_mode  _IPMODE_;      ///< IP configuration mode
#endif

/* Cached socket buffer sizes to avoid per-operation SPI reads. */
uint16_t wizchip_txmax_cache[_WIZCHIP_SOCK_NUM_];
uint16_t wizchip_rxmax_cache[_WIZCHIP_SOCK_NUM_];

static wizchip_state_t chip_state = WIZCHIP_STATE_UNINIT;
static int8_t chip_last_error;
static wizchip_time_cbs_t time_cbs;
static wizchip_wait_hook_fn wait_hook;
static uint8_t wizchip_transport_healthy = 0;
static uint32_t configured_timeout_us;
static uint32_t poll_counter;
static wizchip_time_us_cb_t time_us_cb;
static uint32_t wrap_last_raw_us;
static uint64_t wrap_base_us;
static wizchip_timeout_config_t timeout_config = {
    10000u,
    WIZCHIP_OPERATION_TIMEOUT_DEFAULT_US,
    10000u
};
static uint32_t requested_operation_timeout_us =
    WIZCHIP_OPERATION_TIMEOUT_DEFAULT_US;

static void wizchip_update_timeout_floor_locked(uint16_t rtr,
                                                uint8_t rcr) {
    uint64_t retry_window_us =
        (uint64_t)rtr * 100u * ((uint64_t)rcr + 1u) +
        WIZCHIP_RETRY_MARGIN_US;

    if (retry_window_us > UINT32_MAX) {
        retry_window_us = UINT32_MAX;
    }
    timeout_config.operation_timeout_us = requested_operation_timeout_us;
    if (timeout_config.operation_timeout_us < retry_window_us) {
        timeout_config.operation_timeout_us = (uint32_t)retry_window_us;
    }
    configured_timeout_us = timeout_config.operation_timeout_us;
}

static void wizchip_setinterruptmask_locked(intr_kind intr);
static intr_kind wizchip_getinterruptmask_locked(void);

static void wizchip_time_source_reset(void) {
    wrap_last_raw_us = 0u;
    wrap_base_us = 0u;
}

static int8_t wizchip_time_source_active(void) {
    return (time_us_cb != 0 || time_cbs._now_us != 0) ? 1 : 0;
}

void reg_wizchip_time_cbfunc(wizchip_time_fn now_fn, wizchip_wait_fn wait_fn) {
    time_cbs._now_us = now_fn;
    time_cbs._wait_us = wait_fn;
    wait_hook = 0;
    time_us_cb = 0;
    wizchip_time_source_reset();
}

void reg_wizchip_time_hook_cbfunc(wizchip_time_fn now_fn,
                                  wizchip_wait_hook_fn wait_fn) {
    time_cbs._now_us = now_fn;
    time_cbs._wait_us = 0;
    wait_hook = wait_fn;
    time_us_cb = 0;
    wizchip_time_source_reset();
}

void reg_wizchip_time_us_cbfunc(wizchip_time_us_cb_t now_us) {
    time_us_cb = now_us;
    wizchip_time_source_reset();
}

int8_t wizchip_set_operation_timeout_us(uint32_t timeout_us) {
    uint16_t rtr;
    uint8_t rcr;

    if (timeout_us == 0u) {
        return -1;
    }
    WIZCHIP_GLOBAL_LOCK();
    rtr = getRTR();
    rcr = getRCR();
    requested_operation_timeout_us = timeout_us;
    wizchip_update_timeout_floor_locked(rtr, rcr);
    WIZCHIP_GLOBAL_UNLOCK();
    return 0;
}

int8_t wizchip_deadline_config_valid(void) {
    if (wizchip_time_source_active() == 0) {
        return 0;
    }
    if (timeout_config.command_timeout_us == 0u ||
        timeout_config.operation_timeout_us == 0u) {
        return 0;
    }
    return 1;
}

uint8_t wizchip_timeout_config_set(uint32_t timeout_us) {
    if (timeout_us == 0u) {
        return 1u;
    }
    WIZCHIP_GLOBAL_LOCK();
    configured_timeout_us = timeout_us;
    WIZCHIP_GLOBAL_UNLOCK();
    return 0u;
}

uint64_t wizchip_deadline_abs(uint64_t timeout_us) {
    if (wizchip_time_source_active()) {
        return wizchip_time_now() + timeout_us;
    }
    poll_counter = 0u;
    configured_timeout_us = timeout_us > UINT32_MAX
                            ? UINT32_MAX
                            : (uint32_t)timeout_us;
    return 0u;
}

int8_t wizchip_deadline_expired(uint64_t deadline_us) {
    if (wizchip_time_source_active()) {
        return wizchip_time_now() >= deadline_us;
    }
    ++poll_counter;
    return poll_counter >= deadline_us;
}

/* Reconstruct a monotonic 64-bit timeline from a source that may wrap at 2^32
 * microseconds, so absolute deadlines stay comparable across a wrap.  The
 * source is treated as 32-bit whichever callback supplied it; a genuine 64-bit
 * source never decreases in its low word without having wrapped.  Callers must
 * poll more often than once per wrap period, about 71 minutes. */
uint64_t wizchip_time_now(void) {
    uint32_t raw;

    if (time_us_cb) {
        raw = time_us_cb();
    } else if (time_cbs._now_us) {
        raw = (uint32_t)time_cbs._now_us();
    } else {
        return poll_counter;
    }
    if (raw < wrap_last_raw_us) {
        wrap_base_us += 0x100000000ull;
    }
    wrap_last_raw_us = raw;
    return wrap_base_us + (uint64_t)raw;
}

int8_t wizchip_set_timeout_config(const wizchip_timeout_config_t *config) {
    uint16_t rtr;
    uint8_t rcr;

    if (!config || config->command_timeout_us == 0u ||
        config->operation_timeout_us == 0u || config->phy_timeout_us == 0u) {
        return -1;
    }

    WIZCHIP_GLOBAL_LOCK();
    rtr = getRTR();
    rcr = getRCR();
    timeout_config = *config;
    requested_operation_timeout_us = config->operation_timeout_us;
    wizchip_update_timeout_floor_locked(rtr, rcr);
    WIZCHIP_GLOBAL_UNLOCK();
    return 0;
}

int8_t wizchip_get_timeout_config(wizchip_timeout_config_t *config) {
    if (!config) {
        return -1;
    }
    *config = timeout_config;
    return 0;
}

void wizchip_deadline_start(wizchip_deadline_t *deadline,
                            uint64_t timeout_us) {
    if (!deadline) {
        return;
    }
    deadline->started_us = wizchip_time_now();
    deadline->deadline_us = wizchip_deadline_abs(timeout_us);
    deadline->timeout_us = timeout_us;
    deadline->polls = 0u;
}

int8_t wizchip_deadline_poll(wizchip_deadline_t *deadline) {
    if (!deadline) {
        return -16;
    }

    if (time_cbs._wait_us) {
        time_cbs._wait_us(1u);
    } else if (wait_hook) {
        wait_hook();
    }

    ++deadline->polls;
    if (deadline->polls >= _WIZCHIP_POLL_MAX_) {
        return -16;
    }
    if (wizchip_time_source_active() &&
        (wizchip_time_now() - deadline->started_us) >= deadline->timeout_us) {
        return -16;
    }
    return 1;
}

wizchip_state_t wizchip_get_state(void) {
    return chip_state;
}

void wizchip_mark_faulted(void) {
    chip_state = WIZCHIP_STATE_FAULTED;
}

int8_t wizchip_get_last_error(void) {
    return chip_last_error;
}

void wizchip_set_last_error(int8_t error) {
    chip_last_error = error;
}

void wizchip_clear_last_error(void) {
    chip_last_error = 0;
}

int8_t wizchip_recover(void) {
    if (chip_state == WIZCHIP_STATE_READY) {
        wizchip_clear_spi_error();
        chip_last_error = 0;
        return 0;
    }
    if (chip_state != WIZCHIP_STATE_FAULTED) {
        return -1;
    }
    wizchip_clear_spi_error();
    chip_last_error = 0;
    chip_state = WIZCHIP_STATE_READY;
    return 0;
}

uint8_t wizchip_get_spi_error(void) {
    if (WIZCHIP.SPISTATUS._check_error) {
        return WIZCHIP.SPISTATUS._check_error();
    }
    return 0u;
}

void wizchip_clear_spi_error(void) {
    if (WIZCHIP.SPISTATUS._clear) {
        WIZCHIP.SPISTATUS._clear();
    }
}

void wizchip_invalidate_transport_cache(void) {
    wizchip_transport_healthy = 0;
}

static int8_t wizchip_spi_fault(void) {
    chip_state = WIZCHIP_STATE_FAULTED;
    chip_last_error = SOCKERR_IO;
    return SOCKERR_IO;
}

static void wizchip_spi_write_header(uint32_t addr, uint8_t control) {
    uint8_t header[3];

    addr |= control;
    header[0] = (uint8_t)(addr >> 16);
    header[1] = (uint8_t)(addr >> 8);
    header[2] = (uint8_t)addr;
    if (WIZCHIP.IF.SPI._write_burst) {
        WIZCHIP.IF.SPI._write_burst(header, sizeof(header));
    } else {
        WIZCHIP.IF.SPI._write_byte(header[0]);
        WIZCHIP.IF.SPI._write_byte(header[1]);
        WIZCHIP.IF.SPI._write_byte(header[2]);
    }
}

int8_t wizchip_read8_checked_out(uint32_t addr, uint8_t *out) {
    if (!out) {
        return SOCKERR_ARG;
    }
    WIZCHIP.CRIS._enter();
    wizchip_clear_spi_error();
    WIZCHIP.CS._select();
    wizchip_spi_write_header(addr, _W5500_SPI_READ_);
    *out = WIZCHIP.IF.SPI._read_byte();
    WIZCHIP.CS._deselect();
#ifdef WIZCHIP_SAFE_SPI
    if (WIZCHIP_SPI_BUSY_CHECK() || WIZCHIP_SPI_ERROR_CHECK()) {
        WIZCHIP.CRIS._exit();
        return wizchip_spi_fault();
    }
#else
    if (!wizchip_transport_healthy) {
        if (WIZCHIP_SPI_BUSY_CHECK() || WIZCHIP_SPI_ERROR_CHECK()) {
            WIZCHIP.CRIS._exit();
            return wizchip_spi_fault();
        }
        wizchip_transport_healthy = 1;
    }
#endif
    WIZCHIP.CRIS._exit();
    return 0;
}

uint8_t wizchip_read8_checked(uint32_t addr) {
    uint8_t value = 0xFF;

    (void)wizchip_read8_checked_out(addr, &value);
    return value;
}

int8_t wizchip_write8_checked(uint32_t addr, uint8_t data) {
    uint8_t frame[4];

    addr |= _W5500_SPI_WRITE_;
    frame[0] = (uint8_t)(addr >> 16);
    frame[1] = (uint8_t)(addr >> 8);
    frame[2] = (uint8_t)addr;
    frame[3] = data;

    WIZCHIP.CRIS._enter();
    wizchip_transport_healthy = 0;
    wizchip_clear_spi_error();
    WIZCHIP.CS._select();
    if (WIZCHIP.IF.SPI._write_burst) {
        WIZCHIP.IF.SPI._write_burst(frame, sizeof(frame));
    } else {
        WIZCHIP.IF.SPI._write_byte(frame[0]);
        WIZCHIP.IF.SPI._write_byte(frame[1]);
        WIZCHIP.IF.SPI._write_byte(frame[2]);
        WIZCHIP.IF.SPI._write_byte(frame[3]);
    }
    WIZCHIP.CS._deselect();
#ifdef WIZCHIP_SAFE_SPI
    if (WIZCHIP_SPI_BUSY_CHECK() || WIZCHIP_SPI_ERROR_CHECK()) {
        WIZCHIP.CRIS._exit();
        return wizchip_spi_fault();
    }
#endif
    WIZCHIP.CRIS._exit();
    return 0;
}

int8_t wizchip_read_buf_checked(uint32_t addr, uint8_t *buf, uint16_t len) {
    uint16_t i;

    if (len == 0u) {
        return 0;
    }
    if (!buf) {
        return SOCKERR_ARG;
    }

    WIZCHIP.CRIS._enter();
    wizchip_clear_spi_error();
    WIZCHIP.CS._select();
    wizchip_spi_write_header(addr, _W5500_SPI_READ_);
    if (WIZCHIP.IF.SPI._read_burst) {
        WIZCHIP.IF.SPI._read_burst(buf, len);
    } else {
        for (i = 0; i < len; ++i) {
            buf[i] = WIZCHIP.IF.SPI._read_byte();
        }
    }
    WIZCHIP.CS._deselect();
#ifdef WIZCHIP_SAFE_SPI
    if (WIZCHIP_SPI_BUSY_CHECK() || WIZCHIP_SPI_ERROR_CHECK()) {
        WIZCHIP.CRIS._exit();
        return wizchip_spi_fault();
    }
#else
    if (!wizchip_transport_healthy) {
        if (WIZCHIP_SPI_BUSY_CHECK() || WIZCHIP_SPI_ERROR_CHECK()) {
            WIZCHIP.CRIS._exit();
            return wizchip_spi_fault();
        }
        wizchip_transport_healthy = 1;
    }
#endif
    WIZCHIP.CRIS._exit();
    return 0;
}

int8_t wizchip_write_buf_checked(uint32_t addr, const uint8_t *buf,
                                 uint16_t len) {
    uint16_t i;

    if (len == 0u) {
        return 0;
    }
    if (!buf) {
        return SOCKERR_ARG;
    }

    WIZCHIP.CRIS._enter();
    wizchip_transport_healthy = 0;
    wizchip_clear_spi_error();
    WIZCHIP.CS._select();
    wizchip_spi_write_header(addr, _W5500_SPI_WRITE_);
    if (WIZCHIP.IF.SPI._write_burst) {
        WIZCHIP.IF.SPI._write_burst((uint8_t *)buf, len);
    } else {
        for (i = 0; i < len; ++i) {
            WIZCHIP.IF.SPI._write_byte(buf[i]);
        }
    }
    WIZCHIP.CS._deselect();
#ifdef WIZCHIP_SAFE_SPI
    if (WIZCHIP_SPI_BUSY_CHECK() || WIZCHIP_SPI_ERROR_CHECK()) {
        WIZCHIP.CRIS._exit();
        return wizchip_spi_fault();
    }
#endif
    WIZCHIP.CRIS._exit();
    return 0;
}

void reg_wizchip_cris_cbfunc(void(*cris_en)(void), void(*cris_ex)(void)) {
    if (!cris_en || !cris_ex) {
        WIZCHIP.CRIS._enter = wizchip_cris_enter;
        WIZCHIP.CRIS._exit  = wizchip_cris_exit;
    } else {
        WIZCHIP.CRIS._enter = cris_en;
        WIZCHIP.CRIS._exit  = cris_ex;
    }
}

void reg_wizchip_cs_cbfunc(void(*cs_sel)(void), void(*cs_desel)(void)) {
    if (!cs_sel || !cs_desel) {
        WIZCHIP.CS._select   = wizchip_cs_select;
        WIZCHIP.CS._deselect = wizchip_cs_deselect;
    } else {
        WIZCHIP.CS._select   = cs_sel;
        WIZCHIP.CS._deselect = cs_desel;
    }
}

//M20150515 : For integrating with W5300
//void reg_wizchip_bus_cbfunc(uint8_t(*bus_rb)(uint32_t addr), void (*bus_wb)(uint32_t addr, uint8_t wb))
void reg_wizchip_bus_cbfunc(iodata_t(*bus_rb)(uint32_t addr), void (*bus_wb)(uint32_t addr, iodata_t wb)) {
    if (!(WIZCHIP.if_mode & _WIZCHIP_IO_MODE_BUS_)) return;
    //M20150601 : Rename call back function for integrating with W5300
    /*
        if(!bus_rb || !bus_wb)
        {
        WIZCHIP.IF.BUS._read_byte   = wizchip_bus_readbyte;
        WIZCHIP.IF.BUS._write_byte  = wizchip_bus_writebyte;
        }
        else
        {
        WIZCHIP.IF.BUS._read_byte   = bus_rb;
        WIZCHIP.IF.BUS._write_byte  = bus_wb;
        }
    */
    if (!bus_rb || !bus_wb) {
        WIZCHIP.IF.BUS._read_data   = wizchip_bus_readdata;
        WIZCHIP.IF.BUS._write_data  = wizchip_bus_writedata;
    } else {
        WIZCHIP.IF.BUS._read_data   = bus_rb;
        WIZCHIP.IF.BUS._write_data  = bus_wb;
    }
}
#if 1
// 20231103 taylor
void reg_wizchip_busbuf_cbfunc(void(*busbuf_rb)(uint32_t AddrSel, iodata_t* pBuf, int16_t len, uint8_t addrinc), void (*busbuf_wb)(uint32_t AddrSel, iodata_t* pBuf, int16_t len, uint8_t addrinc)) {
    if (!(WIZCHIP.if_mode & _WIZCHIP_IO_MODE_BUS_)) return;
    //M20150601 : Rename call back function for integrating with W5300
    /*
        if(!bus_rb || !bus_wb)
        {
        WIZCHIP.IF.BUS._read_byte   = wizchip_bus_readbyte;
        WIZCHIP.IF.BUS._write_byte  = wizchip_bus_writebyte;
        }
        else
        {
        WIZCHIP.IF.BUS._read_byte   = bus_rb;
        WIZCHIP.IF.BUS._write_byte  = bus_wb;
        }
    */
    if (!busbuf_rb || !busbuf_wb) {
        WIZCHIP.IF.BUS._read_data_buf   = wizchip_bus_read_buf;
        WIZCHIP.IF.BUS._write_data_buf  = wizchip_bus_write_buf;
    } else {
        WIZCHIP.IF.BUS._read_data_buf   = busbuf_rb;
        WIZCHIP.IF.BUS._write_data_buf  = busbuf_wb;
    }
}
#endif

#if _WIZCHIP_ == W6100

void reg_wizchip_spi_cbfunc(uint8_t (*spi_rb)(void),
                            void (*spi_wb)(uint8_t wb),
                            void (*spi_rbuf)(uint8_t* buf, datasize_t len),
                            void (*spi_wbuf)(uint8_t* buf, datasize_t len)) {
    while (!(WIZCHIP.if_mode & _WIZCHIP_IO_MODE_SPI_));

    if (!spi_rb) {
        WIZCHIP.IF.SPI._read_byte      = wizchip_spi_readbyte;
    } else {
        WIZCHIP.IF.SPI._read_byte      = spi_rb;
    }
    if (!spi_wb) {
        WIZCHIP.IF.SPI._write_byte     = wizchip_spi_writebyte;
    } else {
        WIZCHIP.IF.SPI._write_byte     = spi_wb;
    }

    if (!spi_rbuf) {
        WIZCHIP.IF.SPI._read_burst  = wizchip_spi_readburst;
    } else {
        WIZCHIP.IF.SPI._read_burst  = spi_rbuf;
    }
    if (!spi_wbuf) {
        WIZCHIP.IF.SPI._write_burst = wizchip_spi_writeburst;
    } else {
        WIZCHIP.IF.SPI._write_burst = spi_wbuf;
    }
}
#else

void reg_wizchip_spi_cbfunc(uint8_t (*spi_rb)(void), void (*spi_wb)(uint8_t wb)) {
    while (!(WIZCHIP.if_mode & _WIZCHIP_IO_MODE_SPI_));

    if (!spi_rb || !spi_wb) {
        WIZCHIP.IF.SPI._read_byte   = wizchip_spi_readbyte;
        WIZCHIP.IF.SPI._write_byte  = wizchip_spi_writebyte;
    } else {
        WIZCHIP.IF.SPI._read_byte   = spi_rb;
        WIZCHIP.IF.SPI._write_byte  = spi_wb;
    }
}
#endif

// 20140626 Eric Added for SPI burst operations
void reg_wizchip_spiburst_cbfunc(void (*spi_rb)(uint8_t* pBuf, uint16_t len), void (*spi_wb)(uint8_t* pBuf, uint16_t len)) {
    while (!(WIZCHIP.if_mode & _WIZCHIP_IO_MODE_SPI_));

    WIZCHIP.IF.SPI._read_burst = spi_rb;
    WIZCHIP.IF.SPI._write_burst = spi_wb;
}

void reg_wizchip_spistatus_cbfunc(
    uint8_t (*busy_cb)(void),
    int8_t (*error_cb)(void),
    void (*clear_cb)(void))
{
    uint8_t (*busy)(void) = busy_cb ? busy_cb : wizchip_spi_busy_default;
    int8_t (*error)(void) = error_cb ? error_cb : wizchip_spi_error_default;
    void (*clear)(void) = clear_cb ? clear_cb : wizchip_spi_clear_default;

    WIZCHIP.CRIS._enter();
    WIZCHIP.SPISTATUS._check_busy = busy;
    WIZCHIP.SPISTATUS._check_error = error;
    WIZCHIP.SPISTATUS._clear = clear;
    WIZCHIP.CRIS._exit();
}
#if 1 //teddy 240122
void reg_wizchip_qspi_cbfunc(void (*qspi_rb)(uint8_t opcode, uint16_t addr, uint8_t* pBuf, uint16_t len),
                             void (*qspi_wb)(uint8_t opcode, uint16_t addr, uint8_t* pBuf, uint16_t len)) {
    while (!(WIZCHIP.if_mode & _WIZCHIP_IO_MODE_SPI_QSPI_));

    if (!qspi_rb || !qspi_wb) {
        WIZCHIP.IF.QSPI._read_qspi   = wizchip_qspi_read;
        WIZCHIP.IF.QSPI._write_qspi  = wizchip_qspi_write;
    } else {
        WIZCHIP.IF.QSPI._read_qspi   = qspi_rb;
        WIZCHIP.IF.QSPI._write_qspi  = qspi_wb;
    }
}
#endif

/**
    @brief Register socket and global concurrency lock callbacks.
    @details Multi-task deployments must call this to replace the default
    no-op implementations with real mutex/semaphore locks. Per-socket
    locks serialize operations on one socket; the global lock protects
    cross-socket shared state.
*/
int8_t reg_wizchip_lock_cbfunc(
    void (*sock_enter)(uint8_t sn),
    void (*sock_exit)(uint8_t sn),
    void (*global_enter)(void),
    void (*global_exit)(void))
{
    int8_t result = 0;

    if ((sock_enter == NULL) != (sock_exit == NULL)) {
        WIZCHIP.LOCK._sock_enter = wizchip_sock_lock_default;
        WIZCHIP.LOCK._sock_exit = wizchip_sock_unlock_default;
        result = -1;
    } else if (sock_enter != NULL) {
        WIZCHIP.LOCK._sock_enter = sock_enter;
        WIZCHIP.LOCK._sock_exit = sock_exit;
    } else {
        WIZCHIP.LOCK._sock_enter = wizchip_sock_lock_default;
        WIZCHIP.LOCK._sock_exit = wizchip_sock_unlock_default;
    }

    if ((global_enter == NULL) != (global_exit == NULL)) {
        WIZCHIP.LOCK._global_enter = wizchip_global_lock_default;
        WIZCHIP.LOCK._global_exit = wizchip_global_unlock_default;
        result = -1;
    } else if (global_enter != NULL) {
        WIZCHIP.LOCK._global_enter = global_enter;
        WIZCHIP.LOCK._global_exit = global_exit;
    } else {
        WIZCHIP.LOCK._global_enter = wizchip_global_lock_default;
        WIZCHIP.LOCK._global_exit = wizchip_global_unlock_default;
    }

    return result;
}

int8_t ctlwizchip(ctlwizchip_type cwtype, void* arg) {
    uint8_t* ptmp[2] = {0, 0};
    switch (cwtype) {
        //teddy 240122
#if _WIZCHIP_ == W6100 || _WIZCHIP_ == W6300
    case CW_SYS_LOCK:
        if (arg == 0) return -1;
        {
            uint8_t tmp = *(uint8_t*)arg;
            if (tmp & SYS_CHIP_LOCK) {
                CHIPLOCK();
            }
            if (tmp & SYS_NET_LOCK) {
                NETLOCK();
            }
            if (tmp & SYS_PHY_LOCK) {
                PHYLOCK();
            }
        }
        break;
    case CW_SYS_UNLOCK:
        if (arg == 0) return -1;
        {
            uint8_t tmp = *(uint8_t*)arg;
            if (tmp & SYS_CHIP_LOCK) {
                CHIPUNLOCK();
            }
            if (tmp & SYS_NET_LOCK) {
                NETUNLOCK();
            }
            if (tmp & SYS_PHY_LOCK) {
                PHYUNLOCK();
            }
        }
        break;
    case CW_GET_SYSLOCK:
        if (arg == 0) return -1;
        *(uint8_t*)arg = getSYSR() >> 5;
        break;
#endif
    case CW_RESET_WIZCHIP:
        {
            int8_t ret = wizchip_sw_reset();
            if (ret != 0) return ret;
        }
        break;
    case CW_INIT_WIZCHIP:
        if (arg != 0) {
            ptmp[0] = (uint8_t*)arg;
            ptmp[1] = ptmp[0] + _WIZCHIP_SOCK_NUM_;
        }
        return wizchip_init(ptmp[0], ptmp[1]);
    case CW_CLR_INTERRUPT:
        if (arg == 0) return -1;
        WIZCHIP_GLOBAL_LOCK();
        wizchip_clrinterrupt(*((intr_kind*)arg));
        WIZCHIP_GLOBAL_UNLOCK();
        break;
    case CW_GET_INTERRUPT:
        if (arg == 0) return -1;
        WIZCHIP_GLOBAL_LOCK();
        *((intr_kind*)arg) = wizchip_getinterrupt();
        WIZCHIP_GLOBAL_UNLOCK();
        break;
    case CW_SET_INTRMASK:
        if (arg == 0) return -1;
        WIZCHIP_GLOBAL_LOCK();
        wizchip_setinterruptmask_locked(*((intr_kind*)arg));
        WIZCHIP_GLOBAL_UNLOCK();
        break;
    case CW_GET_INTRMASK:
        if (arg == 0) return -1;
        WIZCHIP_GLOBAL_LOCK();
        *((intr_kind*)arg) = wizchip_getinterruptmask_locked();
        WIZCHIP_GLOBAL_UNLOCK();
        break;
        //M20150601 : This can be supported by W5200, W5500
        //#if _WIZCHIP_ > W5100
#if (_WIZCHIP_ == W5200 || _WIZCHIP_ == W5500)
    case CW_SET_INTRTIME:
        setINTLEVEL(*(uint16_t*)arg);
        break;
    case CW_GET_INTRTIME:
        *(uint16_t*)arg = getINTLEVEL();
        break;
        //teddy 240122
#elif ((_WIZCHIP_ == W6100) || (_WIZCHIP_ == W6300))
    case CW_SET_INTRTIME:
        setINTPTMR(*(uint16_t*)arg);
        break;
    case CW_GET_INTRTIME:
        *(uint16_t*)arg = getINTPTMR();
        break;
#endif
    case CW_GET_ID:
        ((uint8_t*)arg)[0] = WIZCHIP.id[0];
        ((uint8_t*)arg)[1] = WIZCHIP.id[1];
        ((uint8_t*)arg)[2] = WIZCHIP.id[2];
        ((uint8_t*)arg)[3] = WIZCHIP.id[3];
        ((uint8_t*)arg)[4] = WIZCHIP.id[4];
        ((uint8_t*)arg)[5] = WIZCHIP.id[5];
        ((uint8_t*)arg)[6] = 0;
        break;
#if 1
        // 20231017 taylor//teddy 240122
#if _WIZCHIP_ == W6100 || _WIZCHIP_ == W6300
    case CW_GET_VER:
        *(uint16_t*)arg = getVER();
        break;
#endif
#endif
        //teddy 240122
#if _WIZCHIP_ == W5100S || _WIZCHIP_ == W5500 || _WIZCHIP_ == W6100 || _WIZCHIP_ == W6300
    case CW_RESET_PHY:
        return wizphy_reset();
    case CW_SET_PHYCONF:
        return wizphy_setphyconf((wiz_PhyConf*)arg);
    case CW_GET_PHYCONF:
        return wizphy_getphyconf((wiz_PhyConf*)arg);
    case CW_GET_PHYSTATUS:
#if 1
        // 20231012 taylor
#if _WIZCHIP_ == W5500
        return wizphy_getphystat((wiz_PhyConf*)arg);
#endif
#else
        return wizphy_getphystat((wiz_PhyConf*)arg);
#endif
        break;
    case CW_SET_PHYPOWMODE:
        if (arg == 0) return -1;
        //teddy 240122
#if _WIZCHIP_ == W6100 ||_WIZCHIP_ == W6300
        return wizphy_setphypmode(*(uint8_t*)arg);
#else
        return wizphy_setphypmode(*(uint8_t*)arg);
#endif
#endif
        //teddy 240122
#if _WIZCHIP_ == W5100S || _WIZCHIP_ == W5200 || _WIZCHIP_ == W5500 || _WIZCHIP_ == W6100 || _WIZCHIP_ == W6300
    case CW_GET_PHYPOWMODE:
        if (arg == 0) return -1;
        {
            uint8_t tmp = wizphy_getphypmode();
            if ((int8_t)tmp == -1) {
                return -1;
            }
            *(uint8_t*)arg = tmp;
        }
        break;
    case CW_GET_PHYLINK:
        if (arg == 0) return -1;
        {
            uint8_t tmp = wizphy_getphylink();
            if ((int8_t)tmp == -1) {
                return -1;
            }
            *(uint8_t*)arg = tmp;
        }
        break;
#endif
    default:
        return -1;
    }
    return 0;
}


static void wizchip_setnetinfo_locked(wiz_NetInfo* pnetinfo);
static void wizchip_getnetinfo_locked(wiz_NetInfo* pnetinfo);
static void wizchip_settimeout_locked(wiz_NetTimeout* nettime);
static void wizchip_gettimeout_locked(wiz_NetTimeout* nettime);

int8_t ctlnetwork(ctlnetwork_type cntype, void* arg) {

    switch (cntype) {
    case CN_SET_NETINFO:
        WIZCHIP_GLOBAL_LOCK();
        wizchip_setnetinfo_locked((wiz_NetInfo*)arg);
        WIZCHIP_GLOBAL_UNLOCK();
        break;
    case CN_GET_NETINFO:
        WIZCHIP_GLOBAL_LOCK();
        wizchip_getnetinfo_locked((wiz_NetInfo*)arg);
        WIZCHIP_GLOBAL_UNLOCK();
        break;
    case CN_SET_NETMODE:
#if (_WIZCHIP_ == W5100 || _WIZCHIP_ == W5100S || _WIZCHIP_ == W5200 || _WIZCHIP_ == W5300 || _WIZCHIP_ == W5500)
        return wizchip_setnetmode(*(netmode_type*)arg);
        //teddy 240122
#elif ((_WIZCHIP_ == 6100)||(_WIZCHIP_ == W6300))
        wizchip_setnetmode(*(netmode_type*)arg);
#endif
    case CN_GET_NETMODE:
        *(netmode_type*)arg = wizchip_getnetmode();
        break;
    case CN_SET_TIMEOUT:
        WIZCHIP_GLOBAL_LOCK();
        wizchip_settimeout_locked((wiz_NetTimeout*)arg);
        WIZCHIP_GLOBAL_UNLOCK();
        break;
    case CN_GET_TIMEOUT:
        WIZCHIP_GLOBAL_LOCK();
        wizchip_gettimeout_locked((wiz_NetTimeout*)arg);
        WIZCHIP_GLOBAL_UNLOCK();
        break;
        //teddy 240122
#if ((_WIZCHIP_ == 6100)||(_WIZCHIP_ == 6300))
    case CN_SET_PREFER:
        setSLPSR(*(uint8_t*)arg);
        break;
    case CN_GET_PREFER:
        *(uint8_t*)arg = getSLPSR();
        break;
#endif
    default:
        return -1;
    }
    return 0;
}

static void wizchip_transaction_lock(void) {
    uint8_t sn;

    WIZCHIP_GLOBAL_LOCK();
    for (sn = 0; sn < _WIZCHIP_SOCK_NUM_; ++sn) {
        WIZCHIP_SOCK_LOCK(sn);
    }
}

static void wizchip_transaction_unlock(void) {
    uint8_t sn = _WIZCHIP_SOCK_NUM_;

    while (sn != 0U) {
        --sn;
        WIZCHIP_SOCK_UNLOCK(sn);
    }
    WIZCHIP_GLOBAL_UNLOCK();
}

static void wizchip_transaction_reset_socket_state(void) {
    uint8_t sn;

    for (sn = 0; sn < _WIZCHIP_SOCK_NUM_; ++sn) {
        wizchip_socket_state_reset_one(sn);
        wizchip_txmax_cache[sn] = 0U;
        wizchip_rxmax_cache[sn] = 0U;
    }
}

static int8_t wizchip_transaction_finish(int8_t ret) {
    wizchip_transaction_unlock();
    if (ret == 0) {
        chip_state = WIZCHIP_STATE_READY;
        chip_last_error = 0;
    } else {
        chip_state = WIZCHIP_STATE_FAULTED;
        if (chip_last_error == 0) {
            chip_last_error = ret;
        }
    }
    return ret;
}

static int8_t wizchip_refresh_socket_caches(void) {
    uint8_t sn;

    for (sn = 0; sn < _WIZCHIP_SOCK_NUM_; ++sn) {
#if _WIZCHIP_ == W5500
        uint8_t tx_size;
        uint8_t rx_size;

        if (wizchip_read8_checked_out(Sn_TXBUF_SIZE(sn), &tx_size) != 0 ||
            wizchip_read8_checked_out(Sn_RXBUF_SIZE(sn), &rx_size) != 0) {
            return SOCKERR_IO;
        }
        wizchip_txmax_cache[sn] = (uint16_t)tx_size * 1024U;
        wizchip_rxmax_cache[sn] = (uint16_t)rx_size * 1024U;
#else
        wizchip_txmax_cache[sn] =
            (uint16_t)WIZCHIP_READ(Sn_TXBUF_SIZE(sn)) * 1024U;
        wizchip_rxmax_cache[sn] =
            (uint16_t)WIZCHIP_READ(Sn_RXBUF_SIZE(sn)) * 1024U;
        if (chip_state == WIZCHIP_STATE_FAULTED) {
            return SOCKERR_IO;
        }
#endif
    }
    return 0;
}

static int8_t wizchip_sw_reset_locked(void) {
    uint8_t gw[4], sn[4], sip[4];
    uint8_t mac[6];
    int8_t ret = 0;

    wizchip_previous_link = -1;
    //teddy 240122
#if ((_WIZCHIP_ == 6100) ||(_WIZCHIP_ == 6300))
    uint8_t gw6[16], sn6[16], lla[16], gua[16];
    uint8_t islock = getSYSR();
#endif

#if _WIZCHIP_ == W5500
    uint8_t mr;

    if (wizchip_read_buf_checked(SHAR, mac, sizeof(mac)) != 0 ||
        wizchip_read_buf_checked(GAR, gw, sizeof(gw)) != 0 ||
        wizchip_read_buf_checked(SUBR, sn, sizeof(sn)) != 0 ||
        wizchip_read_buf_checked(SIPR, sip, sizeof(sip)) != 0 ||
        wizchip_write8_checked(MR, MR_RST) != 0 ||
        wizchip_read8_checked_out(MR, &mr) != 0 ||
        wizchip_write_buf_checked(SHAR, mac, sizeof(mac)) != 0 ||
        wizchip_write_buf_checked(GAR, gw, sizeof(gw)) != 0 ||
        wizchip_write_buf_checked(SUBR, sn, sizeof(sn)) != 0 ||
        wizchip_write_buf_checked(SIPR, sip, sizeof(sip)) != 0) {
        return SOCKERR_IO;
    }
#elif (_WIZCHIP_ == W5100 || _WIZCHIP_ == W5100S || _WIZCHIP_ == W5200 || _WIZCHIP_ == W5300)
    //A20150601
#if _WIZCHIP_IO_MODE_  == _WIZCHIP_IO_MODE_BUS_INDIR_
    uint16_t mr = (uint16_t)getMR();
    setMR(mr | MR_IND);
#endif
    //
    getSHAR(mac);
    getGAR(gw);  getSUBR(sn);  getSIPR(sip);
    setMR(MR_RST);
    getMR(); // for delay
    //A2015051 : For indirect bus mode
#if _WIZCHIP_IO_MODE_  == _WIZCHIP_IO_MODE_BUS_INDIR_
    setMR(mr | MR_IND);
#endif
    //
    setSHAR(mac);
    setGAR(gw);
    setSUBR(sn);
    setSIPR(sip);
    //teddy 240122
#elif ((_WIZCHIP_ == W6100)||(_WIZCHIP_ == W6300))
    CHIPUNLOCK();

    getSHAR(mac);
    getGAR(gw);  getSUBR(sn);  getSIPR(sip); getGA6R(gw6); getSUB6R(sn6); getLLAR(lla); getGUAR(gua);
    setSYCR0(SYCR0_RST);
    getSYCR0(); // for delay

    NETUNLOCK();

    setSHAR(mac);
    setGAR(gw);
    setSUBR(sn);
    setSIPR(sip);
    setGA6R(gw6);
    setSUB6R(sn6);
    setLLAR(lla);
    setGUAR(gua);
    if (islock & SYSR_CHPL) {
        CHIPLOCK();
    }
    if (islock & SYSR_NETL) {
        NETLOCK();
    }
#endif
    /* Verify chip identity — VERSIONR must read 0x04 */
    {
        uint32_t _poll = 0;
#if _WIZCHIP_ == W5500
        uint8_t version;

        do {
            if (wizchip_read8_checked_out(VERSIONR, &version) != 0) {
                return SOCKERR_IO;
            }
            ret = (version == 0x04U) ? 0 : -1;
        } while (ret != 0 && ++_poll < _WIZCHIP_POLL_MAX_);
#else
        do {
            ret = (getVERSIONR() == 0x04) ? 0 : -1;
        } while (ret != 0 && ++_poll < _WIZCHIP_POLL_MAX_);
#endif
    }
    return ret;
}

int8_t wizchip_sw_reset(void) {
    int8_t ret;

    wizchip_transaction_lock();
    chip_state = WIZCHIP_STATE_UNINIT;
    chip_last_error = 0;
    wizchip_transaction_reset_socket_state();
    ret = wizchip_sw_reset_locked();
    if (ret == 0) {
        ret = wizchip_refresh_socket_caches();
    }
    return wizchip_transaction_finish(ret);
}

int8_t wizchip_init(uint8_t* txsize, uint8_t* rxsize) {
    int8_t i;
#if _WIZCHIP_ < W5200
    int8_t j;
#endif
    int8_t tmp = 0;
    uint16_t tx_total = 0, rx_total = 0;

    /* Validate buffer arrays before any hardware access (AUD-013) */
    if (txsize) {
        for (i = 0; i < _WIZCHIP_SOCK_NUM_; i++) {
#if _WIZCHIP_ == W5500
            if (txsize[i] != 0 && txsize[i] != 2 && txsize[i] != 4 &&
                txsize[i] != 8 && txsize[i] != 16) {
#else
            if (txsize[i] != 0 && txsize[i] != 1 && txsize[i] != 2 &&
                txsize[i] != 4 && txsize[i] != 8 && txsize[i] != 16) {
#endif
                return -1;
            }
            tx_total += txsize[i];
            if (tx_total > 16) return -1;
        }
    }
    if (rxsize) {
        for (i = 0; i < _WIZCHIP_SOCK_NUM_; i++) {
#if _WIZCHIP_ == W5500
            if (rxsize[i] != 0 && rxsize[i] != 2 && rxsize[i] != 4 &&
                rxsize[i] != 8 && rxsize[i] != 16) {
#else
            if (rxsize[i] != 0 && rxsize[i] != 1 && rxsize[i] != 2 &&
                rxsize[i] != 4 && rxsize[i] != 8 && rxsize[i] != 16) {
#endif
                return -1;
            }
            rx_total += rxsize[i];
            if (rx_total > 16) return -1;
        }
    }

    wizchip_transaction_lock();
    chip_state = WIZCHIP_STATE_UNINIT;
    chip_last_error = 0;
    wizchip_transaction_reset_socket_state();
    tmp = wizchip_sw_reset_locked();
    if (tmp != 0) goto wizchip_init_done;
    if (txsize) {
        tmp = 0;
        //M20150601 : For integrating with W5300
#if _WIZCHIP_ == W5300
        for (i = 0 ; i < _WIZCHIP_SOCK_NUM_; i++) {
            if (txsize[i] > 64) {
                tmp = -1; goto wizchip_init_done;    //No use 64KB even if W5300 support max 64KB memory allocation
            }
            tmp += txsize[i];
            if (tmp > 128) {
                tmp = -1; goto wizchip_init_done;
            }
        }
        if (tmp % 8) {
            tmp = -1; goto wizchip_init_done;
        }
#else
        for (i = 0 ; i < _WIZCHIP_SOCK_NUM_; i++) {
            tmp += txsize[i];

#if _WIZCHIP_ < W5200	//2016.10.28 peter add condition for w5100 and w5100s
            if (tmp > 8) {
                tmp = -1; goto wizchip_init_done;
            }
#elif  _WIZCHIP_ == W6300
            if (tmp > 32) {
                tmp = -1; goto wizchip_init_done;
            }
#else
            if (tmp > 16) {
                tmp = -1; goto wizchip_init_done;
            }
#endif
        }
#endif
        for (i = 0 ; i < _WIZCHIP_SOCK_NUM_; i++) {
#if _WIZCHIP_ < W5200	//2016.10.28 peter add condition for w5100
            j = 0;
            while ((txsize[i] >> j != 1) && (txsize[i] != 0)) {
                j++;
            }
            setSn_TXBUF_SIZE(i, j);
#elif _WIZCHIP_ == W5500
            tmp = wizchip_write8_checked(Sn_TXBUF_SIZE(i), txsize[i]);
            if (tmp != 0) {
                goto wizchip_init_done;
            }
            wizchip_txmax_cache[i] = (uint16_t)txsize[i] * 1024U;
#else
            setSn_TXBUF_SIZE(i, txsize[i]);
#endif
        }
    }

    if (rxsize) {
        tmp = 0;
#if _WIZCHIP_ == W5300
        for (i = 0 ; i < _WIZCHIP_SOCK_NUM_; i++) {
            if (rxsize[i] > 64) {
                tmp = -1; goto wizchip_init_done;    //No use 64KB even if W5300 support max 64KB memory allocation
            }
            tmp += rxsize[i];
            if (tmp > 128) {
                tmp = -1; goto wizchip_init_done;
            }
        }
        if (tmp % 8) {
            tmp = -1; goto wizchip_init_done;
        }
#else
        for (i = 0 ; i < _WIZCHIP_SOCK_NUM_; i++) {
            tmp += rxsize[i];
#if _WIZCHIP_ < W5200	//2016.10.28 peter add condition for w5100 and w5100s
            if (tmp > 8) {
                tmp = -1; goto wizchip_init_done;
            }
#elif  _WIZCHIP_ == W6300
            if (tmp > 32) {
                tmp = -1; goto wizchip_init_done;
            }
#else
            if (tmp > 16) {
                tmp = -1; goto wizchip_init_done;
            }
#endif
        }
#endif
        for (i = 0 ; i < _WIZCHIP_SOCK_NUM_; i++) {
#if _WIZCHIP_ < W5200	// add condition for w5100
            j = 0;
            while ((rxsize[i] >> j != 1) && (txsize[i] != 0)) {
                j++;
            }
            setSn_RXBUF_SIZE(i, j);
#elif _WIZCHIP_ == W5500
            tmp = wizchip_write8_checked(Sn_RXBUF_SIZE(i), rxsize[i]);
            if (tmp != 0) {
                goto wizchip_init_done;
            }
            wizchip_rxmax_cache[i] = (uint16_t)rxsize[i] * 1024U;
#else
            setSn_RXBUF_SIZE(i, rxsize[i]);
#endif
        }
    }
#if _WIZCHIP_ == W5500
    {
        uint8_t version;
        tmp = wizchip_read8_checked_out(VERSIONR, &version);
        if (tmp == 0 && version != 0x04U) {
            tmp = -1;
        }
    }
    if (tmp != 0) goto wizchip_init_done;
#endif
#if _WIZCHIP_ == W5500
    {
        uint8_t phycfgr;

        if (wizchip_read8_checked_out(PHYCFGR, &phycfgr) != 0) {
            tmp = SOCKERR_IO;
            goto wizchip_init_done;
        }
        wizchip_previous_link = (phycfgr & PHYCFGR_LNK_ON)
                                ? PHY_LINK_ON
                                : PHY_LINK_OFF;
    }
#endif
wizchip_init_done:
    return wizchip_transaction_finish(tmp);
}

/*
 * Socket interrupt ownership (AUD-012)
 *
 * Socket interrupt events MUST have a single software consumer:
 *   - Prefer an ISR that snapshots Sn_IR into atomic software-pending
 *     bits and wakes the owner task; the task performs hardware clears.
 *   - Avoid blanket-clearing Sn_IR from ISR context while polling APIs
 *     are consuming SENDOK/TIMEOUT, as this can remove events before
 *     a polling API observes them.
 *   - Polling paths must read Sn_IR once into a local variable before
 *     testing individual bits to minimize the ISR race window.
 */
void wizchip_clrinterrupt(intr_kind intr) {
    uint8_t ir  = (uint8_t)intr;
    uint8_t sir = (uint8_t)((uint16_t)intr >> 8);

    //teddy 240122
#if _WIZCHIP_ == W6100 || _WIZCHIP_ == W6300
    int i;
    uint8_t slir = (uint8_t)((uint32_t)intr >> 16);
    setIRCLR(ir);
    for (i = 0; i < _WIZCHIP_SOCK_NUM_; i++) {
        if (sir & (1 << i)) {
            setSn_IRCLR(i, 0xFF);
        }
    }
    setSLIRCLR(slir);
    return;
#endif

#if _WIZCHIP_ < W5500
    ir |= (1 << 4); // IK_WOL
#endif
#if _WIZCHIP_ == W5200
    ir |= (1 << 6);
#endif

#if _WIZCHIP_ < W5200
    sir &= 0x0F;
#endif

#if _WIZCHIP_ <= W5100S
    ir |= sir;
    setIR(ir);
    //A20150601 : For integrating with W5300
#elif _WIZCHIP_ == W5300
    setIR(((((uint16_t)ir) << 8) | (((uint16_t)sir) & 0x00FF)));
#else
    setIR(ir);
    //M20200227 : For clear
    //setSIR(sir);
    for (ir = 0; ir < 8; ir++) {
        if (sir & (0x01 << ir)) {
            setSn_IR(ir, 0xff);
        }
    }

#endif
}

intr_kind wizchip_getinterrupt(void) {
    uint8_t ir  = 0;
    uint8_t sir = 0;
    uint32_t ret = 0;

#if _WIZCHIP_ <= W5100S
    ir = getIR();
    sir = ir & 0x0F;
    //A20150601 : For integrating with W5300
#elif _WIZCHIP_  == W5300
    ret = getIR();
    ir = (uint8_t)(ret >> 8);
    sir = (uint8_t)ret;
#else
    {
        uint8_t tmp3[3];
        WIZCHIP_READ_BUF(IR, tmp3, 3);
        ir = tmp3[0];
        sir = tmp3[2];
    }
#endif

    //M20150601 : For Integrating with W5300
    //#if _WIZCHIP_ < W5500
#if _WIZCHIP_ < W5200
    ir &= ~(1 << 4); // IK_WOL
#endif
#if _WIZCHIP_ == W5200
    ir &= ~(1 << 6);
#endif
    ret = sir;
    ret = (ret << 8) + ir;
    //teddy 240122
#if _WIZCHIP_ == W6100 || _WIZCHIP_ == W6300
    ret = (((uint32_t)getSLIR()) << 16) | ret;
#endif

    return (intr_kind)ret;
}

static void wizchip_setinterruptmask_locked(intr_kind intr) {
    uint8_t imr  = (uint8_t)intr;
    uint8_t simr = (uint8_t)((uint16_t)intr >> 8);
#if _WIZCHIP_ < W5500
    imr &= ~(1 << 4); // IK_WOL
#endif
#if _WIZCHIP_ == W5200
    imr &= ~(1 << 6);
#endif

#if _WIZCHIP_ < W5200
    simr &= 0x0F;
    imr |= simr;
    setIMR(imr);
    //A20150601 : For integrating with W5300
#elif _WIZCHIP_ == W5300
    setIMR(((((uint16_t)imr) << 8) | (((uint16_t)simr) & 0x00FF)));
#else
    setIMR(imr);
    setSIMR(simr);
    //teddy 240122
#if _WIZCHIP_ == W6100 || _WIZCHIP_ == W6300
    uint8_t slimr = (uint8_t)((uint32_t)intr >> 16);
    setSLIMR(slimr);
#endif
#endif
}

void wizchip_setinterruptmask(intr_kind intr) {
    WIZCHIP_GLOBAL_LOCK();
    wizchip_setinterruptmask_locked(intr);
    WIZCHIP_GLOBAL_UNLOCK();
}

static intr_kind wizchip_getinterruptmask_locked(void) {
    uint8_t imr  = 0;
    uint8_t simr = 0;
    uint32_t ret = 0;
#if _WIZCHIP_ < W5200
    imr  = getIMR();
    simr = imr & 0x0F;
    //A20150601 : For integrating with W5300
#elif _WIZCHIP_ == W5300
    ret = getIMR();
    imr = (uint8_t)(ret >> 8);
    simr = (uint8_t)ret;
#else
    imr  = getIMR();
    simr = getSIMR();
#endif

#if _WIZCHIP_ < W5500
    imr &= ~(1 << 4); // IK_WOL
#endif
#if _WIZCHIP_ == W5200
    imr &= ~(1 << 6);  // IK_DEST_UNREACH
#endif
    ret = simr;
    ret = (ret << 8) + imr;

    //teddy 240122
#if _WIZCHIP_ == W6100 || _WIZCHIP_ == W6300
    ret = (((uint32_t)getSLIMR()) << 16) | ret;
#endif

    return (intr_kind)ret;
}

intr_kind wizchip_getinterruptmask(void) {
    intr_kind intr;

    WIZCHIP_GLOBAL_LOCK();
    intr = wizchip_getinterruptmask_locked();
    WIZCHIP_GLOBAL_UNLOCK();
    return intr;
}

int8_t wizphy_getphylink(void) {
    int8_t tmp = PHY_LINK_OFF;
#if _WIZCHIP_ == W5100S
    if (getPHYSR() & PHYSR_LNK) {
        tmp = PHY_LINK_ON;
    }
#elif   _WIZCHIP_ == W5200
    if (getPHYSTATUS() & PHYSTATUS_LINK) {
        tmp = PHY_LINK_ON;
    }
#elif _WIZCHIP_ == W5500
    if (getPHYCFGR() & PHYCFGR_LNK_ON) {
        tmp = PHY_LINK_ON;
    }

#elif ((_WIZCHIP_ == W6100)||(_WIZCHIP_ == W6300))


#if (_PHY_IO_MODE_ == _PHY_IO_MODE_PHYCR_)
    return (getPHYSR() & PHYSR_LNK);
#elif (_PHY_IO_MODE_ == _PHY_IO_MODE_MII_)
    if (wiz_mdio_read(PHYRAR_BMSR) & BMSR_LINK_STATUS) {
        return PHY_LINK_ON;
    }
    return PHY_LINK_OFF;
#endif

#else
    tmp = -1;
#endif
    if ((tmp == PHY_LINK_OFF || tmp == PHY_LINK_ON) &&
        tmp != wizchip_previous_link) {
        wizchip_previous_link = tmp;
        wizchip_phy_link_callback((uint8_t)tmp);
    }
    return tmp;
}

#if _WIZCHIP_ == W5500

int8_t wizphy_powerdown(void) {
    setPHYCFGR(PHYCFGR_OPMDC_PDOWN);
    setPHYCFGR(PHYCFGR_OPMDC_PDOWN | PHYCFGR_OPMD);
    return 0;
}

int8_t wizphy_powerup(void) {
    setPHYCFGR(PHYCFGR_OPMDC_ALLA);
    setPHYCFGR(PHYCFGR_OPMDC_ALLA | PHYCFGR_OPMD);
    return 0;
}

void wiznet_wol_enable(uint8_t sn) {
    (void)sn;
    setMR(getMR() | MR_WOL);
}

void wiznet_wol_disable(void) {
    setMR(getMR() & ~MR_WOL);
}

#elif _WIZCHIP_ == W5100S

int8_t wizphy_powerdown(void) {
    uint16_t tmp = wiz_mdio_read(PHYMDIO_BMCR);
    tmp |= BMCR_PWDN;
    wiz_mdio_write(PHYMDIO_BMCR, tmp);
    return 0;
}

int8_t wizphy_powerup(void) {
    uint16_t tmp = wiz_mdio_read(PHYMDIO_BMCR);
    tmp &= ~BMCR_PWDN;
    wiz_mdio_write(PHYMDIO_BMCR, tmp);
    return 0;
}

void wiznet_wol_enable(uint8_t sn) { (void)sn; }
void wiznet_wol_disable(void) {}

#endif

#if _WIZCHIP_ > W5100

int8_t wizphy_getphypmode(void) {
    int8_t tmp = 0;
#if   _WIZCHIP_ == W5200
    if (getPHYSTATUS() & PHYSTATUS_POWERDOWN) {
        tmp = PHY_POWER_DOWN;
    } else {
        tmp = PHY_POWER_NORM;
    }
#elif _WIZCHIP_ == 5500
    if ((getPHYCFGR() & PHYCFGR_OPMDC_ALLA) == PHYCFGR_OPMDC_PDOWN) {
        tmp = PHY_POWER_DOWN;
    } else {
        tmp = PHY_POWER_NORM;
    }
    //teddy 240122
#elif _WIZCHIP_ == W6100 || _WIZCHIP_ == W6300
#if (_PHY_IO_MODE_ == _PHY_IO_MODE_PHYCR_)
    if (getPHYCR1() & PHYCR1_PWDN) {
        return PHY_POWER_DOWN;
    }
#elif (_PHY_IO_MODE_ == _PHY_IO_MODE_MII_)
    if (wiz_mdio_read(PHYRAR_BMCR) & BMCR_PWDN) {
        return PHY_POWER_DOWN;
    }
#endif
    return PHY_POWER_NORM;
#else
    tmp = -1;
#endif
    return tmp;
}
#endif

#if _WIZCHIP_ == W5100S
int8_t wizphy_reset(void) {
    uint16_t tmp = wiz_mdio_read(PHYMDIO_BMCR);
    tmp |= BMCR_RESET;
    wiz_mdio_write(PHYMDIO_BMCR, tmp);
    while (wiz_mdio_read(PHYMDIO_BMCR)&BMCR_RESET) {}
    return 0;
}

int8_t wizphy_setphyconf(wiz_PhyConf* phyconf) {
    uint16_t tmp;

    if (!phyconf) {
        return -1;
    }
    tmp = wiz_mdio_read(PHYMDIO_BMCR);
    if (phyconf->mode == PHY_MODE_AUTONEGO) {
        tmp |= BMCR_AUTONEGO;
    } else {
        tmp &= ~BMCR_AUTONEGO;
        if (phyconf->duplex == PHY_DUPLEX_FULL) {
            tmp |= BMCR_DUP;
        } else {
            tmp &= ~BMCR_DUP;
        }
        if (phyconf->speed == PHY_SPEED_100) {
            tmp |= BMCR_SPEED;
        } else {
            tmp &= ~BMCR_SPEED;
        }
    }
    wiz_mdio_write(PHYMDIO_BMCR, tmp);
    return 0;
}

int8_t wizphy_getphyconf(wiz_PhyConf* phyconf) {
    uint16_t tmp = 0;

    if (!phyconf) {
        return -1;
    }
    tmp = wiz_mdio_read(PHYMDIO_BMCR);
    phyconf->by   = PHY_CONFBY_SW;
    if (tmp & BMCR_AUTONEGO) {
        phyconf->mode = PHY_MODE_AUTONEGO;
    } else {
        phyconf->mode = PHY_MODE_MANUAL;
        if (tmp & BMCR_DUP) {
            phyconf->duplex = PHY_DUPLEX_FULL;
        } else {
            phyconf->duplex = PHY_DUPLEX_HALF;
        }
        if (tmp & BMCR_SPEED) {
            phyconf->speed = PHY_SPEED_100;
        } else {
            phyconf->speed = PHY_SPEED_10;
        }
    }
    return 0;
}

int8_t wizphy_setphypmode(uint8_t pmode) {
    uint16_t tmp = 0;

    if (pmode != PHY_POWER_NORM && pmode != PHY_POWER_DOWN) {
        return -1;
    }
    tmp = wiz_mdio_read(PHYMDIO_BMCR);
    if (pmode == PHY_POWER_DOWN) {
        tmp |= BMCR_PWDN;
    } else {
        tmp &= ~BMCR_PWDN;
    }
    wiz_mdio_write(PHYMDIO_BMCR, tmp);
    tmp = wiz_mdio_read(PHYMDIO_BMCR);
    if (pmode == PHY_POWER_DOWN) {
        if (tmp & BMCR_PWDN) {
            return 0;
        }
    } else {
        if ((tmp & BMCR_PWDN) != BMCR_PWDN) {
            return 0;
        }
    }
    return -1;
}

#elif _WIZCHIP_ == W5500
#define WIZPHY_RESET_HOLD_US 200u
#define WIZPHY_RST_BIT ((uint8_t)~PHYCFGR_RST)
#define WIZPHY_RESET_MASK ((uint8_t)(WIZPHY_RST_BIT | PHYCFGR_OPMD | \
                                     PHYCFGR_OPMDC_ALLA))
#define WIZPHY_DEADLINE_ERROR (-16)

static int8_t wizphy_wait_until(uint64_t deadline_us, uint64_t delay_us) {
    uint32_t polls = 0u;

    if (!time_cbs._now_us) {
        if (!time_cbs._wait_us) {
            return WIZPHY_DEADLINE_ERROR;
        }
        time_cbs._wait_us(delay_us);
        return 0;
    }

    while (!wizchip_deadline_expired(deadline_us)) {
        if (time_cbs._wait_us) {
            time_cbs._wait_us(1u);
        } else if (wait_hook) {
            wait_hook();
        } else {
            return WIZPHY_DEADLINE_ERROR;
        }
        if (++polls >= _WIZCHIP_POLL_MAX_) {
            return WIZPHY_DEADLINE_ERROR;
        }
    }
    return 0;
}

static int8_t wizphy_wait_readback(uint8_t expected) {
    uint64_t deadline_us = wizchip_deadline_abs(timeout_config.phy_timeout_us);
    uint32_t poll_limit = _WIZCHIP_POLL_MAX_;
    uint32_t polls = 0u;

    if (!time_cbs._now_us && time_cbs._wait_us &&
        timeout_config.phy_timeout_us < poll_limit) {
        poll_limit = timeout_config.phy_timeout_us;
    }

    do {
        if ((getPHYCFGR() & WIZPHY_RESET_MASK) ==
            (expected & WIZPHY_RESET_MASK)) {
            return 0;
        }
        if (time_cbs._wait_us) {
            time_cbs._wait_us(1u);
        } else if (wait_hook) {
            wait_hook();
        } else {
            return WIZPHY_DEADLINE_ERROR;
        }
        if (++polls >= poll_limit) {
            return WIZPHY_DEADLINE_ERROR;
        }
    } while (!time_cbs._now_us || !wizchip_deadline_expired(deadline_us));

    return WIZPHY_DEADLINE_ERROR;
}

static int8_t wizphy_reset_locked(uint8_t configured) {
    uint8_t reset_low = configured & (uint8_t)~WIZPHY_RST_BIT;
    uint8_t reset_high = configured | WIZPHY_RST_BIT;
    uint64_t hold_deadline_us;

    setPHYCFGR(reset_low);
    if (wizphy_wait_readback(reset_low) != 0) {
        return WIZPHY_DEADLINE_ERROR;
    }

    hold_deadline_us = wizchip_deadline_abs(WIZPHY_RESET_HOLD_US);
    if (wizphy_wait_until(hold_deadline_us, WIZPHY_RESET_HOLD_US) != 0) {
        return WIZPHY_DEADLINE_ERROR;
    }

    setPHYCFGR(reset_high);
    return wizphy_wait_readback(reset_high);
}

int8_t wizphy_reset(void) {
    uint8_t configured;
    int8_t ret;

    WIZCHIP_GLOBAL_LOCK();
    configured = getPHYCFGR();
    ret = wizphy_reset_locked(configured);
    WIZCHIP_GLOBAL_UNLOCK();
    return ret;
}

int8_t wizphy_setphyconf(wiz_PhyConf* phyconf) {
    uint8_t tmp;
    int8_t ret;

    if (!phyconf || phyconf->by > PHY_CONFBY_SW ||
        phyconf->mode > PHY_MODE_AUTONEGO ||
        phyconf->speed > PHY_SPEED_100 ||
        phyconf->duplex > PHY_DUPLEX_FULL) {
        return -1;
    }

    WIZCHIP_GLOBAL_LOCK();
    tmp = getPHYCFGR();
    tmp &= (uint8_t)~(PHYCFGR_OPMD | PHYCFGR_OPMDC_ALLA);
    if (phyconf->by == PHY_CONFBY_SW) {
        tmp |= PHYCFGR_OPMD;
    }
    if (phyconf->mode == PHY_MODE_AUTONEGO) {
        tmp |= PHYCFGR_OPMDC_ALLA;
    } else {
        if (phyconf->duplex == PHY_DUPLEX_FULL) {
            if (phyconf->speed == PHY_SPEED_100) {
                tmp |= PHYCFGR_OPMDC_100F;
            } else {
                tmp |= PHYCFGR_OPMDC_10F;
            }
        } else {
            if (phyconf->speed == PHY_SPEED_100) {
                tmp |= PHYCFGR_OPMDC_100H;
            } else {
                tmp |= PHYCFGR_OPMDC_10H;
            }
        }
    }
    setPHYCFGR(tmp);
    ret = wizphy_reset_locked(tmp);
    WIZCHIP_GLOBAL_UNLOCK();
    return ret;
}

int8_t wizphy_getphyconf(wiz_PhyConf* phyconf) {
    uint8_t tmp;

    if (!phyconf) {
        return -1;
    }

    WIZCHIP_GLOBAL_LOCK();
    tmp = getPHYCFGR();
    WIZCHIP_GLOBAL_UNLOCK();
    phyconf->by   = (tmp & PHYCFGR_OPMD) ? PHY_CONFBY_SW : PHY_CONFBY_HW;
    switch (tmp & PHYCFGR_OPMDC_ALLA) {
    case PHYCFGR_OPMDC_ALLA:
    case PHYCFGR_OPMDC_100FA:
        phyconf->mode = PHY_MODE_AUTONEGO;
        break;
    default:
        phyconf->mode = PHY_MODE_MANUAL;
        break;
    }
    switch (tmp & PHYCFGR_OPMDC_ALLA) {
    case PHYCFGR_OPMDC_100FA:
    case PHYCFGR_OPMDC_100F:
    case PHYCFGR_OPMDC_100H:
        phyconf->speed = PHY_SPEED_100;
        break;
    default:
        phyconf->speed = PHY_SPEED_10;
        break;
    }
    switch (tmp & PHYCFGR_OPMDC_ALLA) {
    case PHYCFGR_OPMDC_100FA:
    case PHYCFGR_OPMDC_100F:
    case PHYCFGR_OPMDC_10F:
        phyconf->duplex = PHY_DUPLEX_FULL;
        break;
    default:
        phyconf->duplex = PHY_DUPLEX_HALF;
        break;
    }
    return 0;
}

int8_t wizphy_getphystat(wiz_PhyConf* phyconf) {
    uint8_t tmp;

    if (!phyconf) {
        return -1;
    }

    WIZCHIP_GLOBAL_LOCK();
    tmp = getPHYCFGR();
    WIZCHIP_GLOBAL_UNLOCK();
    phyconf->duplex = (tmp & PHYCFGR_DPX_FULL) ? PHY_DUPLEX_FULL : PHY_DUPLEX_HALF;
    phyconf->speed  = (tmp & PHYCFGR_SPD_100) ? PHY_SPEED_100 : PHY_SPEED_10;
    return (tmp & PHYCFGR_LNK_ON) ? PHY_LINK_ON : PHY_LINK_OFF;
}

int8_t wizphy_setphypmode(uint8_t pmode) {
    uint8_t tmp;
    int8_t ret;

    if (pmode != PHY_POWER_NORM && pmode != PHY_POWER_DOWN) {
        return -1;
    }

    WIZCHIP_GLOBAL_LOCK();
    tmp = getPHYCFGR();
    if ((tmp & PHYCFGR_OPMD) == 0) {
        WIZCHIP_GLOBAL_UNLOCK();
        return -1;
    }
    tmp &= (uint8_t)~PHYCFGR_OPMDC_ALLA;
    if (pmode == PHY_POWER_DOWN) {
        tmp |= PHYCFGR_OPMDC_PDOWN;
    } else {
        tmp |= PHYCFGR_OPMDC_ALLA;
    }
    setPHYCFGR(tmp);
    ret = wizphy_reset_locked(tmp);
    WIZCHIP_GLOBAL_UNLOCK();
    return ret;
}

//teddy 240122
#elif _WIZCHIP_ == W6100 || _WIZCHIP_ == W6300
int8_t wizphy_reset(void) {
#if (_PHY_IO_MODE_ == _PHY_IO_MODE_PHYCR_)
    uint8_t tmp = getPHYCR1() | PHYCR1_RST;
    PHYUNLOCK();
    setPHYCR1(tmp);
    PHYLOCK();
#elif (_PHY_IO_MODE_ == _PHY_IO_MODE_MII_)
    wiz_mdio_write(PHYRAR_BMCR, wiz_mdio_read(PHYRAR_BMCR) | BMCR_RST);
    while (wiz_mdio_read(PHYRAR_BMCR) & BMCR_RST);
#endif
    return 0;
}

int8_t wizphy_setphyconf(wiz_PhyConf* phyconf) {
    if (!phyconf) {
        return -1;
    }
#if (_PHY_IO_MODE_ == _PHY_IO_MODE_PHYCR_)
    uint8_t tmp = 0;
    if (phyconf->mode == PHY_MODE_TE) {
        setPHYCR1(getPHYCR1() | PHYCR1_TE);
        tmp = PHYCR0_AUTO;
    } else {
        setPHYCR1(getPHYCR1() & ~PHYCR1_TE);
        if (phyconf->mode == PHY_MODE_AUTONEGO) {
            tmp = PHYCR0_AUTO;
        } else {
            tmp |= 0x04;
            if (phyconf->speed  == PHY_SPEED_10) {
                tmp |= 0x02;
            }
            if (phyconf->duplex == PHY_DUPLEX_HALF) {
                tmp |= 0x01;
            }
        }
    }
    setPHYCR0(tmp);
#elif (_PHY_IO_MODE_ == _PHY_IO_MODE_MII_)
    uint16_t tmp = wiz_mdio_read(PHYRAR_BMCR);
    if (phyconf->mode == PHY_MODE_TE) {
        setPHYCR1(getPHYCR1() | PHYCR1_TE);
        setPHYCR0(PHYCR0_AUTO);
    } else {
        setPHYCR1(getPHYCR1() & ~PHYCR1_TE);
        if (phyconf->mode == PHY_MODE_AUTONEGO) {
            tmp |= BMCR_ANE;
        } else {
            tmp &= ~(BMCR_ANE | BMCR_DPX | BMCR_SPD);
            if (phyconf->duplex == PHY_DUPLEX_FULL) {
                tmp |= BMCR_DPX;
            }
            if (phyconf->speed == PHY_SPEED_100) {
                tmp |= BMCR_SPD;
            }
        }
        wiz_mdio_write(PHYRAR_BMCR, tmp);
    }
#endif
    return 0;
}

int8_t wizphy_getphyconf(wiz_PhyConf* phyconf) {
    if (!phyconf) {
        return -1;
    }
#if (_PHY_IO_MODE_ == _PHY_IO_MODE_PHYCR_)
    uint8_t tmp = 0;
    tmp = getPHYSR();
    if (getPHYCR1() & PHYCR1_TE) {
        phyconf->mode = PHY_MODE_TE;
    } else {
        phyconf->mode = (tmp & (1 << 5)) ? PHY_MODE_MANUAL : PHY_MODE_AUTONEGO ;
    }
    phyconf->speed  = (tmp & (1 << 4)) ? PHY_SPEED_10    : PHY_SPEED_100;
    phyconf->duplex = (tmp & (1 << 3)) ? PHY_DUPLEX_HALF : PHY_DUPLEX_FULL;
#elif (_PHY_IO_MODE_ == _PHY_IO_MODE_MII_)
    uint16_t tmp = 0;
    tmp = wiz_mdio_read(PHYRAR_BMCR);
    if (getPHYCR1() & PHYCR1_TE) {
        phyconf->mode = PHY_MODE_TE;
    } else {
        phyconf->mode   = (tmp & BMCR_ANE) ? PHY_MODE_AUTONEGO : PHY_MODE_MANUAL;
    }
    phyconf->duplex = (tmp & BMCR_DPX) ? PHY_DUPLEX_FULL   : PHY_DUPLEX_HALF;
    phyconf->speed  = (tmp & BMCR_SPD) ? PHY_SPEED_100     : PHY_SPEED_10;
#endif
    return 0;
}

int8_t wizphy_getphystat(wiz_PhyConf* phyconf) {
    uint8_t tmp = 0;

    if (!phyconf) {
        return -1;
    }
    tmp = getPHYSR();
    if (getPHYCR1() & PHYCR1_TE) {
        phyconf->mode = PHY_MODE_TE;
    } else {
        phyconf->mode   = (tmp & (1 << 5))    ? PHY_MODE_MANUAL : PHY_MODE_AUTONEGO ;
    }
    phyconf->speed  = (tmp & PHYSR_SPD) ? PHY_SPEED_10    : PHY_SPEED_100;
    phyconf->duplex = (tmp & PHYSR_DPX) ? PHY_DUPLEX_HALF : PHY_DUPLEX_FULL;
    return 0;
}

int8_t wizphy_setphypmode(uint8_t pmode) {
    if (pmode != PHY_POWER_NORM && pmode != PHY_POWER_DOWN) {
        return -1;
    }
#if (_PHY_IO_MODE_ == _PHY_IO_MODE_PHYCR_)
    uint8_t tmp = getPHYCR1();
    if (pmode == PHY_POWER_DOWN) {
        tmp |= PHYCR1_PWDN;
    } else {
        tmp &= ~PHYCR1_PWDN;
    }
    setPHYCR1(tmp);
#elif (_PHY_IO_MODE_ == _PHY_IO_MODE_MII_)
    uint16_t tmp = 0;
    tmp = wiz_mdio_read(PHYRAR_BMCR);
    if (pmode == PHY_POWER_DOWN) {
        tmp |= BMCR_PWDN;
    } else {
        tmp &= ~BMCR_PWDN;
    }
    wiz_mdio_write(PHYRAR_BMCR, tmp);
#endif
    return 0;
}

int8_t wizchip_arp(wiz_ARP* arp) {
    uint8_t tmp;
    if (arp->destinfo.len == 16) {
        setSLDIP6R(arp->destinfo.ip);
        setSLCR(SLCR_ARP6);
    } else {
        setSLDIP4R(arp->destinfo.ip);
        setSLCR(SLCR_ARP4);
    }
    while (getSLCR());
    while ((tmp = getSLIR()) == 0x00);
    setSLIRCLR(~SLIR_RA);
    if (tmp & (SLIR_ARP4 | SLIR_ARP6)) {
        getSLDHAR(arp->dha);
        return 0;
    }
    return -1;
}

int8_t wizchip_ping(wiz_PING* ping) {
    uint8_t tmp;
    setPINGIDR(ping->id);
    setPINGSEQR(ping->seq);
    if (ping->destinfo.len == 16) {
        setSLDIP6R(ping->destinfo.ip);
        setSLCR(SLCR_PING6);
    } else {
        setSLDIP4R(ping->destinfo.ip);
        setSLCR(SLCR_PING4);
    }
    while (getSLCR());
    while ((tmp = getSLIR()) == 0x00);
    setSLIRCLR(~SLIR_RA);
    if (tmp & (SLIR_PING4 | SLIR_PING6)) {
        return 0;
    }
    return -1;
}

int8_t wizchip_dad(uint8_t* ipv6) {
    uint8_t tmp;
    setSLDIP6R(ipv6);
    setSLCR(SLCR_NS);
    while (getSLCR());
    while ((tmp = getSLIR()) == 0x00);
    setSLIRCLR(~SLIR_RA);
    if (tmp & SLIR_TOUT) {
        return 0;
    }
    return -1;
}

int8_t wizchip_slaac(wiz_Prefix* prefix) {
    uint8_t tmp;
    setSLCR(SLCR_RS);
    while (getSLCR());
    while ((tmp = getSLIR()) == 0x00);
    setSLIRCLR(~SLIR_RA);
    if (tmp & SLIR_RS) {
        prefix->len = getPLR();
        prefix->flag = getPFR();
        prefix->valid_lifetime = getVLTR();
        prefix->preferred_lifetime = getPLTR();
        getPAR(prefix->prefix);
        return 0;
    }
    return -1;
}

int8_t wizchip_unsolicited(void) {
    uint8_t tmp;
    setSLCR(SLCR_UNA);
    while (getSLCR());
    while ((tmp = getSLIR()) == 0x00);
    setSLIRCLR(~SLIR_RA);
    if (tmp & SLIR_TOUT) {
        return 0;
    }
    return -1;
}

int8_t wizchip_getprefix(wiz_Prefix * prefix) {
    if (getSLIR() & SLIR_RA) {
        prefix->len = getPLR();
        prefix->flag = getPFR();
        prefix->valid_lifetime = getVLTR();
        prefix->preferred_lifetime = getPLTR();
        getPAR(prefix->prefix);
        setSLIRCLR(SLIR_RA);
    }
    return -1;
}


#endif

#if (_WIZCHIP_ == W5100 || _WIZCHIP_ == W5100S || _WIZCHIP_ == W5200 || _WIZCHIP_ == W5300 || _WIZCHIP_ == W5500)
static void wizchip_setnetinfo_locked(wiz_NetInfo* pnetinfo) {
    setSHAR(pnetinfo->mac);
    setGAR(pnetinfo->gw);
    setSUBR(pnetinfo->sn);
    setSIPR(pnetinfo->ip);
    _DNS_[0] = pnetinfo->dns[0];
    _DNS_[1] = pnetinfo->dns[1];
    _DNS_[2] = pnetinfo->dns[2];
    _DNS_[3] = pnetinfo->dns[3];
    _DHCP_   = pnetinfo->dhcp;
}

void wizchip_setnetinfo(wiz_NetInfo* pnetinfo) {
    WIZCHIP_GLOBAL_LOCK();
    wizchip_setnetinfo_locked(pnetinfo);
    WIZCHIP_GLOBAL_UNLOCK();
}

static void wizchip_getnetinfo_locked(wiz_NetInfo* pnetinfo) {
    getSHAR(pnetinfo->mac);
    getGAR(pnetinfo->gw);
    getSUBR(pnetinfo->sn);
    getSIPR(pnetinfo->ip);
    pnetinfo->dns[0] = _DNS_[0];
    pnetinfo->dns[1] = _DNS_[1];
    pnetinfo->dns[2] = _DNS_[2];
    pnetinfo->dns[3] = _DNS_[3];
    pnetinfo->dhcp  = _DHCP_;
}

void wizchip_getnetinfo(wiz_NetInfo* pnetinfo) {
    WIZCHIP_GLOBAL_LOCK();
    wizchip_getnetinfo_locked(pnetinfo);
    WIZCHIP_GLOBAL_UNLOCK();
}

int8_t wizchip_setnetmode(netmode_type netmode) {
    uint8_t tmp = 0;
#if _WIZCHIP_ != W5500
    if (netmode & ~(NM_WAKEONLAN | NM_PPPOE | NM_PINGBLOCK)) {
        return -1;
    }
#else
    if (netmode & ~(NM_WAKEONLAN | NM_PPPOE | NM_PINGBLOCK | NM_FORCEARP)) {
        return -1;
    }
#endif
    tmp = getMR();
#if _WIZCHIP_ != W5500
    tmp &= ~(NM_WAKEONLAN | NM_PPPOE | NM_PINGBLOCK);
#else
    tmp &= ~(NM_WAKEONLAN | NM_PPPOE | NM_PINGBLOCK | NM_FORCEARP);
#endif
    tmp |= (uint8_t)netmode;
    setMR(tmp);
    return 0;
}

netmode_type wizchip_getnetmode(void) {
    return (netmode_type) getMR();
}

static void wizchip_settimeout_locked(wiz_NetTimeout* nettime) {
    uint8_t retry_cnt = nettime->retry_cnt;
    uint16_t time_100us = nettime->time_100us;

    if (retry_cnt < WIZCHIP_RCR_MIN) retry_cnt = WIZCHIP_RCR_MIN;
    if (time_100us < WIZCHIP_RTR_MIN) time_100us = WIZCHIP_RTR_MIN;
    setRTR(time_100us);
    setRCR(retry_cnt);
    wizchip_update_timeout_floor_locked(time_100us, retry_cnt);
}

void wizchip_settimeout(wiz_NetTimeout* nettime) {
    WIZCHIP_GLOBAL_LOCK();
    wizchip_settimeout_locked(nettime);
    WIZCHIP_GLOBAL_UNLOCK();
}

static void wizchip_gettimeout_locked(wiz_NetTimeout* nettime) {
    nettime->retry_cnt = getRCR();
    nettime->time_100us = getRTR();
}

void wizchip_gettimeout(wiz_NetTimeout* nettime) {
    WIZCHIP_GLOBAL_LOCK();
    wizchip_gettimeout_locked(nettime);
    WIZCHIP_GLOBAL_UNLOCK();
}
//teddy 240122
#elif ((_WIZCHIP_ == 6100) ||(_WIZCHIP_ == 6300))
static void wizchip_setnetinfo_locked(wiz_NetInfo* pnetinfo) {
    uint8_t i = 0;
    setSHAR(pnetinfo->mac);
    setGAR(pnetinfo->gw);
    setSUBR(pnetinfo->sn);
    setSIPR(pnetinfo->ip);
    setGA6R(pnetinfo->gw6);
    setSUB6R(pnetinfo->sn6);
    setLLAR(pnetinfo->lla);
    setGUAR(pnetinfo->gua);

    for (i = 0; i < 4; i++) {
        _DNS_[i]  = pnetinfo->dns[i];
    }
    for (i = 0; i < 16; i++) {
        _DNS6_[i] = pnetinfo->dns6[i];
    }

    _IPMODE_   = pnetinfo->ipmode;
}

void wizchip_setnetinfo(wiz_NetInfo* pnetinfo) {
    WIZCHIP_GLOBAL_LOCK();
    wizchip_setnetinfo_locked(pnetinfo);
    WIZCHIP_GLOBAL_UNLOCK();
}

static void wizchip_getnetinfo_locked(wiz_NetInfo* pnetinfo) {
    uint8_t i = 0;
    getSHAR(pnetinfo->mac);

    getGAR(pnetinfo->gw);
    getSUBR(pnetinfo->sn);
    getSIPR(pnetinfo->ip);

    getGA6R(pnetinfo->gw6);
    getSUB6R(pnetinfo->sn6);
    getLLAR(pnetinfo->lla);
    getGUAR(pnetinfo->gua);
    for (i = 0; i < 4; i++) {
        pnetinfo->dns[i] = _DNS_[i];
    }
    for (i = 0; i < 16; i++) {
        pnetinfo->dns6[i]  = _DNS6_[i];
    }

    pnetinfo->ipmode = _IPMODE_;
}

void wizchip_getnetinfo(wiz_NetInfo* pnetinfo) {
    WIZCHIP_GLOBAL_LOCK();
    wizchip_getnetinfo_locked(pnetinfo);
    WIZCHIP_GLOBAL_UNLOCK();
}

void wizchip_setnetmode(netmode_type netmode) {
    uint32_t tmp = (uint32_t) netmode;
    setNETMR((uint8_t)tmp);
    setNETMR2((uint8_t)(tmp >> 8));
    setNET4MR((uint8_t)(tmp >> 16));
    setNET6MR((uint8_t)(tmp >> 24));
}

netmode_type wizchip_getnetmode(void) {
    uint32_t ret = 0;
    ret = getNETMR();
    ret = (ret << 8)  + getNETMR2();
    ret = (ret << 16) + getNET4MR();
    ret = (ret << 24) + getNET6MR();
    return (netmode_type)ret;
}

// netmode_type wizchip_getnetmode(void)
// {
//    return (netmode_type) getMR();
// }

static void wizchip_settimeout_locked(wiz_NetTimeout* nettime) {
    uint8_t retry_cnt = nettime->s_retry_cnt;
    uint16_t time_100us = nettime->s_time_100us;

    if (retry_cnt < WIZCHIP_RCR_MIN) retry_cnt = WIZCHIP_RCR_MIN;
    if (time_100us < WIZCHIP_RTR_MIN) time_100us = WIZCHIP_RTR_MIN;
    setRTR(time_100us);
    setRCR(retry_cnt);
    setSLRCR(nettime->sl_retry_cnt);
    setSLRTR(nettime->sl_time_100us);
    wizchip_update_timeout_floor_locked(time_100us, retry_cnt);
}

void wizchip_settimeout(wiz_NetTimeout* nettime) {
    WIZCHIP_GLOBAL_LOCK();
    wizchip_settimeout_locked(nettime);
    WIZCHIP_GLOBAL_UNLOCK();
}

static void wizchip_gettimeout_locked(wiz_NetTimeout* nettime) {
    nettime->s_retry_cnt   = getRCR();
    nettime->s_time_100us  = getRTR();
    nettime->sl_retry_cnt  = getSLRCR();
    nettime->sl_time_100us = getSLRTR();
}

void wizchip_gettimeout(wiz_NetTimeout* nettime) {
    WIZCHIP_GLOBAL_LOCK();
    wizchip_gettimeout_locked(nettime);
    WIZCHIP_GLOBAL_UNLOCK();
}
#endif
