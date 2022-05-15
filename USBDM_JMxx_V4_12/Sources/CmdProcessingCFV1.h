#ifndef _CMDPROCESSINGCFV1_H_
#define _CMDPROCESSINGCFV1_H_

#include <stdint.h>
uint8_t f_CMD_CF_READ_ALL_CORE_REGS(void);
uint8_t f_CMD_CF_WRITE_MEM(void);
uint8_t f_CMD_CF_READ_MEM(void);
uint8_t f_CMD_CF_WRITE_REG(void);
uint8_t f_CMD_CF_READ_REG(void);
uint8_t f_CMD_CF_WRITE_DREG(void);
uint8_t f_CMD_CF_READ_DREG(void);
uint8_t f_CMD_CF_WRITE_CREG(void);
uint8_t f_CMD_CF_READ_CREG(void);
uint8_t f_CMD_CF_WRITE_CSR2(void);
uint8_t f_CMD_CF_READ_CSR2(void);
uint8_t f_CMD_CF_WRITE_CSR3(void);
uint8_t f_CMD_CF_READ_CSR3(void);

#endif // _CMDPROCESSINGCFV1_H_
