// -*-c++-*-
#ifndef MD_WRITE_ONLY_H
#define MD_WRITE_ONLY_H

#include "store/benchmark/async/common/key_selector.h"
#include "store/benchmark/async/md/md_transaction.h"
#include <cstddef>
#include <random>

namespace md {
     class WriteOnly : public MDTransaction {
     public:
          WriteOnly(KeySelector *keySelector, std::mt19937 &rand, const std::string cid, const std::string csn);
          virtual ~WriteOnly();

     protected:
          Operation GetNextOperation(std::size_t op_index) override;
     };
}


#endif
