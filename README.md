What began as a throwaway toy project quickly turned into my main focus and obsession for my resume, and I proudly present my first proper C++ software, a BitTorrent client :) Tired of all the weather scraping/CRUD/E-commerce websites, I finally built something I could actually use in my daily life.

I have lifted some code and ideas from my old project, and added new code and optimisations wherever I felt necessary, and spent hours, days, weeks debugging the absolute worst class of async, memory-related, lifetime, concurrency bugs, but it finally clicked, and it was well worth the trouble!

Features:
-> A custom built BEncode parser with minimal copying
-> Fully asynchronous HTTPS/UDP trackers (I plan to add HTTP in the future) with decent error handling and retries
-> Follows the BitTorrent spec and respects reannounce timers
-> Peer connection pool supporting both IPv4 & IPv6 peers
-> Asynchronous peer networking, but no uploads yet
-> Centralised piece picker 
-> Low memory usage 
-> File writer thread to keep network i/o separate from disc i/o
-> Minimalistic web UI
-> supports multiple torrents
-> performs well on bad networks, I got decent speeds of 6-7 MB/s on a mobile hotspot

Standouts:
-> BEncode parser is copy-free
-> The entire project has just one lock, that too in a file writer thread 
-> Piece concurrency on several hundred peers was a nightmare to get right, but it finally works
-> exclusively uses coroutines-based Boost.ASIO and asio-strands for concurrency

Libraries used:
Boost.ASIO, Boost.Beast, Boost.Endian, Boost.URL, OpenSSL/SHA1

Why did I make this?
C++ is hard, async networking is hard, concurrency, multithreading is hard. I like hard stuff.
I wanted to get good at low-level systems programming.

TODOs:
Endgame mode
HTTP trackers
Fast resume feature
Persistence across shutdowns