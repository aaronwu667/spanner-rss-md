#include "store/benchmark/async/bench_client.h"

#include <cstdint>
#include <cstdlib>
#include <sys/time.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>

#include "lib/latency.h"
#include "lib/message.h"
#include "lib/timeval.h"
#include "lib/transport.h"
#include "store/benchmark/async/md/md_transaction.h"
#include "store/common/frontend/client.h"
#include "store/strongstore/client.h"

DEFINE_LATENCY(op);


BenchmarkClient::BenchmarkClient(const std::vector<Client *> &clients, uint32_t timeout,
                                 Transport &transport, uint64_t seed,
                                 BenchmarkClientMode mode,
                                 double switch_probability,
                                 double arrival_rate, double think_time, double stay_probability,
                                 int mpl,
                                 int expDuration, int warmupSec, int cooldownSec,
                                 uint32_t abortBackoff, bool retryAborted,
                                 uint32_t maxBackoff, uint32_t maxAttempts, uint64_t id,
                                 const std::string &latencyFilename)
: transport_(transport),
  session_states_{},
  clients_{clients},
  client_id_{seed},
  timeout_{timeout},
  rand_{seed},
  next_arrival_dist_{arrival_rate * 1e-6},
  think_time_dist_{1 / think_time * 1e-6},
  stay_dist_{stay_probability},
  switch_dist_{switch_probability},
  mpl_{mpl},
  exp_duration_{expDuration},
  warmupSec{warmupSec},
  cooldownSec{cooldownSec},
  latencyFilename{latencyFilename},
  maxBackoff{maxBackoff},
  abortBackoff{abortBackoff},
  retryAborted{retryAborted},
  maxAttempts{maxAttempts},
  started{false},
  done{false},
  cooldownStarted{false},
  mode_{mode},
  cid{id},
  csn{1},
  last_exec_csn{0}
{
     if (arrival_rate <= 0)
     {
          Panic("Arrival rate must be (strictly) positive!");
     }

     _Latency_Init(&latency, "txn");
}

BenchmarkClient::~BenchmarkClient()
{
     Debug("session_states_.size(): %lu", session_states_.size());
}

void BenchmarkClient::Start(bench_done_callback bdcb)
{
     n_sessions_started_ = 0;
     load_balance_ind = 0;
     n = 0;
     curr_bdcb_ = bdcb;
     transport_.Timer(warmupSec * 1000, std::bind(&BenchmarkClient::WarmupDone, this));
     gettimeofday(&startTime, NULL);

     transport_.TimerMicro(0, std::bind(&BenchmarkClient::SendNext, this));
}

void BenchmarkClient::SendNext()
{
     Debug("[%d] SendNext", n_sessions_started_);
     n_sessions_started_++;
     if (load_balance_ind == clients_.size()) {
          load_balance_ind = 0;
     } else {
          load_balance_ind++;
     }
    
     // round robin over strongstore clients
     std::size_t client_index = load_balance_ind % clients_.size();
     auto &client = *clients_[client_index];

     auto &session = client.BeginSession();
     auto sid = session.id();

     Debug("session id: %lu", sid);

     auto ecb = std::bind(&BenchmarkClient::ExecuteCallback, this, sid, std::placeholders::_1);
     auto transaction = GetNextTransaction();
     stats.Increment(transaction->GetTransactionType() + "_attempts", 1);

     session_states_.emplace(sid, SessionState{session, transaction, ecb, client_index});

     auto &ss = session_states_.find(sid)->second;
     _Latency_StartRec(ss.lat());

     auto bcb = std::bind(&BenchmarkClient::ExecuteNextOperation, this, sid);
     auto btcb = []() {};

     Operation op = transaction->GetNextOperation(0);
     switch (op.type)
     {
     case BEGIN_RO:
     case BEGIN_RW:
          client.Begin(session, bcb, btcb, timeout_);
          break;

     default:
          NOT_REACHABLE();
     }

     if (!cooldownStarted)
     {
          bool send_next = false;
          uint64_t next_arrival_us = 0;
          switch (mode_)
          {
          case BenchmarkClientMode::CLOSED:
               send_next = (n_sessions_started_ < mpl_);
               next_arrival_us = 0;
               break;
          case BenchmarkClientMode::OPEN:          
          default:
               Panic("Unexpected client mode!");
          }

          if (send_next)
          {
               Debug("next arrival in %lu us", next_arrival_us);
               transport_.TimerMicro(next_arrival_us, std::bind(&BenchmarkClient::SendNext, this));
          }
     }
}

void BenchmarkClient::ExecuteNextOperation(const uint64_t session_id)
{
     Debug("[%lu] ExecuteNextOperation", session_id);
     auto search = session_states_.find(session_id);
     ASSERT(search != session_states_.end());

     auto &ss = search->second;
     auto transaction = ss.transaction();
     auto op_index = ss.op_index();
     auto &session = ss.session();

     Operation op = transaction->GetNextOperation(op_index);
     ss.incr_op_index();

     auto gcb = std::bind(&BenchmarkClient::GetCallback, this, session_id, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
     auto gtcb = std::bind(&BenchmarkClient::GetTimeout, this, session_id, std::placeholders::_1, std::placeholders::_2);
     auto pcb = std::bind(&BenchmarkClient::PutCallback, this, session_id, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
     auto ptcb = std::bind(&BenchmarkClient::PutTimeout, this, session_id, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
     auto ccb = std::bind(&BenchmarkClient::CommitCallback, this, session_id, std::placeholders::_1);
     auto ctcb = std::bind(&BenchmarkClient::CommitTimeout, this);
     auto acb = std::bind(&BenchmarkClient::AbortCallback, this, session_id, ABORTED_USER);
     auto atcb = std::bind(&BenchmarkClient::AbortTimeout, this);
     auto client_index = ss.current_client_index();
     auto &client = *clients_[client_index];

     switch (op.type)
     {
     case GET:
          client.Get(session, op.key, gcb, gtcb, timeout_);
          break;

     case GET_FOR_UPDATE:
          client.GetForUpdate(session, op.key, gcb, gtcb, timeout_);
          break;

     case PUT:
          client.Put(session, op.key, op.value, pcb, ptcb, timeout_);
          break;

     case COMMIT:
          client.Commit(session, ccb, ctcb, timeout_);
          break;

     case ABORT:
          client.Abort(session, acb, atcb, timeout_);
          break;

     case ROCOMMIT:
          client.ROCommit(session, op.keys, ccb, ctcb, timeout_);
          break;
      
          // check for sequence number constraint
     case CHECK_SEQNO: {
          auto seq_no_cb = std::bind(&BenchmarkClient::SeqNoCallback, this,
                                     session_id, op.value,
                                     std::placeholders::_1,
                                     std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
          auto gtcb = std::bind(&BenchmarkClient::GetTimeout, this, session_id, std::placeholders::_1, std::placeholders::_2);          
          client.Get(session, op.key, seq_no_cb, gtcb, timeout_);
     }
          break;

          // check write dependency
     case CHECK_WRITE_CONST: {
          BenchmarkClient::WriteConstraintCallback(session_id, op.value);
     }

     case WAIT:
          break;

     default:
          NOT_REACHABLE();
     }
}

void BenchmarkClient::SeqNoCallback(const uint64_t session_id, const std::string &csn,
                                    int status, const std::string &key, const std::string &val, Timestamp ts)
{
     Debug("[%lu] SeqNoCallback(%s) callback", session_id, key.c_str());
     auto search = session_states_.find(session_id);
     ASSERT(search != session_states_.end());
  
     auto &ss = search->second;
     auto n_attempts = ss.n_attempts();
  
     if (status == REPLY_OK){    
          uint64_t csn_num = std::stoull(csn);
          uint64_t val_num = std::stoull(val);
          Debug("csn: [%lu]; stored csn: [%lu]", csn_num, val_num);
          if (csn_num == val_num) {
               ExecuteNextOperation(session_id);
          } else if (csn_num > val_num) {
               // need to wait until sequence number matches ours
               uint64_t backoff = 0;
               if (abortBackoff > 0)
               {
                    uint64_t exp = n_attempts - 1;
                    backoff = static_cast<uint64_t>(1000 * 50 * (std::pow(1.3, exp)));
                    backoff = std::min(backoff, 1000 * maxBackoff);
                    Debug("Backing off for %lu us: %lu", backoff, n_attempts);
               }

               transport_.TimerMicro(backoff, [this, session_id, key, csn]
                    {
                         auto search = session_states_.find(session_id);
                         ASSERT(search != session_states_.end());

                         auto &ss = search->second;
                         ss.retry_transaction();

                         stats.Increment(ss.transaction()->GetTransactionType() + "_attempts", 1);
        
                         auto seq_no_cb = std::bind(&BenchmarkClient::SeqNoCallback, this,
                                                    session_id, csn,
                                                    std::placeholders::_1,
                                                    std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
                         auto gtcb = std::bind(&BenchmarkClient::GetTimeout, this, session_id, std::placeholders::_1, std::placeholders::_2);          
                         auto &client = *clients_[ss.current_client_index()];
                         client.Get(ss.session(), key, seq_no_cb, gtcb, timeout_);
                    });
          } else {
               Panic("Transaction should have already finished");
          }
     } else if (status == REPLY_FAIL) {
          ExecuteAbort(session_id, ABORTED_SYSTEM);
     } else {
          Panic("Unknown status for Get %d.", status);
     }
}

void BenchmarkClient::WriteConstraintCallback(const uint64_t session_id, const std::string &dep) {
     // TODO: Pass in handle to libeventBase
     // This way, we can call event_active on blocked reads
     // and avoid wasted CPU time on backoffs.
     Debug("[%lu] WriteConstraint(%s) callback", session_id, dep.c_str());
     auto search = session_states_.find(session_id);
     ASSERT(search != session_states_.end());

     auto &ss = search->second;
     auto n_attempts = ss.n_attempts();
          
     uint64_t dep_num = std::stoull(dep);
     if (dep_num <= last_exec_csn) {
          ExecuteNextOperation(session_id);
     } else {
          uint64_t backoff = 0;
          if (abortBackoff > 0)
          {
               uint64_t exp = n_attempts - 1;
               backoff = static_cast<uint64_t>(1000 * 50 * (std::pow(1.3, exp)));
               backoff = std::min(backoff, 1000 * maxBackoff);
               Debug("Read-only backing off for %lu us: %lu", backoff, n_attempts);
          }

          transport_.TimerMicro(backoff, std::bind(&BenchmarkClient::WriteConstraintCallback, this, session_id, dep));
     }
}

void BenchmarkClient::ExecuteAbort(const uint64_t session_id, transaction_status_t status)
{
     Debug("[%lu] ExecuteAbort", session_id);
     auto search = session_states_.find(session_id);
     ASSERT(search != session_states_.end());

     auto &ss = search->second;
     auto transaction = ss.transaction();
     auto op_index = ss.op_index();
     auto &session = ss.session();

     auto client_index = ss.current_client_index();
     auto &client = *clients_[client_index];

     auto acb = std::bind(&BenchmarkClient::AbortCallback, this, session_id, status);
     auto atcb = std::bind(&BenchmarkClient::AbortTimeout, this);

     client.Abort(session, acb, atcb, timeout_);
}

void BenchmarkClient::GetCallback(const uint64_t session_id, int status,
                                  const std::string &key, const std::string &val, Timestamp ts)
{
     Debug("[%lu] Get(%s) callback", session_id, key.c_str());
     auto search = session_states_.find(session_id);
     ASSERT(search != session_states_.end());

     auto &ss = search->second;

     if (status == REPLY_OK)
     {
          ExecuteNextOperation(session_id);
     }
     else if (status == REPLY_FAIL)
     {
          ExecuteAbort(session_id, ABORTED_SYSTEM);
     }
     else
     {
          Panic("Unknown status for Get %d.", status);
     }
}

void BenchmarkClient::GetTimeout(const uint64_t session_id,
                                 int status, const std::string &key)
{
     Warning("[%lu] Get(%s) timed out :(", session_id, key.c_str());
     auto search = session_states_.find(session_id);
     ASSERT(search != session_states_.end());

     auto &ss = search->second;
     auto &session = ss.session();

     auto client_index = ss.current_client_index();
     auto &client = *clients_[client_index];

     auto gcb = std::bind(&BenchmarkClient::GetCallback, this, session_id, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
     auto gtcb = std::bind(&BenchmarkClient::GetTimeout, this, session_id, std::placeholders::_1, std::placeholders::_2);

     client.Get(session, key, gcb, gtcb, timeout_);
}

void BenchmarkClient::PutCallback(const uint64_t session_id, int status,
                                  const std::string &key, const std::string &val)
{
     Debug("[%lu] Put(%s,%s) callback.", session_id, key.c_str(), val.c_str());
     auto search = session_states_.find(session_id);
     ASSERT(search != session_states_.end());

     auto &ss = search->second;

     if (status == REPLY_OK)
     {
          ExecuteNextOperation(session_id);
     }
     else if (status == REPLY_FAIL)
     {
          ExecuteAbort(session_id, ABORTED_SYSTEM);
     }
     else
     {
          Panic("Unknown status for Put %d.", status);
     }
}

void BenchmarkClient::PutTimeout(const uint64_t session_id, int status,
                                 const std::string &key, const std::string &val)
{
     Warning("[%lu] Put(%s,%s) timed out :(", session_id, key.c_str(), val.c_str());
}

void BenchmarkClient::CommitCallback(const uint64_t session_id, transaction_status_t status)
{
     Debug("[%lu] Commit callback.", session_id);
     auto search = session_states_.find(session_id);
     ASSERT(search != session_states_.end());

     auto &ss = search->second;
     auto ecb = ss.ecb();

     ecb(status);
}

void BenchmarkClient::CommitTimeout()
{
     Warning("Commit timed out :(");
}

void BenchmarkClient::AbortCallback(const uint64_t session_id, transaction_status_t status)
{
     Debug("[%lu] Abort callback.", session_id);
     auto search = session_states_.find(session_id);
     ASSERT(search != session_states_.end());

     auto &ss = search->second;
     auto ecb = ss.ecb();

     ecb(status);
}

void BenchmarkClient::AbortTimeout()
{
     Warning("Abort timed out :(");
}

void BenchmarkClient::ExecuteCallback(uint64_t session_id,
                                      transaction_status_t result)
{
     Debug("[%lu] ExecuteCallback with result %d.", session_id, result);
     auto search = session_states_.find(session_id);
     ASSERT(search != session_states_.end());

     auto &ss = search->second;
     auto transaction = ss.transaction();
     auto &ttype = transaction->GetTransactionType();
     auto n_attempts = ss.n_attempts();

     if (result == COMMITTED || result == ABORTED_USER ||
         (maxAttempts != -1 && n_attempts >= static_cast<uint64_t>(maxAttempts)) ||
         !retryAborted)
     {
          if (result == COMMITTED)
          {
               stats.Increment(ttype + "_committed", 1);
               // treat mpl as number of concurrent requests at any given time
               Debug("end of session");
          }

          if (retryAborted)
          {
               stats.Add(ttype + "_attempts_list", n_attempts);
          }

          OnReply(session_id, result, true);
     }
     else
     {
          stats.Increment(ttype + "_" + std::to_string(result), 1);
          BenchmarkClient::BenchState state = GetBenchState();
          Debug("Current bench state: %d.", state);
          if (state == DONE)
          {
               OnReply(session_id, ABORTED_SYSTEM, true);
          }
          else
          {
               uint64_t backoff = 0;
               if (abortBackoff > 0)
               {
                    uint64_t exp = n_attempts - 1;
                    backoff = static_cast<uint64_t>(1000 * 50 * (std::pow(1.3, exp)));
                    backoff = std::min(backoff, 1000 * maxBackoff);
                    Debug("Backing off for %lu us: %lu", backoff, n_attempts);
               }

               transport_.TimerMicro(backoff, [this, session_id]
                    {
                         auto search = session_states_.find(session_id);
                         ASSERT(search != session_states_.end());

                         auto &ss = search->second;
                         ss.retry_transaction();

                         stats.Increment(ss.transaction()->GetTransactionType() + "_attempts", 1);

                         auto bcb = std::bind(&BenchmarkClient::ExecuteNextOperation, this, session_id);
                         auto btcb = []() {};

                         auto &client = *clients_[ss.current_client_index()];
                         client.Retry(ss.session(), bcb, btcb, timeout_); });
          }
     }
}

void BenchmarkClient::WarmupDone()
{
     started = true;
     Notice("Completed warmup period of %d seconds with %d requests", warmupSec, n);
     n = 0;
}

void BenchmarkClient::CleanupContinue()
{
     auto n = session_states_.size();
     Notice("Waiting for %lu outstanding transactions.", n);

     if (n > 0)
     {
          transport_.TimerMicro(1e6, std::bind(&BenchmarkClient::CleanupContinue, this));
     }
     else
     {
          CooldownDone();
     }
}

void BenchmarkClient::Cleanup()
{
     auto n = session_states_.size();
     Notice("Aborting %lu outstanding transactions.", n);

     if (n > 0)
     {
          for (auto &kv : session_states_)
          {
               auto transaction_id = kv.first;
               auto &ss = kv.second;

               auto op_index = ss.op_index();

               auto client_index = ss.current_client_index();
               auto &client = *clients_[client_index];

               client.ForceAbort(transaction_id);
          }

          transport_.TimerMicro(1e6, std::bind(&BenchmarkClient::CleanupContinue, this));
     }
     else
     {
          CooldownDone();
     }
}

void BenchmarkClient::CooldownDone()
{
     done = true;

     char buf[1024];
     Notice("Finished cooldown period.");
     std::sort(latencies.begin(), latencies.end());

     if (latencies.size() > 0)
     {
          uint64_t ns = latencies[latencies.size() / 2];
          LatencyFmtNS(ns, buf);
          Notice("Median latency is %ld ns (%s)", ns, buf);

          ns = 0;
          for (auto latency : latencies)
          {
               ns += latency;
          }
          ns = ns / latencies.size();
          LatencyFmtNS(ns, buf);
          Notice("Average latency is %ld ns (%s)", ns, buf);

          ns = latencies[latencies.size() * 90 / 100];
          LatencyFmtNS(ns, buf);
          Notice("90th percentile latency is %ld ns (%s)", ns, buf);

          ns = latencies[latencies.size() * 95 / 100];
          LatencyFmtNS(ns, buf);
          Notice("95th percentile latency is %ld ns (%s)", ns, buf);

          ns = latencies[latencies.size() * 99 / 100];
          LatencyFmtNS(ns, buf);
          Notice("99th percentile latency is %ld ns (%s)", ns, buf);
     }
     curr_bdcb_();
}

void BenchmarkClient::OnReply(uint64_t transaction_id, int result, bool erase_session)
{
     Debug("[%lu] OnReply with result %d.", transaction_id, result);
     auto search = session_states_.find(transaction_id);
     ASSERT(search != session_states_.end());

     auto &ss = search->second;
     auto transaction = ss.transaction();
     auto lat = ss.lat();

     if (started)
     {
          // record latency
          if (!cooldownStarted)
          {
               _Latency_EndRec(&latency, lat);
               uint64_t ns = lat->accum;
               // TODO: use standard definitions across all clients for
               // success/commit and failure/abort
               if (result == 0)
               { // only record result if success
                    struct timespec curr;
                    clock_gettime(CLOCK_MONOTONIC, &curr);
                    if (latencies.size() == 0UL)
                    {
                         gettimeofday(&startMeasureTime, NULL);
                         startMeasureTime.tv_sec -= ns / 1000000000ULL;
                         startMeasureTime.tv_usec -= (ns % 1000000000ULL) / 1000ULL;
                         // std::cout << "#start," << startMeasureTime.tv_sec << ","
                         // << startMeasureTime.tv_usec << std::endl;
                    }
                    uint64_t currNanos = curr.tv_sec * 1000000000ULL + curr.tv_nsec;
                    if (transaction -> GetTType() == RW) {
                         std::cout << transaction->GetSequenceNumber() << ", "
                                   << ns
                                   << std::endl;
                    } else {
                         std::cout << "Read" << ", "
                                   << ns
                                   << std::endl;
                    }
                    latencies.push_back(ns);
               }
          }

          struct timeval diff;
          BenchState state = GetBenchState(diff);
          if ((state == COOL_DOWN || state == DONE) && !cooldownStarted)
          {
               Debug("Starting cooldown after %ld seconds.", diff.tv_sec);
               Finish();
          }
          else
          {
               Debug("Not done after %ld seconds.", diff.tv_sec);
          }
     }

     // decrement number of outstanding requests
     n_sessions_started_--;

     // update if we are a write
     if (transaction->GetTType() == RW) {
          uint64_t txn_seq = std::stoull(transaction->GetSequenceNumber());
          Assert(txn_seq == last_exec_csn + 1);
          last_exec_csn++;
     }
     
     if (!cooldownStarted) {
          Debug("Adding concurrent req. capcity");
          transport_.Timer(0, std::bind(&BenchmarkClient::SendNext, this));
     }

     delete transaction;

     if (erase_session)
     {
          auto &client = *clients_[ss.current_client_index()];
          client.EndSession(ss.session());
          session_states_.erase(search);
     }
     
     n++;
}

BenchmarkClient::BenchState BenchmarkClient::GetBenchState(struct timeval &diff) const
{
     struct timeval currTime;
     gettimeofday(&currTime, NULL);

     diff = timeval_sub(currTime, startTime);
     if (diff.tv_sec > exp_duration_)
     {
          return DONE;
     }
     else if (diff.tv_sec > exp_duration_ - warmupSec)
     {
          return COOL_DOWN;
     }
     else if (started)
     {
          return MEASURE;
     }
     else
     {
          return WARM_UP;
     }
}

BenchmarkClient::BenchState BenchmarkClient::GetBenchState() const
{
     struct timeval diff;
     return GetBenchState(diff);
}

void BenchmarkClient::Finish()
{
     gettimeofday(&endTime, NULL);
     struct timeval diff = timeval_sub(endTime, startMeasureTime);

     std::cout << "#end," << diff.tv_sec << "," << diff.tv_usec << "," << client_id_
               << std::endl;

     Notice("Completed %d requests in " FMT_TIMEVAL_DIFF " seconds", n,
            VA_TIMEVAL_DIFF(diff));
     Notice("%lu outstanding transactions.", session_states_.size());

     if (latencyFilename.size() > 0)
     {
          Latency_FlushTo(latencyFilename.c_str());
     }

     cooldownStarted = true;

     uint64_t cooldown_us = cooldownSec * 1e6;
     transport_.TimerMicro(cooldown_us, std::bind(&BenchmarkClient::Cleanup, this));
}
