# NoSQL Database in C++

A project to build a persistent, reliable NoSQL database from scratch using C++. 

## Weekly Implementation Timeline

### Week 1 — Foundations
**Build:**
- In-memory key-value store
- `PUT key value`
- `GET key`
- `DELETE key`

### Week 2 — Disk Storage
**Implement:**
- Append-only storage file
- Serialization
- Record reading

*Result: The DB can now persist data.*

### Week 3 — Write Ahead Logging
**Add:**
- WAL file
- Crash recovery

*Result: The DB is now durable.*

### Week 4 — MemTable
**Add:**
- Sorted in-memory structure
- Flush when full

*Result: Writes become fast.*

### Week 5 — SSTables
**Implement:**
- Sorted disk files
- Binary search for lookups

*Result: Reads are efficient.*

### Week 6 — Compaction
**Implement:**
- Merge SSTables.

*Result: The system behaves like a real LSM database.*

---

## Detailed Implementation Roadmap

This project is broken down into core database components. Here is the current development progress:

- [x] **Component 1** — In-Memory Key-Value Store
- [x] **Component 2** — Persistent Storage on Disk
- [x] **Component 3** — Write Ahead Logging (WAL)
- [x] **Component 4** — MemTable Design
- [x] **Component 5** — SSTable File Format
- [ ] **Component 6** — SSTable Search & Lookups
- [ ] **Component 7** — Full LSM Tree Architecture
- [ ] **Component 8** — Compaction Process
- [ ] **Component 9** — Bloom Filters
- [ ] **Component 10** — Background Compaction Threads

---

## Current Status

**Current phase:** Week 5 / Transitioning from **Component 5** to **Component 6**

The fundamental in-memory (`memtable.h`) and on-disk file formats (`sstable.h`) have been designed and laid out. The immediate next step is to implement the search logic required to query the SSTables (Component 6), which will pave the way for a full LSM Tree architecture (Component 7).
