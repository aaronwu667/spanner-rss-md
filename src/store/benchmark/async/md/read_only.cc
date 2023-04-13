#include "store/benchmark/async/md/read_only.h"
#include "store/benchmark/async/common/key_selector.h"
#include "store/benchmark/async/md/md_transaction.h"
#include "store/common/frontend/transaction_utils.h"
#include <string>
#include <unordered_set>

namespace md {
     ReadOnly::ReadOnly(KeySelector* keySelector, std::mt19937 &rand,
                        const std::string dep_arg)
          : MDTransaction(keySelector, 1 + rand() % 10, rand, "read_only", "" , "", RO), dep{dep_arg}{}
     ReadOnly::~ReadOnly(){}

     Operation ReadOnly::GetNextOperation(std::size_t op_index) {
          if(op_index == 0) {
               return BeginRO();
          } else if (op_index == 1) {
               Debug("dep %s", dep.c_str());
               return CheckWriteConst(dep);
          }
          else if (op_index == 2) {
               std::unordered_set<std::string> keys;
               for (auto i = 0; i < GetNumKeys(); i++) {
                    keys.insert(GetKey(i));
               }
               return ROCommit(std::move(keys));
          }
          else {
               return Wait();
          }
     }     
}
