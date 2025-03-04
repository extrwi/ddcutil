/** \file i2c_strategy_dispatcher.h
 *
 *  Allows for alternative mechanisms to read and write to the IC2 bus.
 */
// Copyright (C) 2014-2022 Sanford Rockowitz <rockowitz@minsoft.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef I2C_STRATEGY_DISPATCHER_H_
#define I2C_STRATEGY_DISPATCHER_H_

#include "util/coredefs.h"

#include "base/execution_stats.h"
#include "base/status_code_mgt.h"

#include "i2c_execute.h"

/** I2C IO strategy ids  - currently only 1 option*/
typedef enum {
   I2C_IO_STRATEGY_IOCTL}     ///< use ioctl(I2C_RDWR)
I2C_IO_Strategy_Id;

char * i2c_io_strategy_name(I2C_IO_Strategy_Id id);

/** Describes one I2C IO strategy */
typedef struct {
   I2C_IO_Strategy_Id strategy_id;       ///< id of strategy
   I2C_Writer         i2c_writer;        ///< writer function
   I2C_Reader         i2c_reader;        ///< read function
   char *             i2c_writer_name;   ///< write function name
   char *             i2c_reader_name;   ///< read function name
} I2C_IO_Strategy;

I2C_IO_Strategy_Id i2c_set_io_strategy(I2C_IO_Strategy_Id strategy_id);
I2C_IO_Strategy_Id i2c_get_io_strategy();

// quick and dirty for use in testing framework
// extern I2C_IO_Strategy Default_I2c_Strategy;
extern bool I2C_Read_Bytewise;
extern bool EDID_Read_Bytewise;
extern bool EDID_Write_Before_Read;
extern int  EDID_Read_Size;


Status_Errno_DDC
invoke_i2c_writer(
      int    fd,
      Byte   slave_address,
      int    bytect,
      Byte * bytes_to_write);

Status_Errno_DDC
invoke_i2c_reader(
       int        fd,
       Byte       slave_address,
       bool       read_bytewise,
       int        bytect,
       Byte *     readbuf);


#endif /* I2C_STRATEGY_DISPATCHER_H_ */
