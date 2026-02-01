/* SPDX-License-Identifier: GPL-2.0 */

#ifndef INCLUDE__NEXUS_RV_MSG_H__
#define INCLUDE__NEXUS_RV_MSG_H__

//****************************************************************************
// Nexus RISC-V Trace header.

// Header with common Nexus Trace definitions

#include <stdint.h> // For uint32_t and uint64_t

//****************************************************************************
// Nexus specific values (based on Nexus Standard PDF)

#define NEXUS_FLDSIZE_TCODE                       6 // This is standard

// Nexus TCODE values applicable to RISC-V
#define NEXUS_TCODE_Ownership                     2
#define NEXUS_TCODE_DirectBranch                  3
#define NEXUS_TCODE_IndirectBranch                4
#define NEXUS_TCODE_Error                         8
#define NEXUS_TCODE_ProgTraceSync                 9
#define NEXUS_TCODE_DirectBranchSync              11
#define NEXUS_TCODE_IndirectBranchSync            12
#define NEXUS_TCODE_ResourceFull                  27
#define NEXUS_TCODE_IndirectBranchHist            28
#define NEXUS_TCODE_IndirectBranchHistSync        29
#define NEXUS_TCODE_RepeatBranch                  30
#define NEXUS_TCODE_ProgTraceCorrelation          33

// End of standard values
//****************************************************************************

//****************************************************************************
// RISC-V Nexus Trace related values (most 'recommended' by Nexus)

//  Sizes of fields:
#define NEXUS_FLDSIZE_BTYPE     2 // Branch type
#define NEXUS_FLDSIZE_SYNC      4 // Synchronization code
#define NEXUS_FLDSIZE_ETYPE     4 // Error type
#define NEXUS_FLDSIZE_EVCODE    4 // Event code (correlation)
#define NEXUS_FLDSIZE_CDF       2
#define NEXUS_FLDSIZE_RCODE     4 // Resource full code size
//  from PROCESS fields:
#define NEXUS_FLDSIZE_FORMAT    2
#define NEXUS_FLDSIZE_PRV       2
#define NEXUS_FLDSIZE_V         1

#define NEXUS_PAR_SIZE_SRC      0 // SRC field size is defined by parameter #0

#define NEXUS_HIST_BITS         31 // Number of valid HIST bits

// Address skipping (on RISC-V LSB of PC is always 0, so it is not encoded)
#define NEXUS_PARAM_AddrSkip    1
#define NEXUS_PARAM_AddrUnit    1

// End of RISC-V related values
//****************************************************************************

// Nexus RISC-V Trace message definitions (dump & decode)

// Macros to define Nexus Messages (NEXM_...)
//  NOTE: These macros refer to 'NEXUS_TCODE_...' and 'NEXUS_FLDSIZE_...'
//  It is also possible to NOT do it, but it provides less flexibility.
//
//                          name   def (marker | value)
//#define NEXM_BEG(n, t)      {#n,    0x100 | (t)                 }
#define NEXM_BEG(n, t)      {#n,    0x100 | (NEXUS_TCODE_##n)   } \
                            // , NEXM_FLD_PAR(SRC) // SRC is always following TCODE
//#define   NEXM_FLD(n, s)    {#n,    0x200 | (s)                 }
#define   NEXM_FLD(n, s)    {#n,    0x200 | (NEXUS_FLDSIZE_##n) }
#define   NEXM_FLD_PAR(n)   {#n,    0x200 | 0x80 | (NEXUS_PAR_SIZE_##n) }  // 0x80 means size is a parameter
#define   NEXM_VAR(n)       {#n,    0x400                       }
#define   NEXM_ADR(n)       {#n,    0xC00                       }
#define NEXM_END()          {NULL,  1                           }

// Definition of Nexus Messages (subset applicable to RISC-V PC trace)
static struct NEXM_MSGDEF_STRU {
  const char *name; // Name of message/field
  int def;          // Definition of field (see NEXM_... above)
} NEXUS_MSG_DEF[] = {

  NEXM_BEG(Ownership, 2),
    NEXM_FLD_PAR(SRC),
    //NEXM_VAR(PROCESS),
    NEXM_FLD(FORMAT, 2),
    NEXM_FLD(PRV, 2),
    NEXM_FLD(V, 1),
    NEXM_VAR(CONTEXT),
    NEXM_VAR(TSTAMP),
  NEXM_END(),

  NEXM_BEG(DirectBranch, 3),
    NEXM_FLD_PAR(SRC),
    NEXM_VAR(ICNT),
    NEXM_VAR(TSTAMP),
  NEXM_END(),

  NEXM_BEG(IndirectBranch, 4),
    NEXM_FLD_PAR(SRC),
    NEXM_FLD(BTYPE, 2),
    NEXM_VAR(ICNT),
    NEXM_ADR(UADDR),
    NEXM_VAR(TSTAMP),
  NEXM_END(),

  NEXM_BEG(Error, 8),
    NEXM_FLD_PAR(SRC),
    NEXM_FLD(ETYPE, 4),
    NEXM_VAR(PAD),
    NEXM_VAR(TSTAMP),
  NEXM_END(),

  NEXM_BEG(ProgTraceSync, 9),
    NEXM_FLD_PAR(SRC),
    NEXM_FLD(SYNC, 4),
    NEXM_VAR(ICNT),
    NEXM_ADR(FADDR),
    NEXM_VAR(TSTAMP),
  NEXM_END(),

  NEXM_BEG(DirectBranchSync, 11),
    NEXM_FLD_PAR(SRC),
    NEXM_FLD(SYNC, 4),
    NEXM_VAR(ICNT),
    NEXM_ADR(FADDR),
    NEXM_VAR(TSTAMP),
  NEXM_END(),

  NEXM_BEG(IndirectBranchSync, 12),
    NEXM_FLD_PAR(SRC),
    NEXM_FLD(SYNC, 4),
    NEXM_FLD(BTYPE, 2),
    NEXM_VAR(ICNT),
    NEXM_ADR(FADDR),
    NEXM_VAR(TSTAMP),
  NEXM_END(),

  NEXM_BEG(ResourceFull, 27),
    NEXM_FLD_PAR(SRC),
    NEXM_FLD(RCODE, 4),
    NEXM_VAR(RDATA),
    NEXM_VAR(HREPEAT),
    NEXM_VAR(TSTAMP),
  NEXM_END(),

  NEXM_BEG(IndirectBranchHist, 28),
    NEXM_FLD_PAR(SRC),
    NEXM_FLD(BTYPE, 2),
    NEXM_VAR(ICNT),
    NEXM_ADR(UADDR),
    NEXM_VAR(HIST),
    NEXM_VAR(TSTAMP),
  NEXM_END(),

  NEXM_BEG(IndirectBranchHistSync, 29),
    NEXM_FLD_PAR(SRC),
    NEXM_FLD(SYNC, 4),
    NEXM_FLD(BTYPE, 2),
    // NEXM_FLD(CANCEL, 1),
    NEXM_VAR(ICNT),
    NEXM_ADR(FADDR),
    NEXM_VAR(HIST),
    NEXM_VAR(TSTAMP),
  NEXM_END(),

  NEXM_BEG(RepeatBranch, 30),
    NEXM_FLD_PAR(SRC),
    NEXM_VAR(BCNT),
    NEXM_VAR(TSTAMP),
  NEXM_END(),

  NEXM_BEG(ProgTraceCorrelation, 33),
    NEXM_FLD_PAR(SRC),
    NEXM_FLD(EVCODE, 4),
    NEXM_FLD(CDF, 2),
    NEXM_VAR(ICNT),
    NEXM_VAR(HIST),   // Only if CDF=1!
    NEXM_VAR(TSTAMP),
  NEXM_END(),

  { NULL, 0 } // End-marker ('def == 0' is not otherwise used - see NEXM_...)
};

#endif
