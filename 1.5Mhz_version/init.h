/*
 * init.h
 *
 *  Created on: 2026Äê3ÔÂ25ÈÕ
 *      Author: 16857
 */

#ifndef INIT_H_
#define INIT_H_
#include "no_os_spi.h"
void PSCInit(void);
void OmaplFpgauPPSetup(void);
int32_t ti_spi_init(struct no_os_spi_desc **desc, const struct no_os_spi_init_param *param);
int32_t ti_spi_write_and_read(struct no_os_spi_desc *desc, uint8_t *data, uint16_t len);
int32_t ti_spi_remove(struct no_os_spi_desc *desc);
#endif /* INIT_H_ */
