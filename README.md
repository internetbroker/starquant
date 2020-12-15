Welcome to StarQuant
==================

<p align="left">
   <img src ="https://img.shields.io/badge/language-c%2B%2B%7Cpython-orange.svg"/>
   <img src ="https://img.shields.io/badge/c%2B%2B-%3E11-blue.svg"/>
    <img src ="https://img.shields.io/badge/python-3.7-blue.svg" />
    <img src ="https://img.shields.io/badge/platform-linux%7Cwindows-brightgreen.svg"/>
    <img src ="https://img.shields.io/badge/build-passing-green.svg" />
    <img src ="https://img.shields.io/badge/license-MIT-blue.svg"/>
</p>

[English](README_eng.md) 



**StarQuant**(中文名：易数交易系统)是一个轻量的、面向个人( 普通）用户的综合量化交易回测系统，目前主要用于期货期权程序化交易（CTP接口，在实盘测试中），也支持股票交易功能（中泰xtp，宽睿oes接口，待测试和完善）。

当前进展：完成1.0版本beta版本，在ctp实盘测试中：

1）对于流动性好，盘口大的品种非大笔交易下tick级回测与实盘的成交时间和价位一致；

2）无人工干预阿里云/腾讯云 7*24h连续工作，api柜台自动重连断开，行情自动记录，策略自动初始化、开始和停止；



## 功能特色

* 采用多进程和多线程模式，行情交易接口，策略执行，gui界面均为独立进程，支持多种进程通信模式，对于普通应用，可以采用消息队列，延迟在百微秒量级（实测在30-100微秒之间,吞吐量在10k/s左右，对期货足够），对于普通用户延迟足够低 ，对于股票L2逐笔数据的高吞吐和低延迟可以采用共享内存通信模式，实现10微秒级的低延迟，同时落地所有交易数据；
* 支持实盘程序化交易，支持多个API接口（如期货的ctp穿透式, 易盛tap，股票的中泰xtp，宽睿oes），支持多个期货、证券公司账号同时登录， 多个策略组独立运行，可设置触发条件实现自动登录，登出，重置等，可以实现7*24小时工作;具有风险控制模块，可以设置流控、总量控制等参数;　支持本地条件单（止损），一键全撤，平仓等自定义指令类型；
* 支持策略的实时动态管理（增加，删除，编辑参数，启动，停止，重新载入等，类似vnpy的ctastrategy 模块），可以管理多个策略进程；
* 支持c++或python语言的策略回测和实盘交易，回测框架中的事件驱动模式是基于vnpy改写，支持动态重新加载策略，策略在回测和交易程序中形式一样，无需重写代码, 同时python策略的数据格式、函数形式与vnpy兼容，方便转换、迁移和测试；
* 支持bar和tick不同时间尺度回测；具有lite和pro以及批量回测模式，其中lite模式是单标的模式，在批量模式下可以多进程同时批量回测不同策略不同合约，方便将不同策略组合查看综合收益，pro模式支持任意多个标的同时回测，正则表达式确定标的名称范围，批量模式下支持多进程同时回测，方便将标的分开来实现并行快速回测；
* 回测结果可以查看总体指标（收益率，夏普率，最大回撤等），收益曲线，所有成交明细，每日明细，在k线上显示对应买卖点标记，显示tick数据，方便分析；可以筛选指定条件的成交，回测结果可以导出为csv和相应图片；支持自定义指标显示分析；
* 支持回测参数优化筛选功能，可以多进程参数优化和遗传算法优化;
* 支持实盘行情订阅和数据存储（tick,bar），消息队列通信模式下类似vnpy的data recorder, 单独进程模式，同时也具有vnpy的csv loader的功能，支持bar和tick数据导入；共享内存通信模式下内存数据直接异步保存在硬盘，有专门的导出查看脚本用于数据处理和分析；支持从多个数据源下载数据（RQData，Tushare，JoinQuant）；
* 支持基于实盘行情数据的模拟交易（Paper brokerage，简单的撮合），方便实盘前的测试；
* 采用Qt可视化界面作为前端，方便管理，监控和操作，相关监控信息均有记录，可以导出为csv文件；可以查看实时k线数据；可以查看合约基本信息；可以选择性显示/隐藏指定视图控件单元，调整视图布局；也支持命令行（cli,tui）形式监控；
*  支持微信实时推送和接收信息（itchat 或Server酱等方式)
*  Linux，windows跨平台支持；

注：目前开源的代码具备了从回测到实盘交易的所有基础功能，也展示了系统的原型和框架，方便在此基础上二次开发和定制，定制化的代码(pro版)目前尚未开源，后期如果star数量较多时（1k以上）可以考虑开源。

## PRO版特点
* 回测支持任意多个标的；支持不同策略组合的多进程并行批量回测；回测结果可以按指定条件筛选；
* mongodb数据读入写入基于pymongo的批量模式，相对普通版速度提高2-10倍；
* 回测支持数据读取游标模式，节省内存消耗，支持批量组合不同回测结果；
* 回测支持设置保存，回测结果读取，以及脚本模式；
* 回测支持分布式计算和优化;
* 支持因子统计分析、相关分析，批量因子生成和回归分析，方便快速因子筛选；
* 支持基于预测模型的向量化快速回测（日内和隔夜），测试集、训练集单独评估；
* 支持自定义各种指标显示，包括订单流；
* 支持tick级别标的组合价格（价差）分析和指标分析, 支持tick数据分析和指标显示
* 实盘支持共享内存通信方式，延迟在10微秒级别，支持10万/秒的吞吐量；


## 系统架构

系统主框架基于c++实现，采用c-s架构，回测具有事件驱动模式和向量化两种模式，采用模块化松耦合设计，服务端的行情，交易、数据记录为单独线程，服务端与gui界面、策略之间的进程通信采用消息队列方式（nanomsg）或共享内存（基于功夫易筋经），行情数据可以通过相关端口以消息形式转发到策略进程，策略下单操作也通过相关端口将指令转发到服务端，然后调用相关柜台api，行情api支持CTP，TAP等，数据可以记录到本地（csv文件或Mongodb数据库），策略可以采用python或c++实现。GUI是基于PyQt5，支持手动交易，策略交易，委托持仓账号等信息查看。





## 开发环境
本系统在开发过程中参考了已有的开源软件vnpy,kungfu等。
主要开发环境：

(1) Manjaro（arch，Linux内核4.14)，python 3.7.2，gcc 9.1

(2) Ubuntu18.04, 

(3) Windows 10 ,vs2015

第三方库：
(1)需要系统安装:boost,libmongoc-1.0

(2)需要系统安装或者自行编译cppsrc/ 下的 nanomsg 1.0, log4cplus 2.04,yaml-cpp,fmt5.3, ta-lib,

(3)python依赖psutil，pyyaml, pyqt, qdarkstyle等包。

## 运行

首先需要编译完成cppsrc下的库，需要事先安装boost，nanomsg以及CTP,TAP等柜台api的动态链接库。
编译过程和原项目类似，使用 [CMake](https://cmake.org) 进行编译：

```
$ cd cppsrc
$ mkdir build
$ cd build
$ cmake ..
$ make
```
编译完成后将cppsrc/build/StarQuant下的可执行文件apiserver.exe拷贝到主目录运行即可启动服务端，
gui界面执行gui.py即可启动，运行strategy.py可启动单独的策略进程，运行recorder.py可启动行情记录进程。

## 编写约定
-------------------
c++参考google 的c++ guide, cpplint检查；

python 采用flake8检查，autopep8格式化；

变量命名：类名采用驼峰式，单词首字母大写，不包含_字符；类成员变量字母一般小写，后加_，如data_,全局变量加g_修饰，函数一般大小写混合形式。



## 使用说明
-------
品种符号约定：
  采用全名的形式：交易所 类型 商品名 合约，如SHFE F RB 1905
  对于ctp，程序内部api会转换为对应的简写形式，rb1905，
行情、交易、策略之间消息传递的格式：消息头|消息内容
 消息头:目的地|源地址|类型，类型有：

 消息内容：对应类型的数据


## Demo
-----------
![ ](demos/demopro.gif "pro版回测演示1")
![ ](demos/live3.png  "普通版实盘交易模式展示")
![ ](demos/live5.png  "视图布局选择")
![ ](demos/livepro.png  "定制版实盘交易展示")
![ ](demos/btpro4.png  "定制版回测订单流分析展示")
![ ](demos/btpro1.png  "定制版批量回测")
![ ](demos/btpro2.png  "定制版回测结果筛选，k线、指标分析")
![ ](demos/ctlpro.png  "定制版c++命令行界面")
## TODO

本系统完成了版本1.0的beta版本，正在实盘测试中，同时在完善c++回测、向量化回测，cli界面。




