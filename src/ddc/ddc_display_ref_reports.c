/** @file ddc_display_ref_reports.c
 *  Report functions factored out of ddc_displays.c due to size of that file.
 *  ddc_display_ref_reports.c and ddc_displays.c cross-reference each other.
 */

// Copyright (C) 2014-2022 Sanford Rockowitz <rockowitz@minsoft.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <glib-2.0/glib.h>
#include <string.h>
#include <sys/stat.h>

#include "util/report_util.h"
#include "util/string_util.h"

#include "public/ddcutil_types.h"

#include "base/core.h"
#include "base/monitor_model_key.h"
#include "base/monitor_quirks.h"
#include "base/rtti.h"

#include "i2c/i2c_bus_core.h"
#include "i2c/i2c_sysfs.h"

#ifdef USE_USB
#include "usb/usb_displays.h"
#endif

#include "ddc/ddc_packet_io.h"
#include "ddc/ddc_vcp_version.h"
#include "ddc/ddc_vcp.h"

#include "ddc/ddc_displays.h"
#include "ddc/ddc_display_ref_reports.h"


// Default trace class for this file
static const DDCA_Trace_Group TRACE_GROUP = DDCA_TRC_DDC;

//
// Display_Ref reports
//

/** Gets the controller firmware version as a string
 *
 * @param dh  pointer to display handle
 * @return    pointer to character string, which is valid until the next
 *            call to this function.
 *
 * @remark
 * Consider caching the value in dh->dref
 */
// static
char *
get_firmware_version_string_t(Display_Handle * dh) {
   bool debug = false;

   DBGTRC_STARTING(debug, TRACE_GROUP, "dh=%s", dh_repr(dh));
   static GPrivate  firmware_version_key = G_PRIVATE_INIT(g_free);
   char * version = get_thread_fixed_buffer(&firmware_version_key, 40);

   DDCA_Any_Vcp_Value * valrec = NULL;
   Public_Status_Code psc = 0;
   Error_Info * ddc_excp = ddc_get_vcp_value(
                               dh,
                               0xc9,                     // firmware detection
                               DDCA_NON_TABLE_VCP_VALUE,
                               &valrec);
   psc = (ddc_excp) ? ddc_excp->status_code : 0;
   if (psc != 0) {
      strcpy(version, "Unspecified");
      if (psc != DDCRC_REPORTED_UNSUPPORTED && psc != DDCRC_DETERMINED_UNSUPPORTED) {
         DBGMSF(debug, "get_vcp_value(0xc9) returned %s", psc_desc(psc));
         strcpy(version, "DDC communication failed");
         if (debug || IS_TRACING() || is_report_ddc_errors_enabled())
            errinfo_report(ddc_excp, 1);
      }
      errinfo_free(ddc_excp);
   }
   else {
      g_snprintf(version, 40, "%d.%d", valrec->val.c_nc.sh, valrec->val.c_nc.sl);
      free_single_vcp_value(valrec);
   }
   DBGTRC_DONE(debug, TRACE_GROUP, "Returning: %s", version);
   return version;
}


/** Gets the controller manufacturer name for an open display.
 *
 * @param dh  pointer to display handle
 * @return pointer to character string, which is valid until the next
 * call to this function.
 *
 * @remark
 * Consider caching the value in dh->dref
 */
static char *
get_controller_mfg_string_t(Display_Handle * dh) {
   bool debug = false;
   DBGTRC_STARTING(debug, TRACE_GROUP, "dh = %s", dh_repr(dh));

   const int MFG_NAME_BUF_SIZE = 100;

   static GPrivate  buf_key = G_PRIVATE_INIT(g_free);
   char * mfg_name_buf = get_thread_fixed_buffer(&buf_key, MFG_NAME_BUF_SIZE);

   char * mfg_name = NULL;
   DDCA_Any_Vcp_Value * valrec;

   DDCA_Status ddcrc = 0;
   Error_Info * ddc_excp = ddc_get_vcp_value(dh, 0xc8, DDCA_NON_TABLE_VCP_VALUE, &valrec);
   ddcrc = (ddc_excp) ? ddc_excp->status_code : 0;

   if (ddcrc == 0) {
      DDCA_Feature_Value_Entry * vals = pxc8_display_controller_type_values;
      mfg_name =  sl_value_table_lookup(
                            vals,
                            valrec->val.c_nc.sl);
      if (!mfg_name) {
         g_snprintf(mfg_name_buf, MFG_NAME_BUF_SIZE,
                       "Unrecognized manufacturer code 0x%02x",
                       valrec->val.c_nc.sl);

         mfg_name = mfg_name_buf;
      }
      free_single_vcp_value(valrec);
   }
   else if (ddcrc == DDCRC_REPORTED_UNSUPPORTED || ddcrc == DDCRC_DETERMINED_UNSUPPORTED) {
      mfg_name = "Unspecified";
      errinfo_free(ddc_excp);
   }
   else {
      // if (debug) {
      //    DBGMSG("get_nontable_vcp_value(0xc8) returned %s", psc_desc(ddcrc));
      //    DBGMSG("    Try errors: %s", errinfo_causes_string(ddc_excp));
      // }
      ERRINFO_FREE_WITH_REPORT(ddc_excp, debug || IS_TRACING() || is_report_ddc_errors_enabled() );
      mfg_name = "DDC communication failed";
    }

   DBGTRC_DONE(debug, TRACE_GROUP, "Returning: %s", mfg_name);
   return mfg_name;
}


/** Shows information about a display, specified by a #Display_Ref
 *
 *  This function is used by the DISPLAY command.
 *
 *  Output is written using report functions
 *
 * @param dref   pointer to display reference
 * @param depth  logical indentation depth
 *
 * @remark
 * The detail level shown is controlled by the output level setting
 * for the current thread.
 */
void
ddc_report_display_by_dref(Display_Ref * dref, int depth) {
   bool debug = false;
   // DBGTRC_STARTING(debug, TRACE_GROUP, "dref=%s, communication flags: %s",
   //               dref_repr_t(dref), dref_basic_flags_t(dref->flags));
   DBGTRC_STARTING(debug, TRACE_GROUP, "dref=%s",  dref_repr_t(dref));
   DBGTRC_NOPREFIX(debug, TRACE_GROUP, "dref->flags: %s", interpret_dref_flags_t(dref->flags));
   TRACED_ASSERT(dref && memcmp(dref->marker, DISPLAY_REF_MARKER, 4) == 0);
   int d1 = depth+1;

   switch(dref->dispno) {
   case DISPNO_BUSY:       // -4
      rpt_vstring(depth, "Busy display");
      break;
   case DISPNO_REMOVED:  // -3
      rpt_vstring(depth, "Removed display");
      break;
   case DISPNO_PHANTOM:    // -2
      rpt_vstring(depth, "Phantom display");
      break;
   case DISPNO_INVALID:   // -1
      rpt_vstring(depth, "Invalid display");
      break;
   case 0:          // valid display, no assigned display number
      d1 = depth;   // adjust indent  ??
      break;
   default:         // normal case
      rpt_vstring(depth, "Display %d", dref->dispno);
   }

   switch(dref->io_path.io_mode) {
   case DDCA_IO_I2C:
      {
         I2C_Bus_Info * curinfo = dref->detail;
         TRACED_ASSERT(curinfo && memcmp(curinfo, I2C_BUS_INFO_MARKER, 4) == 0);

         i2c_report_active_display(curinfo, d1);
      }
      break;
   case DDCA_IO_ADL:
      PROGRAM_LOGIC_ERROR("ADL implementation removed");
      break;
   case DDCA_IO_USB:
#ifdef USE_USB
      usb_show_active_display_by_dref(dref, d1);
#else
      PROGRAM_LOGIC_ERROR("ddcutil not built with USB support");
#endif
      break;
   }

   TRACED_ASSERT(dref->flags & DREF_DDC_COMMUNICATION_CHECKED);

   DDCA_Output_Level output_level = get_output_level();

   if (output_level >= DDCA_OL_NORMAL) {
      if (!(dref->flags & DREF_DDC_COMMUNICATION_WORKING) ) {
         rpt_vstring(d1, "DDC communication failed");
         char msgbuf[100] = {0};
         char * msg = NULL;
         if (dref->dispno == DISPNO_PHANTOM) {
            if (dref->actual_display) {
               snprintf(msgbuf, 100, "Use non-phantom device %s",
                        dref_short_name_t(dref->actual_display));
               msg = msgbuf;
            }
            else {
               // should never occur
               msg = "Use non-phantom device";
            }
         }
         else {
            if (dref->io_path.io_mode == DDCA_IO_I2C)
            {
                I2C_Bus_Info * curinfo = dref->detail;
                if (curinfo->flags & I2C_BUS_EDP)
                    msg = "This is an eDP laptop display. Laptop displays do not support DDC/CI.";
                else if (curinfo->flags & I2C_BUS_LVDS)
                     msg = "This is a LVDS laptop display. Laptop displays do not support DDC/CI.";
                else if ( is_embedded_parsed_edid(dref->pedid) )
                    msg = "This appears to be a laptop display. Laptop displays do not support DDC/CI.";
                // else if ( curinfo->flags & I2C_BUS_BUSY) {
                else if ( dref->dispno == DISPNO_BUSY) {
                   rpt_label(d1, "I2C device is busy");
                   int busno = dref->io_path.path.i2c_busno;

                   GPtrArray * conflicts = collect_conflicting_drivers(busno, -1);
                   if (conflicts && conflicts->len > 0) {
                      // report_conflicting_drivers(conflicts);
                      rpt_vstring(d1, "Likely conflicting drivers: %s", conflicting_driver_names_string_t(conflicts));
                      free_conflicting_drivers(conflicts);
                   }
                   else {
                      struct stat stat_buf;
                      char buf[20];
                      g_snprintf(buf, 20, "/dev/bus/ddcci/%d", dref->io_path.path.i2c_busno);
                      // DBGMSG("buf: %s", buf);
                      int rc = stat(buf, &stat_buf);
                      // DBGMSG("stat returned %d", rc);
                      if (rc == 0)
                         rpt_label(d1, "I2C device is busy.  Likely conflict with driver ddcci.");
                   }
// #ifndef I2C_IO_IOCTL_ONLY
                   msg = "Try using option --force-slave-address";
// #endif
                }
            }
            if (output_level >= DDCA_OL_VERBOSE) {
               if (!msg) {
                  msg = "Is DDC/CI enabled in the monitor's on-screen display?";
               }
            }
         }
         if (msg) {
            rpt_vstring(d1, msg);
         }
      }
      else {    // communication working
         DDCA_MCCS_Version_Spec vspec = get_vcp_version_by_dref(dref);
         // DBGMSG("vspec = %d.%d", vspec.major, vspec.minor);
         if ( vspec.major   == 0)
            rpt_vstring(d1, "VCP version:         Detection failed");
         else
            rpt_vstring(d1, "VCP version:         %d.%d", vspec.major, vspec.minor);

         if (output_level >= DDCA_OL_VERBOSE) {
            // n. requires write access since may call get_vcp_value(), which does a write
            Display_Handle * dh = NULL;
            Public_Status_Code psc = ddc_open_display(dref, CALLOPT_ERR_MSG, &dh);
            if (psc != 0) {
               rpt_vstring(d1, "Error opening display %s, error = %s",
                                  dref_short_name_t(dref), psc_desc(psc));
            }
            else {
               // display controller mfg, firmware version
               rpt_vstring(d1, "Controller mfg:      %s", get_controller_mfg_string_t(dh) );
               rpt_vstring(d1, "Firmware version:    %s", get_firmware_version_string_t(dh));;
               ddc_close_display(dh);
            }

            if (dref->io_path.io_mode != DDCA_IO_USB) {
               rpt_vstring(d1, "Monitor returns DDC Null Response for unsupported features: %s",
                                  sbool(dref->flags & DREF_DDC_USES_NULL_RESPONSE_FOR_UNSUPPORTED));
               // rpt_vstring(d1, "Monitor returns success with mh=ml=sh=sl=0 for unsupported features: %s",
               //                    sbool(dref->flags & DREF_DDC_USES_MH_ML_SH_SL_ZERO_FOR_UNSUPPORTED));
            }
         }
         DDCA_Monitor_Model_Key mmk = monitor_model_key_value_from_edid(dref->pedid);
         // DBGMSG("mmk = %s", mmk_repr(mmk) );
         Monitor_Quirk_Data * quirk = get_monitor_quirks(&mmk);
         if (quirk) {
            char * msg = NULL;
            switch(quirk->quirk_type) {
            case  MQ_NONE:
               break;
            case MQ_NO_SETTING:
               msg = "WARNING: Setting feature values has been reported to permanently cripple this monitor!";
               break;
            case MQ_NO_MFG_RANGE:
               msg = "WARNING: Setting manufacturer reserved features has been reported to permanently cripple this monitor!";
               break;
            case MQ_OTHER:
               msg = quirk->quirk_msg;
            }
            if (msg)
               rpt_vstring(d1, msg);
         }
      }
   }

   DBGTRC_DONE(debug, TRACE_GROUP, "");
}


/** Reports all displays found.
 *
 * Output is written to the current report destination using report functions.
 *
 * @param   include_invalid_displays  if false, report only valid displays\n
 *                                    if true,  report all displays
 * @param   depth       logical indentation depth
 *
 * @return total number of displays reported
 */
int
ddc_report_displays(bool include_invalid_displays, int depth) {
   bool debug = false;
   DBGMSF(debug, "Starting");

   ddc_ensure_displays_detected();

   int display_ct = 0;
   GPtrArray * all_displays = ddc_get_all_displays();
   for (int ndx=0; ndx<all_displays->len; ndx++) {
      Display_Ref * dref = g_ptr_array_index(all_displays, ndx);
      TRACED_ASSERT(memcmp(dref->marker, DISPLAY_REF_MARKER, 4) == 0);
      if (dref->dispno > 0 || include_invalid_displays) {
         display_ct++;
         ddc_report_display_by_dref(dref, depth);
         rpt_title("",0);
      }
   }
   if (display_ct == 0) {
      rpt_vstring(depth, "No %sdisplays found.", (!include_invalid_displays) ? "active " : "");
      if ( get_output_level() >= DDCA_OL_NORMAL ) {
         rpt_label(depth, "Is DDC/CI enabled in the monitor's on screen display?");
         rpt_label(depth, "Run \"ddcutil environment\" to check for system configuration problems.");
      }
   }

   DBGMSF(debug, "Done.     Returning: %d", display_ct);
   return display_ct;
}


/** Debugging function to display the contents of a #Display_Ref.
 *
 * @param dref  pointer to #Display_Ref
 * @param depth logical indentation depth
 */
void
ddc_dbgrpt_display_ref(Display_Ref * dref, int depth) {
   int d1 = depth+1;
   int d2 = depth+2;
   // no longer needed for i2c_dbgreport_bus_info()
   // DDCA_Output_Level saved_output_level = get_output_level();
   // set_output_level(DDCA_OL_VERBOSE);
   rpt_structure_loc("Display_Ref", dref, depth);
   rpt_int("dispno", NULL, dref->dispno, d1);

   // rpt_vstring(d1, "dref: %p:", dref->dref);
   dbgrpt_display_ref(dref, d1);

   rpt_vstring(d1, "edid: %p (Skipping report)", dref->pedid);
   // report_parsed_edid(drec->edid, false, d1);

   rpt_vstring(d1, "io_mode: %s", io_mode_name(dref->io_path.io_mode));
   // rpt_vstring(d1, "flags:   0x%02x", drec->flags);
   switch(dref->io_path.io_mode) {
   case(DDCA_IO_I2C):
         rpt_vstring(d1, "I2C bus information: ");
         I2C_Bus_Info * businfo = dref->detail;
         TRACED_ASSERT( memcmp(businfo->marker, I2C_BUS_INFO_MARKER, 4) == 0);
         i2c_dbgrpt_bus_info(businfo, d2);
         break;
   case(DDCA_IO_ADL):
         PROGRAM_LOGIC_ERROR("ADL implementation removed");
         break;
   case(DDCA_IO_USB):
#ifdef USE_USB
         rpt_vstring(d1, "USB device information: ");
         Usb_Monitor_Info * moninfo = dref->detail;
         TRACED_ASSERT(memcmp(moninfo->marker, USB_MONITOR_INFO_MARKER, 4) == 0);
         dbgrpt_usb_monitor_info(moninfo, d2);
#else
         PROGRAM_LOGIC_ERROR("Built without USB support");
#endif
   break;
   }

   // set_output_level(saved_output_level);
}


/** Emits a debug report a GPtrArray of display references
 *
 *  @param msg       initial message line
 *  @param ptrarray  array of pointers to #Display_Ref
 *  @param depth     logical indentation depth
 */
void
ddc_dbgrpt_drefs(char * msg, GPtrArray * ptrarray, int depth) {
   int d1 = depth + 1;
   rpt_vstring(depth, "%s", msg);
   if (ptrarray->len == 0)
      rpt_vstring(d1, "None");
   else {
      for (int ndx = 0; ndx < ptrarray->len; ndx++) {
         Display_Ref * dref = g_ptr_array_index(ptrarray, ndx);
         TRACED_ASSERT(dref);
         dbgrpt_display_ref(dref, d1);
      }
   }
}


#ifdef UNUSED
void dbgrpt_valid_display_refs(int depth) {
   rpt_vstring(depth, "Valid display refs = all_displays:");
   if (all_displays) {
      if (all_displays->len == 0)
         rpt_vstring(depth+1, "None");
      else {
         for (int ndx = 0; ndx < all_displays->len; ndx++) {
            Display_Ref * dref = g_ptr_array_index(all_displays, ndx);
            rpt_vstring(depth+1, "%p, dispno=%d", dref, dref->dispno);
         }
      }
   }
   else
      rpt_vstring(depth+1, "all_displays == NULL");
}
#endif


void init_ddc_display_ref_reports() {
   RTTI_ADD_FUNC(get_controller_mfg_string_t);
   RTTI_ADD_FUNC(get_firmware_version_string_t);
   RTTI_ADD_FUNC(ddc_report_display_by_dref);
}


