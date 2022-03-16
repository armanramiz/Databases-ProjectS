/**
 * @author See Contributors.txt for code contributors and overview of BadgerDB.
 *
 * @section LICENSE
 * Copyright (c) 2012 Database Group, Computer Sciences Department, University
 * of Wisconsin-Madison.
 */

#include "buffer.h"

#include <iostream>
#include <memory>

#include "exceptions/bad_buffer_exception.h"
#include "exceptions/buffer_exceeded_exception.h"
#include "exceptions/hash_not_found_exception.h"
#include "exceptions/page_not_pinned_exception.h"
#include "exceptions/page_pinned_exception.h"

namespace badgerdb
{

  constexpr int HASHTABLE_SZ(int bufs) { return ((int)(bufs * 1.2) & -2) + 1; }

  //----------------------------------------
  // Constructor of the class BufMgr
  //----------------------------------------

  BufMgr::BufMgr(std::uint32_t bufs)
      : numBufs(bufs),
        hashTable(HASHTABLE_SZ(bufs)),
        bufDescTable(bufs),
        bufPool(bufs)
  {
    for (FrameId i = 0; i < bufs; i++)
    {
      bufDescTable[i].frameNo = i;
      bufDescTable[i].valid = false;
    }

    clockHand = bufs - 1;
  }

  /**
 * @brief 
 * 
 */
  void BufMgr::advanceClock()
  {
    clockHand++;
    clockHand %= numBufs;
  }

  void BufMgr::allocBuf(FrameId &frame)
  {
    FrameId start = clockHand;
    do {
      advanceClock();
      // if not valid, dispose of page and return that frame
      if (!bufDescTable[clockHand].valid)
      {
        frame = clockHand;
        return;
      }
      // if ref bit is set, clear it, start counting over, and advance
      if (bufDescTable[clockHand].refbit)
      {
        bufDescTable[clockHand].refbit = 0;
        start = clockHand;
        continue;
      }
      // If frame is pinned, advance
      if (bufDescTable[clockHand].pinCnt != 0)
      {
        continue;
      }
      // If frame is dirty, then we flush
      if (bufDescTable[clockHand].dirty)
      {
        flushFile(bufDescTable[clockHand].file);
      }

      frame = clockHand;
      return;
    }
    while (clockHand != start);
    throw BufferExceededException(); //TODO: add parameters
  }

  void BufMgr::readPage(File &file, const PageId pageNo, Page *&page)
  {
    FrameId frameNo;
    try
    {
      // We already have the page in the buffer
      hashTable.lookup(file, pageNo, frameNo);

      // Now we should have set the frameNo
      bufDescTable[frameNo].refbit = 1;
      bufDescTable[frameNo].pinCnt++;
    }
    catch (HashNotFoundException const&)
    {
      // We don't have it and need to allocate some space for it
      allocBuf(frameNo);
      *page = file.readPage(pageNo);
      hashTable.insert(file, pageNo, frameNo);
      bufDescTable[frameNo].Set(file, pageNo);
      bufStats.diskreads++;
    }

    bufStats.accesses++;
  }

  void BufMgr::unPinPage(File &file, const PageId pageNo, const bool dirty)
  {
    // Try to find the frame number that corresponse to (file, pageNo)
    FrameId frameNo;
    try
    {
      hashTable.lookup(file, pageNo, frameNo);
    }
    catch (HashNotFoundException const&)
    {
      return;
    }
    // throw exception if pin count is already 0 and we can't decrement pin count
    if (bufDescTable[frameNo].pinCnt == 0)
    {
      throw PageNotPinnedException(file.filename(), pageNo, frameNo);
    }
    // Update pincount and dirtybit
    bufDescTable[frameNo].pinCnt--;
    if (dirty)
    {
      bufDescTable[frameNo].dirty = true;
    }
  }

  void BufMgr::allocPage(File &file, PageId &pageNo, Page *&page)
  { 
    Page page_temp = file.allocatePage();

    *page = page_temp;
    
    
    FrameId frameNo;
    allocBuf(frameNo); 

    pageNo = page_temp.page_number();
    bufDescTable[frameNo].Set(file, pageNo);

    
    hashTable.insert(file, pageNo, frameNo);
    
    
    
  }

  //a) If the page is dirty, call file.writePage() to flush the page to disk and then set the dirt bit for the page to false
  //b) remove the page from the hashtable (whether the page is clean or dirty
  //c) invoke the Clear() method of BufDesc for the page frame.

  //throws PagePinnedException if some page of the file is pinned. Throws BadBuffer-
  //Exception if an invalid page belonging to the file is encountered.
  /*
 * Writes out all dirty pages of the file to disk.
   * All the frames assigned to the file need to be unpinned from buffer pool
   * before this function can be successfully called. Otherwise Error returned.
   */
  void BufMgr::flushFile(File &file)
  {

    for (FrameId i = 0; i < numBufs; i++)
    {
      if (bufDescTable[i].file == file)
      {
        if (bufDescTable[i].pinCnt != 0)
        {
          throw PagePinnedException(file.filename(), bufDescTable[i].pageNo, bufDescTable[i].frameNo);
        }
        if (!bufDescTable[i].valid)
        {
          throw BadBufferException(i, bufDescTable[i].dirty, bufDescTable[i].valid, bufDescTable[i].refbit);
        }
        if (bufDescTable[i].dirty)
        {
          file.writePage(bufPool[i]);
          bufStats.diskwrites++;
        }
        bufDescTable[i].clear();
      }
    }
  }

  void BufMgr::disposePage(File &file, const PageId PageNo)
  {
    FrameId frameNo;
    try
    {
      //Check if the page to be deleted is allocated a frame in the buffer pool
      hashTable.lookup(file, PageNo, frameNo);
      //Initialize frame for its next usage
      bufDescTable[frameNo].clear();
      //remove the page from the hashtable
      hashTable.remove(file, PageNo);
    }
    catch (HashNotFoundException const&)
    {
      //Do nothing?
    }
    if (bufDescTable[PageNo].pinCnt != 0)
    { // Page should not be pinned...
      throw PagePinnedException(file.filename(), PageNo, bufDescTable[PageNo].frameNo);
    }
    //delete the page from the file
    file.deletePage(PageNo);
  }

  void BufMgr::printSelf(void)
  {
    int validFrames = 0;

    for (FrameId i = 0; i < numBufs; i++)
    {
      std::cout << "FrameNo:" << i << " ";
      bufDescTable[i].Print();

      if (bufDescTable[i].valid)
        validFrames++;
    }

    std::cout << "Total Number of Valid Frames:" << validFrames << "\n";
  }

} // namespace badgerdb
