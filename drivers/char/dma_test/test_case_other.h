/*
 * drivers/char/dma_test/test_case_other.h
 * (C) Copyright 2010-2015
 * Reuuimlla Technology Co., Ltd. <www.reuuimllatech.com>
 * liugang <liugang@reuuimllatech.com>
 *
 * sun7i dma test head file
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef __TEST_CASE_OTHER_H
#define __TEST_CASE_OTHER_H

/* total length and one buf length */
#define TOTAL_LEN_EAD		SZ_256K
#define ONE_LEN_EAD		SZ_32K

#define TOTAL_LEN_MANY_ENQ	SZ_256K
#define ONE_LEN_MANY_ENQ	SZ_4K

#define TOTAL_LEN_STOP		SZ_512K
#define ONE_LEN_STOP		SZ_4K

u32 __dtc_enq_aftdone(void);
u32 __dtc_many_enq(void);
u32 __dtc_stop(void);

#endif /* __TEST_CASE_OTHER_H */

