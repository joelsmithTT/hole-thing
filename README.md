# Hole Thing
This is a lightweight interface to
[tt-kmd](https://github.com/tenstorrent/tt-kmd) and a place to store code
snippets related to its development.  You probably want
[tt-umd](https://github.com/tenstorrent/tt-umd) or
[Luwen](https://github.com/tenstorrent/luwen), not this.

## Goals
* Support Blackhole, Wormhole chips
* Fast build
* Minimal dependencies

## Dependencies
* g++-12 or better
* cmake

## Build
```
mkdir build
cd build
cmake ..
make
```