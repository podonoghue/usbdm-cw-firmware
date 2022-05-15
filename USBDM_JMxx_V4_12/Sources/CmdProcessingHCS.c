/*! \file
    \brief USBDM - common HCS12, HCS08, RS08 & Coldfire V1 BDM commands.

    This file processes the commands received over the USB link from the host

   \verbatim
   This software was modified from \e TBLCF software
   This software was modified from \e TBDML software

   USBDM
   Copyright (C) 2007  Peter O'Donoghue

   Turbo BDM Light
   Copyright (C) 2005  Daniel Malik

   Turbo BDM Light ColdFire
   Copyright (C) 2005  Daniel Malik

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
   \endverbatim

   \verbatim
   Change History
   +========================================================================================
   | 27 Dec 2012 | Improved waiting for reset rise (prevent USB timeouts)              - pgo V4.10.4
   | 26 Oct 2012 | Added HCS08 & HCS12 fast read/write                                 - pgo V4.10.4
   | 27 Jan 2012 | Added setBdmprr() & associated changes (HCS12 - Global access)      - pgo V4.9
   |  1 Oct 2011 | Improved error checking on HCS08 reads & writes                     - pgo V4.7
   | 24 Feb 2011 | Extended auto-connect options                                       - pgo V4.6
   | 10 Feb 2011 | Changed f_CMD_RESET() to do reset only, re-connect now done in DLLs - pgo V4.5 
   | 10 May 2010 | Changed to accommodate changes to Vpp interface                     - pgo 
   | 14 Apr 2010 | f_CMD_RESET() - fixed case overlap (multiple resets occuring)       - pgo
   | 14 Apr 2010 | f_CMD_CONNECT() - made status re-write safer for CFV1               - pgo
   | 14 Apr 2010 | f_CMD_CONNECT() - fixed status re-write so only done if needed      - pgo
   |  4 Feb 2010 | f_CMD_RESET() now supports extended modes & re-connects more often  - pgo
   | 15 Sep 2009 | Changed to use of bdm_physicalConnect() in f_CMD_READ_STATUS_REG()  - pgo
   |    Sep 2009 | Major changes for V2                                                - pgo
   -========================================================================================
   |  4 Feb 2010 | Changes to CMD_SET_SPEED() to reduce speed checks                   - pgo
   |  1 Apr 2009 | Changes to CMD_CONNECT to improve Alt CLK                           - pgo
   | 27 Dec 2008 | Added option BDM_OPT_SET_INTERFACELEVEL                             - pgo
   | 10 Dec 2008 | Added MC51AC256 Hack                                                - pgo
   | 23 Jul 2008 | Added ICP code                                                      - pgo
   | 13 Jul 2008 | Moved commandBuffer from Z_PAGE to make larger                      - pgo
   | 10 Jul 2008 | Changed re-connect strategy                                         - pgo
   | 20 Jun 2008 | Fixed RS08 CMD_WRITE_8 when doing Flash prog.                       - pgo
   | 20 Jun 2008 | Changed autoconnect condition                                       - pgo
   | 13 Jun 2008 | Changed mem_write/read [coldfire]                                   - pgo
   |  8 Jun 2008 | Added CFv1 code                                                     - pgo
   | 11 Apr 2008 | Added reconnect code [SYNC on READ_STATUS]                          - pgo
   | 11 Apr 2008 | Added option code                                                   - pgo
   | 23 Mar 2008 | Changed return code to use BDM_RC_... codes                         - pgo
   |  3 Mar 2008 | Started changes for JM60 - lots                                     - pgo
   +========================================================================================
   \endverbatim
*/
#include <string.h>
#include "Common.h"
#include "Configure.h"
#include "Commands.h"
#include "BDM.h"
#include "BDM_RS08.h"
#include "BDMMacros.h"
#include "BDMCommon.h"
#include "CmdProcessing.h"
#include "CmdProcessingHCS.h"
#include "TargetDefines.h"

//======================================================================
//======================================================================
//======================================================================

#pragma MESSAGE DISABLE C4000 // Disable warnings about always true

/*!
 * Write an arbitrary command using BDM protocol
 * 
 * @return 
 *     == \ref BDM_RC_OK => success        \n
 *     != \ref BDM_RC_OK => error
 */
uint8_t f_CMD_CUSTOM_COMMAND(void) {
	BDM_CMD_0_0_NOACK(_BDMZ12_ERASE_FLASH);
	bdm_wait64();
	BDM_CMD_0_0_NOACK(_BDMZ12_ERASE_FLASH);
	bdm_wait64();
	return BDM_RC_OK;
}

//! HCS12/HCS08/RS08/CFV1 - Try to connect to the target
//!
//! @return
//!    == \ref BDM_RC_OK => success        \n
//!    != \ref BDM_RC_OK => error
//!
uint8_t f_CMD_CONNECT(void) {
uint8_t rc;

   rc = bdm_connect();
   if ((rc == BDM_RC_OK) && (bdm_option.useAltBDMClock != CS_DEFAULT)) {
      uint8_t bdm_sts;

      // Re-write Status/control reg. since Force BDM clock is active
      rc = bdm_readBDMStatus(&bdm_sts);
      if (rc != BDM_RC_OK) {
         return rc;
      }
      if (cable_status.target_type == T_CFV1) {
         bdm_sts &= ~(CFV1_XCSR_SEC); // Make sure we don't accidently erase the chip!
      }
      rc = bdm_writeBDMControl(bdm_sts);
      if (rc != BDM_RC_OK) {
         return rc;
      }
      rc = bdm_connect(); // Re-connect in case speed changed from above
   }
   return rc;
}

#pragma MESSAGE DEFAULT C4000 // Restore warnings about always true

//! HCS12/HCS08/RS08/CFV1 -  Set comm speed to user supplied value
//!
//! @note
//!  commandBuffer                                 \n
//!  - [1..2] = 16-bit Sync value in 60MHz ticks
//!
//! @return
//!    == \ref BDM_RC_OK => success        \n
//!    != \ref BDM_RC_OK => error
//!
uint8_t f_CMD_SET_SPEED(void) {
uint16_t syncValue = *(uint16_t*)(commandBuffer+2); // Save the new speed
uint8_t rc;

   if (syncValue == 0) {
      cable_status.speed = SPEED_NO_INFO; // Set to unknown (re-enable auto detection etc.)
      // Try to connect
      return f_CMD_CONNECT();
   }
   cable_status.sync_length = syncValue;

   rc = bdm_RxTxSelect(); // Drivers available for this frequency?
   if (rc != BDM_RC_OK) {
      cable_status.sync_length  = 1;
      cable_status.speed        = SPEED_NO_INFO; // Connection cannot be established at this speed
      cable_status.ackn         = WAIT;          // Clear indication of ACKN feature
      return rc;
   }
   cable_status.speed       = SPEED_USER_SUPPLIED; // User told us (even if it doesn't work!)

//   if (cable_status.target_type == T_HC12) {
//      rc = bdmHC12_confirmSpeed(sync_length); // Confirm operation at that speed
//      if (rc != BDM_RC_OK) // Failed
//         return rc;
//   }

   bdm_acknInit();               // Try ACKN feature
   return bdm_enableBDM();       // Try to enable BDM
}

//! HCS12,HCS08,RS08 & CFV1 -  Read current speed
//!
//! @return
//!    == \ref BDM_RC_OK => success                \n
//!    != \ref BDM_RC_OK => error                  \n
//!                                                \n
//!  commandBuffer                                 \n
//!   - [1..2] => 16-bit Sync value in 60MHz ticks
//!
uint8_t f_CMD_GET_SPEED(void) {
   *(uint16_t*)(commandBuffer+1) = cable_status.sync_length;
   returnSize = 3;
   return BDM_RC_OK;
}

////! Directly manipulate BDM interface
////!
////! @note
////!  commandBuffer                                                \n
////!  - [2]    => 8-bit time interval in 10us ticks [ignored]      \n
////!  - [3..4] => interface level [see \ref InterfaceLevelMasks_t]
////!
////! @return
////!   BDM_RC_OK => success
////!
//uint8_t f_CMD_CONTROL_INTERFACE(void) {
//uint16_t  value  = *(uint16_t*)(commandBuffer+3);
//
//   *(uint16_t*)(commandBuffer+1) = bdm_setInterfaceLevel((uint8_t)value);
//   returnSize = 3;
//   return BDM_RC_OK;
//}

//! CFV1 -  Used to reset the CFV1 target interface from overrun condition
//!
//! @return
//!    == \ref BDM_RC_OK   => success       \n
//!    != \ref BDM_RC_FAIL => error         \n
//!
static uint8_t resetCFV1Interface(void) {
uint8_t status;
uint8_t attempt;
uint8_t rc;

   for (attempt=0; attempt < 3; attempt++) {
      // Try CFV1 recovery process
      rc = bdm_connect();         // Reset BDM interface & reconnect
      if (rc != BDM_RC_OK)
         return rc;
      switch(attempt) {
         case 0:
            break;
         case 1: // Issue background command and try again
            BDMCF_CMD_BACKGROUND();
            break;
         case 2: // Reset target and try again
            bdm_softwareReset(RESET_SPECIAL);
            break;
      }
      rc = BDMCF_CMD_NOP();               // Issue NOP
      if (rc != BDM_RC_OK)
         continue;
      rc = bdm_readBDMStatus(&status);    // Re-read status
      if (rc != BDM_RC_OK)
         continue;

      // Interface should now be IDLE
      if ((status & CFV1_XCSR_CSTAT) == CFV1_XCSR_CSTAT_OK)
         return BDM_RC_OK;
   }
   return rc;
}

//! HCS12/HCS08/RS08/CFV1 -  Read Target BDM Status Register
//!
//! @return
//!    == \ref BDM_RC_OK => success       \n
//!    != \ref BDM_RC_OK => error         \n
//!                                       \n
//!  commandBuffer                        \n
//!  - [1..4] => 8-bit Status register [MSBs are zero]
//!
uint8_t f_CMD_READ_STATUS_REG(void) {
uint8_t rc;
   returnSize = 5;
   commandBuffer[1] = 0;
   commandBuffer[2] = 0;
   commandBuffer[3] = 0;
   
   rc = optionalReconnect(AUTOCONNECT_STATUS);
   if (rc != BDM_RC_OK) {
      return rc;
   }
   rc = bdm_readBDMStatus(commandBuffer+4);
   if (rc != BDM_RC_OK) {
      return rc;
   }
   if ((cable_status.target_type == T_CFV1) &&
       ((commandBuffer[4] & CFV1_XCSR_CSTAT) == CFV1_XCSR_CSTAT_OVERRUN)) {
         // Try CFV1 recovery process
         rc = resetCFV1Interface();
         if (rc != BDM_RC_OK)
            return rc;
         rc = bdm_readBDMStatus(commandBuffer+4);
   }
   return rc;
}

//! HCS12/HCS08/RS08/CFV1 -  Write Target BDM Control Register
//!
//! @note
//!  commandBuffer                                          \n
//!   - [2..5] => 8-bit control register value [MSBs ignored]
//!
//! @return
//!    == \ref BDM_RC_OK => success       \n
//!    != \ref BDM_RC_OK => error         \n
//!
uint8_t f_CMD_WRITE_CONTROL_REG(void) {
   return bdm_writeBDMControl(commandBuffer[5]);
}

//! HCS12/HCS08/RS08/CFV1 -  Reset Target
//!
//! @note
//!  commandBuffer                                          \n
//!   - [2] => 8-bit reset control [see \ref TargetMode_t]
//!
//! @return
//!    == \ref BDM_RC_OK => success       \n
//!    != \ref BDM_RC_OK => error         \n
//!
uint8_t f_CMD_RESET(void) {
register uint8_t mode = commandBuffer[2]&RESET_MODE_MASK;
uint8_t rc = BDM_RC_OK;

   // This may take a while
   setBDMBusy();
   
   rc = optionalReconnect(AUTOCONNECT_STATUS);
   if (rc != BDM_RC_OK) {
      return rc;
   }
   cable_status.bdmpprValue = 0x00;
   
   // ToDo - check more
   switch (commandBuffer[2] & RESET_TYPE_MASK) {
#if (HW_CAPABILITY&CAP_RST_IO)   
      case RESET_HARDWARE :
         rc = bdm_hardwareReset(mode);
         break;
#endif         
      case RESET_SOFTWARE :
         rc = bdm_softwareReset(mode);
         break;
#if (HW_CAPABILITY&CAP_VDDCONTROL)   
      case RESET_POWER :
         rc =  bdm_cycleTargetVdd(mode);
         break;
#endif         
      case RESET_ALL :
      default:
         rc = bdm_targetReset(mode);
   }
   if (cable_status.speed != SPEED_USER_SUPPLIED) {
	   // Assume we have lost connection after reset attempt
      cable_status.speed  = SPEED_NO_INFO;
      bdm_rx_ptr          = bdm_rxEmpty;       // Clear the Tx/Rx pointers
      bdm_tx_ptr          = bdm_txEmpty;       //    i.e. no routines found
   }
   cable_status.reset  = NO_RESET_ACTIVITY; // BDM resetting the target doesn't count as a reset!
   cable_status.ackn   = WAIT;              // ACKN feature is disabled after reset

#if 0
   if (rc != BDM_RC_OK) {
     return rc;
   }
   if (cable_status.speed == SPEED_USER_SUPPLIED) {          // User specified speed?
      (void)bdmHC12_confirmSpeed(cable_status.sync_length);  // Confirm we can still operate at that speed
      // ToDo - check what should be done with rc
      (void)bdm_enableBDM();                                 //  & enable BDM mode
      }
   else if ((bdm_option.autoReconnect) || (cable_status.speed == SPEED_SYNC))
      // Re-connect if Auto re-connect enabled or it's quick to do (ACKN was found previously)
      (void)bdm_connect();             //    Done even if no SYNC feature - may be slow!
   else  {
      cable_status.speed  = SPEED_NO_INFO;   // Indicate we no longer have a connection
   }
#endif

   return rc;
}

// HCS12/HCS08/RS08/CFV1 -  Step over 1 instruction
//
//! @return
//!    == \ref BDM_RC_OK => success       \n
//!    != \ref BDM_RC_OK => error         \n
//!
uint8_t f_CMD_STEP(void) {
   (void)bdm_step();
   return BDM_RC_OK; //! Todo check rc?
}

// HCS12/HCS08/RS08/CFV1 -  Start code execution
//
//! @return
//!    == \ref BDM_RC_OK => success       \n
//!    != \ref BDM_RC_OK => error         \n
//!
uint8_t f_CMD_GO(void) {
   (void)bdm_go();
   return BDM_RC_OK; //! Todo check rc?
}

// HCS12/HCS08/RS08/CFV1 -  Stop the target
//
//! @return
//!    == \ref BDM_RC_OK => success       \n
//!    != \ref BDM_RC_OK => error         \n
//!
uint8_t f_CMD_HALT(void) {
   return bdm_halt();
}

//===================================================
//

//! HCS12 Write debug register/memory map
//!
//! @note
//!  commandBuffer                                       \n
//!  - [2..3] => 16-bit register number [MSB ignored]    \n
//!  - [4..7] => 32-bit register value  [MSBs ignored]
//!
//! @return
//!    == \ref BDM_RC_OK => success       \n
//!    != \ref BDM_RC_OK => error
//!
uint8_t f_CMD_WRITE_BD(void) {
   uint16_t addr = *(uint16_t*)(commandBuffer+2);
   if (addr == HC12_BDMSTS) {
	   // Access to BDMSTS is mapped to write control
      return bdm_writeBDMControl(commandBuffer[7]);
   }
   return BDM12_CMD_BDWRITEB(addr,commandBuffer[7]);
}

//! HCS12 Read debug register/memory map
//!
//! @note
//!  commandBuffer                                    \n
//!  - [2..3] => 16-bit register number [MSB ignored]
//!
//! @return
//!    == \ref BDM_RC_OK => success                   \n
//!    != \ref BDM_RC_OK => error                     \n
//!                                                   \n
//!  commandBuffer                                    \n
//!  - [1..4] => 32-bit register value  [MSBs zeroed]
//!
uint8_t f_CMD_READ_BD(void) {
   uint16_t addr = *(uint16_t*)(commandBuffer+2);
   // If reading HCS12 status use f_CMD_READ_STATUS_REG()
   if (addr == HC12_BDMSTS)
      return f_CMD_READ_STATUS_REG();

   commandBuffer[1] = 0;
   commandBuffer[2] = 0;
   commandBuffer[3] = 0;
   returnSize = 5;
   return BDM12_CMD_BDREADB(addr,commandBuffer+4);
}

//! HCS08/RS08 Write to Breakpoint reg
//!
//! @note
//!  commandBuffer                                    \n
//!  - [2..3] => 16-bit register number [ignored]     \n
//!  - [4..7] => 32-bit register value  [MSBs ignored]
//!
//! @return
//!    == \ref BDM_RC_OK => success        \n
//!    != \ref BDM_RC_OK => error
//!
uint8_t f_CMD_WRITE_BKPT(void) {
   BDM08_CMD_WRITE_BKPT(*(uint16_t *)(commandBuffer+6));
   return BDM_RC_OK; 
}

//! HCS08/RS08 Read from Breakpoint reg
//!
//! @note
//!  commandBuffer                                    \n
//!  - [2..3] => 16-bit register number [ignored]     \n
//!  - [1..4] => 32-bit register value  [MSBs zeroed]
//!
//! @return
//!    == \ref BDM_RC_OK => success                         \n
//!    != \ref BDM_RC_OK => error                           \n
//!                                                         \n
//!  commandBuffer                                          \n
//!  - [1..4] => 32-bit register value  [some MSBs ignored]
//!
uint8_t f_CMD_READ_BKPT(void) {
   commandBuffer[1] = 0;
   commandBuffer[2] = 0;
   BDM08_CMD_READ_BKPT((uint16_t*)(commandBuffer+3));
   returnSize = 5;
   return BDM_RC_OK;
}

#if 0
#pragma MESSAGE DISABLE C4001 // Disable warnings about condition always true
//! HCS12/RS08/HCS08  Read all registers
//!
//! @return
//!    == \ref BDM_RC_OK => success       \n
//!    != \ref BDM_RC_OK => error         \n
//!                                       \n
//!  commandBuffer                        \n
//!  - [1..] => ?-bit value
//!
uint8_t f_CMD_READ_REGS(void) {
   switch (cable_status.target_type) {
      case T_HC12:
         BDM12_CMD_READ_PC(commandBuffer+1);
         BDM12_CMD_READ_SP(commandBuffer+3);
         BDM12_CMD_READ_X(commandBuffer+5);
         BDM12_CMD_READ_Y(commandBuffer+7);
         BDM12_CMD_READ_D(commandBuffer+9);
         BDM12_CMD_BDREADW(HC12_BDMCCR,commandBuffer+12);
         returnSize = 13;
         return BDM_RC_OK;
      case T_HCS08:
         BDM08_CMD_READ_PC(commandBuffer+1);
         BDM08_CMD_READ_SP(commandBuffer+3);
         BDM08_CMD_READ_HX(commandBuffer+5);
         BDM08_CMD_READ_A(commandBuffer+7);
         BDM08_CMD_READ_CCR(commandBuffer+9);
         returnSize = 9;
         return BDM_RC_OK;
      case T_RS08:
         BDM08_CMD_READ_PC(commandBuffer+1);    // RS08 Read CCR+PC
         BDM08_CMD_READ_SP(commandBuffer+3);	   // RS08 Read Shadow PC
         commandBuffer[5] = 0;                  // RS08 doesn't have Read HX
         commandBuffer[6] = 0;
         BDM08_CMD_READ_A(commandBuffer+7);     // RS08 Read A
         commandBuffer[8] = 0;                  // RS08 doesn't have Read CCR
         returnSize = 9;
         return BDM_RC_OK;
      }
   return BDM_RC_ILLEGAL_COMMAND;
}
#pragma MESSAGE DEFAULT C4001 // Restore warnings about condition always true
#endif

//======================================================================
//======================================================================
//======================================================================

//! HCS12 -  set BDMPPR register
//!
//! @param memorySpace - used to determine if using Global address
//! @param addr23To16  - Global Page number address[23:16]
//! 
//! @return
//!    == \ref BDM_RC_OK => success        \n
//!    != \ref BDM_RC_OK => error          \n
//!
uint8_t setBdmppr(uint8_t memorySpace, uint8_t addr23To16) {
uint8_t rc = BDM_RC_OK;

   if ((memorySpace&MS_SPACE) == MS_Global) {
       // Using Global address - set BDMPPR
	  cable_status.bdmpprValue = HC12_BDMPPR_BPAE|addr23To16;
	  rc = BDM12_CMD_BDWRITEB(HC12_BDMPPR, HC12_BDMPPR_BPAE|addr23To16);
   }
   else {
      // Not using Global address - Clear BDMPPR if set
      if (cable_status.bdmpprValue != 0) {
         cable_status.bdmpprValue = 0;
         rc = BDM12_CMD_BDWRITEB(HC12_BDMPPR, 0x00);
      }
   }
   return rc;
}
#if 1
//! HCS12 -  Write block of bytes to memory
//!
//! @note
//!  commandBuffer                                   \n
//!  - [2]    = element size [ignored]/memory space  \n
//!  - [3]    = # of bytes                           \n
//!  - [4..7] = address [MSB ignored]                \n
//!  - [8..N] = data to write
//!
//! @return
//!    == \ref BDM_RC_OK => success       \n
//!    != \ref BDM_RC_OK => error         \n
//!
uint8_t f_CMD_HCS12_WRITE_MEM(void) {
   uint8_t  count       = commandBuffer[3];
   uint16_t addr        = *(uint16_t*)(commandBuffer+6);
   uint8_t  *data_ptr   = commandBuffer+8;
   uint8_t  rc          = BDM_RC_OK;

   rc = setBdmppr(commandBuffer[2], commandBuffer[5]); // element size & address[23:16]
   if (rc != BDM_RC_OK) {
      return rc;
   }
   if (addr&0x0001) {
      // Address is odd
      rc = BDM12_CMD_WRITEB(addr,*data_ptr); // write byte
      addr     +=1;                    // increment memory address
      data_ptr +=1;                    // increment buffer pointer
      count    -=1;                    // decrement count of bytes
   }
   if (commandBuffer[2]&MS_FAST) {
      // Fast word writes - corrupts X
      // Write address to X
      rc = BDM12_CMD_WRITE_X(addr-2);
      // Exclude 0xFF00-0xFFFF as BDM code in Memory map
      while ((count > 1) && (rc == BDM_RC_OK) && ((addr&0xFF00) != 0xFF00)) {
         rc = BDM12_CMD_WRITE_NEXT(*((uint16_t *)data_ptr)); // write word
         addr     +=2;                    // increment memory address
         data_ptr +=2;                    // increment buffer pointer
         count    -=2;                    // decrement count of bytes
      }
   }
   while ((count > 1) && (rc == BDM_RC_OK)) {
      // Slow Word writes
      rc = BDM12_CMD_WRITEW(addr,*((uint16_t *)data_ptr));  // write a word
      addr     +=2;                          // increment memory address
      data_ptr +=2;                          // increment buffer pointer
      count    -=2;                          // decrement count of bytes
   }
   if (count > 0) {
      // Odd last byte
      rc = BDM12_CMD_WRITEB(addr,*data_ptr);  // fetch a byte
   }
   return rc;
}
#else
//! HCS12 -  Write block of bytes to memory
//!
//! @note
//!  commandBuffer                                   \n
//!  - [2]    = element size [ignored]/memory space  \n
//!  - [3]    = # of bytes                           \n
//!  - [4..7] = address [MSB ignored]                \n
//!  - [8..N] = data to write
//!
//! @return
//!    == \ref BDM_RC_OK => success       \n
//!    != \ref BDM_RC_OK => error         \n
//!
uint8_t f_CMD_HCS12_WRITE_MEM(void) {
   uint8_t  count       = commandBuffer[3];
   uint16_t addr        = *(uint16_t*)(commandBuffer+6);
   uint8_t  *data_ptr   = commandBuffer+8;
   uint8_t  rc          = BDM_RC_OK;

   rc = setBdmppr(commandBuffer[2], commandBuffer[5]); // element size & address[23:16]
   if (rc != BDM_RC_OK) {
      return rc;
   }
   while ((count > 0) && (rc == BDM_RC_OK)) {
      if ((addr&0x0001) || (count == 1)) {
         // Address is odd or only 1 byte remaining
         rc = BDM12_CMD_WRITEB((uint16_t)addr,*data_ptr);// write byte
         addr     +=1;                    // increment memory address
         data_ptr +=1;                    // increment buffer pointer
         count    -=1;                    // decrement count of bytes
      }
      else {
         // Even address && >=2 bytes remaining
         rc = BDM12_CMD_WRITEW((uint16_t)addr,*((uint16_t *)data_ptr)); // write a word
         addr     +=2;                    // increment memory address
         data_ptr +=2;                    // increment buffer pointer
         count    -=2;                    // decrement count of bytes
      }
   }
   return rc;
}
#endif

#if 1
//! HCS12 -  Read block of data from memory
//!
//! @note
//!  commandBuffer                                   \n
//!  - [2]    = element size [ignored]/memory space  \n
//!  - [3]    = # of bytes                           \n
//!  - [4..7] = address [MSB ignored]                \n
//!
//! @return
//!    == \ref BDM_RC_OK => success       \n
//!    != \ref BDM_RC_OK => error         \n
//!                                       \n
//!  commandBuffer                        \n
//!  - [1..N] = data read
//!
uint8_t f_CMD_HCS12_READ_MEM(void) {
uint8_t  count       = commandBuffer[3];
uint16_t addr        = *(uint16_t*)(commandBuffer+6);
uint8_t  *data_ptr   = commandBuffer+1;
uint8_t  rc          = BDM_RC_OK;

   if (count>MAX_COMMAND_SIZE-1) {
      return BDM_RC_ILLEGAL_PARAMS;  // requested block+status is too long to fit into the buffer
   }
   rc = setBdmppr(commandBuffer[2], commandBuffer[5]); // element size & address[23:16]
   if (rc != BDM_RC_OK) {
	   return rc;
   }
   returnSize = count+1;
   if (addr&0x0001) {
      // Odd first byte
      rc = BDM12_CMD_READB((uint16_t)addr,data_ptr);  // fetch a byte
      addr     +=1;                    // increment memory address
      data_ptr +=1;                    // increment buffer pointer
      count    -=1;                    // decrement count of bytes
   }
   if (commandBuffer[2]&MS_FAST) {
      // Fast word reads - corrupts X
      // Write address to X
      rc = BDM12_CMD_WRITE_X(addr-2);
      // Exclude 0xFF00-0xFFFF as BDM code in Memory map
      while ((count > 1) && (rc == BDM_RC_OK) && ((addr&0xFF00) != 0xFF00)) {
         rc = BDM12_CMD_READ_NEXT((uint16_t*)data_ptr);
         addr     +=2;                    // increment memory address
         data_ptr +=2;                    // increment buffer pointer
         count    -=2;                    // decrement count of bytes
      }
   }
   while ((count > 1) && (rc == BDM_RC_OK)) {
      // Slow Word reads
      rc = BDM12_CMD_READW(addr,(uint16_t*)data_ptr);  // fetch a word
      addr     +=2;                          // increment memory address
      data_ptr +=2;                          // increment buffer pointer
      count    -=2;                          // decrement count of bytes
   }
   if (count > 0) {
      // Odd last byte
      rc = BDM12_CMD_READB((uint16_t)addr,data_ptr);  // fetch a byte
   }
   return rc;
}
#else
//! HCS12 -  Read block of data from memory
//!
//! @note
//!  commandBuffer                                   \n
//!  - [2]    = element size [ignored]/memory space  \n
//!  - [3]    = # of bytes                           \n
//!  - [4..7] = address [MSB ignored]                \n
//!
//! @return
//!    == \ref BDM_RC_OK => success       \n
//!    != \ref BDM_RC_OK => error         \n
//!                                       \n
//!  commandBuffer                        \n
//!  - [1..N] = data read
//!
uint8_t f_CMD_HCS12_READ_MEM(void) {
uint8_t  count       = commandBuffer[3];
uint16_t addr        = *(uint16_t*)(commandBuffer+6);
uint8_t  *data_ptr   = commandBuffer+1;
uint8_t  rc          = BDM_RC_OK;

   if (count>MAX_COMMAND_SIZE-1) {
      return BDM_RC_ILLEGAL_PARAMS;  // requested block+status is too long to fit into the buffer
   }
   rc = setBdmppr(commandBuffer[2], commandBuffer[5]); // element size & address[23:16]
   if (rc != BDM_RC_OK) {
	   return rc;
   }
   returnSize = count+1;
   while ((count > 0) && (rc == BDM_RC_OK)) {
      if ((addr&0x0001) || (count == 1)) {
         // Address is odd or only 1 byte remaining
         rc = BDM12_CMD_READB((uint16_t)addr,data_ptr);  // fetch a byte
         addr     +=1;                    // increment memory address
         data_ptr +=1;                    // increment buffer pointer
         count    -=1;                    // decrement count of bytes
      }
      else {
         // Even address && >=2 bytes remaining
         rc = BDM12_CMD_READW((uint16_t)addr,(uint16_t*)data_ptr);  // fetch a word
         addr     +=2;                          // increment memory address
         data_ptr +=2;                          // increment buffer pointer
         count    -=2;                          // decrement count of bytes
      }
   }
   return rc;
}
#endif

//! HCS08/RS08 -  Write block of bytes to memory
//!
//! @note
//!  commandBuffer                           \n
//!  - [2]    = element size/mode            \n
//!  - [3]    = # of bytes                   \n
//!  - [4..7] = address [MSB ignored]        \n
//!  - [8..N] = data to write
//!
//! @return
//!    == \ref BDM_RC_OK => success       \n
//!    != \ref BDM_RC_OK => error         \n
//!
uint8_t f_CMD_HCS08_WRITE_MEM(void) {
   uint8_t  count      = commandBuffer[3];
   uint16_t addr       = *(uint16_t*)(commandBuffer+6);
   uint8_t *data_ptr   = commandBuffer+8;
   uint8_t  rc         = BDM_RC_OK;

   if (cable_status.speed == SPEED_NO_INFO) {
      return BDM_RC_NO_CONNECTION;
   }
   if (commandBuffer[2]&MS_FAST) {
	   // Fast write - corrupts H:X
	   // Write address to H:X
       rc = BDM08_CMD_WRITE_HX(addr-1);
       while ((count > 0) && (rc == BDM_RC_OK)) {
          rc = BDM08_CMD_WRITE_NEXT(*data_ptr);
          data_ptr +=1;                    // increment buffer pointer
          count    -=1;                    // decrement count of bytes
       }
   }
   else {
      while ((count > 0) && (rc == BDM_RC_OK)) {
#if 0
    	 uint8_t status;
         BDM08_CMD_WRITEB_WS(addr,*data_ptr++,&status); // write data & receive status
         if (status&(HC08_BDCSCR_WSF|HC08_BDCSCR_DVF)) {
        	 // Status read may fail because of clock change! 
        	 (void)bdm_physicalConnect();
             BDM08_CMD_READSTATUS(&status);
         }
         if (status&(HC08_BDCSCR_WSF|HC08_BDCSCR_DVF)) {
             // The only 'expected' error that should occur is because the device has entered stop or wait mode
             // Don't try to recover as this requires changing the machine state.
      	    rc = BDM_RC_HCS_ACCESS_ERROR;
         }
#else
         rc = BDM08_CMD_WRITEB(addr,*data_ptr); // write byte
         data_ptr +=1;                    // increment buffer pointer
#endif
         addr     +=1;                    // increment memory address
         count    -=1;                    // decrement count of bytes
      }
   }
   return rc;
}

//! HCS08/RS08 -  Read block of data from memory
//!
//! @note
//!  commandBuffer                       \n
//!  - [2]    = element size/mode        \n
//!  - [3]    = # of bytes               \n
//!  - [4..7] = address [MSB ignored]
//!
//! @return
//!    == \ref BDM_RC_OK => success      \n
//!    != \ref BDM_RC_OK => error        \n
//!                                      \n
//!  commandBuffer                       \n
//!  - [1..N]  = data read
//!
uint8_t f_CMD_HCS08_READ_MEM(void) {
uint8_t  count      = commandBuffer[3];
uint16_t addr       = *(uint16_t*)(commandBuffer+6);
uint8_t  *data_ptr  = commandBuffer+1;
uint8_t  rc         = BDM_RC_OK;

   if (cable_status.speed == SPEED_NO_INFO) {
      return BDM_RC_NO_CONNECTION;
   }
   if (count>MAX_COMMAND_SIZE-1) {
      return BDM_RC_ILLEGAL_PARAMS;  // requested block+status is too long to fit into the buffer
   }
   returnSize = count+1;
   if (commandBuffer[2]&MS_FAST) {
	  // Write address to H:X
      rc = BDM08_CMD_WRITE_HX(addr-1);
      while ((count > 0) && (rc == BDM_RC_OK)) {
         rc = BDM08_CMD_READ_NEXT(data_ptr);
         data_ptr +=1;                    // increment buffer pointer
         count    -=1;                    // decrement count of bytes
      }
   }
   else {
      while ((count > 0) && (rc == BDM_RC_OK)) {
#if 0
    	  uint8_t buffer[2];
    	  uint8_t retry = 10;
          BDM08_CMD_READB_WS(addr,(uint16_t*)buffer); // read status & data byte
          while ((retry-->0) && (buffer[0]&(HC08_BDCSCR_DVF))) {
        	  BDM08_CMD_READ_LAST((uint16_t*)buffer);
          }
          *data_ptr++ = buffer[1];               // save data
          if (buffer[0]&(HC08_BDCSCR_DVF|HC08_BDCSCR_WSF)) {
             // The only 'expected' error that should occur is because the device is in stop or wait mode
             // Don't try to recover as this requires changing the machine state.
         	 rc = BDM_RC_HCS_ACCESS_ERROR;
          }
#else
         rc = BDM08_CMD_READB(addr,data_ptr);    // fetch a byte
         data_ptr +=1;                           // increment buffer pointer
#endif
         addr     +=1;                           // increment memory address
         count    -=1;                           // decrement count of bytes
      }
   }
   return rc;
}

//======================================================================
//======================================================================
//======================================================================


//! HCS12 Write core register
//!
//! @note
//!  commandBuffer                                          \n
//!  - [2..3] => 16-bit register number [MSB ignored]      \n
//!  - [4..7] => 32-bit register value  [some MSBs ignored]
//!
//! @return
//!    == \ref BDM_RC_OK => success       \n
//!    != \ref BDM_RC_OK => error         \n
//!
uint8_t f_CMD_HCS12_WRITE_REG(void) {
   if ((commandBuffer[3]<HCS12_RegPC) || (commandBuffer[3]>HCS12_RegSP)) {
      return BDM_RC_ILLEGAL_PARAMS;
   }
   return BDM12_CMD_WRITE_REG(commandBuffer[3],*(uint16_t*)(commandBuffer+6));
}

//! HCS12 Read core register
//!
//! @note
//!  commandBuffer                                       \n
//!  - [2..3] => 16-bit register number [MSB ignored]    \n
//!
//! @return
//!    == \ref BDM_RC_OK => success                         \n
//!    != \ref BDM_RC_OK => error                           \n
//!                                                         \n
//!  commandBuffer                                          \n
//!  - [1..4] => 32-bit register value  [some MSBs ignored]
//!
uint8_t f_CMD_HCS12_READ_REG(void) {
   commandBuffer[1] = 0;
   commandBuffer[2] = 0;
   returnSize = 5;

   if ((commandBuffer[3]<HCS12_RegPC) || (commandBuffer[3]>HCS12_RegSP)) {
      return BDM_RC_ILLEGAL_PARAMS;
   }
   return BDM12_CMD_READ_REG(commandBuffer[3],(uint16_t*)(commandBuffer+3));
}

//! RS08/HCS08 Write core register
//!
//! @note
//!  commandBuffer                                         \n
//!  - [2..3] => 16-bit register number [MSB ignored]      \n
//!  - [4..7] => 32-bit register value  [some MSBs ignored]
//!
//! @return
//!    == \ref BDM_RC_OK => success       \n
//!    != \ref BDM_RC_OK => error         \n
//!
uint8_t f_CMD_HCS08_WRITE_REG(void) {
uint16_t value = *(uint16_t*)(commandBuffer+6);
uint8_t rc = BDM_RC_ILLEGAL_PARAMS;

   switch (commandBuffer[3]) {
      case HCS08_RegPC :  // RS08_RegCCR_PC :
         rc = BDM08_CMD_WRITE_PC(value);
         break;
      case HCS08_RegHX  :
         if (cable_status.target_type == T_HCS08)
            rc = BDM08_CMD_WRITE_HX(value);
         break;
      case HCS08_RegSP : // RS08_RegSPC
         rc = BDM08_CMD_WRITE_SP(value);
         break;
      case HCS08_RegA  :  // RS08_RegA
         rc = BDM08_CMD_WRITE_A((uint8_t)value);
         break;
      case HCS08_RegCCR  :
         if (cable_status.target_type == T_HCS08)
            rc = BDM08_CMD_WRITE_CCR((uint8_t)value);
         break;
   }
   return rc;
}

//! RS08/HCS08 Read core register
//!
//! @note
//!  commandBuffer                                         \n
//!  - [2..3] => 16-bit register number [MSB ignored]      \n
//!
//! @return
//!    == \ref BDM_RC_OK => success                         \n
//!    != \ref BDM_RC_OK => error                           \n
//!                                                         \n
//!  commandBuffer                                          \n
//!  - [1..4] => 32-bit register value  [some MSBs ignored]
//!
uint8_t f_CMD_HCS08_READ_REG(void) {
uint8_t rc = BDM_RC_ILLEGAL_PARAMS;

   commandBuffer[1] = 0;
   commandBuffer[2] = 0;

   switch (commandBuffer[3]) {
      case HCS08_RegPC : // RS08_RegCCR_PC :
         rc = BDM08_CMD_READ_PC((uint16_t*)(commandBuffer+3));
         break;
      case HCS08_RegHX  :
         if (cable_status.target_type == T_HCS08)
            rc = BDM08_CMD_READ_HX((uint16_t*)(commandBuffer+3));
         break;
      case HCS08_RegSP : // RS08_RegSPC
         rc = BDM08_CMD_READ_SP((uint16_t*)(commandBuffer+3));
         break;
      case HCS08_RegA  : // RS08_RegA
         commandBuffer[3] = 0;
         rc = BDM08_CMD_READ_A(commandBuffer+4);
         break;
      case HCS08_RegCCR :
         commandBuffer[3] = 0;
         if (cable_status.target_type == T_HCS08)
            rc = BDM08_CMD_READ_CCR(commandBuffer+4);
         break;
   }
   returnSize = 5;
   return rc;
}

//=============================================================
//==   RS08 Code                                             ==
//=============================================================
#if (HW_CAPABILITY & CAP_FLASH)

//! Control target VPP level
//!
//! @note
//!  commandBuffer                 \n
//!  - [2] =>       (FlashState_t) control value for VPP \n
//!
//! @return
//!     BDM_RC_OK   => success \n
//!     else        => Error
//!
uint8_t f_CMD_SET_VPP(void) {
   return bdmSetVpp(commandBuffer[2]);
}
#endif // (HW_CAPABILITY & CAP_FLASH)
