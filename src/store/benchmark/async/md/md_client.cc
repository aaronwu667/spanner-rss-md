#include "store/benchmark/async/md/md_client.h"
#include "store/benchmark/async/common/key_selector.h"
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
                       retryAborted, maxBackoff, maxAttempts, id, latencyFilename),
       keySelector{keySelector},
       workType{work},
       ind{0}{
          
     }

     MDClient::~MDClient() {
     }


     AsyncTransaction* MDClient::GetNextTransaction() {
          AsyncTransaction* res;
          if (workType == WO) {
               res = new WriteOnly(keySelector, GetRand(), std::to_string(cid), std::to_string(csn));
               csn++;
               return res;
          } else if (workType == MIX) {
               if (ind % 10 == 0 && ind != 0) {
                    res = new WriteOnly(keySelector, GetRand(), std::to_string(cid), std::to_string(csn));
                    csn++;
               } else {
                    std::string test = std::to_string(csn - 1);
                    Debug("Dep %s", test.c_str());
                    res = new ReadOnly(keySelector, GetRand(), test);
               }
               ind++;
               return res;
          } else {
               Panic("Unknown workload type");
          }
     }  
}
