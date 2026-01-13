# CTorrent-v2

What began as a throwaway toy project quickly turned into my main focus and obsession for my resume.  
This is my first proper **C++ BitTorrent client**, built from scratch.

After building one too many weather scrapers, CRUD apps, and e-commerce demos, I wanted to create something **low-level, performance-critical, and actually useful** in my daily life.

I reused some ideas from an older prototype, rewrote large parts of it, and spent weeks debugging the hardest class of bugs there is: **async, memory-lifetime, and concurrency issues**.  
Eventually it all clicked, and it was absolutely worth it.

---

## Features

- Custom **BEncode parser** with minimal copying
- Fully asynchronous **HTTPS and UDP trackers** (HTTP planned)
- Follows the BitTorrent specification and respects re-announce timers
- Peer connection pool supporting **IPv4 and IPv6**
- Fully asynchronous peer networking - downloads + uploads
- Centralized piece picker
- Low memory usage
- Dedicated file writer thread to keep **disk I/O separate from network I/O**
- Minimalistic web UI
- Supports multiple torrents
- Performs well on unreliable networks  
  *(reached a peak speed of ~27 MB/s on a mobile hotspot)*

---

## Standout Design Decisions

- **Copy-free BEncode parser**
- The entire project uses **only one mutex**, confined to the file writer thread
- Piece selection and concurrency across hundreds of peers was painful to get right — but it works
- Uses **coroutine-based Boost.Asio** with **asio strands** for concurrency instead of callbacks

---

## Libraries Used

- Boost.Asio  
- Boost.Beast  
- Boost.Endian  
- Boost.URL 
- Boost.JSON 
- OpenSSL (SHA-1)

---

## Why Did I Build This?

C++ is hard.  
Async networking is hard.  
Concurrency and multithreading are hard.

I like hard problems.

This project was an excuse to go deep into **low-level systems programming**, networking, async design, and performance, and to build something real instead of another demo app.

---

## TODO

- ~~Endgame mode~~ Done
- HTTP trackers
- Fast resume
- Persistence across shutdowns
- ~~Upload support~~ Done