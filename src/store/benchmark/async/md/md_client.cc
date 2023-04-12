#include "store/benchmark/async/md/md_client.h"
#include "store/common/frontend/async_transaction.h"
#include "store/benchmark/async/md/write_only.h"
#include "store/benchmark/async/md/read_only.h"

#include <iostream>
#include <string>

namespace md {
  MDClient::MDClient(KeySelector *keySelector, const std::vector<Client *> &clients, uint32_t timeout,
                           Transport &transport, uint64_t seed,
                           BenchmarkClientMode mode,
                           double switch_probability,
                           double arrival_rate, double think_time, double stay_probability,
                           int mpl,
                           int expDuration, int warmupSec, int cooldownSec, int tputInterval, uint32_t abortBackoff,
                     bool retryAborted, uint32_t maxBackoff, uint32_t maxAttempts,
                     uint64_t id, WorkloadType work,
                     const std::string &latencyFilename)
  : BenchmarkClient(clients, timeout, transport, seed,
                    mode,
                    switch_probability,
                    arrival_rate, think_time, stay_probability,
                    mpl,
                    expDuration, warmupSec, cooldownSec, abortBackoff,
                    retryAborted, maxBackoff, maxAttempts, latencyFilename),
    keySelector{keySelector},
      workType{work},
      ind{0},
      csn{0},
      cid{id}
  {
  }

  MDClient::~MDClient() {
  }


  AsyncTransaction* MDClient::GetNextTransaction() {
    csn++;
    if (workType == WO) {
      return new WriteOnly(keySelector, GetRand(), std::to_string(cid), std::to_string(csn - 1));
    } else {
      Panic("Not implemented");
    }
  }  
}
