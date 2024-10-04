# Non-Blocking Queue Implementations

This project contains multiple implementations of non-blocking queues in C. 

## Queue Implementations

1. **SimpleQueue**
    Single-Linked List Queue with Two Mutexes

This is one of the simpler queue implementations. A separate mutex for producers and for consumers allows better parallelization of operations.

The structure of SimpleQueue consists of:

A singly-linked list of nodes, where each node contains:
An atomic pointer next to the next node in the list,
A value of type Value;
A pointer head to the first node in the list, along with a mutex to secure access to it;
A pointer tail to the last node in the list, along with a mutex to secure access to it.

2.  **RingsQueue**
    RingsQueue: Circular Buffer List Queue

This is a combination of a SimpleQueue and a queue implemented on a circular buffer, merging the unlimited size of the former with the performance of the latter (singly linked lists are relatively slow due to constant memory allocations).

The structure of RingsQueue consists of:

A singly-linked list of nodes, where each node contains:
An atomic pointer next to the next node in the list,
A circular buffer in the form of an array of RING_SIZE values of type Value,
An atomic counter push_idx tracking the number of pushes performed in this node,
An atomic counter pop_idx tracking the number of pops performed in this node;
A pointer head to the first node in the list;
A pointer tail to the last node in the list (head and tail can point to the same node);
A mutex pop_mtx to lock the entire pop operation;
A mutex push_mtx to lock the entire push operation.

3. **LLQueue**
   LLQueue: Lock-Free Singly-Linked List Queue

This is one of the simplest implementations of a lock-free queue.

The structure of LLQueue consists of:

A singly-linked list of nodes, where each node contains:
An atomic pointer next to the next node in the list,
A value of type Value, which is set to EMPTY_VALUE if the value has already been retrieved from the node;
An atomic pointer head to the first node in the list;
An atomic pointer tail to the last node in the list;
A HazardPointer structure (see below).
Push should operate in a loop, attempting the following steps:

Read the pointer to the last node in the queue (the thread performing the push operation).
Update the next pointer of the last node to point to a new node containing our element.
3a. If successful, update the pointer to the last node in the queue to our new node and exit the function.
3b. If unsuccessful (another thread has already extended the list), start the process again, ensuring that the tail pointer has been updated.
Pop should operate in a loop, attempting the following steps:

Read the pointer to the first node in the queue.
Read the value from this node and replace it with EMPTY_VALUE.
 If the read value is different from EMPTY_VALUE, update the pointer to the first node (if necessary) and return the result.
 If the read value is EMPTY_VALUE, check whether the queue is empty.
If so, return EMPTY_VALUE. If not, retry the process, ensuring that the head pointer has been updated.

4. **BLQueue**
     BLQueue: A Queue Combining Singly-Linked List with a Buffer

This is one of the simpler yet highly efficient implementations of a queue. The idea behind this queue is to merge a singly-linked list structure with a queue implemented using an array with atomic push and pop indices (although the number of operations would be limited by the size of the array). We combine the advantages of both by making a list of arrays; only when the array is filled do we move to a new node. The array here is not a circular buffer—each field is filled at most once (the variant with circular buffers would be much more complex).

BLQueue consists of:

A singly-linked list of nodes, where each node contains:
An atomic pointer next to the next node in the list,
A buffer with BUFFER_SIZE atomic values of type Value,
An atomic index push_idx indicating the next place in the buffer to be filled by the push operation (incremented with each push attempt),
An atomic index pop_idx indicating the next place in the buffer to be emptied by the pop operation (incremented with each pop attempt);
An atomic pointer head to the first node in the list;
An atomic pointer tail to the last node in the list;
A HazardPointer structure (see below).
The queue initially contains one node with an empty buffer. The buffer's elements initially hold the value EMPTY_VALUE. Pop operations replace the retrieved or empty values with TAKEN_VALUE (allowing the pop to sometimes waste elements in the array). The constant BUFFER_SIZE is defined in BLQueue.h and is set to 1024, though it can be changed to a smaller power of two larger than 2 for testing.

Push operates in a loop attempting to perform the following steps:

Read the pointer to the last node in the queue.
Retrieve and increment the index from this node to get the next place in the buffer for the push operation (no other thread will try to push into this spot).
If the index is less than the buffer size, attempt to insert the element into this buffer slot.
If another thread has already changed this spot (for example, by setting it to TAKEN_VALUE during a pop operation), start the process over.
If the insertion succeeds, exit the function.
If the index is greater than or equal to the buffer size, the buffer is full, and a new node will need to be created, or we need to move to the next node. First, check if the next node has already been created.
If so, ensure that the pointer to the last node in the queue has been updated and try again from the beginning.
If not, create a new node with our element in its buffer. Attempt to insert the pointer to the new node as the next node.
If this fails (another thread has already extended the list), remove our node and start over.
If successful, update the pointer to the last node in the queue to our new node.
Pop operates in a loop attempting to perform the following steps:

Read the pointer to the first node in the queue.
Retrieve and increment the index from this node to get the next place in the buffer for the pop operation (no other thread will try to pop from this spot).
If the index is less than the buffer size, read the element from this buffer slot and replace it with TAKEN_VALUE.
If EMPTY_VALUE is retrieved, retry the process.
If another element is retrieved, exit the function.
If the index is greater than or equal to the buffer size, the buffer is completely emptied, and the next node needs to be accessed. First, check if the next node has already been created.
If not, the queue is empty, and the function exits.
If so, ensure that the pointer to the first node in the queue has been updated and retry the process from the beginning.


5. **Hazard Pointer**
   Hazard Pointer

Hazard Pointer is a technique used to handle the problem of safely freeing memory in data structures shared by multiple threads, and to deal with the ABA problem.
The idea is that each thread can reserve one address for a node (one for each instance of the queue), which it needs to protect from being deleted (or swapped in an ABA scenario) during operations like push, pop, or is_empty.
When a thread wants to free a node (via free()), instead of freeing it immediately, it adds its address to its own set of retired addresses. Later, it periodically scans this set, freeing the addresses that are no longer reserved by any thread.



