
# LSML C Parser

C Implementation Goals
---
- Be portable
    - Only require standard C libraries
    - Don't require dynamic allocation
    - Handle UTF-8 text encoding for best compatability
- Be safe
    - Test to verify behavior
- Be performant
    - Maximize data contiguity in memory
    - Benchmarks to compare performance
- Be resilient
    - Improper syntax is handled gracefully
        - Retain info from parsing when possible, even when some parts of the file are invalid

Basic Usage
---
```c
TODO
```