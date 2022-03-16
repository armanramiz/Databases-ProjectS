/**
 * @author See Contributors.txt for code contributors and overview of BadgerDB.
 *
 * @section LICENSE
 * Copyright (c) 2012 Database Group, Computer Sciences Department, University of Wisconsin-Madison.
 */
#include <string>
#include "btree.h"
#include "filescan.h"
#include "exceptions/bad_index_info_exception.h"
#include "exceptions/bad_opcodes_exception.h"
#include "exceptions/bad_scanrange_exception.h"
#include "exceptions/no_such_key_found_exception.h"
#include "exceptions/scan_not_initialized_exception.h"
#include "exceptions/index_scan_completed_exception.h"
#include "exceptions/file_not_found_exception.h"
#include "exceptions/end_of_file_exception.h"

//#define DEBUG

namespace badgerdb
{
	// -----------------------------------------------------------------------------
	// BTreeIndex::BTreeIndex -- Constructor
	// -----------------------------------------------------------------------------
	BTreeIndex::BTreeIndex(const std::string &relationName,
						   std::string &outIndexName,
						   BufMgr *bufMgrIn,
						   const int attrByteOffset,
						   const Datatype attrType)
	{
		// Initialize private fields
		bufMgr = bufMgrIn;
		attributeType = attrType;
		this->attrByteOffset = attrByteOffset;
		
		// Find target file name
		std::ostringstream idxStr;
		idxStr << relationName << "." << attrByteOffset;
		std::string indexName = idxStr.str(); // name of the index file
		outIndexName = indexName;

		// We will keep the root page in the buffer pool during the entirety of our program
		Page* cachedRoot;

		// We won't keep the header page in the buffer pool. We update it at the end.
		Page * headerPage;

		// Check to see if a file exists with name indexName...
		try {
			file =  new BlobFile(indexName, false);
			std::cout << "Opened existing index file\n";
			
			// Load header page
			bufMgr->readPage(file, headerPageNum, headerPage);
			IndexMetaInfo* header = (IndexMetaInfo*)headerPage;
			

			// Assuming meta info in file doesn't conflit with actuals
			rootPageNum=header->rootPageNo;
			leafOccupancy=header->leafOccupancy;
			nodeOccupancy=header->nodeOccupancy;
			
			// unpin (updated at the end)
			bufMgr->unPinPage(file, headerPageNum, true);

			// read the root page
			bufMgr->readPage(file, rootPageNum, cachedRoot);

			//printNode(rootPageNum, cachedRoot, false);		
			std::cout << "Leaf num: "<<leafOccupancy<<"\n";
			std::cout << "internal num: "<<nodeOccupancy<<"\n";
			
		} catch (const FileNotFoundException &e) {
			std::cout << "Created new index file\n";
			file =  new BlobFile(indexName, true);
			
			// Load header page
			bufMgr->allocPage(file, headerPageNum, headerPage);
			IndexMetaInfo* header = (IndexMetaInfo*)headerPage;

			// initialize meta info in header
			*header = IndexMetaInfo();
			
			// save actuals
			for (int i=0; i < 20 && relationName[i] != 0; i++) {
				header->relationName[i] = relationName[i];
			}
			header->attrByteOffset = attrByteOffset;
			header->attrType = attributeType;
			
			// unpin (updated at the end)
			bufMgr->unPinPage(file, headerPageNum, true);

			// Create new root page
			bufMgr->allocPage(file, rootPageNum, cachedRoot);

			// Initial root to empty leaf node
			LeafNodeInt* rootNode = (LeafNodeInt*)(cachedRoot);
			*rootNode = LeafNodeInt();

			// Scan through and insert data
			FileScan fscan(relationName, bufMgr);
			try {
				RecordId scanRid;
				while (1) {
					fscan.scanNext(scanRid);
					//Assuming RECORD.i is our key, lets extract the key, which we know is INTEGER and whose byte offset is also know inside the record. 
					std::string recordStr = fscan.getRecord();
					const char* record = recordStr.c_str();
					const void* key = (void *)(record + attrByteOffset);
					insertEntry(key, scanRid);
				}
			}
			catch (const EndOfFileException &e){}
		}
	}

	// -----------------------------------------------------------------------------
	// BTreeIndex::~BTreeIndex -- destructor
	// -----------------------------------------------------------------------------
	BTreeIndex::~BTreeIndex()
	{
		// End any currently running scans
		if(scanExecuting) endScan();

		// Write data to header page
		Page* headerPage;
		bufMgr->readPage(file, headerPageNum, headerPage);
		IndexMetaInfo* header = (IndexMetaInfo*) headerPage;
		header->rootPageNo = rootPageNum;
		header->leafOccupancy = leafOccupancy;
		header->nodeOccupancy = nodeOccupancy;
		bufMgr->unPinPage(file,headerPageNum,true);

		//Unpin root (we have been keeping it pinned)
		bufMgr->unPinPage(file,rootPageNum,true);

		// Delete
		bufMgr->flushFile(file);
		delete file; 
	}

	// -----------------------------------------------------------------------------
	// BTreeIndex::insertEntry
	// -----------------------------------------------------------------------------
	void BTreeIndex::insertEntry(const void* key, const RecordId rid)
	{
		
		// No matter what, we add something
		leafOccupancy++;

		// Key we are looking for
		int currKey = *(int*)(key);

		// Load root page from buffer pool
		Page* currPage;
		
		// Id of current loaded page
		PageId currId = rootPageNum;

		// current depth in tree (root is 0)
		int currDepth = 0;
		
		// Traverse the tree until we get to a leaf
		findLeaf(currId, currPage, currKey, currDepth, !nodeOccupancy);

		// Now we are at a leaf node
		LeafNodeInt* leafNode = (LeafNodeInt*) currPage;

		// Check if the new entry will fit
		if(leafNode->numValidKeys < INTARRAYLEAFSIZE){
			/* The leaf is not full: add the element */
			
			// find the insertion index
			int insertAt = findIndex(currId,currPage,currKey,true);

			// make room for the new element
			shiftData(currId, currPage, insertAt, true);

			// insert new element
			leafNode->keyArray[insertAt]=currKey;
			leafNode->ridArray[insertAt]=rid;
			
			// Update count
			leafNode->numValidKeys++;
			
			// Unpin current (dirty) leaf page
			bufMgr->unPinPage(file, currId, true);
			
			return;
		}
		
		
		/* The leaf is full: divide the leaf node and copy middle element up */

		// create page to copy half of the data into
		Page* secondPage;
		PageId secondPageId;
		bufMgr->allocPage(file,secondPageId,secondPage);

		// Create node struct in new page
		LeafNodeInt* secondLeafNode = (LeafNodeInt*) secondPage;
		*secondLeafNode = LeafNodeInt();
		secondLeafNode->numValidKeys=INTARRAYLEAFSIZE/2;

		// the side that we insert on
		bool insertLeft = findIndex(currId, currPage, currKey, true) <= INTARRAYLEAFSIZE/2;
		
		// Number of keys to move to second node
		int copyNum = (INTARRAYLEAFSIZE + insertLeft) / 2;

		// Copy over last copyNum keys and rids into second node
		for (int i = 0; i < copyNum; i++) {
			secondLeafNode->keyArray[i] = leafNode->keyArray[INTARRAYLEAFSIZE - copyNum + i];
			secondLeafNode->ridArray[i] = leafNode->ridArray[INTARRAYLEAFSIZE - copyNum + i];
		}
		secondLeafNode->numValidKeys = copyNum;
		leafNode->numValidKeys = INTARRAYLEAFSIZE - copyNum;
		
		
		if (insertLeft) {
			// find where to insert at in left node
			int insertAt = findIndex(currId, currPage, currKey, true);
			shiftData(currId, currPage, insertAt, true);
			
			// insert new element
			leafNode->keyArray[insertAt] = currKey;
			leafNode->ridArray[insertAt] = rid; 
			leafNode->numValidKeys++;
		} else {
			// find where to insert at in left node
			int insertAt = findIndex(secondPageId, secondPage, currKey, true);
			shiftData(secondPageId, secondPage, insertAt, true);
			
			// insert new element
			secondLeafNode->keyArray[insertAt] = currKey;
			secondLeafNode->ridArray[insertAt] = rid;
			secondLeafNode->numValidKeys++;
		}
		/* Now we copy up middle key */

		// Update family locations
		secondLeafNode->rightSibPageNo = leafNode->rightSibPageNo;
		leafNode->rightSibPageNo=secondPageId;

		// key to pass up
		currKey = secondLeafNode->keyArray[0];

		// page location passed up
		PageId prevId = secondPageId;

		// unpin (dirty) leaf pages
		bufMgr->unPinPage(file,secondPageId, true);
		bufMgr->unPinPage(file,currId, true);

		// Move up one level
		currId = findParent(currId);
		currDepth--;

		// While we are below the root node, split parent and push up middle parent key if needed
		while (currDepth >= 0) {
			// Load new node (parent of old)
			bufMgr->readPage(file, currId, currPage);
			NonLeafNodeInt* currNode = (NonLeafNodeInt*) currPage;

			// No matter what, we are adding a key to an internal node
			nodeOccupancy++;
			
			// If the parent is not full, insert the key and finish
			if (currNode->numValidKeys < INTARRAYNONLEAFSIZE) {
				
				// find the insertion index
				int insertAt = findIndex(currId, currPage, currKey, false);
				
				// make room for the new element
				shiftData(currId, currPage, insertAt, false);
				
				// insert new element
				currNode->keyArray[insertAt] = currKey;
				currNode->pageNoArray[insertAt+1] = prevId;
				
				// Update key count
				currNode->numValidKeys++;

				// Unpin current (dirty) node page
				bufMgr->unPinPage(file, currId, true);

				return;
			}
			// Else we split the parent, push up the middle parent key, and add the new value to the correct sibling

			// create page to copy half of the data into
			bufMgr->allocPage(file, secondPageId, secondPage);
			
			// Create node struct in new page
			NonLeafNodeInt* secondNode = (NonLeafNodeInt*) secondPage;
			*secondNode = NonLeafNodeInt();

			// the side that we insert on
			bool insertLeft = findIndex(currId, currPage, currKey, false) < INTARRAYNONLEAFSIZE/2;
			
			// Number of keys to move to second node
			int copyNum = (INTARRAYNONLEAFSIZE - !insertLeft) / 2;

			// Copy over last copyNum keys and copyNum+1 pages into second node
			for (int i = 0; i < copyNum; i++) {
				secondNode->keyArray[i] = currNode->keyArray[INTARRAYNONLEAFSIZE - copyNum + i];
				secondNode->pageNoArray[i] = currNode->pageNoArray[INTARRAYNONLEAFSIZE - copyNum + i];
			}
			secondNode->pageNoArray[copyNum] = currNode->pageNoArray[INTARRAYNONLEAFSIZE];
			secondNode->numValidKeys = copyNum;
			currNode->numValidKeys = INTARRAYNONLEAFSIZE - copyNum;
			if (insertLeft) {
				// If we are inserting at the end of the left node, push up current key and insert previous page at end
				if (currKey > currNode->keyArray[currNode->numValidKeys-1]) {
					currNode->pageNoArray[currNode->numValidKeys] = prevId; // add page
					// push up currKey again
				}
				else{
					// get key to push up
					currKey = currNode->keyArray[currNode->numValidKeys--];
					
					int insertAt = findIndex(currId, currPage, currKey, false);
					shiftData(currId, currPage, insertAt, false);
					
					// insert new element
					currNode->keyArray[insertAt] = currKey;
					currNode->pageNoArray[insertAt+1] = prevId;
					currNode->numValidKeys++;
				}
			} else {
				// find where to insert at in left node
				int insertAt = findIndex(secondPageId, secondPage, currKey, false);
				shiftData(secondPageId, secondPage, insertAt, false);
				
				// insert new element
				secondNode->keyArray[insertAt] = currKey;
				secondNode->pageNoArray[insertAt+1] = prevId;
				secondNode->numValidKeys++;

				// get key to push up
				currKey = currNode->keyArray[--currNode->numValidKeys];
			}
			
			// Update counts and relationships
			secondNode->level = currNode->level; // same as sibling

			// We don't need these anymore
			bufMgr->unPinPage(file, secondPageId, true);
			bufMgr->unPinPage(file, currId, true);

			currId = findParent(currId);
			prevId = secondPageId;
			currDepth--;
		}
		
		/*
		 * If we get to here, then we must have spilt 
		 * the old root and we need to make a new one.
		 */
		
		// Make new root node
		Page* newRootPage;
		bufMgr->allocPage(file, rootPageNum, newRootPage);
		NonLeafNodeInt* newRootNode = (NonLeafNodeInt*) newRootPage;
		*newRootNode = NonLeafNodeInt();

		// Set root values
		newRootNode->keyArray[0] = currKey;
		newRootNode->numValidKeys = 1;
		newRootNode->level = !nodeOccupancy; 
		nodeOccupancy++; 

		/* Set children */ 

		// (parent of root is root so currId will be left child)
		newRootNode->pageNoArray[0] = currId; // left child
		newRootNode->pageNoArray[1] = secondPageId; // right child

		// Load children
		bufMgr->readPage(file, currId, currPage);
		bufMgr->readPage(file, secondPageId, secondPage);
		
		/* Unpinning */ 
		
		// This is the old root and so it get's unpinned twice
		bufMgr->unPinPage(file, currId, true); 
		bufMgr->unPinPage(file, currId, true);

		// unpin right child
		bufMgr->unPinPage(file, secondPageId, true);	

		// Note: We leave the new root pinned
	}

	/**
	 * @brief Traverse the tree until we get to a leaf
	 * 
	 * @param pageNo starting page num (gets overwritten with target)
	 * @param page overwrites passed page with target page
	 * @param key key to search for
	 * @param currDepth current depth in tree (root is 0)
	 * @param isLeaf are we at a leaf (does nothing in this case)
	 */
	void BTreeIndex::findLeaf(PageId& pageNo, Page*& page, int key, int& currDepth, bool isLeaf){
		bufMgr->readPage(file, pageNo, page);
		while (!isLeaf) { 
			// By assumption, we are at an internal node
			NonLeafNodeInt* currNode = (NonLeafNodeInt*) page;

			// Will the next node be a leaf?
			isLeaf=currNode->level;

			// Determine where to traverse to next
			int insertAt=0;
			while (insertAt < currNode->numValidKeys 
					&& key >= currNode->keyArray[insertAt]) insertAt++;

			// used for unpinning
			PageId old = pageNo;

			// Update to new page id
			pageNo = currNode->pageNoArray[insertAt];

			// Unpin old page (not modified)
			bufMgr->unPinPage(file, old, false);
			
			// Load the next page into the buffer pool
			bufMgr->readPage(file, pageNo, page);

			// We have moved one level down the tree
			currDepth++;
		}
	}

	/**
	 * @brief Traverse the tree from the root until we get to a leaf
	 * 
	 * @param pageNo starting page num (gets overwritten with target)
	 * @param page overwrites passed page with target page
	 * @param key key to search for
	 * @param currDepth current depth in tree (root is 0)
	 * @param isLeaf are we at a leaf (does nothing in this case)
	 */
	void BTreeIndex::findLeafFromRoot(PageId& pageNo, Page*& page, int key){
		int dummy = 0;
		findLeaf(pageNo = rootPageNum,page,key,dummy,!nodeOccupancy);
	}

	/**
	 * @brief Traverse the tree until we get to a 
	 * node which has a the target page id
	 * 
	 * PARENT OF ROOT IS ROOT
	 * 
	 * @param target target page num 
	 * 
	 * @returns the parent node's id
	 */
	PageId BTreeIndex::findParent(PageId target){
		if(target==rootPageNum) return target; //root case
		
		// Current node
		Page* page;
		PageId id = rootPageNum; // Start at root

		// Find a key to look for
		bufMgr->readPage(file, target, page);
		int key = ((NonLeafNodeInt*)page)->keyArray[0];
		bufMgr->unPinPage(file, target, false);

		// Read root
		bufMgr->readPage(file, id, page);
		while (true) { 
			// By assumption, we are at an internal node
			NonLeafNodeInt* currNode = (NonLeafNodeInt*) page;

			// Determine where to traverse to next
			int index=0;
			while (index < currNode->numValidKeys 
					&& key >= currNode->keyArray[index]) index++;
			
			// Check if we have found the target page
			if(currNode->pageNoArray[index] == target) break;

			// used for unpinning
			PageId old = id;

			// Update to new page id
			id = currNode->pageNoArray[index];

			// Unpin old page (not modified)
			bufMgr->unPinPage(file, old, false);
			
			// Load the next page into the buffer pool
			bufMgr->readPage(file, id, page);
		}

		// Unpin current
		bufMgr->unPinPage(file, id, false);

		// return parent id
		return id;
	}

	/**
	 * @brief traverse the array until we find the desired location 
	 * 
	 * @param pageNo page id of node
	 * @param page page of node
	 * @param key key of new entry
	 * @param isLeaf is the node a leaf?
	 * @return int position of location
	 */
	int BTreeIndex::findIndex(PageId& pageNo, Page*& page, int key, bool isLeaf){
		int index=0;	
		
		if (isLeaf) {
			// cast to node type
			LeafNodeInt* node = (LeafNodeInt*) page;

			//increment index
			while (index < node->numValidKeys && key >= node->keyArray[index]) index++;
		}
		else {
			// cast to node type
			NonLeafNodeInt* node = (NonLeafNodeInt*) page;

			//increment index
			while (index < node->numValidKeys && key >= node->keyArray[index]) index++;
		}

		return index;
	}

	/**
	 * @brief traverse the array shifting elements 1 to the right 
	 * until we reach the desired location 
	 * 
	 * @param pageNo page id of node
	 * @param page page of node
	 * @param index position of
	 * @param isLeaf is the node a leaf
	 */
	void BTreeIndex::shiftData(PageId& pageNo, Page*& page, int index, bool isLeaf){
		if(isLeaf){
			// cast to node type
			LeafNodeInt* node = (LeafNodeInt*) page;

			// start at the end of the array
			for(int i = node->numValidKeys; i>index; i--){
				// Move current entry right by one
				node->ridArray[i]=node->ridArray[i-1];
				node->keyArray[i]=node->keyArray[i-1];
			}
		}
		else{
			// cast to node type
			NonLeafNodeInt* node = (NonLeafNodeInt*) page;
			
			// start at the end of the array
			for(int i = node->numValidKeys; i>index; i--){
				// Move current entry right by one
				node->pageNoArray[i+1]=node->pageNoArray[i];
				node->keyArray[i]=node->keyArray[i-1];				
			}
		}
	}

	/**
	 * @brief prints out the keys and page ids in the provided node
	 * 
	 * @param pageNo id of page to print
	 * @param page page to print
	 * @param isLeaf is this internal or leaf?
	 */
	void BTreeIndex::printNode(PageId pageNo, Page* page, bool isLeaf) {
		std::cout << "Node "<< pageNo << " = [";
		if(isLeaf){
			LeafNodeInt* currNode = (LeafNodeInt* ) page;
			for(int i = 0; i < currNode->numValidKeys; i++){
				std::cout << currNode->keyArray[i] << " / ";
			}
		}
		else{
			NonLeafNodeInt* currNode = (NonLeafNodeInt*) page;
			for(int i = 0; i < currNode->numValidKeys; i++){
				std::cout << currNode->pageNoArray[i] << " | ";
				std::cout << currNode->keyArray[i] << " | ";
			}
			std::cout << currNode->pageNoArray[currNode->numValidKeys];
		}
		std::cout << "]\n";
		
	}

 	/**
	 * Begin a filtered scan of the index.  For instance, if the method is called 
	 * using ("a",GT,"d",LTE) then we should seek all entries with a value 
	 * greater than "a" and less than or equal to "d".
	 * If another scan is already executing, that needs to be ended here.
	 * Set up all the variables for scan. Start from root to find out the leaf page that contains the first RecordID
	 * that satisfies the scan parameters. Keep that page pinned in the buffer pool.
	 * @param lowVal	Low value of range, pointer to integer / double / char string
	 * @param lowOp		Low operator (GT/GTE)
	 * @param highVal	High value of range, pointer to integer / double / char string
	 * @param highOp	High operator (LT/LTE)
	 * @throws  BadOpcodesException If lowOp and highOp do not contain one of their their expected values 
	 * @throws  BadScanrangeException If lowVal > highval
	 * @throws  NoSuchKeyFoundException If there is no key in the B+ tree that satisfies the scan criteria.
	**/
	void BTreeIndex::startScan(const void* lowValParm,
							   const Operator lowOpParm,
							   const void* highValParm,
							   const Operator highOpParm)
	{
		//If lowOp and highOp do not contain one of their their expected values
		if(lowOpParm != GTE && lowOpParm != GT ) throw BadOpcodesException(); 
		if(highOpParm != LTE && highOpParm != LT ) throw BadOpcodesException(); 

		// Check if tree is empty
		if(!leafOccupancy) throw NoSuchKeyFoundException();	

		//End the scan if its already executing
		if(scanExecuting) endScan();

		//cast these parameters to int for comparison
		lowValInt = *(int *)lowValParm;
		highValInt = *(int *)highValParm;
		
	    //If lowVal > highval
		if(lowValInt > highValInt) throw BadScanrangeException();
		
		scanExecuting = true;
		lowOp = lowOpParm;
		highOp = highOpParm;

		// Sets page data and id to starting leaf page
		findLeafFromRoot(currentPageNum, currentPageData, lowValInt);

		// cast to leaf node
		LeafNodeInt* currentNode = (LeafNodeInt*) currentPageData;
		
		//check if the key satisfies the range
		while(true){
			nextEntry=0;
			while(nextEntry < currentNode->numValidKeys){
				int key = currentNode->keyArray[nextEntry];
				if((highOp == LT && key >= highValInt) 
						|| (highOp == LTE && key >highValInt)){
					// end scan will upin for us
					throw NoSuchKeyFoundException();	
				}
				if((lowOp==GTE && lowValInt <= key) 
						|| (lowOp==GT && lowValInt < key))
					return;
				nextEntry++;
			}
			
			// save next page id
			PageId nextPageId = currentNode->rightSibPageNo;

			// Check if this is the last leaf
			if(!nextPageId) throw NoSuchKeyFoundException();

			// Unpin old page
			bufMgr->unPinPage(file, currentPageNum, false);

			//change currently scaning page to the next page
			currentPageNum=nextPageId;
			bufMgr->readPage(file, currentPageNum, currentPageData);
		}
	}
	

  /**
	 * Fetch the record id of the next index entry that matches the scan.
	 * Return the next record from current page being scanned. If current page has been scanned to its entirety, move on to the right sibling of current page, if any exists, to start scanning that page. Make sure to unpin any pages that are no longer required.
     * @param outRid	RecordId of next record found that satisfies the scan criteria returned in this
	 * @throws ScanNotInitializedException If no scan has been initialized.
	 * @throws IndexScanCompletedException If no more records, satisfying the scan criteria, are left to be scanned.
	**/
	void BTreeIndex::scanNext(RecordId &outRid)
	{
		// throw this if no scan is initialized
		if(!scanExecuting){
			throw ScanNotInitializedException();
		}
		
		// fetch the record id for the index that satisfies the scan 
		LeafNodeInt* currentNode = (LeafNodeInt*) currentPageData;

		// move to the next pageif needed
		if(nextEntry >= currentNode->numValidKeys ){ 

			// check if we are at the end of the tree
			if (!currentNode->rightSibPageNo) throw IndexScanCompletedException(); 
			
			//change currently scaning page to the next page
			PageId nextPageId =currentNode->rightSibPageNo;
			bufMgr->unPinPage(file, currentPageNum, false);
			currentPageNum=nextPageId;
			bufMgr->readPage(file, currentPageNum, currentPageData);
			nextEntry = 0;
			currentNode = (LeafNodeInt*) currentPageData;
		}
		
		// set return value
		outRid = currentNode ->ridArray[nextEntry]; 

		// Key of record
		int keyValue = currentNode ->keyArray[nextEntry];
		
		// key value is out of range, or reaches the upper boundary
		if(keyValue > highValInt 
				|| (keyValue == highValInt && highOp == LT)) 
			throw IndexScanCompletedException(); // end scan handles unpinning

		// increase next entry
		nextEntry++;
	}


	/**
  	 *@brief This method terminates the current scan and unpins all the pages that have been pinned for the purpose of the scan.
     *@throws ScanNotInitializedException when called before a successful startScan call.
     */
	void BTreeIndex::endScan()
	{
		if(!scanExecuting) {
			throw ScanNotInitializedException(); 
		}
		scanExecuting = false;
		bufMgr->unPinPage(file, currentPageNum, false);
		
	}

}
/// AIDENS CODE HOARDING PILE
//int i=0;
//while (header->relationName[i] = relationName[i++]);

/// SARAH'S CODE HOARDING PILE
// // Add new element to correct sibiling
		// if (currKey < secondLeafNode->keyArray[0]) {
		// 	// Insert into the left leaf node
		// 	int insertAt = findIndex(currId, currPage, currKey, true);
		// 	shiftData(currId, currPage, insertAt, true);
		// 	leafNode->keyArray[insertAt] = currKey;
		// 	leafNode->ridArray[insertAt+1] = rid;
		// 	leafNode->numValidKeys++;
		// } else {
		// 	// Insert into the right leaf node
		// 	int insertAt = findIndex(secondPageId, secondPage, currKey, true);
		// 	shiftData(secondPageId, secondPage, insertAt, true);
		// 	secondLeafNode->keyArray[insertAt] = currKey;
		// 	secondLeafNode->ridArray[insertAt+1] = rid;
		// 	secondLeafNode->numValidKeys++;
		// }