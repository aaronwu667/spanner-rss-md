#ifndef MD_TRANSACTION_H
#define MD_TRANSACTION_H

#include <random>
#include <string>
#include <vector>

#include "store/benchmark/async/common/key_selector.h"
#include "store/common/frontend/async_transaction.h"
#include "store/common/frontend/client.h"

namespace md {

class MDTransaction : public AsyncTransaction {
   public:
  MDTransaction(KeySelector *keySelector, int numKeys, std::mt19937 &rand, const std::string ttype, const std::string cid, const std::string csn);
    virtual ~MDTransaction();

   protected:
    inline const std::string &GetKey(int i) const {
        return keySelector->GetKey(keyIdxs[i]);
    }

    inline size_t GetNumKeys() const { return keyIdxs.size(); }

    const std::string &GetTransactionType() override { return ttype_; };

    KeySelector *keySelector;
    std::string cid;
    std::string csn;
    
   private:
    std::vector<int> keyIdxs;
    std::string ttype_;

};

} 

#endif