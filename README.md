# NoSQL Database in C++

A project to build a persistent, reliable NoSQL database from scratch using C++. 

## 🗺️ Project Roadmap

This project is broken down into 14 progressive modules. Here is the current development progress:

- [x] **Module 1** — What databases actually do
- [x] **Module 2** — Key-value storage fundamentals
- [x] **Module 3** — Disk vs RAM and why databases are hard
- [x] **Module 4** — Basic key-value store in memory
- [x] **Module 5** — Persistent storage on disk
- [x] **Module 6** — Write Ahead Logging (WAL)
- [x] **Module 7** — MemTable design
- [x] **Module 8** — SSTable file format
- [ ] **Module 9** — Searching SSTables
- [ ] **Module 10** — LSM Tree architecture
- [ ] **Module 11** — Compaction process
- [ ] **Module 12** — Bloom Filters
- [ ] **Module 13** — Background compaction threads
- [ ] **Module 14** — Performance improvements

---

## 📍 Current Status

**Current phase:** Transitioning from **Module 8** to **Module 9**

The fundamental in-memory (`memtable.h`) and on-disk file formats (`sstable.h`) have been designed and laid out. The immediate next step is to implement the search logic required to query the SSTables (Module 9), which will pave the way for a full LSM Tree architecture (Module 10).
