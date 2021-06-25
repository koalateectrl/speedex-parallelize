%#include "xdr/sam_types.h"

namespace sam_edce {

enum OperationType
{
	CREATE_ACCOUNT = 0,
	CREATE_SELL_OFFER = 1,
	CANCEL_SELL_OFFER = 2,
	PAYMENT = 3,
	MONEY_PRINTER = 4
};

struct CreateAccountOp
{
	int64 startingBalance;
	AccountID newAccountId;
    PublicKey newAccountPublicKey;
};

struct CreateSellOfferOp
{
	OfferCategory category;
	int64 amount;
	Price minPrice;
};

struct CancelSellOfferOp
{
    OfferCategory category;
    uint64 offerId;
    Price minPrice;
};

struct PaymentOp
{
	AccountID receiver;
	AssetID asset;
	int64 amount;
};

struct MoneyPrinterOp
{
	AssetID asset;
	int64 amount;
};

struct Operation {
    union switch (OperationType type) {
    case CREATE_ACCOUNT:
        CreateAccountOp createAccountOp;
    case CREATE_SELL_OFFER:
        CreateSellOfferOp createSellOfferOp;
    case CANCEL_SELL_OFFER:
        CancelSellOfferOp cancelSellOfferOp;
    case PAYMENT:
        PaymentOp paymentOp;
    case MONEY_PRINTER:
        MoneyPrinterOp moneyPrinterOp;
    } body;
};



const MAX_OPS_PER_TX = 256;

struct TransactionMetadata {
       AccountID sourceAccount;
       uint64 sequenceNumber;
};


struct Transaction {
       TransactionMetadata metadata;
       Operation operations<MAX_OPS_PER_TX>;
       uint32 fee;
};

struct SignedTransaction {
       Transaction transaction;
       Signature signature;
};

}