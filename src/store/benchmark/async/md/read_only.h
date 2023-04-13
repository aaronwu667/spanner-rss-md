// -*-c++-*-
#ifndef MD_READ_ONLY_H
#define MD_READ_ONLY_H

#include "store/benchmark/async/md/md_transaction.h"
namespace md {

     class ReadOnly : public MDTransaction {
     public:
          ReadOnly(KeySelector *keySelector, std::mt19937 &rand, const std::string dep);
          virtual ~ReadOnly();

     protected:
          Operation GetNextOperation(std::size_t op_index) override;
          const std::string dep;
     };

}



#endif
