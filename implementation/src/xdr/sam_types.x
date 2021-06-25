
namespace sam_edce {

typedef unsigned int uint32;
typedef int int32;

typedef unsigned hyper uint64;
typedef hyper int64;

typedef uint64 AccountID;

typedef uint32 AssetID;

typedef opaque Signature[64];	//512 bits
typedef opaque PublicKey[32];	//256 bits

enum OfferType
{
    SELL = 0
};

struct OfferCategory
{
    AssetID sellAsset;
    AssetID buyAsset;
    OfferType type;
};

typedef uint64 Price;

}