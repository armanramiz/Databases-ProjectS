# B+ Tree Design Report
### Aiden Dally, Sarah Fahlberg, Arman Ramiz

## Design Choices
### Additions to nodes
- numValidKeys: Indicates the number of valid keys in a given node. We added this additional field to both leaf and non-leaf nodes so that we know exactly how many valid keys and pages/rids there are in that given node. This was especially important in the leaf nodes where there are no reserved values for key and rid to indicate the end of a leaf node. Although we could have used a page value of 0 in non-leaf nodes to indicate the end of the node, we decided to use numValidKeys for consistency of our code and because the additional integer takes up little space.
- INTARRAYLEAFSIZE and INTARRAYNONLEAFSIZE: we updated how these values were calculated with the additional field in the leaf and non-leaf structs.

### Buffer pool
- We decided to always keep the root node in the buffer pool to reduce the number I/Os.
- Our program requires a minimum buffer pool size of 4, including the ridArray page pinned by the caller, in order to store the root (always pinned), and in the case of splitting a node, the current node, and the node it was split into.

### Helper Methods
- We chose to implement private helper methods for operations that were performed mulitple times throughout the code including:
    - findLeafFromRoot: traverse tree from root to the leaf with a given key.
    - findLeaf: traverse the tree to find the leaf containing a given key and helper method for findLeafFromRoot.
    - findIndex: find the index of keyArray where a given key should be located in a given leaf or non-leaf node.
    - shiftData: for a given leaf or non-leaf node, shift a portion of the key array and rid or pageArray to make space for a new key-rid/page pair.
    - findParent: finds the non-leaf parent node pageId of a given node. If the node passed is the root, the root pageId is returned.
    - printNode: prints the keys and pageIds or recordIds stored in a given node. Only used for testing and debugging. 


## Aditional Tests
- We also tested test1, test2, and test3 with relationSize = 1,000,000 to test recursion of copying/pushing up keys and splitting of root nodes, but we left this test out of main.cpp because it takes 30 minutes to run.
- All of our tests passed with a buffer pool with only 4 frames.
- BadOpcodesException Test 3 & BadOpcodesException Test 4: two additional test cases to check for all possible badOpcodeExceptions
- Additional checkPassFail tests: We added a number of addition checkPassFail tests including the following
    - If lowVal == relationSize, number of results should be 0 since outside range of the tree
    - If lowVal is near the end of the relation (relationSize-10) and highVal goes past the end of the relation, then we should find 10 results
- test4: Check that we correctly save and load an index file. To do this, we create a relation, create an index on the relation, deconstruct the index, reconstruct the index, and run the usual set of tests on the reconstructed relation. In reconstructing the relation, we also reduce the size of the buffer pool to 2 which should only allow enough space for the header and the root node upon intialization and not enough space to reconstruct the index from scratch. We commented this test out temporarily since this test causes a seg fault (after it passes and returns) that prevents other tests from running.
- test5: Check that our tests return 0 when there are no elements in the tree. 
