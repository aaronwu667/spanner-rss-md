#ifndef RETWIS_TRANSACTION_H
#define RETWIS_TRANSACTION_H

#include <random>
#include <string>
#include <vector>

#include "store/benchmark/async/common/key_selector.h"
#include "store/common/frontend/async_transaction.h"
#include "store/common/frontend/client.h"

namespace retwis {

class RetwisTransaction : public AsyncTransaction {
   public:
     RetwisTransaction(KeySelector *keySelector, int numKeys, std::mt19937 &rand, const std::string ttype, TType type);
    virtual ~RetwisTransaction();

   protected:
    inline const std::string &GetKey(int i) const {
        return keySelector->GetKey(keyIdxs[i]);
    }

    inline size_t GetNumKeys() const { return keyIdxs.size(); }

    const std::string &GetTransactionType() override { return ttype_; };
    const std::string &GetSequenceNumber() override {return csn;}
    const TType GetTType() override {return transaction_type_enum;}
     

    KeySelector *keySelector;
    const std::string csn = "";
   private:
    std::vector<int> keyIdxs;
    std::string ttype_;
    TType transaction_type_enum;

};

}  // namespace retwis

#endif /* RETWIS_TRANSACTION_H */
