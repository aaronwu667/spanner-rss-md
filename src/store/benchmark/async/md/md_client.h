// -*-c++-*-
#ifndef MD_CLIENT_H
#define MD_CLIENT_H

#include <cstdint>
#include <string>
#include <vector>

#include "store/benchmark/async/bench_client.h"
#include "store/benchmark/async/common/key_selector.h"
#include "store/benchmark/async/retwis/retwis_transaction.h"
#include "store/common/frontend/client.h"

// single client end-to-end latency test
namespace md {

     enum WorkloadType {
          WO,
          MIX
     };

     class MDClient : public BenchmarkClient {
     public:
          MDClient(KeySelector *keySelector, const std::vector<Client *> &clients, uint32_t timeout,
                   Transport &transport, uint64_t seed,
                   BenchmarkClientMode mode,
                   double switch_probability,
                   double arrival_rate, double think_time, double stay_probability,
                   int mpl,
                   int expDuration, int warmupSec, int cooldownSec, int tputInterval, uint32_t abortBackoff,
                   bool retryAborted, uint32_t maxBackoff, uint32_t maxAttempts,
                   uint64_t id, WorkloadType work = WO, const std::string &latencyFilename = "latency");

          virtual ~MDClient();

     protected:
          virtual AsyncTransaction *GetNextTransaction() override;

     private:
          KeySelector *keySelector;
          std::string lastOp;
          WorkloadType workType;
          uint32_t ind;
     };

}

#endif 
