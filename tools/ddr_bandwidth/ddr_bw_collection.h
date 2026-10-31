#ifndef DDR_BW_COLLECTION_H
#define DDR_BW_COLLECTION_H

#include <signal.h>
#include "ddr_bw_cli.h"
#include "ddr_bw_pmu.h"
#include "ddr_bw_types.h"

int RunDdrBandwidthCollection(ArgsContext& act, volatile sig_atomic_t* stopRequested, volatile int* execErrNo);

#endif
