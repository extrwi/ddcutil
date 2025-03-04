/** \file i2c_strategy_dispatcher.c
 *
 *  Allows for alternative mechanisms to read and write to the IC2 bus.
 */
// Copyright (C) 2014-2022 Sanford Rockowitz <rockowitz@minsoft.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/** \cond */
#include <assert.h>
#include <stdio.h>
/** \endcond */

#include "util/file_util.h"
#include "util/string_util.h"

#include "base/core.h"
#include "base/parms.h"
#include "base/status_code_mgt.h"

#include "i2c_strategy_dispatcher.h"

char * i2c_io_strategy_name(I2C_IO_Strategy_Id id) {
   char * result = NULL;
   switch(id) {
   case I2C_IO_STRATEGY_IOCTL:
      result = "I2C_IO_STRATEGY_IOCTL";
      break;
   }
   return result;
}


// I2C_IO_Strategy_Id Default_I2c_Strategy = DEFAULT_I2C_IO_STRATEGY;
bool EDID_Write_Before_Read          = DEFAULT_EDID_WRITE_BEFORE_READ;
bool I2C_Read_Bytewise               = DEFAULT_I2C_READ_BYTEWISE;
bool EDID_Read_Bytewise              = DEFAULT_EDID_READ_BYTEWISE;
int  EDID_Read_Size                  = DEFAULT_EDID_READ_SIZE;



// Trace class for this file
static DDCA_Trace_Group TRACE_GROUP = DDCA_TRC_I2C;


I2C_IO_Strategy i2c_ioctl_io_strategy = {
      I2C_IO_STRATEGY_IOCTL,
      i2c_ioctl_writer,
      i2c_ioctl_reader,
      "ioctl_writer",
      "ioctl_reader"
};

static I2C_IO_Strategy * i2c_io_strategy = &i2c_ioctl_io_strategy;


/** Sets an alternative I2C IO strategy.
 *
 * @param strategy_id  I2C IO strategy id
 * @return old strategy id
 */
I2C_IO_Strategy_Id
i2c_set_io_strategy(I2C_IO_Strategy_Id strategy_id) {
   I2C_IO_Strategy_Id old = i2c_io_strategy->strategy_id;
   switch (strategy_id) {

   case (I2C_IO_STRATEGY_IOCTL):
         i2c_io_strategy= &i2c_ioctl_io_strategy;
         break;
   }
   return old;
}


I2C_IO_Strategy_Id
i2c_get_io_strategy() {
   return i2c_io_strategy->strategy_id;
}


/** Writes to the I2C bus, using the function specified in the
 * currently active strategy.
 *
 * @param   fd              Linux file descriptor for open /dev/i2c bus
 * @param   slave_address   slave address to write to
 * @param   bytect          number of bytes to write
 * @param   bytes_to_write  pointer to bytes to be written
 * @return  status code
 */
Status_Errno_DDC invoke_i2c_writer(
      int    fd,
      Byte   slave_address,
      int    bytect,
      Byte * bytes_to_write)
{
   bool debug = false;
   DBGTRC_STARTING(debug, TRACE_GROUP,
                 "fd=%d, filename=%s, slave_address=0x%02x, bytect=%d, bytes_to_write=%p -> %s",
                 fd,
                 filename_for_fd_t(fd),
                 slave_address,
                 bytect,
                 bytes_to_write,
                 hexstring_t(bytes_to_write, bytect));

   Status_Errno_DDC rc;
   rc = i2c_io_strategy->i2c_writer(fd, slave_address, bytect, bytes_to_write );
   assert (rc <= 0);

   DBGTRC_RET_DDCRC(debug, TRACE_GROUP, rc, "");
   return rc;
}


/** Reads from the I2C bus, using the function specified in the
 *  currently active strategy.
 *
 *  @param   fd              Linux file descriptor for open /dev/i2c bus
 *  @param   slave_address   I2C slave address to read from
 *  @param   read_bytewise   if true, read one byte at a time
 *  @param   bytect          number of bytes to read
 *  @param   readbuf         location where bytes will be read to
 *  @return  status code
 */
Status_Errno_DDC invoke_i2c_reader(
       int        fd,
       Byte       slave_address,
       bool       read_bytewise,
       int        bytect,
       Byte *     readbuf)
{
     bool debug = false;
     DBGTRC_STARTING(debug, TRACE_GROUP,
                   "fd=%d, filename=%s, slave_address=0x%02x, bytect=%d, read_bytewise=%s, readbuf=%p",
                   fd,
                   filename_for_fd_t(fd),
                   slave_address,
                   bytect,
                   sbool(read_bytewise),
                   readbuf);

     Status_Errno_DDC rc;
     rc = i2c_io_strategy->i2c_reader(fd, slave_address, read_bytewise, bytect, readbuf);
     assert (rc <= 0);

     if (rc == 0) {
        DBGTRC_NOPREFIX(debug, TRACE_GROUP, "Bytes read: %s", hexstring_t(readbuf, bytect) );
     }
     DBGTRC_RET_DDCRC(debug, TRACE_GROUP, rc, "");
     return rc;
}



