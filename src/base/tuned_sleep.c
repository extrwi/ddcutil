/** @file tuned_sleep.c
 *
 *  Perform sleep. The sleep time is determined by io mode, sleep event time,
 *  and applicable multipliers.
 */

// Copyright (C) 2019-2022 Sanford Rockowitz <rockowitz@minsoft.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include <assert.h>
#include <sys/types.h>

#include "public/ddcutil_types.h"

#include "base/core.h"
#include "base/dynamic_sleep.h"
#include "base/execution_stats.h"
#include "base/rtti.h"
#include "base/sleep.h"
#include "base/thread_sleep_data.h"

// Trace class for this file
static DDCA_Trace_Group TRACE_GROUP = DDCA_TRC_NONE;

//
// Deferred sleep
//
// If enabled, sleep is not performed immediately, but instead not until
// immediately before the next DDC call that requires that a wait has
// occurred. The elapsed time between when the call is requested and
// when actually occurs is subtracted from the specified sleep time to
// obtain the actual sleep time.
//
// In testing, this has proven to have a negligable effect on elapsed
// execution time.
//

static bool deferred_sleep_enabled = false;


/** Enables or disables deferred sleep.
 *  @param  onoff new  setting
 *  @return old setting
 */
bool enable_deferred_sleep(bool onoff) {
   // DBGMSG("enable = %s", sbool(enable));
   bool old = deferred_sleep_enabled;
   deferred_sleep_enabled = onoff;
   return old;
}


/** Reports whether deferred sleep is enabled.
 *  @return true/false
 */
bool is_deferred_sleep_enabled() {
   return deferred_sleep_enabled;
}


//
// Perform sleep
//

/* Two multipliers are applied to the sleep time determined from the
 * io mode and event type.
 *
 * sleep_multiplier_factor: Set globally, e.g. from arg passed on
 * command line.  (Consider making thread specific.)
 *
 * sleep_multiplier_ct: Per thread adjustment,initiated by IO retries.
 */

/** Sleep for the period of time required by the DDC protocol, as indicated
 *  by the io mode and sleep event type.
 *
 *  The time is further adjusted by the sleep factor and sleep multiplier
 *  currently in effect.
 *
 *  @todo
 *  Take into account the time since the last monitor return in the
 *  current thread.
 *  @todo
 *  Take into account per-display error statistics.  Would require
 *  error statistics be maintained on a per-display basis, either
 *  in the display reference or display handle.
 *
 * @param event_type  reason for sleep
 * @oaran special_sleep_time_millis  sleep time for event_type SE_SPECIAL
 * @param func        name of function that invoked sleep
 * @param lineno      line number in file where sleep was invoked
 * @param filename    name of file from which sleep was invoked
 * @param msg         text to append to trace message
 */
void tuned_sleep_with_trace(
      Display_Handle * dh,
      Sleep_Event_Type event_type,
      int              special_sleep_time_millis,
      const char *     func,
      int              lineno,
      const char *     filename,
      const char *     msg)
{
   bool debug = false;
   DBGTRC_STARTING(debug, TRACE_GROUP,
         "Sleep event type = %s, dh=%s, special_sleep_time_millis=%d",
         sleep_event_name(event_type), dh_repr(dh), special_sleep_time_millis);
   assert(dh);
   assert( (event_type != SE_SPECIAL && special_sleep_time_millis == 0) ||
           (event_type == SE_SPECIAL && special_sleep_time_millis >  0) );

   DDCA_IO_Mode io_mode = dh->dref->io_path.io_mode;

   int spec_sleep_time_millis = 0;    // should be a default
   bool deferrable_sleep = false;

   if (io_mode == DDCA_IO_I2C) {
      switch(event_type) {

      // Sleep events with values defined in DDC/CI spec

      case (SE_WRITE_TO_READ):
            // 4.3 Get VCP Feature & VCP Feature Reply:
            //     The host should wait at least 40 ms in order to enable the decoding and
            //     and preparation of the reply message by the display
            // 4.6 Capabilities Request & Reply:
            //     write to read interval unclear, assume 50 ms
            //  Use 50 ms for both
            //  spec_sleep_time_millis = DDC_TIMEOUT_MILLIS_BETWEEN_GETVCP_WRITE_READ;
            spec_sleep_time_millis = DDC_TIMEOUT_MILLIS_DEFAULT;
            break;
      case (SE_POST_WRITE):  // post SET VCP FEATURE write, between SET TABLE write fragments, after final?
            // 4.4 Set VCP Feature:
            //   The host should wait at least 50ms to ensure next message is received by the display
            spec_sleep_time_millis = DDC_TIMEOUT_MILLIS_POST_NORMAL_COMMAND;
            deferrable_sleep = deferred_sleep_enabled;
            break;
      case (SE_POST_READ):
            deferrable_sleep = deferred_sleep_enabled;
            spec_sleep_time_millis = DDC_TIMEOUT_MILLIS_POST_NORMAL_COMMAND;
            break;
      case (SE_POST_SAVE_SETTINGS):
            // 4.5 Save Current Settings:
            // The host should wait at least 200 ms before sending the next message to the display
            deferrable_sleep = deferred_sleep_enabled;
            spec_sleep_time_millis = DDC_TIMEOUT_MILLIS_POST_SAVE_SETTINGS;   // per DDC spec
            break;
      case SE_MULTI_PART_WRITE_TO_READ:
         // Not defined in spec for capabilities or table read. Assume 50 ms.
         //
         // Note: This constant is not used.  ddc_i2c_write_read_raw() can't distinguish a normal write/read
         // from one inside a multi part read, and always uses SE_WRITE_TO_READ.
         // Address this by using 50 ms for SE_WRITE_TO_READ.
         spec_sleep_time_millis = DDC_TIMEOUT_MILLIS_DEFAULT;
         break;
      case SE_AFTER_EACH_CAP_TABLE_SEGMENT:
         // 4.6 Capabilities Request & Reply:
         //     The host should wait at least 50ms before sending the next message to the display
         // 4.8.1 Table Write
         //     The host should wait at least 50ms before sending the next message to the display
         // 4.8.2 Table Read
         //     The host should wait at least 50ms before sending the next message to the display
         spec_sleep_time_millis = DDC_TIMEOUT_MILLIS_BETWEEN_CAP_TABLE_FRAGMENTS;
         break;
      case SE_POST_CAP_TABLE_COMMAND:
         // unused, SE_AFTER_EACH_CAP_TABLE_SEGMENT called after each segment, not
         // just between segments
         deferrable_sleep = deferred_sleep_enabled;
         spec_sleep_time_millis = DDC_TIMEOUT_MILLIS_POST_CAP_TABLE_COMMAND;
         break;

      // Not in DDC/CI spec

      case SE_DDC_NULL:
         spec_sleep_time_millis = DDC_TIMEOUT_MILLIS_NULL_RESPONSE_INCREMENT;
         break;
      case SE_PRE_MULTI_PART_READ:
         // before reading capabilities - this is based on testing, not defined in spec
         spec_sleep_time_millis = 200;
         break;
      case SE_SPECIAL:
         // 4/2020: no current use
         spec_sleep_time_millis = special_sleep_time_millis;
         break;
      }  // switch within DDC_IO_DEVI2C
   } // end of DDCA_IO_DEVI2C

   else {
      assert(io_mode == DDCA_IO_USB);
      PROGRAM_LOGIC_ERROR("call_tuned_sleep() called for USB_IO\n");
   }

   // DBGMSF(debug, "deferrable_sleep=%s", sbool(deferrable_sleep));

   // TODO:
   //   get error rate (total calls, total errors), current adjustment value
   //   adjust by time since last i2c event

   Per_Thread_Data * tsd = tsd_get_thread_sleep_data();

   DBGTRC_NOPREFIX(debug, DDCA_TRC_NONE,
          "event type: %s,"
          " spec_sleep_time_millis = %d,"
          " sleep_multiplier_factor = %2.1f,"
          " deferrable sleep: %s",
          sleep_event_name(event_type),
          spec_sleep_time_millis,
          tsd->sleep_multiplier_factor, sbool(deferrable_sleep) );

   int adjusted_sleep_time_millis = spec_sleep_time_millis; // will be changed
   double sleep_multiplier_factor = tsd_get_sleep_multiplier_factor();  // set by --sleep-multiplier
   if (tsd->dynamic_sleep_enabled) {
      dsa_update_adjustment_factor(dh, spec_sleep_time_millis);
      adjusted_sleep_time_millis =
            tsd->cur_sleep_adjustment_factor * sleep_multiplier_factor * spec_sleep_time_millis;
      DBGTRC_NOPREFIX(debug, DDCA_TRC_NONE,
                "using dynamic sleep: true,"
                " adjustment factor: %4.2f,"
                " adjusted_sleep_time_millis = %d",
                tsd->cur_sleep_adjustment_factor,
                adjusted_sleep_time_millis);
   }
   else {
      // DBGMSG("sleep_multiplier_factor = %5.2f", sleep_multiplier_factor);
      // crude, should be sensitive to event type?
      int sleep_multiplier_ct = tsd_get_sleep_multiplier_ct();  // per thread
      adjusted_sleep_time_millis = sleep_multiplier_ct * sleep_multiplier_factor *
                                          spec_sleep_time_millis;
      DBGTRC_NOPREFIX(debug, DDCA_TRC_NONE,
             "using dynamic sleep: false,"
             " sleep_multiplier_ct = %d,"
             " modified_sleep_time_millis=%d",
             sleep_multiplier_ct,
             adjusted_sleep_time_millis);
   }

   record_sleep_event(event_type);

   char msg_buf[100];
   const char * evname = sleep_event_name(event_type);
   if (msg)
      g_snprintf(msg_buf, 100, "Event type: %s, %s", evname, msg);
   else
      g_snprintf(msg_buf, 100, "Event_type: %s", evname);

   if (deferrable_sleep) {
      uint64_t new_deferred_time =
            cur_realtime_nanosec() + (1000 *1000) * (int) adjusted_sleep_time_millis;
      if (new_deferred_time > dh->dref->next_i2c_io_after) {
         DBGTRC(debug, DDCA_TRC_NONE, "Setting deferred sleep");
         dh->dref->next_i2c_io_after = new_deferred_time;
      }
   }
   else {
      sleep_millis_with_trace(adjusted_sleep_time_millis, func, lineno, filename, msg_buf);
   }

   DBGTRC_DONE(debug, TRACE_GROUP, "");
}


/** Compares if the current clock time is less than the delayed io start time
 *  for a display handle, and if so sleeps for the difference.
 *
 *  The delayed io start time is stored in the display reference associated with
 *  the display handle, so persists across open and close
 *
 *  @param  dh        #Display_Handle
 *  #param  func      name of function performing check
 *  @param  lineno    line number of check
 *  @param  filename  file from which the check is invoked
 */
void check_deferred_sleep(
      Display_Handle * dh,
      const char *     func,
      int              lineno,
      const char *     filename)
{
   bool debug = false;
   uint64_t curtime = cur_realtime_nanosec();
   // DBGMSF(debug, "curtime=%"PRIu64", next_i2c_io_after=%"PRIu64,
   //               curtime / (1000*1000), dh->dref->next_i2c_io_after/(1000*1000));
   DBGTRC(debug, DDCA_TRC_NONE,"Checking from %s() at line %d in file %s", func, lineno, filename);
   if (dh->dref->next_i2c_io_after > curtime) {
      int sleep_time = (dh->dref->next_i2c_io_after - curtime)/ (1000*1000);
      DBGTRC(debug, DDCA_TRC_NONE, "Sleeping for %d milliseconds", sleep_time);
      sleep_millis_with_trace(sleep_time, func, lineno, filename, "deferred");
   }
   else {
      DBGTRC(debug, DDCA_TRC_NONE, "No sleep necessary");
   }
}


/** Module initialization */
void init_tuned_sleep() {
   RTTI_ADD_FUNC(check_deferred_sleep);
   RTTI_ADD_FUNC(tuned_sleep_with_trace);
}
