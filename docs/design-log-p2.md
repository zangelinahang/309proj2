
# Design Log — Project 2

## Project Overview

In this project, a LLM harness is used to simulate a conversation between a user and a model. The main goal is to implement the conversation memory and streamed sentinel detection. The remaining model and harness components are provided. The project focuses on dynamic memory, growable arrays, the Rule of Five, and streamed text processing.

The implementation consisted of a `Conversation` container and a `SentinelScanner`. `Conversation` was implemented as a dynamically allocated growable array without using `std::vector`. Its capacity was doubled when needed, and the Rule of Five was used to provide safe copy, move, and destruction behavior.

`SentinelScanner` was implemented to detect `<|end_conversation|>` across arbitrary streamed chunks while storing at most `sentinel length - 1` pending characters. The provided `Harness` and `ModelClient` classes were then used to test both components in the complete conversation loop.

## Growable Conversation Array

The `Conversation` class was implemented as a growable array of `Message` objects (`std::vector` was avoided as required). Three values are tracked: `data_`, `size_`, and `capacity_`.

- `data_` points to the allocated array.
- `size_` stores the current number of messages.
- `capacity_` stores the total number of available array locations.

A growth factor of 2 was used. When `size_ == capacity_`, a new array with twice the previous capacity is allocated. The original messages are moved into the new array and the old array is deleted.

The capacity therefore grows as:

```text
1, 2, 4, 8, 16, 32, ...
```

This gives `append()` an amortized time complexity of $O(1)$. A single resize may require moving $k$ existing messages, so one append is not always constant time. However, resizing does not occur on every append.

Over $n$ insertions, the total number of moved elements is approximately:

```text
1 + 2 + 4 + 8 + ... < 2n
```

This total is $O(n)$. Dividing the total work across $n$ append operations gives an amortized cost of $O(1)$ per append.

## Rule of Five and Memory Safety

The Rule of Five was implemented to handle the object's memory safely. This includes the destructor, copy constructor, copy assignment operator, move constructor, and move assignment operator.

The destructor uses `delete[]` to release the owned `Message` array. The copy constructor creates a new array and copies each stored message into separate memory. This prevents two `Conversation` objects from owning the same pointer. The copy assignment operator also creates independent storage before replacing the current contents.

Move operations transfer ownership instead of copying each message. The destination object receives the original `data_`, `size_`, and `capacity_`. The source object is then reset to:

```cpp
data_ = nullptr;
size_ = 0;
capacity_ = 0;
```

This leaves the moved-from object valid and safe to destroy.

The tests verify that the copied conversations use different pointer addresses and that moved conversations transfer the original pointer. The project was also compiled and tested with AddressSanitizer enabled. No memory errors or leaks were observed during testing.

## Bounded Sentinel Buffer

`SentinelScanner` checks streamed model output for the sentinel:

```text
<|end_conversation|>
```

The sentinel may be divided across multiple chunks, so some trailing characters must be temporarily stored in `pending_`.

The maximum required pending size is:

```text
sentinel_.size() - 1
```

Let the sentinel length be $S$. If `pending_` contained $S$ matching characters, then the complete sentinel would already have been detected. Therefore, only the final $S - 1$ characters can remain unresolved between chunks.

When a chunk is received, `pending_` and the new chunk are searched together. If the sentinel is not found, all text except the last $S - 1$ characters is safe to release. The remaining suffix becomes the new `pending_`.

Therefore:

```text
pending_.size() <= S - 1
```

after every call to `feed()`.

This keeps the scanner memory bounded even when processing a very large stream. The stress test feeds approximately 4 MB of data one character at a time and verifies that the pending buffer never grows beyond this limit.

## What I Would Change Differently

In hindsight, I would make the sentinel search logic simpler to follow by separating it into smaller helper functions. The current implementation combines `pending_` with each new chunk and then searches the combined text for the sentinel - which works correctly, but the logic inside `feed()` could be easier to read and test.

In addition, the prefix and suffix checking into a separate helper function or sentinel characters can be tracked to see how many characters have matched so far instead of storing as much temporary text.

These changes would not change the process of the program too much, but they would make the scanner easier to understand. For this project, the current implementation was kept because it directly satisfies the requirements. 
