/* NUKLEAR KLEE begin (ENTIRE FILE) */

#ifndef KLEE_NUKLEARHASH_H
#define KLEE_NUKLEARHASH_H

#include "klee/util/BitArray.h"
#include "llvm/Support/raw_ostream.h"

namespace klee {

/*
 * Message Hash DB
 */

//#define HASHVALSIZE 1
//#define HASHVALMASK 0xff
#define HASHVALSIZE 2
#define HASHVALMASK 0xffff
//#define HASHVALSIZE 4
//#define HASHVALMASK 0xffffffff


#define HASHDEBUG(x)

// Return next/prev index in the circular array 
#define HASHDBNEXT(cur, sz) (((cur) + 1) % (sz))
#define HASHDBPREV(cur, sz) (((cur + sz) - 1) % (sz))

#define UCPTOINT(buf) (((*(buf))<<24) + ((*((buf)+1))<<16) + ((*((buf)+2))<<8) + (*((buf)+3)))

#define INTTOUCP(buf,n) {\
    (buf)[0] = (unsigned char)((n) >> 24);	\
    (buf)[1] = (unsigned char)((n) >> 16);	\
    (buf)[2] = (unsigned char)((n) >> 8);	\
    (buf)[3] = (unsigned char)(n);		\
}

struct hashval {
	uint8_t val[HASHVALSIZE];
	uint32_t size;
};

// hashdb holds a circular array of N vals
struct hashdb {
  int currsize;
  int curridx;
	int hvsize;
  struct hashval **vals;
};

// TODO: handle incomplete recovery
// TODO: use single code file between nuklear and xpilot 

// TODO: need to make a bit-for-byte from a summary, with associated mask for
// missing messages.
 
typedef enum {
  NUK_MSG_SERVER_TO_CLIENT,
  NUK_MSG_CLIENT_TO_SERVER_DROP,
  NUK_MSG_CLIENT_TO_SERVER,
  SOCKETMESSAGE_UNKNOWN 
} NuklearMessageType;

class NuklearHash {
public:

  NuklearSocket *_ns;
  int _dbsize;
  int _hvsize;

  // XPilot header lengths 
  int _roundnum_len;
  int _seqnum_len;
  int _hashval_len;
  int _ackmsglen_len;

  int _vals_len;

  BitArray *_mask_bitarray;
  BitArray *_vals_bitarray;
  
  uint8_t *_mask;
  uint8_t *_vals;

  NuklearHash(NuklearSocket *ns) 
  : _ns(ns), _dbsize(HASHVALSIZE*8), _hvsize(HASHVALSIZE) {

    // XPilot header lengths 
    _roundnum_len = 4;
    _seqnum_len = 4;
    _hashval_len = _hvsize;
    _ackmsglen_len = 4;

    //// xpilotkleeheader: seqnum | summary_hv | msg_hv | ackmsglen | ackmsg 

    _vals_len = _seqnum_len + _hashval_len + _hashval_len;
    _mask_bitarray = new BitArray(8*_vals_len, 0);
    _vals_bitarray = new BitArray(8*_vals_len, 0);

    _mask = new uint8_t[_vals_len];
    _vals = new uint8_t[_vals_len];

    for (int i=0;i<_vals_len;i++) 
      _mask[i] = _vals[i] = 0;
  }

  ~NuklearHash() {
    delete _mask_bitarray;
    delete _vals_bitarray;
    delete _mask;
    delete _vals;
  }


  void print_bitarray(FILE *out, uint8_t *b, int size) {
    int i;
    for (i=0;i<size;i++) {
      if (b[i] == 0) 
        fprintf(out, "0 ");
      else
        fprintf(out, "1 ");
    }
  }

  void bitarray_to_bytearray(BitArray* a, uint8_t* b, int len) {
    int i, j, barray_i;
    for (int i=0; i<len; i++) {
      uint8_t mask = 0x01;
      for (int j=0; j<8; j++) {
        if (a->get(i*8 + j))
          b[i] |= mask;
        else 
          b[i] &= mask;
        mask = mask << 1;
      }
    }
  }


  int recover_hash(int index) {

    KTestObject* recover_obj = &(_ns->ktest->objects[index]);

    // create new hashdb 
    hashdb *hdb = create_hashdb(_dbsize, _hvsize);
    hdb->curridx = 1;

    int i;
    int count = hdb->currsize;
    int currindex = index + 1;

    // Advance mask and bits up to the summary hash value in the message.
    // xpilotlogheader: round-number | seqnum | summary_hv | msg_hv | ackmsglen | ackmsg 
    // xpilotkleeheader:               seqnum | summary_hv | msg_hv | ackmsglen | ackmsg 

    int hashval_start = (8*_vals_len) - _dbsize;
    int curr_mask_pos = hashval_start;

    // Rebuild original hash from future summary hashes. 
    // Possible errors:
    // 1. If a message summary was dropped we use a zero summary and set the
    // mask value so we know to set that bit symbolic.
    // 2. Not enough messages to reconstruct a full hash if we are near the end
    // of the log, set the bits in the mask for the rest of the missing summary.

    hashval *summary = create_hashval(_hvsize);
    while( count && currindex < _ns->ktest->numObjects) {
    
      KTestObject* obj = &(_ns->ktest->objects[currindex]);
      clear_hashval(summary);

      if (getSocketMessageType(obj->name) == NUK_MSG_CLIENT_TO_SERVER_DROP) {
        _mask_bitarray->unset(curr_mask_pos);

        reconstruct_from_summary(hdb, summary);
        increment_index(hdb);

        count--;
        curr_mask_pos++;
      } else if (getSocketMessageType(obj->name) == NUK_MSG_CLIENT_TO_SERVER) {
        _mask_bitarray->set(curr_mask_pos);

        extract_summary(obj, summary);

        reconstruct_from_summary(hdb, summary);
        increment_index(hdb);

        count--;
        curr_mask_pos++;
      }
      currindex++;
    }
    delete_hashval(summary);

    struct hashval* reconstructed_hv = hdb->vals[0];

    bitarray_to_bytearray(_mask_bitarray, _mask, _vals_len);

    for (int i=0; i<_hvsize; i++) {
      _vals[_vals_len - (_hvsize-i)] = reconstructed_hv->val[i];
    }

    if (count != 0) {
      llvm::errs() << "NUKLEARHASH : Summary Extraction failed, index="
        << index << "\n";
      delete_hashdb(hdb);
      return -1;
    }
    return 0;
  }

  int test_recover_hash( int index ) {
    
    KTestObject* recover_obj = &(_ns->ktest->objects[index]);

    // compute the hash for this message
    hashval* computed_hv = create_hashval(_hvsize);
    extract_hash(recover_obj, NULL, computed_hv);
    
    // create new hashdb 
    hashdb *hdb = create_hashdb(_dbsize, _hvsize);
    hdb->curridx = 1;

    int count = hdb->currsize;
    int currindex = index + 1;

    // rebuild original hash from next (HVALSIZE*8) summary hashes (only in c2s)
    hashval *summary = create_hashval(_hvsize);
    while( count && currindex < _ns->ktest->numObjects) {
    
      KTestObject* obj = &(_ns->ktest->objects[currindex]);
      clear_hashval(summary);

      if (getSocketMessageType(obj->name) == NUK_MSG_CLIENT_TO_SERVER) {
        extract_summary(obj, summary);
        reconstruct_from_summary(hdb, summary);
        increment_index(hdb);
        count--;
      }

      currindex++;
    }

    delete_hashval(summary);

    if (count != 0) {
      llvm::errs() << "NUKLEARHASH : Summary Extraction failed, index="
        << index << "\n";
      delete_hashdb(hdb);
      return -1;
    }

    struct hashval* reconstructed_hv = hdb->vals[0];

    if (0 == hash_compare(reconstructed_hv, computed_hv)) {
      llvm::errs() << "NUKLEARHASH : Correct Hash recovery! index="
        << index << "\n";

      fprintf(stderr, "\n\tsummary hv:   ");
      print_hashval(stderr, reconstructed_hv);
      fprintf(stderr, "\n\tcomputed hv:  ");
      print_hashval(stderr, computed_hv);
      fprintf(stderr, "\n");
      fprintf(stderr, "\n\thashdb:  ");
      print_hashdb(stderr, hdb);
      fprintf(stderr, "\n\n");

      delete_hashdb(hdb);
      return 0;
    } else {
      llvm::errs() << "NUKLEARHASH : Unrecoverable hash, index="
        << index << "\n";
      print_hashdb(stderr, hdb);
      fprintf(stderr, "\n\tsummary hv:   ");
      print_hashval(stderr, reconstructed_hv);
      fprintf(stderr, "\n\tcomputed hv:  ");
      print_hashval(stderr, computed_hv);
      fprintf(stderr, "\n");
      delete_hashdb(hdb);
      return -1;
    }

  }

  // TODO: put this into NuklearManager?
  NuklearMessageType getSocketMessageType(std::string s) {
    // we don't care about s2c or dc2s messages
    std::string socketReadStr("s2c"), socketWriteStr("c2s"), socketWriteDropStr("dc2s");

    if (s.substr(0, socketReadStr.size()) == socketReadStr) {
      return NUK_MSG_SERVER_TO_CLIENT;
    } else if (s.substr(0, socketWriteDropStr.size()) == socketWriteDropStr) {
      return NUK_MSG_CLIENT_TO_SERVER_DROP;
    } else if (s.substr(0, socketWriteStr.size()) == socketWriteStr) {
      return NUK_MSG_CLIENT_TO_SERVER;
    }

    return SOCKETMESSAGE_UNKNOWN;
  }

  int extract_summary(KTestObject* ktestobj, 
                      hashval *summary_hv) {
  
    // we don't care about s2c or dc2s messages
    if (NUK_MSG_CLIENT_TO_SERVER != getSocketMessageType(ktestobj->name))
      return -1;

    unsigned char* msg = ktestobj->bytes;

    // header: round-number | seqnum | hashval | ackmsglen | ackmsg 
    if (summary_hv != NULL) {
      if (_roundnum_len + _seqnum_len + _hashval_len < ktestobj->numBytes) {
        // summary hash value is offset into buf. 
        memcpy(&(summary_hv->val[0]), 
              msg + _roundnum_len + _seqnum_len, _hashval_len);
        return 0;
      }
    }
    return -1;
  }

  int extract_hash(KTestObject* ktestobj, 
                    hashval *summary_hv, hashval* computed_hv) {
  
    // we don't care about s2c or dc2s messages
    if (NUK_MSG_CLIENT_TO_SERVER != getSocketMessageType(ktestobj->name))
      return -1;

    unsigned char* msg = ktestobj->bytes;
    int msg_len = ktestobj->numBytes;

    // header: round-number | seqnum | hashval | ackmsglen | ackmsg 

    if (computed_hv != NULL) {
      if (_roundnum_len + _seqnum_len + _hashval_len + _ackmsglen_len < msg_len) {
        int ackmsg_len = UCPTOINT(&(msg[_roundnum_len + _seqnum_len + _hashval_len]));
        int header_len 
          = _roundnum_len + _seqnum_len + _hashval_len + _ackmsglen_len + ackmsg_len;
  
        // set recover_msg to point the start of the orig msg 
        unsigned char* base_msg = (unsigned char*)(msg + header_len);
        int base_msg_len = msg_len - header_len;

        // compute hash of base message 
        compute_hash(computed_hv, base_msg, base_msg_len);
      }
    }

    return 0;
  }

  void print_byte(FILE* out, uint8_t b) {
    int i;
    uint8_t x = b; 
    for (i=0;i<8;i++)
    {
      if (i%4 == 0) fprintf(out, " ");
      if ((x & 0x01) != 0)
        fprintf(out, "1 ");
      else
        fprintf(out, "0 ");

      x = x >> 1;
    }
  }

  void print_byte_offset(FILE* out, uint8_t b, int offset) {
    int i;
    uint8_t x = b; 
    for (i=0;i<8;i++)
    {
      if (i%4 == 0) fprintf(out, " ");
      if (i==offset) {
        if ((x & 0x01) != 0)
          fprintf(out, "1 ");
        else
          fprintf(out, "0 ");
      } else {
        fprintf(out, ". ");
      }
      x = x >> 1;
    }
  }

  void print_hashval(FILE* out, struct hashval *hv) {
    int i=0;
    for (i=0;i<hv->size;i++) {
      print_byte(out, hv->val[i]);
    }
  }

  void print_hashval_offset(FILE* out, struct hashval *hv, int offset) {
    int i=0;
    for (i=0;i<HASHVALSIZE;i++) {
      if (offset >= (i*8) && offset < ((i+1)*8)) {
        print_byte_offset(out, hv->val[i], offset%8);
      } else {
        print_byte_offset(out, hv->val[i], -1);
      }
    }
  }

  void print_hashdb(FILE* out, struct hashdb *db) {
    fprintf(out, "hashdb 0x%x\n\tsize=%d, currindex=%d\n",
            db, db->currsize, db->curridx);
    fprintf(out, "\n\thash values:");

    int i,nextidx=HASHDBPREV(db->curridx, db->currsize);

    for (i=0;i<db->currsize;i++) {
      if (i%4 == 0) fprintf(out, "\n");
      fprintf(out, "\n\t[%02d] ", nextidx);
      print_hashval(out, db->vals[i]);
		  //print_hashval_offset(out, db->vals[nextidx], i);
      nextidx = HASHDBPREV(nextidx, db->currsize);
    }
    fprintf(out, "\n");
  }

  // Create a hashval
  void clear_hashval(struct hashval *hv) {
    int i;
    for (i=0;i<hv->size;i++) {
      hv->val[i] = 0;
    }
  }

  // Create a hashval
  struct hashval* create_hashval(uint32_t size) {
    struct hashval *hv = (struct hashval*)malloc(sizeof(struct hashval));
    hv->size = size;
    clear_hashval(hv);
    return hv;
  }

  // Delete a hashval
  void delete_hashval(struct hashval *hv) {
    free(hv);
  }

  // Create a hashdb
  struct hashdb* create_hashdb(uint32_t size, uint32_t hvsize) {
    struct hashdb *db = (struct hashdb*)malloc(sizeof(struct hashdb));
    db->currsize=size;
    db->hvsize = hvsize;
    db->curridx = 0;
    db->vals = (struct hashval**)malloc(db->currsize * sizeof(struct hashval*));

    int i;
    for (i=0;i<db->currsize;i++) {
      db->vals[i] = create_hashval(hvsize);
    }

    return db;
  }

  // Delete a hashdb
  void delete_hashdb(struct hashdb *db) {
    int i;
    for (i=0;i<db->currsize;i++) {
      delete_hashval(db->vals[i]);
    }
    free(db);
  }

  // Compare two hash values, return 0 if equal
  int hash_compare(struct hashval *hv1, struct hashval *hv2) {
    if (hv1->size != hv2->size) return -1;

    int i;
    for (i=0;i<hv1->size;++i)
      if (hv1->val[i] != hv2->val[i]) 
        return hv1->val[i]-hv2->val[i];

    return 0;
  }

  // Increment current index
  void increment_index(struct hashdb *db) {
    db->curridx = HASHDBNEXT(db->curridx, db->currsize);
  }

  // Compute the hash of a given buffer 
  void compute_hash(struct hashval *hv, uint8_t *buf, uint32_t size) {

    // The checksum mod 2^32
    uint32_t i,checksum = 0;	

    // Compute rotating checksum of buf
    for (i=0;i<size;i++) {
      checksum = (checksum >> 1) + ((checksum & 1) << ((8*HASHVALSIZE)-1));
      checksum += buf[i];
      // Keep it within bounds
      checksum &= HASHVALMASK; 
    }

    for (i=0;i<HASHVALSIZE;i++)
      hv->val[i] = (uint8_t)(checksum >> (i*8));

  }

  // Add a summary value to a hashdb, used to reconstruct
  void reconstruct_from_summary(struct hashdb *db, 
                                struct hashval *summary) {
    // summary = from i=0 to i=N: ith bit of the ith hashval
    
    uint32_t j, i, nextidx=db->curridx;

    // Zero oldest hashval, it was completed last round
    clear_hashval(db->vals[HASHDBPREV(db->curridx,db->currsize)]);

    for (i=0;i<db->hvsize;i++) {
      uint8_t mask = 0x01;
      for (j=0;j<8;j++) {
        nextidx = HASHDBPREV(nextidx, db->currsize);
        if (mask & summary->val[i]) {
          // if jth bit is set...
          db->vals[nextidx]->val[i] |= mask;
        } else {
          // if jth bit is not set...
          db->vals[nextidx]->val[i] &= ~mask;
        }
        mask = mask << 1;
      }
    }
  }

  // Update the current summary after adding a new message hash
  void summarize(struct hashdb *db, struct hashval *summary) {
    // summary = from i=0 to i=N: ith bit of the ith hashval
    
    if (summary == NULL) return;

    clear_hashval(summary);

    uint32_t j, i, nextidx=db->curridx;

    for (i=0;i<db->hvsize;i++) {
      uint8_t mask = 0x01;
      summary->val[i] = 0;
      for (j=0;j<8;j++) {
        nextidx = HASHDBPREV(nextidx, db->currsize);
        summary->val[i] ^= (mask & db->vals[nextidx]->val[i]); 
        mask = mask << 1;
      }
    }
  }

  // Replace oldest hashval in db with the hash of the new message (buf). 
  void add_message(struct hashdb *db, uint8_t *buf, uint32_t size) {
    compute_hash(db->vals[db->curridx], buf, size);
  }

};

} // end namespace klee

#endif //KLEE_NUKLEAHASH_H
/* NUKLEAR KLEE end (ENTIRE FILE) */
