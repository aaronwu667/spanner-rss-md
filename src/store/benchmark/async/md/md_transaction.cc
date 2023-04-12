#include "store/benchmark/async/md/md_transaction.h"

namespace md {
  MDTransaction::MDTransaction(
                               KeySelector *keySelector,
                               int numKeys, std::mt19937 &rand,
                               const std::string ttype,
                               const std::string cid,
                               const std::string csn)
    : keySelector(keySelector), ttype_{ttype}, cid{cid}, csn{csn} {
    for (int i = 0; i < numKeys; ++i) {
      keyIdxs.push_back(keySelector->GetKey(rand));
    }
  }

  MDTransaction::~MDTransaction() {
  }
}  
