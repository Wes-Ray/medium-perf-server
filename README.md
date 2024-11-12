## High Performance HTTP Server

Technical requirements:
* Compiled (C/C++/Rust/Go/etc)
* Single-threaded
* Event-based
    * Use callbacks for events (e.g. onHttpRequest)
* Use epoll
* Avoid copying data, especially the data transferred via the HTTP body
* Do not use a library

Implementation Requirements:
* Support multiple clients concurrently
* Support the /upload and /download endpoint
    * Target upload/download speeds over 500MB/s
    * Upload to or download from an arbitrary file (ignore security implications)
    * For upload/download testing use: /dev/null and /dev/zero (respectively, with a max file size of 100MB)

Example HTTP Request:
```
GET /upload HTTP/1.1
Host: localhost:8080
User-Agent: curl/7.81.0
Accept: */*

```
