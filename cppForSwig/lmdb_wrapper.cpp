////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE or http://www.gnu.org/licenses/agpl.html                      //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
//
#include <iostream>
#include <sstream>
#include <map>
#include <list>
#include <vector>
#include <set>
#include "BinaryData.h"
#include "BtcUtils.h"
#include "BlockObj.h"
#include "StoredBlockObj.h"
#include "lmdb_wrapper.h"
#include "txio.h"

struct MDB_val;

////////////////////////////////////////////////////////////////////////////////
LDBIter::LDBIter(LMDB::Iterator&& mv)
   : iter_(std::move(mv))
{ 
   isDirty_ = true;
}

////////////////////////////////////////////////////////////////////////////////
LDBIter::LDBIter(LDBIter&& mv)
: iter_(std::move(mv.iter_))
{
   isDirty_ = true;
}

LDBIter::LDBIter(const LDBIter& cp)
: iter_(cp.iter_)
{
   isDirty_ = true;
}

////////////////////////////////////////////////////////////////////////////////
LDBIter& LDBIter::operator=(LMDB::Iterator&& mv)
{ 
   iter_ = std::move(mv);
   return *this;
}

////////////////////////////////////////////////////////////////////////////////
LDBIter& LDBIter::operator=(LDBIter&& mv)
{ 
   iter_ = std::move(mv.iter_);
   return *this;
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::isValid(DB_PREFIX dbpref)
{
   if(!isValid() || iter_.key().size() == 0)
      return false;
   return iter_.key()[0] == (char)dbpref;
}


////////////////////////////////////////////////////////////////////////////////
bool LDBIter::advance(void)
{
   ++iter_;
   isDirty_ = true;
   return isValid();
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::retreat(void)
{
   --iter_;
   isDirty_ = true;
   return isValid();
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::advance(DB_PREFIX prefix)
{
   ++iter_;
   isDirty_ = true;
   return isValid(prefix);
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::readIterData(void)
{
   if(!isValid())
   {
      isDirty_ = true;
      return false;
   }

   currKey_ = BinaryData(iter_.key());
   currValue_ = BinaryData(iter_.value());
   currKeyReader_.setNewData( currKey_ );
   currValueReader_.setNewData( currValue_ );
   isDirty_ = false;
   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::advanceAndRead(void)
{
   if(!advance())
      return false; 
   return readIterData(); 
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::advanceAndRead(DB_PREFIX prefix)
{
   if(!advance(prefix))
      return false; 
   return readIterData(); 
}


////////////////////////////////////////////////////////////////////////////////
BinaryData LDBIter::getKey(void) const
{ 
   if(isDirty_)
   {
      LOGERR << "Returning dirty key ref";
      return BinaryData(0);
   }
   return currKey_;
}
   
////////////////////////////////////////////////////////////////////////////////
BinaryData LDBIter::getValue(void) const
{ 
   if(isDirty_)
   {
      LOGERR << "Returning dirty value ref";
      return BinaryData(0);
   }
   return currValue_;
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef LDBIter::getKeyRef(void) const
{ 
   if(isDirty_)
   {
      LOGERR << "Returning dirty key ref";
      return BinaryDataRef();
   }
   return currKeyReader_.getRawRef();
}
   
////////////////////////////////////////////////////////////////////////////////
BinaryDataRef LDBIter::getValueRef(void) const
{ 
   if(isDirty_)
   {
      LOGERR << "Returning dirty value ref";
      return BinaryDataRef();
   }
   return currValueReader_.getRawRef();
}


////////////////////////////////////////////////////////////////////////////////
BinaryRefReader& LDBIter::getKeyReader(void) const
{ 
   if(isDirty_)
      LOGERR << "Returning dirty key reader";
   return currKeyReader_; 
}

////////////////////////////////////////////////////////////////////////////////
BinaryRefReader& LDBIter::getValueReader(void) const
{ 
   if(isDirty_)
      LOGERR << "Returning dirty value reader";
   return currValueReader_; 
}


////////////////////////////////////////////////////////////////////////////////
bool LDBIter::seekTo(BinaryDataRef key)
{
   iter_.seek(CharacterArrayRef(key.getSize(), key.getPtr()), LMDB::Iterator::Seek_GE);
   return readIterData();
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::seekTo(DB_PREFIX pref, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)pref);
   bw.put_BinaryData(key);
   return seekTo(bw.getDataRef());
}


////////////////////////////////////////////////////////////////////////////////
bool LDBIter::seekToExact(BinaryDataRef key)
{
   if(!seekTo(key))
      return false;

   return checkKeyExact(key);
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::seekToExact(DB_PREFIX pref, BinaryDataRef key)
{
   if(!seekTo(pref, key))
      return false;

   return checkKeyExact(pref, key);
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::seekToStartsWith(BinaryDataRef key)
{
   if(!seekTo(key))
      return false;

   return checkKeyStartsWith(key);

}


////////////////////////////////////////////////////////////////////////////////
bool LDBIter::seekToStartsWith(DB_PREFIX prefix)
{
   BinaryWriter bw(1);
   bw.put_uint8_t((uint8_t)prefix);
   if(!seekTo(bw.getDataRef()))
      return false;

   return checkKeyStartsWith(bw.getDataRef());

}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::seekToStartsWith(DB_PREFIX pref, BinaryDataRef key)
{
   if(!seekTo(pref, key))
      return false;

   return checkKeyStartsWith(pref, key);
}


bool LDBIter::seekToBefore(BinaryDataRef key)
{
   iter_.seek(CharacterArrayRef(key.getSize(), key.getPtr()), LMDB::Iterator::Seek_LE);
   return readIterData();
}

bool LDBIter::seekToBefore(DB_PREFIX prefix)
{
   BinaryWriter bw(1);
   bw.put_uint8_t((uint8_t)prefix);
   return seekToBefore(bw.getDataRef());
}

bool LDBIter::seekToBefore(DB_PREFIX pref, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)pref);
   bw.put_BinaryData(key);
   return seekToBefore(bw.getDataRef());
}


////////////////////////////////////////////////////////////////////////////////
bool LDBIter::seekToFirst(void)
{
   iter_.toFirst();
   readIterData();
   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::checkKeyExact(BinaryDataRef key)
{
   if(isDirty_ && !readIterData())
      return false;

   return (key==currKeyReader_.getRawRef());
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::checkKeyExact(DB_PREFIX prefix, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)prefix);
   bw.put_BinaryData(key);
   if(isDirty_ && !readIterData())
      return false;

   return (bw.getDataRef()==currKeyReader_.getRawRef());
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::checkKeyStartsWith(BinaryDataRef key)
{
   if(isDirty_ && !readIterData())
      return false;

   return (currKeyReader_.getRawRef().startsWith(key));
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::verifyPrefix(DB_PREFIX prefix, bool advanceReader)
{
   if(isDirty_ && !readIterData())
      return false;

   if(currKeyReader_.getSizeRemaining() < 1)
      return false;

   if(advanceReader)
      return (currKeyReader_.get_uint8_t() == (uint8_t)prefix);
   else
      return (currKeyReader_.getRawRef()[0] == (uint8_t)prefix);
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::checkKeyStartsWith(DB_PREFIX prefix, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)prefix);
   bw.put_BinaryData(key);
   return checkKeyStartsWith(bw.getDataRef());
}


////////////////////////////////////////////////////////////////////////////////
LMDBBlockDatabase::LMDBBlockDatabase(function<bool(void)> isDBReady) :
isDBReady_(isDBReady)
{
   //for some reason the WRITE_UINT16 macros create 4 byte long BinaryData 
   //instead of 2, so I'm doing this the hard way instead
   uint8_t* ptr = const_cast<uint8_t*>(ZCprefix_.getPtr());
   memset(ptr, 0xFF, 2);
}


/////////////////////////////////////////////////////////////////////////////
LMDBBlockDatabase::~LMDBBlockDatabase(void)
{
   closeDatabases();
}





/////////////////////////////////////////////////////////////////////////////
// The dbType and pruneType inputs are left blank if you are just going to 
// take whatever is the current state of database.  You can choose to 
// manually specify them, if you want to throw an error if it's not what you 
// were expecting
void LMDBBlockDatabase::openDatabases(
   const string& basedir,
   BinaryData const & genesisBlkHash,
   BinaryData const & genesisTxHash,
   BinaryData const & magic,
   ARMORY_DB_TYPE     dbtype,
   DB_PRUNE_TYPE      pruneType
   )
{
   baseDir_ = basedir;

   if (dbtype == ARMORY_DB_SUPER)
   {
      //make sure it is a supernode DB
#ifdef WIN32
      if (access(dbHeadersFilename().c_str(), 0) == 0)
#else
      if (access(dbHeadersFilename().c_str(), F_OK) == 0)
#endif
      {
         LOGERR << "Mismatch in DB type";
         LOGERR << "Requested supernode";
         LOGERR << "Current DB is fullnode";
         throw runtime_error("Mismatch in DB type");
      }

      try
      {
         openDatabasesSupernode(basedir,
            genesisBlkHash, genesisTxHash,
            magic, dbtype, pruneType);
      }
      catch (LMDBException &e)
      {
         LOGERR << "Exception thrown while opening database";
         LOGERR << e.what();
         throw e;
      }
      catch (runtime_error &e)
      {
         throw e;
      }
      catch (...)
      {
         LOGERR << "Exception thrown while opening database";
         closeDatabases();
         throw;
      }

      return;
   }

   /***
   Supernode and Fullnode use different DB. 
   
   Supernode keeps all data within the same file.
   Fullnode is meant for lighter duty and keep its static data (blocks and 
   headers) separate from dynamic data (history, utxo spentness, ssh, ZC). 
   TxHints are also separeated in their dedicated DB. Each block is saved as 
   a single binary string as opposed to Supernode which breaks block data down
   into Tx and TxOut.

   Consequently, in Fullnode, blocks need to be processed after they're pulled 
   from DB, so individual Tx and TxOut cannot be pulled separately from entire
   blocks, as opposed to supernode.

   This allows Fullnode to keep its static data sequential, with very little 
   fragmentation, while random block data access is slower. This in turns speeds
   up DB building and scanning, which suits individual use profile with 
   100~100,000 registered addresses.
   
   Supernode on the other hand tracks all addresses so it will have a tons of 
   fragmentation to begin with, and is meant to handle lots of concurent
   random access, which is LMDB's strong suit with lots of RAM and high 
   permanent storage bandwidth (i.e. servers).

   In Supernode, TxOut entries are in the BLKDATA DB. 
   In Fullnode, they are in the HISTORY DB, only for TxOuts relevant to the 
   tracked set of addresses. Fullnode also carries the amount of txouts per 
   relevant Tx + txHash saved as :
            TxDBKey6 | uint32_t | txHash
   This prevents pulling each Tx from full blocks in order to identity STS
   transactions and getting the hash, keeping ledger computation speed on 
   par with Supernode

   In Supernode, BLKDATA sdbi sits in the BLKDATA DB.
   In Fullnode, BLDDATA sdbi goes in the HISTORY DB instead, while BLKDATA DB 
   has no sdbi

   In Supernode, txHints go in the BLKDATA DB.
   In Fullnode, only hints for relevant transactions are saved, in the 
   dedicated TXHINTS DB. So while Supernode compiles and commits txhints
   in the building phase, Fullnode processes the few relevant ones during
   scans. 

   There are a couple reasons to this: while Supernode may be used to track
   all ZC (which requires hints for all transactions), Fullnode will only ever
   need txhints for those transactions relevant to its set of tracked addresses

   Besides the obvious space gain (~7% smaller), txhints aren't sequential by 
   nature, and as this DB grows it will slow down DB building. The processing 
   cost over doubles the build time from scratch. Even then the hints will be 
   mostly remain in RAM through OS mapped file management, so writes won't 
   impact buidling much.

   However, a cold start with new blocks to commit will grind HDDs to a halt, 
   taking around 10 minutes to catch up on 12h worth of new blocks. So keeping 
   track of all txhints in Fullnode is not only unecessary, it is detrimental
   to overall DB speed.
   ***/

   SCOPED_TIMER("openDatabases");
   LOGINFO << "Opening databases...";

   magicBytes_ = magic;
   genesisTxHash_ = genesisTxHash;
   genesisBlkHash_ = genesisBlkHash;

   armoryDbType_ = dbtype;
   dbPruneType_ = pruneType;

   if (genesisBlkHash_.getSize() == 0 || magicBytes_.getSize() == 0)
   {
      LOGERR << " must set magic bytes and genesis block";
      LOGERR << "           before opening databases.";
      throw runtime_error("magic bytes not set");
   }

   // Just in case this isn't the first time we tried to open it.
   closeDatabases();

   for (int i = 0; i < COUNT; i++)
      dbEnv_[DB_SELECT(i)].reset(new LMDBEnv());

   dbEnv_[BLKDATA]->open(dbBlkdataFilename());

   //make sure it's a fullnode DB
   {
      LMDB checkDBType;
      const char* dataPtr = nullptr;

      {
         LMDBEnv::Transaction tx(dbEnv_[BLKDATA].get(), LMDB::ReadWrite);
         checkDBType.open(dbEnv_[BLKDATA].get(), "blkdata");
         auto dbKey = StoredDBInfo::getDBKey();
         CharacterArrayRef data = checkDBType.get_NoCopy(CharacterArrayRef(
            dbKey.getSize(), (char*)dbKey.getPtr()));

         dataPtr = data.data;
      }

      checkDBType.close();

      if (dataPtr != nullptr)
      {
         LOGERR << "Mismatch in DB type";
         LOGERR << "Requested fullnode";
         LOGERR << "Current DB is supernode";
         throw runtime_error("Mismatch in DB type");
      }
   }



   dbEnv_[HEADERS]->open(dbHeadersFilename());
   dbEnv_[HISTORY]->open(dbHistoryFilename());
   dbEnv_[TXHINTS]->open(dbTxhintsFilename());

   map<DB_SELECT, string> DB_NAMES;
   DB_NAMES[HEADERS] = "headers";
   DB_NAMES[HISTORY] = "history";
   DB_NAMES[BLKDATA] = "blocks";
   DB_NAMES[TXHINTS] = "txhints";

   try
   {
      for (auto& db : DB_NAMES)
      {
         DB_SELECT CURRDB = db.first;
         LMDBEnv::Transaction tx(dbEnv_[CURRDB].get(), LMDB::ReadWrite);

         dbs_[CURRDB].open(dbEnv_[CURRDB].get(), db.second);

         //no SDBI in TXHINTS
         if (CURRDB == TXHINTS)
            continue;

         StoredDBInfo sdbi;
         getStoredDBInfo(CURRDB, sdbi, false);
         if (!sdbi.isInitialized())
         {
            // If DB didn't exist yet (dbinfo key is empty), seed it
            // A new database has the maximum flag settings
            // Flags can only be reduced.  Increasing requires redownloading
            StoredDBInfo sdbi;
            sdbi.magic_ = magicBytes_;
            sdbi.topBlkHgt_ = 0;
            sdbi.topBlkHash_ = genesisBlkHash_;
            sdbi.armoryType_ = armoryDbType_;
            sdbi.pruneType_ = dbPruneType_;
            putStoredDBInfo(CURRDB, sdbi);
         }
         else
         {
            // Check that the magic bytes are correct
            if (magicBytes_ != sdbi.magic_)
            {
               throw runtime_error("Magic bytes mismatch!  Different blkchain?");
            }

            else if (armoryDbType_ != sdbi.armoryType_)
            {
               LOGERR << "Mismatch in DB type";
               LOGERR << "DB is in  mode: " << (uint32_t)armoryDbType_;
               LOGERR << "Expecting mode: " << sdbi.armoryType_;
               throw runtime_error("Mismatch in DB type");
            }

            if (dbPruneType_ != sdbi.pruneType_)
            {
               throw runtime_error("Mismatch in DB type");
            }
         }
      }
   }
   catch (LMDBException &e)
   {
      LOGERR << "Exception thrown while opening database";
      LOGERR << e.what();
      throw e;
   }
   catch (runtime_error &e)
   {
      LOGERR << "Exception thrown while opening database";
      LOGERR << e.what();
      throw e;
   }
   catch (...)
   {
      LOGERR << "Exception thrown while opening database";
      closeDatabases();
      throw;
   }

   dbIsOpen_ = true;
}

void LMDBBlockDatabase::openDatabasesSupernode(
   const string& basedir,
   BinaryData const & genesisBlkHash,
   BinaryData const & genesisTxHash,
   BinaryData const & magic,
   ARMORY_DB_TYPE     dbtype,
   DB_PRUNE_TYPE      pruneType
)
{
   SCOPED_TIMER("openDatabases");
   LOGINFO << "Opening databases...";

   baseDir_ = basedir;

   magicBytes_ = magic;
   genesisTxHash_ = genesisTxHash;
   genesisBlkHash_ = genesisBlkHash;
   
   armoryDbType_ = dbtype;
   dbPruneType_ = pruneType;

   if(genesisBlkHash_.getSize() == 0 || magicBytes_.getSize() == 0)
   {
      LOGERR << " must set magic bytes and genesis block";
      LOGERR << "           before opening databases.";
      throw runtime_error("magic bytes not set");
   }

   // Just in case this isn't the first time we tried to open it.
   closeDatabasesSupernode();
   
   dbEnv_[BLKDATA].reset(new LMDBEnv());
   dbEnv_[BLKDATA]->open(dbBlkdataFilename());
   
   map<DB_SELECT, string> DB_NAMES;
   DB_NAMES[HEADERS] = "headers";
   DB_NAMES[BLKDATA] = "blkdata";

   try
   {
      for(auto& db : DB_NAMES)
      {
         DB_SELECT CURRDB = db.first;
         LMDBEnv::Transaction tx(dbEnv_[BLKDATA].get(), LMDB::ReadWrite);

         dbs_[CURRDB].open(dbEnv_[BLKDATA].get(), db.second);

         StoredDBInfo sdbi;
         getStoredDBInfo(CURRDB, sdbi, false); 
         if(!sdbi.isInitialized())
         {
            // If DB didn't exist yet (dbinfo key is empty), seed it
            // A new database has the maximum flag settings
            // Flags can only be reduced.  Increasing requires redownloading
            StoredDBInfo sdbi;
            sdbi.magic_      = magicBytes_;
            sdbi.topBlkHgt_  = 0;
            sdbi.topBlkHash_ = genesisBlkHash_;
            sdbi.armoryType_ = armoryDbType_;
            sdbi.pruneType_ = dbPruneType_;
            putStoredDBInfo(CURRDB, sdbi);
         }
         else
         {
            // Check that the magic bytes are correct
            if(magicBytes_ != sdbi.magic_)
            {
               throw runtime_error("Magic bytes mismatch!  Different blkchain?");
            }
      
            else if(armoryDbType_ != sdbi.armoryType_)
            {
               LOGERR << "Mismatch in DB type";
               LOGERR << "DB is in  mode: " << (uint32_t)armoryDbType_;
               LOGERR << "Expecting mode: " << sdbi.armoryType_;
               throw runtime_error("Mismatch in DB type");
            }

            if(dbPruneType_ != sdbi.pruneType_)
            {
               throw runtime_error("Mismatch in DB type");
            }
         }
      }
   }
   catch (LMDBException &e)
   {
      LOGERR << "Exception thrown while opening database";
      LOGERR << e.what();
      throw e;
   }
   catch (runtime_error &e)
   {
      LOGERR << "Exception thrown while opening database";
      LOGERR << e.what();
      throw e;
   }
   catch(...)
   {
      LOGERR << "Exception thrown while opening database";
      closeDatabases();
      throw;
   }
   
   dbIsOpen_ = true;
}


/////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::nukeHeadersDB(void)
{
   SCOPED_TIMER("nukeHeadersDB");
   LOGINFO << "Destroying headers DB, to be rebuilt.";
   
   LMDBEnv::Transaction tx;
   beginDBTransaction(&tx, HEADERS, LMDB::ReadWrite);
   
   LMDB::Iterator begin = dbs_[HEADERS].begin();
   LMDB::Iterator end = dbs_[HEADERS].end();
   
   while(begin != end)
   {
      LMDB::Iterator here = begin;
      ++begin;
      
      dbs_[HEADERS].erase(here.key());
   }

   StoredDBInfo sdbi;
   sdbi.magic_      = magicBytes_;
   sdbi.topBlkHgt_  = 0;
   sdbi.topBlkHash_ = genesisBlkHash_;
   sdbi.armoryType_ = armoryDbType_;
   sdbi.pruneType_ = dbPruneType_;
   
   putStoredDBInfo(HEADERS, sdbi);
}


/////////////////////////////////////////////////////////////////////////////
// DBs don't really need to be closed.  Just delete them
void LMDBBlockDatabase::closeDatabases(void)
{
   if (armoryDbType_ == ARMORY_DB_SUPER)
   {
      closeDatabasesSupernode();
      return;
   }

   for(uint32_t db=0; db<COUNT; db++)
   {
      dbs_[(DB_SELECT)db].close();
      if (dbEnv_[(DB_SELECT)db] != nullptr)
         dbEnv_[(DB_SELECT)db]->close();
   }
   dbIsOpen_ = false;
}

/////////////////////////////////////////////////////////////////////////////
// DBs don't really need to be closed.  Just delete them
void LMDBBlockDatabase::closeDatabasesSupernode(void)
{
   dbs_[BLKDATA].close();
   dbs_[HEADERS].close();
   if (dbEnv_[BLKDATA] != nullptr)
      dbEnv_[BLKDATA]->close();
   dbIsOpen_ = false;
}


////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::destroyAndResetDatabases(void)
{
   SCOPED_TIMER("destroyAndResetDatabase");

   // We want to make sure the database is restarted with the same parameters
   // it was called with originally
   if (armoryDbType_ == ARMORY_DB_SUPER)
   {
      closeDatabasesSupernode();
      remove(dbBlkdataFilename().c_str());
   }
   else
   {
      closeDatabases();
      remove(dbHeadersFilename().c_str());
      remove(dbHistoryFilename().c_str());
      remove(dbBlkdataFilename().c_str());
      remove(dbTxhintsFilename().c_str());
   }
   
   // Reopen the databases with the exact same parameters as before
   // The close & destroy operations shouldn't have changed any of that.
   openDatabases(baseDir_, genesisBlkHash_, genesisTxHash_, 
      magicBytes_, armoryDbType_, dbPruneType_);
}


////////////////////////////////////////////////////////////////////////////////
BinaryData LMDBBlockDatabase::getTopBlockHash(DB_SELECT db)
{
   if (armoryDbType_ != ARMORY_DB_SUPER && db == BLKDATA)
      throw runtime_error("No SDBI in BLKDATA in Fullnode");

   LMDBEnv::Transaction tx;
   beginDBTransaction(&tx, db, LMDB::ReadOnly);
   StoredDBInfo sdbi;
   getStoredDBInfo(db, sdbi);
   return sdbi.topBlkHash_;
}


////////////////////////////////////////////////////////////////////////////////
uint32_t LMDBBlockDatabase::getTopBlockHeight(DB_SELECT db)
{
   StoredDBInfo sdbi;
   getStoredDBInfo(db, sdbi);
   return sdbi.topBlkHgt_;
}

/////////////////////////////////////////////////////////////////////////////
// Get value using pre-created slice
BinaryData LMDBBlockDatabase::getValue(DB_SELECT db, BinaryDataRef key) const
{
   return dbs_[db].value( CharacterArrayRef(
      key.getSize(), (char*)key.getPtr() ) );
}

/////////////////////////////////////////////////////////////////////////////
// Get value without resorting to a DB iterator
BinaryDataRef LMDBBlockDatabase::getValueNoCopy(DB_SELECT db, 
   BinaryDataRef key) const
{
   CharacterArrayRef data = dbs_[db].get_NoCopy(CharacterArrayRef(
      key.getSize(), (char*)key.getPtr()));
   if (data.data)
      return BinaryDataRef((uint8_t*)data.data, data.len);
   else
      return BinaryDataRef();
}

/////////////////////////////////////////////////////////////////////////////
// Get value using BinaryData object.  If you have a string, you can use
// BinaryData key(string(theStr));
BinaryData LMDBBlockDatabase::getValue(DB_SELECT db, 
                                       DB_PREFIX prefix,
                                       BinaryDataRef key) const
{
   BinaryData keyFull(key.getSize()+1);
   keyFull[0] = (uint8_t)prefix;
   key.copyTo(keyFull.getPtr()+1, key.getSize());
   try
   {
      return getValue(db, keyFull.getRef());
   }
   catch (...)
   {
      return BinaryData(0);
   }
}

/////////////////////////////////////////////////////////////////////////////
// Get value using BinaryDataRef object.  The data from the get* call is 
// actually copied to a member variable, and thus the refs are valid only 
// until the next get* call.
BinaryDataRef LMDBBlockDatabase::getValueRef(DB_SELECT db, BinaryDataRef key) const
{
   return getValueNoCopy(db, key);
}

/////////////////////////////////////////////////////////////////////////////
// Get value using BinaryDataRef object.  The data from the get* call is 
// actually copied to a member variable, and thus the refs are valid only 
// until the next get* call.
BinaryDataRef LMDBBlockDatabase::getValueRef(DB_SELECT db, 
                                             DB_PREFIX prefix, 
                                             BinaryDataRef key) const
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)prefix);
   bw.put_BinaryData(key);
   return getValueRef(db, bw.getDataRef());
}

/////////////////////////////////////////////////////////////////////////////
// Same as the getValueRef, in that they are only valid until the next get*
// call.  These are convenience methods which basically just save us 
BinaryRefReader LMDBBlockDatabase::getValueReader(
                                             DB_SELECT db, 
                                             BinaryDataRef keyWithPrefix) const
{
   return BinaryRefReader(getValueRef(db, keyWithPrefix));
}

/////////////////////////////////////////////////////////////////////////////
// Same as the getValueRef, in that they are only valid until the next get*
// call.  These are convenience methods which basically just save us 
BinaryRefReader LMDBBlockDatabase::getValueReader(
                                             DB_SELECT db, 
                                             DB_PREFIX prefix, 
                                             BinaryDataRef key) const
{

   return BinaryRefReader(getValueRef(db, prefix, key));
}

/////////////////////////////////////////////////////////////////////////////
// Header Key:  returns header hash
// Tx Key:      returns tx hash
// TxOut Key:   returns serialized OutPoint
BinaryData LMDBBlockDatabase::getHashForDBKey(BinaryData dbkey)
{
   uint32_t hgt;
   uint8_t  dup;
   uint16_t txi; 
   uint16_t txo; 

   size_t sz = dbkey.getSize();
   if(sz < 4 || sz > 9)
   {
      LOGERR << "Invalid DBKey size: " << sz << ", " << dbkey.toHexStr();
      return BinaryData(0);
   }
   
   BinaryRefReader brr(dbkey);
   if(dbkey.getSize() % 2 == 0)
      DBUtils::readBlkDataKeyNoPrefix(brr, hgt, dup, txi, txo);
   else
      DBUtils::readBlkDataKey(brr, hgt, dup, txi, txo);

   return getHashForDBKey(hgt, dup, txi, txo);
}


/////////////////////////////////////////////////////////////////////////////
// Header Key:  returns header hash
// Tx Key:      returns tx hash
// TxOut Key:   returns serialized OutPoint
BinaryData LMDBBlockDatabase::getHashForDBKey(uint32_t hgt,
                                           uint8_t  dup,
                                           uint16_t txi,
                                           uint16_t txo)
{

   if(txi==UINT16_MAX)
   {
      StoredHeader sbh; 
      getBareHeader(sbh, hgt, dup);
      return sbh.thisHash_;
   }
   else if(txo==UINT16_MAX)
   {
      StoredTx stx;
      getStoredTx(stx, hgt, dup, txi, false);
      return stx.thisHash_;
   }
   else 
   {
      StoredTx stx;
      getStoredTx(stx, hgt, dup, txi, false);
      OutPoint op(stx.thisHash_, txo);
      return op.serialize();
   }
}


/////////////////////////////////////////////////////////////////////////////
// Put value based on BinaryData key.  If batch writing, pass in the batch
void LMDBBlockDatabase::putValue(DB_SELECT db, 
                                  BinaryDataRef key, 
                                  BinaryDataRef value)
{
   dbs_[db].insert(
      CharacterArrayRef(key.getSize(), key.getPtr()),
      CharacterArrayRef(value.getSize(), value.getPtr())
   );
}

/////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putValue(DB_SELECT db, 
                              BinaryData const & key, 
                              BinaryData const & value)
{
   putValue(db, key.getRef(), value.getRef());
}

/////////////////////////////////////////////////////////////////////////////
// Put value based on BinaryData key.  If batch writing, pass in the batch
void LMDBBlockDatabase::putValue(DB_SELECT db, 
                                  DB_PREFIX prefix,
                                  BinaryDataRef key, 
                                  BinaryDataRef value)
{
   BinaryWriter bw;
   bw.put_uint8_t((uint8_t)prefix);
   bw.put_BinaryData(key.getPtr(), key.getSize());
   putValue(db, bw.getDataRef(), value);
}

/////////////////////////////////////////////////////////////////////////////
// Delete value based on BinaryData key.  If batch writing, pass in the batch
void LMDBBlockDatabase::deleteValue(DB_SELECT db, 
                                 BinaryDataRef key)
                 
{
   dbs_[db].erase( CharacterArrayRef(key.getSize(), key.getPtr() ) );
}


/////////////////////////////////////////////////////////////////////////////
// Delete Put value based on BinaryData key.  If batch writing, pass in the batch
void LMDBBlockDatabase::deleteValue(DB_SELECT db, 
                                 DB_PREFIX prefix,
                                 BinaryDataRef key)
{
   BinaryWriter bw;
   bw.put_uint8_t((uint8_t)prefix);
   bw.put_BinaryData(key);
   deleteValue(db, bw.getDataRef());
}

/////////////////////////////////////////////////////////////////////////////
// Not sure why this is useful over getHeaderMap() ... this iterates over
// the headers in hash-ID-order, instead of height-order
//void LMDBBlockDatabase::startHeaderIteration()
//{
   //SCOPED_TIMER("startHeaderIteration");
   //seekTo(HEADERS, DB_PREFIX_HEADHASH, BinaryData(0));
//}

/////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::startBlkDataIteration(LDBIter & ldbIter, DB_PREFIX prefix)
{
   return ldbIter.seekToStartsWith(prefix);
}



/////////////////////////////////////////////////////////////////////////////
// "Skip" refers to the behavior that the previous operation may have left
// the iterator already on the next desired block.  So our "advance" op may
// have finished before it started.  Alternatively, we may be on this block 
// because we checked it and decide we don't care, so we want to skip it.
bool LMDBBlockDatabase::advanceToNextBlock(LDBIter & ldbIter, bool skip) const
{
   BinaryData key;
   while(1) 
   {
      if(skip) 
         ldbIter.advanceAndRead();

      //if( !it->Valid() || it->key()[0] != (char)DB_PREFIX_TXDATA)
      if(!ldbIter.isValid(DB_PREFIX_TXDATA))
         return false;
      else if(ldbIter.getKeyRef().getSize() == 5)
         return true;

      if(!skip) 
         ldbIter.advanceAndRead();
         
   } 
   LOGERR << "we should never get here...";
   return false;
}


////////////////////////////////////////////////////////////////////////////////
// We frequently have a Tx hash and need to determine the Hgt/Dup/Index of it.
// And frequently when we do, we plan to read the tx right afterwards, so we
// should leave the itereator there.
bool LMDBBlockDatabase::seekToTxByHash(LDBIter & ldbIter, BinaryDataRef txHash) const
{
   SCOPED_TIMER("seekToTxByHash");
   StoredTxHints sths = getHintsForTxHash(txHash);

   for(uint32_t i=0; i<sths.getNumHints(); i++)
   {
      BinaryDataRef hint = sths.getHint(i);
      ldbIter.seekTo(DB_PREFIX_TXDATA, hint);
      {
         BinaryWriter bw(hint.getSize() + 1);
         bw.put_uint8_t((uint8_t)DB_PREFIX_TXDATA);
         bw.put_BinaryData(hint);
      }
      // We don't actually know for sure whether the seekTo() found a Tx or TxOut
      if(hint != ldbIter.getKeyRef().getSliceRef(1,6))
      {
         //LOGERR << "TxHint referenced a BLKDATA tx that doesn't exist";
         continue;
      }

      ldbIter.getValueReader().advance(2);  // skip flags
      if(ldbIter.getValueReader().get_BinaryDataRef(32) == txHash)
      {
         ldbIter.resetReaders();
         return true;
      }
   }

   //LOGERR << "No tx in DB with hash: " << txHash.toHexStr();
   ldbIter.resetReaders();
   return false;
}


/////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::readStoredScriptHistoryAtIter(LDBIter & ldbIter,
                                                   StoredScriptHistory & ssh,
                                                   uint32_t startBlock,
                                                   uint32_t endBlock) const
{
   SCOPED_TIMER("readStoredScriptHistoryAtIter");

   ldbIter.resetReaders();
   ldbIter.verifyPrefix(DB_PREFIX_SCRIPT, false);

   BinaryDataRef sshKey = ldbIter.getKeyRef();
   ssh.unserializeDBKey(sshKey, true);
   ssh.unserializeDBValue(ldbIter.getValueReader());
      
   size_t sz = sshKey.getSize();
   BinaryData scrAddr(sshKey.getSliceRef(1, sz - 1));
   size_t scrAddrSize = scrAddr.getSize();
   (void)scrAddrSize;

   if (startBlock != 0)
   {
      BinaryData dbkey_withHgtX(sshKey);
      dbkey_withHgtX.append(DBUtils::heightAndDupToHgtx(startBlock, 0));
      
      if (!ldbIter.seekTo(dbkey_withHgtX))
         return false;
   }
   else
   {
      // If for some reason we hit the end of the DB without any tx, bail
      if( !ldbIter.advanceAndRead(DB_PREFIX_SCRIPT))
      {
         //LOGERR << "No sub-SSH entries after the SSH";
         return false;
      }
   }

   // Now start iterating over the sub histories
   map<BinaryData, StoredSubHistory>::iterator iter;
   size_t numTxioRead = 0;
   do
   {
      size_t sz = ldbIter.getKeyRef().getSize();
      BinaryDataRef keyNoPrefix= ldbIter.getKeyRef().getSliceRef(1,sz-1);
      if(!keyNoPrefix.startsWith(ssh.uniqueKey_))
         break;

      pair<BinaryData, StoredSubHistory> keyValPair;
      keyValPair.first = keyNoPrefix.getSliceCopy(sz-5, 4);
      keyValPair.second.unserializeDBKey(ldbIter.getKeyRef());

      //iter is at the right ssh, make sure hgtX <= endBlock
      if (keyValPair.second.height_ > endBlock)
         break;

      keyValPair.second.unserializeDBValue(ldbIter.getValueReader());
      iter = ssh.subHistMap_.insert(keyValPair).first;
      numTxioRead += iter->second.txioMap_.size(); 
   } while( ldbIter.advanceAndRead(DB_PREFIX_SCRIPT) );

   return true;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putStoredScriptHistory( StoredScriptHistory & ssh)
{
   SCOPED_TIMER("putStoredScriptHistory");
   if(!ssh.isInitialized())
   {
      LOGERR << "Trying to put uninitialized SSH into DB";
      return;
   }

   DB_SELECT db;
   if (armoryDbType_ == ARMORY_DB_SUPER)
      db = BLKDATA;
   else
      db = HISTORY;
      
   putValue(db, ssh.getDBKey(), serializeDBValue(ssh, armoryDbType_, dbPruneType_));

   map<BinaryData, StoredSubHistory>::iterator iter;
   for (iter = ssh.subHistMap_.begin();
      iter != ssh.subHistMap_.end();
      iter++)
   {
      StoredSubHistory & subssh = iter->second;
      if (subssh.txioMap_.size() > 0)
         putValue(db, subssh.getDBKey(),
         serializeDBValue(subssh, this, armoryDbType_, dbPruneType_)
         );
   }
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putStoredScriptHistorySummary(StoredScriptHistory & ssh)
{
   SCOPED_TIMER("putStoredScriptHistory");
   if (!ssh.isInitialized())
   {
      LOGERR << "Trying to put uninitialized SSH into DB";
      return;
   }

   if (armoryDbType_ == ARMORY_DB_SUPER)
      putValue(BLKDATA, ssh.getDBKey(), serializeDBValue(ssh, armoryDbType_, dbPruneType_));
   else
      putValue(HISTORY, ssh.getDBKey(), serializeDBValue(ssh, armoryDbType_, dbPruneType_));
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putStoredSubHistory(StoredSubHistory & subssh)

{
   DB_SELECT db;
   if (armoryDbType_ == ARMORY_DB_SUPER)
      db = BLKDATA;
   else
      db = HISTORY;

   if (subssh.txioMap_.size() > 0)
      putValue(db, subssh.getDBKey(),
         serializeDBValue(subssh, this, armoryDbType_, dbPruneType_));
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::getStoredScriptHistorySummary( StoredScriptHistory & ssh,
   BinaryDataRef scrAddrStr) const
{
   LMDBEnv::Transaction tx;
   beginDBTransaction(&tx, HISTORY, LMDB::ReadOnly);

   DB_SELECT db;
   if (armoryDbType_ == ARMORY_DB_SUPER)
      db = BLKDATA;
   else
      db = HISTORY;

   LDBIter ldbIter = getIterator(db);
   ldbIter.seekTo(DB_PREFIX_SCRIPT, scrAddrStr);

   if(!ldbIter.seekToExact(DB_PREFIX_SCRIPT, scrAddrStr))
   {
      ssh.uniqueKey_.resize(0);
      return;
   }

   ssh.unserializeDBKey(ldbIter.getKeyRef());
   ssh.unserializeDBValue(ldbIter.getValueRef());
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredScriptHistory( StoredScriptHistory & ssh,
                                               BinaryDataRef scrAddrStr,
                                               uint32_t startBlock,
                                               uint32_t endBlock) const
{
   LMDBEnv::Transaction tx;
   beginDBTransaction(&tx, HISTORY, LMDB::ReadOnly);
   LDBIter ldbIter = getIterator(getDbSelect(HISTORY));

   if (!ldbIter.seekToExact(DB_PREFIX_SCRIPT, scrAddrStr))
   {
      ssh.uniqueKey_.resize(0);
      return false;
   }

   return readStoredScriptHistoryAtIter(ldbIter, ssh, startBlock, endBlock);
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredSubHistoryAtHgtX(StoredSubHistory& subssh,
   const BinaryData& scrAddrStr, const BinaryData& hgtX) const
{
   BinaryWriter bw(scrAddrStr.getSize() + hgtX.getSize());
   bw.put_BinaryData(scrAddrStr);
   bw.put_BinaryData(hgtX);

   LMDBEnv::Transaction tx;
   beginDBTransaction(&tx, HISTORY, LMDB::ReadOnly);
   LDBIter ldbIter = getIterator(getDbSelect(HISTORY));

   if (!ldbIter.seekToExact(DB_PREFIX_SCRIPT, bw.getDataRef()))
      return false;

   subssh.hgtX_ = hgtX;
   subssh.unserializeDBValue(ldbIter.getValueReader());
   return true;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::getStoredScriptHistoryByRawScript(
                                             StoredScriptHistory & ssh,
                                             BinaryDataRef script) const
{
   BinaryData uniqueKey = BtcUtils::getTxOutScrAddr(script);
   getStoredScriptHistory(ssh, uniqueKey);
}


/////////////////////////////////////////////////////////////////////////////
// This doesn't actually return a SUBhistory, it grabs it and adds it to the
// regular-SSH object.  This does not affect balance or Txio count.  It's 
// simply filling in data that the SSH may be expected to have.  
bool LMDBBlockDatabase::fetchStoredSubHistory( StoredScriptHistory & ssh,
                                            BinaryData hgtX,
                                            bool createIfDNE,
                                            bool forceReadDB)
{
   auto subIter = ssh.subHistMap_.find(hgtX);
   if (!forceReadDB && ITER_IN_MAP(subIter, ssh.subHistMap_))
   {
      return true;
   }

   BinaryData key = ssh.uniqueKey_ + hgtX; 
   BinaryRefReader brr = getValueReader(BLKDATA, DB_PREFIX_SCRIPT, key);

   StoredSubHistory subssh;
   subssh.uniqueKey_ = ssh.uniqueKey_;
   subssh.hgtX_      = hgtX;

   if(brr.getSize() > 0)
      subssh.unserializeDBValue(brr);
   else if(!createIfDNE)
      return false;

   ssh.mergeSubHistory(subssh);
   
   return true;
}


////////////////////////////////////////////////////////////////////////////////
uint64_t LMDBBlockDatabase::getBalanceForScrAddr(BinaryDataRef scrAddr, bool withMulti)
{
   StoredScriptHistory ssh;
   if(!withMulti)
   {
      getStoredScriptHistorySummary(ssh, scrAddr); 
      return ssh.totalUnspent_;
   }
   else
   {
      getStoredScriptHistory(ssh, scrAddr);
      uint64_t total = ssh.totalUnspent_;
      map<BinaryData, UnspentTxOut> utxoList;
      map<BinaryData, UnspentTxOut>::iterator iter;
      getFullUTXOMapForSSH(ssh, utxoList, true);
      for(iter = utxoList.begin(); iter != utxoList.end(); iter++)
         if(iter->second.isMultisigRef())
            total += iter->second.getValue();
      return total;
   }
}


////////////////////////////////////////////////////////////////////////////////
// We need the block hashes and scripts, which need to be retrieved from the
// DB, which is why this method can't be part of StoredBlockObj.h/.cpp
bool LMDBBlockDatabase::getFullUTXOMapForSSH( 
                                StoredScriptHistory & ssh,
                                map<BinaryData, UnspentTxOut> & mapToFill,
                                bool withMultisig)
{
   if(!ssh.haveFullHistoryLoaded())
      return false;

   LMDBEnv::Transaction tx;
   beginDBTransaction(&tx, HISTORY, LMDB::ReadOnly);

   {
      for (const auto& ssPair : ssh.subHistMap_)
      {
         const StoredSubHistory & subSSH = ssPair.second;
         for (const auto& txioPair : subSSH.txioMap_)
         {
            const TxIOPair & txio = txioPair.second;
            if (txio.isUTXO())
            {
               BinaryData txoKey = txio.getDBKeyOfOutput();
               BinaryData txKey = txio.getTxRefOfOutput().getDBKey();
               uint16_t txoIdx = txio.getIndexOfOutput();


               StoredTxOut stxo;
               getStoredTxOut(stxo, txoKey);
               BinaryData txHash = getTxHashForLdbKey(txKey);

               mapToFill[txoKey] = UnspentTxOut(
                  txHash,
                  txoIdx,
                  stxo.blockHeight_,
                  txio.getValue(),
                  stxo.getScriptRef());
            }
         }
      }
   }

   return true;
}




////////////////////////////////////////////////////////////////////////////////
// We must guarantee that we don't overwrite data if 
void LMDBBlockDatabase::addRegisteredScript(BinaryDataRef rawScript, 
                                         uint32_t      blockCreated)
{
   BinaryData uniqKey = BtcUtils::getTxOutScrAddr(rawScript);
   // bool       isMulti = BtcUtils::isMultisigScript(rawScript);

   StoredScriptHistory ssh;
   getStoredScriptHistory(ssh, uniqKey);
   
   uint32_t scannedTo;
   if(!ssh.isInitialized())
   {
      // Script is not registered in the DB yet
      ssh.uniqueKey_  = uniqKey;
      ssh.version_    = ARMORY_DB_VERSION;
      ssh.alreadyScannedUpToBlk_ = blockCreated;
      //ssh.multisigDBKeys_.resize(0);
      putStoredScriptHistory(ssh);
   }
   else
   {
      if(blockCreated!=UINT32_MAX)
         scannedTo = max(ssh.alreadyScannedUpToBlk_, blockCreated);
      
      // Only overwrite if the data in the DB is incorrect
      if(scannedTo != ssh.alreadyScannedUpToBlk_)
      {
         ssh.alreadyScannedUpToBlk_ = scannedTo;
         putStoredScriptHistory(ssh);
      }
   }

   registeredSSHs_[uniqKey] = ssh;
}

/////////////////////////////////////////////////////////////////////////////
// TODO: We should also read the HeaderHgtList entries to get the blockchain
//       sorting that is saved in the DB.  But right now, I'm not sure what
//       that would get us since we are reading all the headers and doing
//       a fresh organize/sort anyway.
void LMDBBlockDatabase::readAllHeaders(
   const function<void(const BlockHeader&, uint32_t, uint8_t)> &callback
)
{
   LMDBEnv::Transaction tx;
   beginDBTransaction(&tx, HEADERS, LMDB::ReadOnly);

   LDBIter ldbIter = getIterator(HEADERS);

   if(!ldbIter.seekToStartsWith(DB_PREFIX_HEADHASH))
   {
      LOGWARN << "No headers in DB yet!";
      return;
   }
   
   StoredHeader sbh;
   BlockHeader  regHead;
   do
   {
      ldbIter.resetReaders();
      ldbIter.verifyPrefix(DB_PREFIX_HEADHASH);
   
      if(ldbIter.getKeyReader().getSizeRemaining() != 32)
      {
         LOGERR << "How did we get header hash not 32 bytes?";
         continue;
      }

      ldbIter.getKeyReader().get_BinaryData(sbh.thisHash_, 32);

      sbh.unserializeDBValue(HEADERS, ldbIter.getValueRef());
      regHead.unserialize(sbh.dataCopy_);
      regHead.setBlockSize(sbh.numBytes_);

      if (sbh.thisHash_ != regHead.getThisHash())
      {
         LOGWARN << "Corruption detected: block header hash " <<
            sbh.thisHash_.copySwapEndian().toHexStr() << " does not match "
            << regHead.getThisHash().copySwapEndian().toHexStr();
      }
      callback(regHead, sbh.blockHeight_, sbh.duplicateID_);

   } while(ldbIter.advanceAndRead(DB_PREFIX_HEADHASH));
}

////////////////////////////////////////////////////////////////////////////////
uint8_t LMDBBlockDatabase::getValidDupIDForHeight(uint32_t blockHgt) const
{
   if(blockHgt != UINT32_MAX && validDupByHeight_.size() < blockHgt+1)
   {
      LOGERR << "Block height exceeds DupID lookup table";
      return UINT8_MAX;
   }
   return validDupByHeight_[blockHgt];
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::setValidDupIDForHeight(uint32_t blockHgt, uint8_t dup,
                                               bool overwrite)
{
   while (blockHgt != UINT32_MAX && validDupByHeight_.size() < blockHgt + 1)
      validDupByHeight_.push_back(UINT8_MAX);
   
   uint8_t& dupid = validDupByHeight_[blockHgt];
   if (!overwrite && dupid != UINT8_MAX)
      return;

   dupid = dup;
}

////////////////////////////////////////////////////////////////////////////////
uint8_t LMDBBlockDatabase::getValidDupIDForHeight_fromDB(uint32_t blockHgt)
{

   BinaryData hgt4((uint8_t*)&blockHgt, 4);
   BinaryRefReader brrHgts = getValueReader(HEADERS, DB_PREFIX_HEADHGT, hgt4);

   if(brrHgts.getSize() == 0)
   {
      LOGERR << "Requested header does not exist in DB";
      return false;
   }

   uint8_t lenEntry = 33;
   uint8_t numDup = (uint8_t)(brrHgts.getSize() / lenEntry);
   for(uint8_t i=0; i<numDup; i++)
   {
      uint8_t dup8 = brrHgts.get_uint8_t(); 
      // BinaryDataRef thisHash = brrHgts.get_BinaryDataRef(lenEntry-1);
      if((dup8 & 0x80) > 0)
         return (dup8 & 0x7f);
   }

   LOGERR << "Requested a header-by-height but none were marked as main";
   return UINT8_MAX;
}


////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putStoredDBInfo(DB_SELECT db, 
   StoredDBInfo const & sdbi)
{
   SCOPED_TIMER("putStoredDBInfo");
   if(!sdbi.isInitialized())
   {
      LOGERR << "Tried to put DB info into DB but it's not initialized";
      return;
   }
   putValue(db, sdbi.getDBKey(), serializeDBValue(sdbi));
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredDBInfo(DB_SELECT db, 
   StoredDBInfo & sdbi, bool warn)
{
   SCOPED_TIMER("getStoredDBInfo");
   LMDBEnv::Transaction tx;
   beginDBTransaction(&tx, db, LMDB::ReadOnly);

   BinaryRefReader brr = getValueRef(db, StoredDBInfo::getDBKey());
    
   if(brr.getSize() == 0 && warn) 
   {
      LOGERR << "No DB info key in database to get";
      return false;
   }
   sdbi.unserializeDBValue(brr);
   return true;
}

////////////////////////////////////////////////////////////////////////////////
// We assume that the SBH has the correct blockheight already included.  Will 
// adjust the dupID value in the SBH after we determine it.
// Will overwrite existing data, for simplicity, and so that this method allows
// us to easily replace/update data, even if overwriting isn't always necessary
//
// NOTE:  If you want this header to be marked valid/invalid, make sure the 
//        isMainBranch_ property of the SBH is set appropriate before calling.
uint8_t LMDBBlockDatabase::putStoredHeader( StoredHeader & sbh, 
   bool withBlkData, bool updateDupID)
{
   SCOPED_TIMER("putStoredHeader");

   if (armoryDbType_ != ARMORY_DB_SUPER)
   {
      LOGERR << "This method is only meant for supernode";
      throw runtime_error("dbType incompatible with putStoredHeader");
   }

   // Put header into HEADERS DB
   uint8_t newDup = putBareHeader(sbh, updateDupID);

   ///////
   // If we only wanted to update the headers DB, we're done.
   if(!withBlkData)
      return newDup;

   LMDBEnv::Transaction tx(dbEnv_[BLKDATA].get(), LMDB::ReadWrite);

   BinaryData key = DBUtils::getBlkDataKey(sbh.blockHeight_, sbh.duplicateID_);
   BinaryWriter bwBlkData;
   sbh.serializeDBValue(bwBlkData, BLKDATA, armoryDbType_, dbPruneType_);
   putValue(BLKDATA, BinaryDataRef(key), bwBlkData.getDataRef());
   

   for(uint32_t i=0; i<sbh.numTx_; i++)
   {
      map<uint16_t, StoredTx>::iterator txIter = sbh.stxMap_.find(i);
      //if(txIter != sbh.stxMap_.end())
      if(ITER_IN_MAP(txIter, sbh.stxMap_))
      {
         // Make sure the txIndex value is correct, then dump it to DB.
         txIter->second.txIndex_ = i;

         // When writing out the tx, we always write out the TxOuts.  
         // (that's what the second "true" argument is specifying)
         // There's no situation where we indicate *at the block-header 
         // level*, that we want to put the Txs but not the TxOuts.  
         // In other contexts, it may be desired to put/update a Tx 
         // without updating the TxOuts.
         putStoredTx(txIter->second, true);
      }
   }

   // If this is a valid block being put in BLKDATA DB, update DBInfo
   if(sbh.isMainBranch_ && withBlkData)
   {
      StoredDBInfo sdbiB;
      getStoredDBInfo(BLKDATA, sdbiB);
      if(sbh.blockHeight_ > sdbiB.topBlkHgt_)
      {
         sdbiB.topBlkHgt_  = sbh.blockHeight_;
         sdbiB.topBlkHash_ = sbh.thisHash_;
         putStoredDBInfo(BLKDATA, sdbiB);
      }
   }

   return newDup;
}


////////////////////////////////////////////////////////////////////////////////
// Puts bare header into HEADERS DB.  Use "putStoredHeader" to add to both
// (which actually calls this method as the first step)
//
// Returns the duplicateID of the header just inserted
uint8_t LMDBBlockDatabase::putBareHeader(StoredHeader & sbh, bool updateDupID)
{
   SCOPED_TIMER("putBareHeader");

   if(!sbh.isInitialized())
   {
      LOGERR << "Attempting to put uninitialized bare header into DB";
      return UINT8_MAX;
   }
   
   if (sbh.blockHeight_ == UINT32_MAX)
   {
      throw runtime_error("Attempted to put a header with no height");
   }

   // Batch the two operations to make sure they both hit the DB, or neither 
   LMDBEnv::Transaction tx;
   beginDBTransaction(&tx, HEADERS, LMDB::ReadWrite);

   StoredDBInfo sdbiH;
   getStoredDBInfo(HEADERS, sdbiH);


   uint32_t height  = sbh.blockHeight_;
   uint8_t sbhDupID = UINT8_MAX;

   // Check if it's already in the height-indexed DB - determine dupID if not
   StoredHeadHgtList hhl;
   getStoredHeadHgtList(hhl, height);

   bool alreadyInHgtDB = false;
   bool needToWriteHHL = false;
   if(hhl.dupAndHashList_.size() == 0)
   {
      sbhDupID = 0;
      hhl.addDupAndHash(0, sbh.thisHash_);
      if(sbh.isMainBranch_)
         hhl.preferredDup_ = 0;
      needToWriteHHL = true;
   }
   else
   {
      int8_t maxDup = -1;
      for(uint8_t i=0; i<hhl.dupAndHashList_.size(); i++)
      {
         uint8_t dup = hhl.dupAndHashList_[i].first;
         maxDup = max(maxDup, (int8_t)dup);
         if(sbh.thisHash_ == hhl.dupAndHashList_[i].second)
         {
            alreadyInHgtDB = true;
            sbhDupID = dup;
            if(hhl.preferredDup_ != dup && sbh.isMainBranch_ && updateDupID)
            {
               // The header was in the head-hgt list, but not preferred
               hhl.preferredDup_ = dup;
               needToWriteHHL = true;
            }
            break;
         }
      }

      if(!alreadyInHgtDB)
      {
         needToWriteHHL = true;
         sbhDupID = maxDup+1;
         hhl.addDupAndHash(sbhDupID, sbh.thisHash_);
         if(sbh.isMainBranch_ && updateDupID)
            hhl.preferredDup_ = sbhDupID;
      }
   }

   sbh.setKeyData(height, sbhDupID);
   

   if(needToWriteHHL)
      putStoredHeadHgtList(hhl);
      
   // Overwrite the existing hash-indexed entry, just in case the dupID was
   // not known when previously written.  
   putValue(HEADERS, DB_PREFIX_HEADHASH, sbh.thisHash_,
      serializeDBValue(sbh, HEADERS, armoryDbType_, dbPruneType_));

   // If this block is valid, update quick lookup table, and store it in DBInfo
   if(sbh.isMainBranch_)
   {
      setValidDupIDForHeight(sbh.blockHeight_, sbh.duplicateID_, updateDupID);
      if(sbh.blockHeight_ >= sdbiH.topBlkHgt_)
      {
         sdbiH.topBlkHgt_  = sbh.blockHeight_;
         sdbiH.topBlkHash_ = sbh.thisHash_;
         putStoredDBInfo(HEADERS, sdbiH);
      }
   }
   return sbhDupID;
}

////////////////////////////////////////////////////////////////////////////////
// "BareHeader" refers to 
bool LMDBBlockDatabase::getBareHeader(StoredHeader & sbh, 
                                   uint32_t blockHgt, 
                                   uint8_t dup)
{
   SCOPED_TIMER("getBareHeader");

   // Get the hash from the head-hgt list
   StoredHeadHgtList hhl;
   if(!getStoredHeadHgtList(hhl, blockHgt))
   {
      LOGERR << "No headers at height " << blockHgt;
      return false;
   }

   for(uint32_t i=0; i<hhl.dupAndHashList_.size(); i++)
      if(dup==hhl.dupAndHashList_[i].first)
         return getBareHeader(sbh, hhl.dupAndHashList_[i].second);

   return false;

}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getBareHeader(StoredHeader & sbh, uint32_t blockHgt)
{
   SCOPED_TIMER("getBareHeader(duplookup)");

   uint8_t dupID = getValidDupIDForHeight(blockHgt);
   if(dupID == UINT8_MAX)
      LOGERR << "Headers DB has no block at height: " << blockHgt; 

   return getBareHeader(sbh, blockHgt, dupID);
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getBareHeader(StoredHeader & sbh, BinaryDataRef headHash)
{
   SCOPED_TIMER("getBareHeader(hashlookup)");

   BinaryRefReader brr = getValueReader(HEADERS, DB_PREFIX_HEADHASH, headHash);

   if(brr.getSize() == 0)
   {
      LOGERR << "Header found in HHL but hash does not exist in DB";
      return false;
   }
   sbh.unserializeDBValue(HEADERS, brr);
   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredHeader( StoredHeader & sbh,
                                      uint32_t blockHgt,
                                      uint8_t blockDup,
                                      bool withTx) const
{
   SCOPED_TIMER("getStoredHeader");

   if (armoryDbType_ == ARMORY_DB_SUPER)
   {
      LMDBEnv::Transaction tx(dbEnv_[BLKDATA].get(), LMDB::ReadOnly);
      if (!withTx)
      {
         //////
         // Don't need to mess with seeking if we don't need the transactions.
         BinaryData blkKey = DBUtils::getBlkDataKey(blockHgt, blockDup);
         BinaryRefReader brr = getValueReader(BLKDATA, blkKey);
         if (brr.getSize() == 0)
         {
            LOGERR << "Header height&dup is not in BLKDATA";
            return false;
         }
         sbh.blockHeight_ = blockHgt;
         sbh.duplicateID_ = blockDup;
         sbh.unserializeDBValue(BLKDATA, brr, false);
         sbh.isMainBranch_ = (blockDup == getValidDupIDForHeight(blockHgt));
         return true;
      }
      else
      {
         //////
         // Do the iterator thing because we're going to traverse the whole block
         LDBIter ldbIter = getIterator(BLKDATA);
         if (!ldbIter.seekToExact(DBUtils::getBlkDataKey(blockHgt, blockDup)))
         {
            LOGERR << "Header heigh&dup is not in BLKDATA DB";
            LOGERR << "(" << blockHgt << ", " << blockDup << ")";
            return false;
         }

         // Now we read the whole block, not just the header
         bool success = readStoredBlockAtIter(ldbIter, sbh);
         sbh.isMainBranch_ = (blockDup == getValidDupIDForHeight(blockHgt));
         return success;
      }
   }
   else
   {
      LMDBEnv::Transaction tx(dbEnv_[BLKDATA].get(), LMDB::ReadOnly);
      BinaryData blkKey = DBUtils::getBlkDataKey(blockHgt, blockDup);
      BinaryRefReader brr = getValueReader(BLKDATA, blkKey);

      if (brr.getSize() == 0)
      {
         LOGERR << "Header height&dup is not in BLKDATA";
         return false;
      }
      sbh.blockHeight_ = blockHgt;
      sbh.duplicateID_ = blockDup;
      
      if (!withTx)
      {
         sbh.unserializeDBValue(BLKDATA, brr, false);
      }
      else
      {
         //Fullnode, need to unserialize txn too
         try
         {
            sbh.unserializeFullBlock(brr, true, false);
         }
         catch(BlockDeserializingException &)
         {
            throw BlockDeserializingException("Error parsing block (corrupt?) and block header invalid");
         }
      }

      sbh.isMainBranch_ = (blockDup == getValidDupIDForHeight(blockHgt));
      return true;
   }
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredHeader( StoredHeader & sbh,
                                      BinaryDataRef headHash, 
                                      bool withTx) const
{
   SCOPED_TIMER("getStoredHeader(hashlookup)");

   BinaryData headEntry = getValue(HEADERS, DB_PREFIX_HEADHASH, headHash); 
   if(headEntry.getSize() == 0)
   {
      LOGERR << "Requested header that is not in DB";
      return false;
   }
   
   BinaryRefReader brr(headEntry);
   sbh.unserializeDBValue(HEADERS, brr);

   return getStoredHeader(sbh, sbh.blockHeight_, sbh.duplicateID_, withTx);
}


////////////////////////////////////////////////////////////////////////////////
/*
bool LMDBBlockDatabase::getStoredHeader( StoredHeader & sbh,
                                      uint32_t blockHgt,
                                      bool withTx)
{
   SCOPED_TIMER("getStoredHeader(duplookup)");

   uint8_t dupID = getValidDupIDForHeight(blockHgt);
   if(dupID == UINT8_MAX)
      LOGERR << "Headers DB has no block at height: " << blockHgt; 

   return getStoredHeader(sbh, blockHgt, dupID, withTx);
}
*/

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::updateStoredTx(StoredTx & stx)
{
   // Add the individual TxOut entries if requested

   uint32_t version = READ_UINT32_LE(stx.dataCopy_.getPtr());

   for (auto& stxo : stx.stxoMap_)
   {      // Make sure all the parameters of the TxOut are set right 
      stxo.second.txVersion_ = version;
      stxo.second.blockHeight_ = stx.blockHeight_;
      stxo.second.duplicateID_ = stx.duplicateID_;
      stxo.second.txIndex_ = stx.txIndex_;
      stxo.second.txOutIndex_ = stxo.first;
      putStoredTxOut(stxo.second);
   }
}


////////////////////////////////////////////////////////////////////////////////
// This assumes that this new tx is "preferred" and will update the list as such
void LMDBBlockDatabase::putStoredTx( StoredTx & stx, bool withTxOut)
{
   if (armoryDbType_ != ARMORY_DB_SUPER)
   {
      LOGERR << "putStoredTx is only meant for Supernode";
      throw runtime_error("mismatch dbType with putStoredTx");
   }

   SCOPED_TIMER("putStoredTx");
   BinaryData ldbKey = DBUtils::getBlkDataKeyNoPrefix(stx.blockHeight_, 
                                                      stx.duplicateID_, 
                                                      stx.txIndex_);


   // First, check if it's already in the hash-indexed DB
   StoredTxHints sths;
   getStoredTxHints(sths, stx.thisHash_);

   // Check whether the hint already exists in the DB
   bool needToAddTxToHints = true;
   bool needToUpdateHints = false;
   for(uint32_t i=0; i<sths.dbKeyList_.size(); i++)
   {
      if(sths.dbKeyList_[i] == ldbKey)
      {
         needToAddTxToHints = false;
         needToUpdateHints = (sths.preferredDBKey_!=ldbKey);
         sths.preferredDBKey_ = ldbKey;
         break;
      }
   }

   // Add it to the hint list if needed
   if(needToAddTxToHints)
   {
      sths.dbKeyList_.push_back(ldbKey);
      sths.preferredDBKey_ = ldbKey;
   }

   if(needToAddTxToHints || needToUpdateHints)
      putStoredTxHints(sths);

   // Now add the base Tx entry in the BLKDATA DB.
   BinaryWriter bw;
   stx.serializeDBValue(bw, armoryDbType_, dbPruneType_);
   putValue(BLKDATA, DB_PREFIX_TXDATA, ldbKey, bw.getDataRef());


   // Add the individual TxOut entries if requested
   if(withTxOut)
   {
      map<uint16_t, StoredTxOut>::iterator iter;
      for(iter  = stx.stxoMap_.begin();
          iter != stx.stxoMap_.end();
          iter++)
      {
         // Make sure all the parameters of the TxOut are set right 
         iter->second.txVersion_   = READ_UINT32_LE(stx.dataCopy_.getPtr());
         iter->second.blockHeight_ = stx.blockHeight_;
         iter->second.duplicateID_ = stx.duplicateID_;
         iter->second.txIndex_     = stx.txIndex_;
         iter->second.txOutIndex_  = iter->first;
         putStoredTxOut(iter->second);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putStoredZC(StoredTx & stx, const BinaryData& zcKey)
{
   SCOPED_TIMER("putStoredTx");

   DB_SELECT dbs = BLKDATA;
   if (armoryDbType_ != ARMORY_DB_SUPER)
      dbs = HISTORY;

   // Now add the base Tx entry in the BLKDATA DB.
   BinaryWriter bw;
   stx.serializeDBValue(bw, armoryDbType_, dbPruneType_);
   bw.put_uint32_t(stx.unixTime_);
   putValue(dbs, DB_PREFIX_ZCDATA, zcKey, bw.getDataRef());


   // Add the individual TxOut entries
   {
      map<uint16_t, StoredTxOut>::iterator iter;
      for (iter = stx.stxoMap_.begin();
         iter != stx.stxoMap_.end();
         iter++)
      {
         // Make sure all the parameters of the TxOut are set right 
         iter->second.txVersion_ = READ_UINT32_LE(stx.dataCopy_.getPtr());
         //iter->second.blockHeight_ = stx.blockHeight_;
         //iter->second.duplicateID_ = stx.duplicateID_;
         iter->second.txIndex_ = stx.txIndex_;
         iter->second.txOutIndex_ = iter->first;
         BinaryData zcStxoKey(zcKey);
         zcStxoKey.append(WRITE_UINT16_BE(iter->second.txOutIndex_));
         putStoredZcTxOut(iter->second, zcStxoKey);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::updatePreferredTxHint( BinaryDataRef hashOrPrefix,
                                            BinaryData    preferDBKey)
{
   SCOPED_TIMER("updatePreferredTxHint");
   StoredTxHints sths;
   getStoredTxHints(sths, hashOrPrefix);

   if(sths.preferredDBKey_ == preferDBKey)
      return;

   // Check whether the hint already exists in the DB
   bool exists = false;
   for(uint32_t i=0; i<sths.dbKeyList_.size(); i++)
   {
      if(sths.dbKeyList_[i] == preferDBKey)
      {
         exists = true;
         break;
      }
   }

   if(!exists)
   {
      LOGERR << "Key not in hint list, something is wrong";
      return;
   }

   // sths.dbKeyList_.push_back(preferDBKey);

   sths.preferredDBKey_ = preferDBKey;
   putStoredTxHints(sths);

}

////////////////////////////////////////////////////////////////////////////////
// We assume we have a valid iterator left at the header entry for this block
bool LMDBBlockDatabase::readStoredBlockAtIter(LDBIter & ldbIter, DBBlock & sbh) const
{
   SCOPED_TIMER("readStoredBlockAtIter");

   ldbIter.resetReaders();
   BinaryData blkDataKey(ldbIter.getKeyReader().getCurrPtr(), 5);

   BLKDATA_TYPE bdtype = DBUtils::readBlkDataKey(ldbIter.getKeyReader(),
                                                 sbh.blockHeight_,
                                                 sbh.duplicateID_);

   if (bdtype == NOT_BLKDATA)
      return false;
      //throw runtime_error("readStoredBlockAtIter: tried to readBlkDataKey, got NOT_BLKDATA");
   
   if (armoryDbType_ != ARMORY_DB_SUPER)
   {
      try
      {
         sbh.blockHeight_ = DBUtils::hgtxToHeight(ldbIter.getKey().getSliceRef(1, 4));
         sbh.duplicateID_ = DBUtils::hgtxToDupID(ldbIter.getKey().getSliceRef(1, 4));
      
         sbh.unserializeFullBlock(ldbIter.getValueReader(), true, false);
      }
      catch (BlockDeserializingException &)
      {
         return false;
      }
      
      return true;
   }

   // Grab the header first, then iterate over 
   sbh.unserializeDBValue(BLKDATA, ldbIter.getValueRef(), false);
   sbh.isMainBranch_ = (sbh.duplicateID_==getValidDupIDForHeight(sbh.blockHeight_));

   // If for some reason we hit the end of the DB without any tx, bail
   //if(!ldbIter.advanceAndRead(DB_PREFIX_TXDATA)
      //return true;  // this isn't an error, it's an block w/o any StoredTx

   // Now start iterating over the tx data
   uint32_t tempHgt;
   uint8_t  tempDup;
   uint16_t currIdx;
   ldbIter.advanceAndRead();
   while(ldbIter.checkKeyStartsWith(blkDataKey))
   {

      // We can't just read the the tx, because we have to guarantee 
      // there's a place for it in the sbh.stxMap_
      BLKDATA_TYPE bdtype = DBUtils::readBlkDataKey(ldbIter.getKeyReader(), 
                                                   tempHgt, 
                                                   tempDup,
                                                   currIdx);
      
      if(currIdx >= sbh.numTx_)
      {
         LOGERR << "Invalid txIndex at height " << (sbh.blockHeight_)
                    << " index " << currIdx;
         return false;
      }

      DBTx& thisTx = sbh.getTxByIndex(currIdx);

      readStoredTxAtIter(ldbIter,
                         sbh.blockHeight_, 
                         sbh.duplicateID_, 
                         thisTx);
   }
   return true;
} 


////////////////////////////////////////////////////////////////////////////////
// We assume we have a valid iterator left at the beginning of (potentially) a 
// transaction in this block.  It's okay if it starts at at TxOut entry (in 
// some instances we may not have a Tx entry, but only the TxOuts).
bool LMDBBlockDatabase::readStoredTxAtIter( LDBIter & ldbIter,
                                         uint32_t height,
                                         uint8_t  dupID,
                                         DBTx & stx) const
{
   SCOPED_TIMER("readStoredTxAtIter");
   BinaryData blkPrefix = DBUtils::getBlkDataKey(height, dupID);

   // Make sure that we are still within the desired block (but beyond header)
   ldbIter.resetReaders();
   BinaryDataRef key = ldbIter.getKeyRef();
   if(!key.startsWith(blkPrefix) || key.getSize() < 7)
      return false;


   // Check that we are at a tx with the correct height & dup
   uint32_t storedHgt;
   uint8_t  storedDup;
   uint16_t storedIdx;
   DBUtils::readBlkDataKey(ldbIter.getKeyReader(), storedHgt, storedDup, storedIdx);

   if(storedHgt != height || storedDup != dupID)
      return false;


   // Make sure the stx has correct height/dup/idx
   stx.blockHeight_ = storedHgt;
   stx.duplicateID_ = storedDup;
   stx.txIndex_     = storedIdx;

   // Use a temp variable instead of stx.numBytes_ directly, because the 
   // stx.unserializeDBValue() call will reset numBytes to UINT32_MAX.
   // Assign it at the end, if we're confident we have the correct value.
   size_t nbytes  = 0;

   BinaryData txPrefix = DBUtils::getBlkDataKey(height, dupID, stx.txIndex_);

   
   // Reset the key again, and then cycle through entries until no longer
   // on an entry with the correct prefix.  Use do-while because we've 
   // already verified the iterator is at a valid tx entry
   ldbIter.resetReaders();
   do
   {


      // Stop if key doesn't start with [PREFIX | HGT | DUP | TXIDX]
      if(!ldbIter.checkKeyStartsWith(txPrefix))
         break;


      // Read the prefix, height and dup 
      uint16_t txOutIdx;
      BLKDATA_TYPE bdtype = DBUtils::readBlkDataKey(ldbIter.getKeyReader(),
                                           stx.blockHeight_,
                                           stx.duplicateID_,
                                           stx.txIndex_,
                                           txOutIdx);

      // Now actually process the iter value
      if(bdtype == BLKDATA_TX)
      {
         // Get everything else from the iter value
         stx.unserializeDBValue(ldbIter.getValueRef());
         nbytes += stx.dataCopy_.getSize();
      }
      else if(bdtype == BLKDATA_TXOUT)
      {
         StoredTxOut & stxo = stx.initAndGetStxoByIndex(txOutIdx);
         readStoredTxOutAtIter(ldbIter, height, dupID, stx.txIndex_, stxo);
         nbytes += stxo.dataCopy_.getSize();
      }
      else
      {
         LOGERR << "Unexpected BLKDATA entry while iterating";
         return false;
      }

   } while(ldbIter.advanceAndRead(DB_PREFIX_TXDATA));


   // If have the correct size, save it, otherwise ignore the computation
   stx.numBytes_ = stx.haveAllTxOut() ? nbytes : UINT32_MAX;

   return true;
} 


////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::readStoredTxOutAtIter(
                                       LDBIter & ldbIter, 
                                       uint32_t height,
                                       uint8_t  dupID,
                                       uint16_t txIndex,
                                       StoredTxOut & stxo) const
{
   if(ldbIter.getKeyRef().getSize() < 9)
      return false;

   ldbIter.resetReaders();

   // Check that we are at a tx with the correct height & dup & txIndex
   uint32_t keyHgt;
   uint8_t  keyDup;
   uint16_t keyTxIdx;
   uint16_t keyTxOutIdx;
   DBUtils::readBlkDataKey(ldbIter.getKeyReader(), 
                          keyHgt, keyDup, keyTxIdx, keyTxOutIdx);

   if(keyHgt != height || keyDup != dupID || keyTxIdx != txIndex)
      return false;

   stxo.blockHeight_ = height;
   stxo.duplicateID_ = dupID;
   stxo.txIndex_     = txIndex;
   stxo.txOutIndex_  = keyTxOutIdx;

   stxo.unserializeDBValue(ldbIter.getValueRef());

   return true;
}


////////////////////////////////////////////////////////////////////////////////
Tx LMDBBlockDatabase::getFullTxCopy( BinaryData ldbKey6B ) const
{
   SCOPED_TIMER("getFullTxCopy");
   if(ldbKey6B.getSize() != 6)
   {
      LOGERR << "Provided zero-length ldbKey6B";
      return Tx();
   }
   
   if (armoryDbType_ == ARMORY_DB_SUPER)
   {
      LMDBEnv::Transaction tx(dbEnv_[BLKDATA].get(), LMDB::ReadOnly);

      LDBIter ldbIter = getIterator(BLKDATA);
      if (!ldbIter.seekToStartsWith(DB_PREFIX_TXDATA, ldbKey6B))
      {
         LOGERR << "TxRef key does not exist in BLKDATA DB";
         return Tx();
      }

      BinaryData hgtx = ldbKey6B.getSliceCopy(0, 4);
      StoredTx stx;
      readStoredTxAtIter(ldbIter,
         DBUtils::hgtxToHeight(hgtx),
         DBUtils::hgtxToDupID(hgtx),
         stx);

      if (!stx.haveAllTxOut())
      {
         LOGERR << "Requested full Tx but not all TxOut available";
         return Tx();
      }

      return stx.getTxCopy();
   }
   else
   {
      //Fullnode, pull full block, deserialize then return Tx
      uint16_t txid = READ_UINT16_BE(ldbKey6B.getSliceRef(4, 2));
      
      LMDBEnv::Transaction tx(dbEnv_[BLKDATA].get(), LMDB::ReadOnly);
      BinaryRefReader brr = getValueReader(
         BLKDATA, DB_PREFIX_TXDATA, ldbKey6B.getSliceRef(0, 4));

      brr.advance(HEADER_SIZE);
      uint32_t nTx = (uint32_t)brr.get_var_int();

      if (txid >= nTx)
      {
         LOGERR << "Requested full Tx but not all TxOut available";
         return Tx();
      }

      uint32_t i = 0;
      while (i < txid)
      {
         uint32_t nBytes = BtcUtils::TxCalcLength(
            brr.getCurrPtr(), brr.getSizeRemaining(), nullptr, nullptr);
         brr.advance(nBytes);
         ++i;
      }

      return Tx(brr);
   }
}

////////////////////////////////////////////////////////////////////////////////
Tx LMDBBlockDatabase::getFullTxCopy( uint32_t hgt, uint16_t txIndex) const
{
   SCOPED_TIMER("getFullTxCopy");
   uint8_t dup = getValidDupIDForHeight(hgt);
   if(dup == UINT8_MAX)
      LOGERR << "Headers DB has no block at height: " << hgt;

   BinaryData ldbKey = DBUtils::getBlkDataKey(hgt, dup, txIndex);
   return getFullTxCopy(ldbKey);
}

////////////////////////////////////////////////////////////////////////////////
Tx LMDBBlockDatabase::getFullTxCopy( 
   uint32_t hgt, uint8_t dup, uint16_t txIndex) const
{
   SCOPED_TIMER("getFullTxCopy");
   BinaryData ldbKey = DBUtils::getBlkDataKey(hgt, dup, txIndex);
   return getFullTxCopy(ldbKey);
}


////////////////////////////////////////////////////////////////////////////////
TxOut LMDBBlockDatabase::getTxOutCopy( 
   BinaryData ldbKey6B, uint16_t txOutIdx) const
{
   SCOPED_TIMER("getTxOutCopy");
   BinaryWriter bw(8);
   bw.put_BinaryData(ldbKey6B);
   bw.put_uint16_t(txOutIdx, BIGENDIAN);
   BinaryDataRef ldbKey8 = bw.getDataRef();

   TxOut txoOut;

   BinaryRefReader brr;
   if (!ldbKey6B.startsWith(ZCprefix_))
      brr = getValueReader(getDbSelect(HISTORY), DB_PREFIX_TXDATA, ldbKey8);
   else
      brr = getValueReader(getDbSelect(HISTORY), DB_PREFIX_ZCDATA, ldbKey8);

   if(brr.getSize()==0) 
   {
      LOGERR << "TxOut key does not exist in BLKDATA DB";
      return TxOut();
   }

   TxRef parent(ldbKey6B);

   brr.advance(2);
   txoOut.unserialize_checked(
      brr.getCurrPtr(), brr.getSizeRemaining(), 0, parent, (uint32_t)txOutIdx);
   return txoOut;
}


////////////////////////////////////////////////////////////////////////////////
TxIn LMDBBlockDatabase::getTxInCopy( 
   BinaryData ldbKey6B, uint16_t txInIdx) const
{
   SCOPED_TIMER("getTxInCopy");

   if (armoryDbType_ == ARMORY_DB_SUPER)
   {
      TxIn txiOut;
      BinaryRefReader brr = getValueReader(BLKDATA, DB_PREFIX_TXDATA, ldbKey6B);
      if (brr.getSize() == 0)
      {
         LOGERR << "TxOut key does not exist in BLKDATA DB";
         return TxIn();
      }

      BitUnpacker<uint16_t> bitunpack(brr); // flags
      uint16_t dbVer = bitunpack.getBits(4);
      (void)dbVer;
      uint16_t txVer = bitunpack.getBits(2);
      (void)txVer;
      uint16_t txSer = bitunpack.getBits(4);

      brr.advance(32);


      if (txSer != TX_SER_FULL && txSer != TX_SER_FRAGGED)
      {
         LOGERR << "Tx not available to retrieve TxIn";
         return TxIn();
      }
      else
      {
         bool isFragged = txSer == TX_SER_FRAGGED;
         vector<size_t> offsetsIn;
         BtcUtils::StoredTxCalcLength(brr.getCurrPtr(), isFragged, &offsetsIn);
         if ((uint32_t)(offsetsIn.size() - 1) < (uint32_t)(txInIdx + 1))
         {
            LOGERR << "Requested TxIn with index greater than numTxIn";
            return TxIn();
         }
         TxRef parent(ldbKey6B);
         uint8_t const * txInStart = brr.exposeDataPtr() + 34 + offsetsIn[txInIdx];
         uint32_t txInLength = offsetsIn[txInIdx + 1] - offsetsIn[txInIdx];
         TxIn txin;
         txin.unserialize_checked(txInStart, brr.getSize() - 34 - offsetsIn[txInIdx], txInLength, parent, txInIdx);
         return txin;
      }
   }
   else
   {
      Tx thisTx = getFullTxCopy(ldbKey6B);
      return thisTx.getTxInCopy(txInIdx);
   }
}




////////////////////////////////////////////////////////////////////////////////
BinaryData LMDBBlockDatabase::getTxHashForLdbKey( BinaryDataRef ldbKey6B ) const
{
   SCOPED_TIMER("getTxHashForLdbKey");
   if (armoryDbType_ == ARMORY_DB_SUPER)
   {
      LMDBEnv::Transaction tx(dbEnv_[BLKDATA].get(), LMDB::ReadOnly);
      BinaryRefReader stxVal;

      if (!ldbKey6B.startsWith(ZCprefix_))
         stxVal = getValueReader(BLKDATA, DB_PREFIX_TXDATA, ldbKey6B);
      else
         stxVal = getValueReader(BLKDATA, DB_PREFIX_ZCDATA, ldbKey6B);

      if (stxVal.getSize() == 0)
      {
         LOGERR << "TxRef key does not exist in BLKDATA DB";
         return BinaryData(0);
      }

      // We can't get here unless we found the precise Tx entry we were looking for
      stxVal.advance(2);
      return stxVal.get_BinaryData(32);
   }
   else
   {
      //Fullnode, check the HISTORY DB for the txhash
      {
         LMDBEnv::Transaction tx(dbEnv_[HISTORY].get(), LMDB::ReadOnly);

         if (!ldbKey6B.startsWith(ZCprefix_))
         {
            BinaryData keyFull(ldbKey6B.getSize() + 1);
            keyFull[0] = (uint8_t)DB_PREFIX_TXDATA;
            ldbKey6B.copyTo(keyFull.getPtr() + 1, ldbKey6B.getSize());

            BinaryDataRef txData = getValueNoCopy(HISTORY, keyFull);

            if (txData.getSize() >= 36)
            {
               return txData.getSliceRef(4, 32);
            }
         }
         else
         {            
            BinaryRefReader stxVal = 
               getValueReader(HISTORY, DB_PREFIX_ZCDATA, ldbKey6B);

            if (stxVal.getSize() == 0)
            {
               LOGERR << "TxRef key does not exist in BLKDATA DB";
               return BinaryData(0);
            }

            // We can't get here unless we found the precise Tx entry we were looking for
            stxVal.advance(2);
            return stxVal.get_BinaryData(32);
         }
      }
      //else pull the full block then grab the txhash
      { 
         LMDBEnv::Transaction tx(dbEnv_[BLKDATA].get(), LMDB::ReadOnly);         
         auto thisTx = getFullTxCopy(ldbKey6B);

         return thisTx.getThisHash();
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
BinaryData LMDBBlockDatabase::getTxHashForHeightAndIndex( uint32_t height,
                                                       uint16_t txIndex)
{
   SCOPED_TIMER("getTxHashForHeightAndIndex");
   uint8_t dup = getValidDupIDForHeight(height);
   if(dup == UINT8_MAX)
      LOGERR << "Headers DB has no block at height: " << height;
   return getTxHashForLdbKey(DBUtils::getBlkDataKeyNoPrefix(height, dup, txIndex));
}

////////////////////////////////////////////////////////////////////////////////
BinaryData LMDBBlockDatabase::getTxHashForHeightAndIndex( uint32_t height,
                                                       uint8_t  dupID,
                                                       uint16_t txIndex)
{
   SCOPED_TIMER("getTxHashForHeightAndIndex");
   return getTxHashForLdbKey(DBUtils::getBlkDataKeyNoPrefix(height, dupID, txIndex));
}

////////////////////////////////////////////////////////////////////////////////
StoredTxHints LMDBBlockDatabase::getHintsForTxHash(BinaryDataRef txHash) const
{
   SCOPED_TIMER("getAllHintsForTxHash");
   StoredTxHints sths;
   sths.txHashPrefix_ = txHash.getSliceRef(0,4);
   BinaryRefReader brr;

   if (armoryDbType_ == ARMORY_DB_SUPER)
      brr = getValueReader(BLKDATA, DB_PREFIX_TXHINTS, sths.txHashPrefix_);
   else
      brr = getValueReader(TXHINTS, DB_PREFIX_TXHINTS, sths.txHashPrefix_);

   if(brr.getSize() == 0)
   {
      // Don't need to throw any errors, we frequently ask for tx that DNE
      //LOGERR << "No hints for prefix: " << sths.txHashPrefix_.toHexStr();
   }
   else
      sths.unserializeDBValue(brr);

   return sths;
}


////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredTx( StoredTx & stx,
                                  BinaryDataRef txHashOrDBKey) const
{
   uint32_t sz = txHashOrDBKey.getSize();
   if(sz == 32)
      return getStoredTx_byHash(txHashOrDBKey, &stx);
   else if(sz == 6 || sz == 7)
      return getStoredTx_byDBKey(stx, txHashOrDBKey);
   else
   {
      LOGERR << "Unrecognized input string: " << txHashOrDBKey.toHexStr();
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredTx_byDBKey( StoredTx & stx,
                                          BinaryDataRef dbKey) const
{
   uint32_t hgt;
   uint8_t  dup;
   uint16_t txi;

   BinaryRefReader brrKey(dbKey);

   if(dbKey.getSize() == 6)
      DBUtils::readBlkDataKeyNoPrefix(brrKey, hgt, dup, txi);
   else if(dbKey.getSize() == 7)
      DBUtils::readBlkDataKey(brrKey, hgt, dup, txi);
   else
   {
      LOGERR << "Unrecognized input string: " << dbKey.toHexStr();
      return false;
   }

   return getStoredTx(stx, hgt, dup, txi, true);
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredZcTx(StoredTx & stx,
   BinaryDataRef zcKey) const
{
   auto dbs = getDbSelect(HISTORY);
   
   //only by zcKey
   BinaryData zcDbKey;

   if (zcKey.getSize() == 6)
   {
      zcDbKey = BinaryData(7);
      uint8_t* ptr = zcDbKey.getPtr();
      ptr[0] = DB_PREFIX_ZCDATA;
      memcpy(ptr + 1, zcKey.getPtr(), 6);
   }
   else
      zcDbKey = zcKey;

   LDBIter ldbIter = getIterator(dbs);
   if (!ldbIter.seekToExact(zcDbKey))
   {
      LOGERR << "BLKDATA DB does not have the requested ZC tx";
      LOGERR << "(" << zcKey.toHexStr() << ")";
      return false;
   }

   size_t nbytes = 0;
   do
   {
      // Stop if key doesn't start with [PREFIX | ZCkey | TXIDX]
      if(!ldbIter.checkKeyStartsWith(zcDbKey))
         break;


      // Read the prefix, height and dup 
      uint16_t txOutIdx;
      BinaryRefReader txKey = ldbIter.getKeyReader();

      // Now actually process the iter value
      if(txKey.getSize()==7)
      {
         // Get everything else from the iter value
         stx.unserializeDBValue(ldbIter.getValueRef());
         nbytes += stx.dataCopy_.getSize();
      }
      else if(txKey.getSize() == 9)
      {
         txOutIdx = READ_UINT16_BE(ldbIter.getKeyRef().getSliceRef(7, 2));
         StoredTxOut & stxo = stx.stxoMap_[txOutIdx];
         stxo.unserializeDBValue(ldbIter.getValueRef());
         stxo.parentHash_ = stx.thisHash_;
         stxo.txVersion_  = stx.version_;
         stxo.txOutIndex_ = txOutIdx;
         nbytes += stxo.dataCopy_.getSize();
      }
      else
      {
         LOGERR << "Unexpected BLKDATA entry while iterating";
         return false;
      }

   } while(ldbIter.advanceAndRead(DB_PREFIX_ZCDATA));


   stx.numBytes_ = stx.haveAllTxOut() ? nbytes : UINT32_MAX;

   return true;
}

////////////////////////////////////////////////////////////////////////////////
// We assume that the first TxHint that matches is correct.  This means that 
// when we mark a transaction/block valid, we need to make sure all the hints
// lists have the correct one in front.  Luckily, the TXHINTS entries are tiny 
// and the number of modifications to make for each reorg is small.
bool LMDBBlockDatabase::getStoredTx_byHash(BinaryDataRef txHash,
                                           StoredTx* stx,
                                           BinaryData *DBkey) const
{
   SCOPED_TIMER("getStoredTx");
   if (armoryDbType_ == ARMORY_DB_SUPER)
      return getStoredTx_byHashSuper(txHash, stx, DBkey);

   if (stx == nullptr && DBkey == nullptr)
      return false;

   BinaryData hash4(txHash.getSliceRef(0,4));
   
   LMDBEnv::Transaction txHints(dbEnv_[TXHINTS].get(), LMDB::ReadOnly);
   LMDBEnv::Transaction txBlkData(dbEnv_[BLKDATA].get(), LMDB::ReadOnly);

   BinaryRefReader brrHints = getValueRef(TXHINTS, DB_PREFIX_TXHINTS, hash4);

   uint32_t valSize = brrHints.getSize();

   if(valSize < 2)
   {
      //LOGERR << "No tx in DB with hash: " << txHash.toHexStr();
      return false;
   }

   LDBIter ldbIter = getIterator(BLKDATA);
   
   uint32_t numHints = (uint32_t)brrHints.get_var_int();
   uint32_t height;
   uint8_t  dup;
   uint16_t txIdx;
   for(uint32_t i=0; i<numHints; i++)
   {
      BinaryDataRef hint = brrHints.get_BinaryDataRef(6);
      BinaryRefReader brrHint(hint);
      BLKDATA_TYPE bdtype = DBUtils::readBlkDataKeyNoPrefix(
         brrHint, height, dup, txIdx);

      if (dup != getValidDupIDForHeight(height) && numHints > 1)
         continue;

      Tx thisTx = getFullTxCopy(hint);
      if (!thisTx.isInitialized())
      {
         LOGERR << "Hinted tx does not exist in DB";
         LOGERR << "TxHash: " << hint.toHexStr().c_str();
         continue;
      }

      if (thisTx.getThisHash() != txHash)
         continue;

      if (stx != nullptr)
      {
         stx->createFromTx(thisTx);
         stx->blockHeight_ = height;
         stx->duplicateID_ = dup;
         stx->txIndex_ = txIdx;

         for (auto& stxo : stx->stxoMap_)
         {
            stxo.second.blockHeight_ = height;
            stxo.second.duplicateID_ = dup;
         }
      }
      else
         DBkey->copyFrom(hint);
            
      return true;
   }

   return false;
}

bool LMDBBlockDatabase::getStoredTx_byHashSuper(BinaryDataRef txHash,
   StoredTx* stx,
   BinaryData *DBkey) const
{
   SCOPED_TIMER("getStoredTx");

   if (stx == nullptr && DBkey == nullptr)
      return false;

   BinaryData hash4(txHash.getSliceRef(0, 4));

   LMDBEnv::Transaction tx(dbEnv_[BLKDATA].get(), LMDB::ReadOnly);

   BinaryRefReader brrHints = getValueRef(BLKDATA, DB_PREFIX_TXHINTS, hash4);

   uint32_t valSize = brrHints.getSize();

   if (valSize < 2)
   {
      //LOGERR << "No tx in DB with hash: " << txHash.toHexStr();
      return false;
   }

   LDBIter ldbIter = getIterator(BLKDATA);

   uint32_t numHints = (uint32_t)brrHints.get_var_int();
   uint32_t height;
   uint8_t  dup;
   uint16_t txIdx;
   for (uint32_t i = 0; i<numHints; i++)
   {
      BinaryDataRef hint = brrHints.get_BinaryDataRef(6);

      if (!ldbIter.seekToExact(DB_PREFIX_TXDATA, hint))
      {
         LOGERR << "Hinted tx does not exist in DB";
         LOGERR << "TxHash: " << hint.toHexStr().c_str();
         continue;
      }

      BLKDATA_TYPE bdtype = DBUtils::readBlkDataKey(ldbIter.getKeyReader(),
         height, dup, txIdx);
      (void)bdtype;

      if (dup != getValidDupIDForHeight(height) && numHints > 1)
         continue;

      // We don't actually know for sure whether the seekTo() found 
      BinaryData key6 = DBUtils::getBlkDataKeyNoPrefix(height, dup, txIdx);
      if (key6 != hint)
      {
         LOGERR << "TxHint referenced a BLKDATA tx that doesn't exist";
         LOGERR << "Key:  '" << key6.toHexStr() << "', "
            << "Hint: '" << hint.toHexStr() << "'";
         continue;
      }

      ldbIter.getValueReader().advance(2);  // skip flags
      if (ldbIter.getValueReader().get_BinaryDataRef(32) == txHash)
      {
         ldbIter.resetReaders();
         if (stx != nullptr)
            return readStoredTxAtIter(ldbIter, height, dup, *stx);
         else
         {
            DBkey->copyFrom(key6);
            return true;
         }
      }
   }

   return false;
}


////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredTx( StoredTx & stx,
                                  uint32_t blockHeight,
                                  uint16_t txIndex,
                                  bool withTxOut) const
{
   uint8_t dupID = getValidDupIDForHeight(blockHeight);
   if(dupID == UINT8_MAX)
      LOGERR << "Headers DB has no block at height: " << blockHeight; 

   return getStoredTx(stx, blockHeight, dupID, txIndex, withTxOut);

}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredTx( StoredTx & stx,
                                  uint32_t blockHeight,
                                  uint8_t  dupID,
                                  uint16_t txIndex,
                                  bool withTxOut) const
{
   SCOPED_TIMER("getStoredTx");

   BinaryData blkDataKey = DBUtils::getBlkDataKey(blockHeight, dupID, txIndex);
   stx.blockHeight_ = blockHeight;
   stx.duplicateID_  = dupID;
   stx.txIndex_     = txIndex;

   if(!withTxOut)
   {
      // In some situations, withTxOut may not matter here:  the TxOuts may
      // actually be serialized with the tx entry, thus the unserialize call
      // may extract all TxOuts.
      BinaryRefReader brr = getValueReader(BLKDATA, blkDataKey);
      if(brr.getSize()==0)
      {
         LOGERR << "BLKDATA DB does not have requested tx";
         LOGERR << "("<<blockHeight<<", "<<dupID<<", "<<txIndex<<")";
         return false;
      }

      stx.unserializeDBValue(brr);
   }
   else
   {
      LDBIter ldbIter = getIterator(BLKDATA);
      if(!ldbIter.seekToExact(blkDataKey))
      {
         LOGERR << "BLKDATA DB does not have the requested tx";
         LOGERR << "("<<blockHeight<<", "<<dupID<<", "<<txIndex<<")";
         return false;
      }

      return readStoredTxAtIter(ldbIter, blockHeight, dupID, stx);

   } 

   return true;
}



////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putStoredTxOut( StoredTxOut const & stxo)
{
    
   SCOPED_TIMER("putStoredTx");

   BinaryData ldbKey = stxo.getDBKey(false);
   BinaryData bw = serializeDBValue(stxo, armoryDbType_, dbPruneType_);
   putValue(getDbSelect(HISTORY), DB_PREFIX_TXDATA, ldbKey, bw);
}

void LMDBBlockDatabase::putStoredZcTxOut(StoredTxOut const & stxo, 
   const BinaryData& zcKey)
{
   SCOPED_TIMER("putStoredTx");

   BinaryData bw = serializeDBValue(stxo, armoryDbType_, dbPruneType_);
   putValue(getDbSelect(HISTORY), DB_PREFIX_ZCDATA, zcKey, bw);
}


////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredTxOut(
   StoredTxOut & stxo, const BinaryData& DBkey) const
{
   if (DBkey.getSize() != 8)
   {
      LOGERR << "Tried to get StoredTxOut, but the provided key is not of the "
                 "proper size. Expect size is 8, this key is: " << DBkey.getSize();
      return false;
   }

   if (armoryDbType_ == ARMORY_DB_SUPER)
   {
      BinaryData key = WRITE_UINT8_BE(DB_PREFIX_TXDATA);
      key.append(DBkey);

      LMDBEnv::Transaction tx(dbEnv_[BLKDATA].get(), LMDB::ReadOnly);
      BinaryRefReader brr = getValueReader(BLKDATA, key);
      if (brr.getSize() == 0)
      {
         LOGERR << "BLKDATA DB does not have the requested TxOut";
         return false;
      }

      stxo.blockHeight_ = DBUtils::hgtxToHeight(DBkey.getSliceRef(0, 4));
      stxo.duplicateID_ = DBUtils::hgtxToDupID(DBkey.getSliceRef(0, 4));
      stxo.txIndex_ = READ_UINT16_BE(DBkey.getSliceRef(4, 2));
      stxo.txOutIndex_ = READ_UINT16_BE(DBkey.getSliceRef(6, 2));

      stxo.unserializeDBValue(brr);

      return true;
   }
   else
   {
      {
         //Let's look in history db first. Stxos are fetched mostly to spend coins,
         //so there is a high chance we wont need to pull the stxo from the raw
         //block, since fullnode keeps track of all relevant stxos in the 
         //history db
         LMDBEnv::Transaction tx(dbEnv_[HISTORY].get(), LMDB::ReadOnly);
         BinaryRefReader brr = getValueReader(HISTORY, DB_PREFIX_TXDATA, DBkey);

         if (brr.getSize() > 0)
         {
            stxo.blockHeight_ = DBUtils::hgtxToHeight(DBkey.getSliceRef(0, 4));
            stxo.duplicateID_ = DBUtils::hgtxToDupID(DBkey.getSliceRef(0, 4));
            stxo.txIndex_ = READ_UINT16_BE(DBkey.getSliceRef(4, 2));
            stxo.txOutIndex_ = READ_UINT16_BE(DBkey.getSliceRef(6, 2));

            stxo.unserializeDBValue(brr);
            return true;
         }
      }

      LMDBEnv::Transaction tx(dbEnv_[BLKDATA].get(), LMDB::ReadOnly);

      //again, in Fullnode, need to pull the entire block, unserialize then
      //return the one stxo
      StoredTx stx;
      Tx thisTx = getFullTxCopy(DBkey.getSliceRef(0, 6));
      stx.createFromTx(thisTx, false, true);
      
      uint16_t txOutId = READ_UINT16_BE(DBkey.getSliceRef(6, 2));
      if (txOutId >= stx.stxoMap_.size())
      {
         LOGERR << "BLKDATA DB does not have the requested TxOut";
         return false;
      }

      stxo = stx.stxoMap_[txOutId];

      return true;
   }

   return false;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredTxOut(      
                              StoredTxOut & stxo,
                              uint32_t blockHeight,
                              uint8_t  dupID,
                              uint16_t txIndex,
                              uint16_t txOutIndex) const
{
   SCOPED_TIMER("getStoredTxOut");
   BinaryData blkKey = DBUtils::getBlkDataKeyNoPrefix(
      blockHeight, dupID, txIndex, txOutIndex);
   return getStoredTxOut(stxo, blkKey);
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredTxOut(      
                              StoredTxOut & stxo,
                              uint32_t blockHeight,
                              uint16_t txIndex,
                              uint16_t txOutIndex) const
{
   uint8_t dupID = getValidDupIDForHeight(blockHeight);
   if(dupID == UINT8_MAX)
      LOGERR << "Headers DB has no block at height: " << blockHeight; 

   return getStoredTxOut(stxo, blockHeight, dupID, txIndex, txOutIndex);
}





////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::putStoredUndoData(StoredUndoData const & sud)
{
   LOGERR << "putStoredUndoData not implemented yet!!!";
   return false;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredUndoData(StoredUndoData & sud, uint32_t height)
{
   LOGERR << "getStoredUndoData not implemented yet!!!";
   return false;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredUndoData(StoredUndoData & sud, 
                                       uint32_t         height, 
                                       uint8_t          dup)
{
   LOGERR << "getStoredUndoData not implemented yet!!!";
   return false;

   /*
   BinaryData key = DBUtils.getBlkDataKeyNoPrefix(height, dup); 
   BinaryRefReader brr = getValueReader(BLKDATA, DB_PREFIX_UNDODATA, key);

   if(brr.getSize() == 0)
   {
      LOGERR << " 
   }

   for(uint32_t i=0; i<sud.stxOutsRemovedByBlock_.size(); i++)
   {
      sud.stxOutsRemovedByBlock_[i].blockHeight_ = height;
      sud.stxOutsRemovedByBlock_[i].duplicateID_ = dup;
   }
   */
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredUndoData(StoredUndoData & sud, 
                                       BinaryDataRef    headHash)
{
   SCOPED_TIMER("getStoredUndoData");
   LOGERR << "getStoredUndoData not implemented yet!!!";
   return false;
}


////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::putStoredTxHints(StoredTxHints const & sths)
{
   SCOPED_TIMER("putStoredTxHints");
   if(sths.txHashPrefix_.getSize()==0)
   {
      LOGERR << "STHS does have a set prefix, so cannot be put into DB";
      return false;
   }

   putValue(getDbSelect(TXHINTS), sths.getDBKey(), sths.serializeDBValue());

   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredTxHints(StoredTxHints & sths, 
                                      BinaryDataRef hashPrefix)
{
   if(hashPrefix.getSize() < 4)
   {
      LOGERR << "Cannot get hints without at least 4-byte prefix";
      return false;
   }
   BinaryDataRef prefix4 = hashPrefix.getSliceRef(0,4);
   sths.txHashPrefix_ = prefix4.copy();

   BinaryDataRef bdr;
   bdr = getValueRef(getDbSelect(TXHINTS), DB_PREFIX_TXHINTS, prefix4);

   if(bdr.getSize() > 0)
   {
      sths.unserializeDBValue(bdr);
      return true;
   }
   else
   {
      sths.dbKeyList_.resize(0);
      sths.preferredDBKey_.resize(0);
      return false;
   }
}


////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::putStoredHeadHgtList(StoredHeadHgtList const & hhl)
{
   SCOPED_TIMER("putStoredHeadHgtList");

   if(hhl.height_ == UINT32_MAX)
   {
      LOGERR << "HHL does not have a valid height to be put into DB";
      return false;
   }

   putValue(getDbSelect(HEADERS), hhl.getDBKey(), hhl.serializeDBValue());
   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredHeadHgtList(StoredHeadHgtList & hhl, uint32_t height)
{
   BinaryData ldbKey = WRITE_UINT32_BE(height);
   BinaryDataRef bdr = getValueRef(getDbSelect(HEADERS), DB_PREFIX_HEADHGT, ldbKey);

   hhl.height_ = height;
   if(bdr.getSize() > 0)
   {
      hhl.unserializeDBValue(bdr);
      return true;
   }
   else
   {
      hhl.preferredDup_ = UINT8_MAX;
      hhl.dupAndHashList_.resize(0);
      return false;
   }
}




////////////////////////////////////////////////////////////////////////////////
TxRef LMDBBlockDatabase::getTxRef( BinaryDataRef txHash )
{
   if (armoryDbType_ == ARMORY_DB_SUPER)
   {
      LDBIter ldbIter = getIterator(getDbSelect(BLKDATA));
      if (seekToTxByHash(ldbIter, txHash))
      {
         ldbIter.getKeyReader().advance(1);
         return TxRef(ldbIter.getKeyReader().get_BinaryDataRef(6));
      }
   }
   else
   {
      BinaryData key;
      getStoredTx_byHash(txHash, nullptr, &key);
      return TxRef(key);
   }
      
   return TxRef();
}

////////////////////////////////////////////////////////////////////////////////
TxRef LMDBBlockDatabase::getTxRef(BinaryData hgtx, uint16_t txIndex)
{
   BinaryWriter bw;
   bw.put_BinaryData(hgtx);
   bw.put_uint16_t(txIndex, BIGENDIAN);
   return TxRef(bw.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
TxRef LMDBBlockDatabase::getTxRef( uint32_t hgt, uint8_t  dup, uint16_t txIndex)
{
   BinaryWriter bw;
   bw.put_BinaryData(DBUtils::heightAndDupToHgtx(hgt,dup));
   bw.put_uint16_t(txIndex, BIGENDIAN);
   return TxRef(bw.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::markBlockHeaderValid(BinaryDataRef headHash)
{
   SCOPED_TIMER("markBlockHeaderValid");
   BinaryRefReader brr = getValueReader(HEADERS, DB_PREFIX_HEADHASH, headHash);
   if(brr.getSize()==0)
   {
      LOGERR << "Invalid header hash: " << headHash.copy().copySwapEndian().toHexStr();
      return false;
   }
   brr.advance(HEADER_SIZE);
   BinaryData hgtx   = brr.get_BinaryData(4);
   uint32_t   height = DBUtils::hgtxToHeight(hgtx);
   uint8_t    dup    = DBUtils::hgtxToDupID(hgtx);

   return markBlockHeaderValid(height, dup);
}




////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::markBlockHeaderValid(uint32_t height, uint8_t dup)
{
   SCOPED_TIMER("markBlockHeaderValid");

   StoredHeadHgtList hhl;
   getStoredHeadHgtList(hhl, height);
   if(hhl.preferredDup_ == dup)
      return true;

   bool hasEntry = false;
   for(uint32_t i=0; i<hhl.dupAndHashList_.size(); i++)
      if(hhl.dupAndHashList_[i].first == dup)
         hasEntry = true;
   

   if(hasEntry)
   {
      hhl.setPreferredDupID(dup);
      putStoredHeadHgtList(hhl);
      setValidDupIDForHeight(height, dup);
      return true;
   }   
   else
   {
      LOGERR << "Header was not found header-height list";
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
// This is an inelegant consequence of this DB design -- if a tx 
// appears in two branches, it will be in the DB twice and appear twice
// in the TXHINTS list.  We have chosen to NOT store a "isValid" flag
// with each tx and txout, to avoid duplication of data that might 
// possibly de-synchronize and cause all sorts of problems (just go verify
// the HEADHGT entry).  But to avoid unnecessary lookups, we must make 
// sure that the correct {hgt,dup,txidx} is in the front of the TXHINTS 
// list.  

//Dropped this behavior starting 0.93
bool LMDBBlockDatabase::markTxEntryValid(uint32_t height,
                                      uint8_t  dupID,
                                      uint16_t txIndex)
{
   SCOPED_TIMER("markTxEntryValid");
   BinaryData blkDataKey = DBUtils::getBlkDataKeyNoPrefix(height, dupID, txIndex);
   BinaryRefReader brrTx = getValueReader(BLKDATA, DB_PREFIX_TXDATA, blkDataKey);

   brrTx.advance(2);
   BinaryData key4 = brrTx.get_BinaryData(4); // only need the first four bytes

   BinaryRefReader brrHints = getValueReader(BLKDATA, DB_PREFIX_TXHINTS, key4);
   uint32_t numHints = brrHints.getSize() / 6;
   if(numHints==0)
   {
      LOGERR << "No TXHINTS entry for specified {hgt,dup,txidx}";      
      return false;
   }
   
   // Create a list of refs with the correct tx in front
   list<BinaryDataRef> collectList;
   bool hasEntry = false;
   for(uint8_t i=0; i<numHints; i++)
   {
      BinaryDataRef thisHint = brrHints.get_BinaryDataRef(6);

      if(thisHint != blkDataKey)
         collectList.push_back(thisHint);
      else
      {
         collectList.push_front(thisHint);
         hasEntry = true;
      }
   }
   
   // If this hint didn't exist, we don't need to change anything (besides 
   // triggering an error/warning it didn't exist.
   if(!hasEntry)
   {
      LOGERR << "Tx was not found in the TXHINTS list";
      return false;
   }


   // If there was no entry with this hash, then all existing values will be 
   // written with not-valid.
   BinaryWriter bwOut(6*numHints);
   list<BinaryDataRef>::iterator iter;
   for(iter = collectList.begin(); iter != collectList.end(); iter++)
      bwOut.put_BinaryData(*iter);
   
   putValue(HEADERS, DB_PREFIX_HEADHGT, key4, bwOut.getDataRef());
   return true;
}

   

////////////////////////////////////////////////////////////////////////////////
// This is used only for debugging and testing with small database sizes.
// For intance, the reorg unit test only has a couple blocks, a couple 
// addresses and a dozen transactions.  We can easily predict and construct
// the output of this function or analyze the output by eye.
KVLIST LMDBBlockDatabase::getAllDatabaseEntries(DB_SELECT db)
{
   SCOPED_TIMER("getAllDatabaseEntries");
   
   if(!databasesAreOpen())
      return KVLIST();

   LMDBEnv::Transaction tx;
   beginDBTransaction(&tx, db, LMDB::ReadOnly);

   KVLIST outList;
   outList.reserve(100);

   LDBIter ldbIter = getIterator(db);
   ldbIter.seekToFirst();
   for(ldbIter.seekToFirst(); ldbIter.isValid(); ldbIter.advanceAndRead())
   {
      size_t last = outList.size();
      outList.push_back( pair<BinaryData, BinaryData>() );
      outList[last].first  = ldbIter.getKey();
      outList[last].second = ldbIter.getValue();
   }

   return outList;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::printAllDatabaseEntries(DB_SELECT db)
{
   SCOPED_TIMER("printAllDatabaseEntries");

   cout << "Printing DB entries... (DB=" << db << ")" << endl;
   KVLIST dbList = getAllDatabaseEntries(db);
   if(dbList.size() == 0)
   {
      cout << "   <no entries in db>" << endl;
      return;
   }

   for(uint32_t i=0; i<dbList.size(); i++)
   {
      cout << "   \"" << dbList[i].first.toHexStr() << "\"  ";
      cout << "   \"" << dbList[i].second.toHexStr() << "\"  " << endl;
   }
}

#define PPRINTENTRY(TYPE, IND) \
    TYPE data; \
    data.unserializeDBKey(key); \
    data.unserializeDBValue(val); \
    data.pprintOneLine(indent + IND); 
   

#define PPRINTENTRYDB(TYPE, IND) \
    TYPE data; \
    data.unserializeDBKey(BLKDATA, key); \
    data.unserializeDBValue(BLKDATA, val); \
    data.pprintOneLine(indent + IND); 

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::pprintBlkDataDB(uint32_t indent)
{
   SCOPED_TIMER("pprintBlkDataDB");
   DB_SELECT db = BLKDATA;

   cout << "Pretty-printing BLKDATA DB" << endl;
   KVLIST dbList = getAllDatabaseEntries(db);
   if(dbList.size() == 0)
   {
      cout << "   <no entries in db>" << endl;
      return;
   }

   BinaryData lastSSH = READHEX("00");
   for(uint32_t i=0; i<dbList.size(); i++)
   {
      BinaryData key = dbList[i].first;
      BinaryData val = dbList[i].second;
      if(key.getSize() == 0)
      {
         cout << "\"" << "\"  ";
         cout << "\"" << dbList[i].second.toHexStr() << "\"  " << endl;
      }
      else if(key[0] == DB_PREFIX_DBINFO)
      {
         PPRINTENTRY(StoredDBInfo, 0);
         cout << "-------------------------------------" << endl;
      }
      else if(key[0] == DB_PREFIX_TXDATA)
      {
         if(key.getSize() == 5)      {PPRINTENTRYDB(StoredHeader, 0);}
         else if(key.getSize() == 7) {PPRINTENTRY(StoredTx, 3); }
         else if(key.getSize() == 9) {PPRINTENTRY(StoredTxOut, 6);}
         else
            cout << "INVALID TXDATA KEY: '" << key.toHexStr() << "'" << endl;
      }
      else if(key[0] == DB_PREFIX_SCRIPT) 
      {
         StoredScriptHistory ssh;
         StoredSubHistory subssh;
      
         if(!key.startsWith(lastSSH))
         {
            // New SSH object, base entry
            ssh.unserializeDBKey(key); 
            ssh.unserializeDBValue(val); 
            ssh.pprintFullSSH(indent + 3); 
            lastSSH = key;
         }
         else
         {
            // This is a sub-history for the previous SSH
            subssh.unserializeDBKey(key); 
            subssh.unserializeDBValue(val); 
            subssh.pprintFullSubSSH(indent + 6);
         }
      }
      else
      {
         for(uint32_t j=0; j<indent; j++)
            cout << " ";

         if(key[0] == DB_PREFIX_TXHINTS)
            cout << "TXHINT: ";
         else if(key[0]==DB_PREFIX_UNDODATA)
            cout << "UNDO: ";

         cout << "\"" << dbList[i].first.toHexStr() << "\"  ";
         cout << "\"" << dbList[i].second.toHexStr() << "\"  " << endl;
      }
         
   }
}

////////////////////////////////////////////////////////////////////////////////
map<uint32_t, uint32_t> LMDBBlockDatabase::getSSHSummary(BinaryDataRef scrAddrStr,
   uint32_t endBlock)
{
   SCOPED_TIMER("getSSHSummary");

   map<uint32_t, uint32_t> SSHsummary;

   LDBIter ldbIter = getIterator(getDbSelect(HISTORY));

   if (!ldbIter.seekToExact(DB_PREFIX_SCRIPT, scrAddrStr))
      return SSHsummary;

   StoredScriptHistory ssh;
   BinaryDataRef sshKey = ldbIter.getKeyRef();
   ssh.unserializeDBKey(sshKey, true);
   ssh.unserializeDBValue(ldbIter.getValueReader());

   if (ssh.totalTxioCount_ == 0)
      return SSHsummary;

   uint32_t sz = sshKey.getSize();
   BinaryData scrAddr(sshKey.getSliceRef(1, sz - 1));
   uint32_t scrAddrSize = scrAddr.getSize();
   (void)scrAddrSize;

   if (!ldbIter.advanceAndRead(DB_PREFIX_SCRIPT))
   {
      LOGERR << "No sub-SSH entries after the SSH";
      return SSHsummary;
   }

   // Now start iterating over the sub histories
   do
   {
      uint32_t sz = ldbIter.getKeyRef().getSize();
      BinaryDataRef keyNoPrefix = ldbIter.getKeyRef().getSliceRef(1, sz - 1);
      if (!keyNoPrefix.startsWith(ssh.uniqueKey_))
         break;

      pair<BinaryData, StoredSubHistory> keyValPair;
      keyValPair.first = keyNoPrefix.getSliceCopy(sz - 5, 4);
      keyValPair.second.unserializeDBKey(ldbIter.getKeyRef());

      //iter is at the right ssh, make sure hgtX <= endBlock
      if (keyValPair.second.height_ > endBlock)
         break;

      keyValPair.second.getSummary(ldbIter.getValueReader());
      SSHsummary[keyValPair.second.height_] = keyValPair.second.txioCount_;
   } while (ldbIter.advanceAndRead(DB_PREFIX_SCRIPT));

   return SSHsummary;
}

uint32_t LMDBBlockDatabase::getStxoCountForTx(const BinaryData & dbKey6) const
{
   if (dbKey6.getSize() != 6)
   {
      LOGERR << "wrong key size";
      return UINT32_MAX;
   }

   LMDBEnv::Transaction tx;
   beginDBTransaction(&tx, getDbSelect(HISTORY), LMDB::ReadOnly);

   if (armoryDbType_ == ARMORY_DB_SUPER)
   {
      if (!dbKey6.startsWith(ZCprefix_))
      {
         StoredTx stx;
         uint32_t hgt;
         uint8_t  dup;
         uint16_t txi;

         BinaryRefReader brrKey(dbKey6);
         DBUtils::readBlkDataKeyNoPrefix(brrKey, hgt, dup, txi);

         if(!getStoredTx(stx, hgt, dup, txi, false))
         {
            LOGERR << "no Tx data at key";
            return UINT32_MAX;
         }

         return stx.stxoMap_.size();
      }
      else
      {
         StoredTx stx;
         if (!getStoredZcTx(stx, dbKey6))
         {
            LOGERR << "no Tx data at key";
            return UINT32_MAX;
         }

         return stx.stxoMap_.size();
      }
   }
   else
   {
      if (!dbKey6.startsWith(ZCprefix_))
      {
         BinaryRefReader brr = getValueRef(getDbSelect(HISTORY), DB_PREFIX_TXDATA, dbKey6);
         if (brr.getSize() == 0)
         {
            LOGERR << "no Tx data at key";
            return UINT32_MAX;
         }

         return brr.get_uint32_t();
      }
      else
      {
         StoredTx stx;
         if (!getStoredZcTx(stx, dbKey6))
         {
            LOGERR << "no Tx data at key";
            return UINT32_MAX;
         }

         return stx.stxoMap_.size();
      }
   }

   return UINT32_MAX;
}

uint8_t LMDBBlockDatabase::putRawBlockData(BinaryRefReader& brr, 
   function<const BlockHeader& (const BinaryData&)> getBH)
{
   if (armoryDbType_ == ARMORY_DB_SUPER)
   {
      LOGERR << "This method is not meant for supernode";
      throw runtime_error("dbType incompatible with putRawBlockData");
   }

   brr.resetPosition();
   StoredHeader sbh;

   BlockHeader bhUnser(brr);
   const BlockHeader & bh = getBH(bhUnser.getThisHash());
   sbh.blockHeight_ = bh.getBlockHeight();
   sbh.duplicateID_ = bh.getDuplicateID();
   sbh.isMainBranch_ = bh.isMainBranch();
   sbh.blockAppliedToDB_ = false;
   sbh.numBytes_ = bh.getBlockSize();

   //put raw block with header data
   {
      LMDBEnv::Transaction tx(dbEnv_[BLKDATA].get(), LMDB::ReadWrite);
      BinaryData dbKey(sbh.getDBKey(true));
      putValue(BLKDATA, BinaryDataRef(dbKey), brr.getRawRef());
   }

   //update SDBI in HISTORY DB
   {
      LMDBEnv::Transaction tx(dbEnv_[HISTORY].get(), LMDB::ReadWrite);
      if (sbh.isMainBranch_)
      {
         StoredDBInfo sdbiB;
         getStoredDBInfo(HISTORY, sdbiB);
         if (sbh.blockHeight_ > sdbiB.topBlkHgt_)
         {
            sdbiB.topBlkHgt_ = sbh.blockHeight_;
            sdbiB.topBlkHash_ = bh.getThisHash();
            putStoredDBInfo(HISTORY, sdbiB);
         }
      }
   }

   return sbh.duplicateID_;
}

/*
////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getUndoDataForTx( Tx const & tx,
                                           list<TxOut> &    txOutsRemoved,
                                           list<OutPoint> & outpointsAdded)
{
   // For ARMORY_DB_FULL we don't need undo data yet.
}

////////////////////////////////////////////////////////////////////////////////
BinaryData LMDBBlockDatabase::getUndoDataForBlock( list<TxOut> &    txOutsRemoved,
                                              list<OutPoint> & outpointsAdded)
{
   // Maybe don't clear them, so that we can accumulate multi-block data, if 
   // we have some reason to do that.  Let the caller clear the lists
   //txOutsRemoved.clear();
   //outpointsAdded.clear();

   // For ARMORY_DB_FULL we don't need undo data yet.
   for(uint32_t i=0; i<numTx_; i++)
      getUndoDataForTx(block.txList[i], txOutsRemoved, outpointsAdded);
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::purgeOldUndoData(uint32_t earlierThanHeight)
{
   // For ARMORY_DB_FULL we don't need undo data yet.
}
*/

   
////////////////////////////////////////////////////////////////////////////////
//  Not sure that this is possible...
/*
bool LMDBBlockDatabase::updateHeaderHeight(BinaryDataRef headHash, 
                                            uint32_t height, uint8_t dup)
{
   BinaryDataRef headVal = getValueRef(HEADERS, headHash);
   if(headVal.isNull())
   {
      LOGERR << " Attempted to update a non-existent header!";
      return false;
   }
      
   BinaryWriter bw(HEADER_SIZE + 4);
   bw.put_BinaryData(headVal.getPtr(), HEADER_SIZE);
   bw.put_BinaryData(heightAndDupToHgtx(height, dup));

   putValue(HEADERS, headHash, bw.getDataRef());
   return true;
}  
*/

// kate: indent-width 3; replace-tabs on;

