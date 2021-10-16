[![Codacy Badge](https://app.codacy.com/project/badge/Grade/f698efaf304c42718295d0742cf9fcc1)](https://www.codacy.com/gh/reficul0/OrderBook/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=reficul0/OrderBook&amp;utm_campaign=Badge_Grade)
[![CodeFactor](https://www.codefactor.io/repository/github/reficul0/orderbook/badge)](https://www.codefactor.io/repository/github/reficul0/orderbook)
<br>
![language](https://img.shields.io/badge/language-c++-blue.svg)
![c++](https://img.shields.io/badge/std-c++14-blue.svg)

## Overview

OrderBook is simplified [order book](https://en.wikipedia.org/wiki/Order_book)(no way).
Functionality:
* Post of orders(only good-till-cancel)
* Cancellation of order by its id.
* Getting data of order by its id.
* Orders merging.
* Getting of market data snapshot. Order data are aggregated and sorted in sacending order.

## Requirements

* [Required] - [Boost (1.64 or newer)](http://www.boost.org/).
* [Required] - pthread(Linux only).

### Windows build

```shell
- git clone git@github.com:reficul0/AlgorithmPractice.git
- mkdir build && cd build
- cmake -A %platform% -G "Visual Studio 15 2017" ..
- cmake --build .
```
### Linux build

```bash
$ git clone git@github.com:reficul0/AlgorithmPractice.git
$ mkdir build && cd build
$ cmake ..
$ cmake --build .
```

## System Compatibility

OS           | Compiler      | Status
------------ | ------------- | -------------
Windows      | msvc15        | :white_check_mark: Working
Linux        | gcc           | :white_check_mark: Working
