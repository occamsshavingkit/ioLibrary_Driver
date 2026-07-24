#ifndef W5500_SPI_MODEL_H
#define W5500_SPI_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define W5500_MODEL_SOCKET_COUNT 8
#define W5500_MODEL_LOG_SIZE 256

typedef enum {
    MODEL_OP_READ8,
    MODEL_OP_WRITE8,
    MODEL_OP_READ_BUF,
    MODEL_OP_WRITE_BUF
} model_op_type_t;

typedef struct {
    model_op_type_t op;
    uint32_t addr;
    uint8_t  data8;
    uint16_t len;
} model_log_entry_t;

typedef struct {
    uint8_t mr;
    uint8_t cr;
    uint8_t sr;
    uint8_t ir;
    uint16_t port;
    uint8_t dipr[4];
    uint16_t dport;
    uint16_t tx_wr;
    uint16_t tx_rr;
    uint16_t rx_rd;
    uint16_t rx_wr;
    uint16_t tx_fsr;
    uint16_t rx_rsr;
} model_socket_t;

typedef struct {
    uint8_t  mr;
    uint8_t  gar[4];
    uint8_t  subr[4];
    uint8_t  shar[6];
    uint8_t  sipr[4];
    uint8_t  sir;
    uint16_t rtr;
    uint16_t rcr;
    uint8_t  phycfgr;
    uint8_t  versionr;
    uint8_t  intlevel;
    model_socket_t sockets[W5500_MODEL_SOCKET_COUNT];
    uint8_t  tx_buf_sizes[W5500_MODEL_SOCKET_COUNT];
    uint8_t  rx_buf_sizes[W5500_MODEL_SOCKET_COUNT];
    uint8_t  tx_data[W5500_MODEL_SOCKET_COUNT][2048];
    uint8_t  rx_data[W5500_MODEL_SOCKET_COUNT][2048];
    uint64_t monotonic_us;
    int      lock_enter_count;
    int      lock_exit_count;
    int      cs_state;
    model_log_entry_t log[W5500_MODEL_LOG_SIZE];
    size_t   log_count;
    uint8_t  spi_busy;
    int8_t   spi_error;
    uint16_t spi_offset;
    uint8_t  spi_block;
    uint8_t  spi_control;
    uint8_t  spi_header_bytes;
} w5500_model_t;

void model_init(w5500_model_t *m);
void model_reset(w5500_model_t *m);
void model_tick_us(w5500_model_t *m, uint64_t delta_us);

uint8_t  model_read8(const w5500_model_t *m, uint32_t addr);
void     model_write8(w5500_model_t *m, uint32_t addr, uint8_t v);
uint16_t model_read16(const w5500_model_t *m, uint32_t addr);
void     model_write16(w5500_model_t *m, uint32_t addr, uint16_t v);

void model_read_buf(w5500_model_t *m, uint32_t addr, uint8_t *buf, uint16_t len);
void model_write_buf(w5500_model_t *m, uint32_t addr, const uint8_t *buf, uint16_t len);

void model_log_op(w5500_model_t *m, model_op_type_t op, uint32_t addr,
                  uint8_t data8, uint16_t len);
const model_log_entry_t *model_find_op(const w5500_model_t *m, model_op_type_t op,
                                        uint32_t addr, size_t *start);

uint64_t model_monotonic_us(const w5500_model_t *m);

void model_cs_select(w5500_model_t *m);
void model_cs_deselect(w5500_model_t *m);
uint8_t model_spi_read_byte(w5500_model_t *m);
void model_spi_write_byte(w5500_model_t *m, uint8_t value);
void model_spi_read_burst(w5500_model_t *m, uint8_t *buf, uint16_t len);
void model_spi_write_burst(w5500_model_t *m, const uint8_t *buf, uint16_t len);

void model_lock_enter(w5500_model_t *m);
void model_lock_exit(w5500_model_t *m);
int  model_lock_balance(const w5500_model_t *m);

#endif
