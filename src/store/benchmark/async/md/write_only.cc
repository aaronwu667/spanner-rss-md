#include "store/benchmark/async/md/write_only.h"
#include "store/benchmark/async/common/key_selector.h"
#include "store/benchmark/async/md/md_transaction.h"
#include "store/common/frontend/transaction_utils.h"
#include <string>

namespace md {
  
     WriteOnly::WriteOnly(KeySelector* keySelector,
                          std::mt19937 &rand,
                          const std::string cid, const std::string csn)
          : MDTransaction(keySelector,
                          1 + rand() % 10,
                          rand,
                          "read_write", cid, csn,
                          RW)
     {
     }

     WriteOnly::~WriteOnly(){}
  
     Operation WriteOnly::GetNextOperation(std::size_t op_index) {
          if (op_index == 0 ) {
               return BeginRW();
          } else if (op_index == 1) {
               return CheckSeqNo(cid, csn);
          } else if ( 1 < op_index && op_index < GetNumKeys() + 2) {
               return Put(GetKey(op_index - 2), GetKey(op_index - 2));
          } else if (op_index == GetNumKeys() + 2) {
               uint64_t curr_csn = std::stoull(csn);
               return Put(cid, std::to_string(curr_csn+1));
          } else if (op_index == GetNumKeys() + 3) {
               return Commit();
          } else {
               return Wait();
          }
     }
  
}
