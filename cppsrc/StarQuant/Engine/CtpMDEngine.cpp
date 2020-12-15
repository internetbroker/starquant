﻿/*****************************************************************************
 * Copyright [2019] 
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *****************************************************************************/

#include <Engine/CtpMDEngine.h>
#include <APIs/Ctp/ThostFtdcMdApi.h>
#include <Common/util.h>
#include <Common/logger.h>
#include <Common/datastruct.h>
#include <Common/config.h>
#include <Data/datamanager.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fmt/format.h>

#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <vector>
#include <sstream>
#include <limits>
#include <mutex>
#include <algorithm>
#include <boost/locale.hpp>
#include <boost/algorithm/string.hpp>




using namespace std;

namespace StarQuant {
// extern std::atomic<bool> gShutdown;

CtpMDEngine ::CtpMDEngine()
    : loginReqId_(0)
    , reqId_(0)
    , apiinited_(false)
    , inconnectaction_(false)
    , autoconnect_(false)
    , timercount_(0)
    , msgqMode_(0) {
    name_ = "CTP.MD";
    init();
}

CtpMDEngine::~CtpMDEngine() {
    if (estate_ != STOP)
        stop();
    releaseapi();
}

void CtpMDEngine::releaseapi() {
    if (api_ != nullptr) {
        // this->api_->Join();
        // make sure ctpapi is idle
        msleep(500);
        this->api_->RegisterSpi(nullptr);
        if (apiinited_)
            this->api_->Release();  // api must init() or will segfault
        this->api_ = nullptr;
    }
}

void CtpMDEngine::reset() {
    disconnect();
    releaseapi();
    CConfig::instance().readConfig();
    init();
    LOG_DEBUG(logger, name_ << " reset");
}

void CtpMDEngine::init() {
    // if (IEngine::msgq_send_ == nullptr){
    // 	lock_guard<mutex> g(IEngine::sendlock_);
    // 	IEngine::msgq_send_ = std::make_unique<CMsgqNanomsg>(MSGQ_PROTOCOL::PUB, CConfig::instance().SERVERPUB_URL);
    // }
    reqId_ = 0;
    lastsubs_.clear();
    msgqMode_ = 0;
    if (logger == nullptr) {
        logger = SQLogger::getLogger("MDEngine.CTP");
    }

    if (messenger_ == nullptr) {
        messenger_ = std::make_unique<CMsgqEMessenger>(name_, CConfig::instance().SERVERSUB_URL);
        msleep(100);
    }
    ctpacc_ = CConfig::instance()._gatewaymap[name_];
    string path = CConfig::instance().logDir() + "/ctp/md";
    boost::filesystem::path dir(path.c_str());
    boost::filesystem::create_directory(dir);
    // 创建API对象
    this->api_ = CThostFtdcMdApi::CreateFtdcMdApi(path.c_str());
    this->api_->RegisterSpi(this);
    estate_ = DISCONNECTED;
    auto pmsgs = make_shared<InfoMsg>(DESTINATION_ALL, name_,
                MSG_TYPE_ENGINE_STATUS,
                to_string(estate_));
    messenger_->send(pmsgs);
    apiinited_ = false;
    autoconnect_ = CConfig::instance().autoconnect;
    LOG_DEBUG(logger, name_ << " inited, api version:" << CThostFtdcMdApi::GetApiVersion());
}

void CtpMDEngine::stop() {
    int32_t tmp = disconnect();
    estate_ = EState::STOP;
    auto pmsgs = make_shared<InfoMsg>(DESTINATION_ALL, name_,
                    MSG_TYPE_ENGINE_STATUS,
                    to_string(estate_));
    messenger_->send(pmsgs);
    LOG_DEBUG(logger, name_ << "  stoped");
}

void CtpMDEngine::start() {
    while (estate_ != EState::STOP) {
        auto pmsgin = messenger_->recv(msgqMode_);
        bool processmsg = ((pmsgin != nullptr) \
        && (startwith(pmsgin->destination_, DESTINATION_ALL) || (pmsgin->destination_ == name_)));
        // if (pmsgin == nullptr || (pmsgin->destination_ != name_ && ! startwith(pmsgin->destination_,DESTINATION_ALL)) ) 
        // 	continue;
        if (processmsg) {
            switch (pmsgin->msgtype_) {
                case MSG_TYPE_ENGINE_CONNECT:
                    if (connect()) {
                        auto pmsgout = make_shared<InfoMsg>(pmsgin->source_, name_,
                            MSG_TYPE_INFO_ENGINE_MDCONNECTED, "CTP.MD connected.");
                        messenger_->send(pmsgout, 1);
                    }
                    break;
                case MSG_TYPE_ENGINE_DISCONNECT:
                    disconnect();
                    break;
                case MSG_TYPE_SUBSCRIBE_MARKET_DATA:
                    if (estate_ == LOGIN_ACK) {
                        auto pmsgin2 = static_pointer_cast<SubscribeMsg>(pmsgin);
                        subscribe(pmsgin2->data_, pmsgin2->symtype_);
                    } else {
                        LOG_DEBUG(logger,name_ << "  is not connected,can not subscribe!");
                        auto pmsgout = make_shared<ErrorMsg>(pmsgin->source_, name_,
                            MSG_TYPE_ERROR_ENGINENOTCONNECTED,
                            "ctp md is not connected,can not subscribe");
                        messenger_->send(pmsgout);
                    }
                    break;
                case MSG_TYPE_UNSUBSCRIBE:
                    if (estate_ == LOGIN_ACK){
                        auto pmsgin2 = static_pointer_cast<UnSubscribeMsg>(pmsgin);
                        unsubscribe(pmsgin2->data_, pmsgin2->symtype_);
                    } else {
                        LOG_DEBUG(logger,name_ <<"  is not connected,can not unsubscribe!");
                        auto pmsgout = make_shared<ErrorMsg>(pmsgin->source_, name_,
                            MSG_TYPE_ERROR_ENGINENOTCONNECTED,
                            "ctp md is not connected,can not unsubscribe");
                        messenger_->send(pmsgout);
                    }
                    break;
                case MSG_TYPE_ENGINE_STATUS:
                    {
                        auto pmsgout = make_shared<InfoMsg>(pmsgin->source_, name_,
                            MSG_TYPE_ENGINE_STATUS,
                            to_string(estate_));
                        messenger_->send(pmsgout);
                    }
                    break;
                case MSG_TYPE_TEST:
                    {
                        auto pmsgout = make_shared<InfoMsg>(pmsgin->source_, name_,
                            MSG_TYPE_TEST,
                            "test");
                        messenger_->send(pmsgout);
                        LOG_DEBUG(logger,name_ << " return test msg!");
                    }
                    break;
                case MSG_TYPE_SWITCH_TRADING_DAY:
                    switchday();
                    break;
                case MSG_TYPE_ENGINE_RESET:
                    reset();
                    break;
                default:
                    processbuf();
                    break;
            }
        } else {
            processbuf();
        }
    }
}

////////////////////////////////////////////////////// outgoing function ///////////////////////////////////////
bool CtpMDEngine::connect() {
    inconnectaction_ = true;
    int32_t error;
    int32_t count = 0;// count numbers of tries, two many tries ends

    CThostFtdcReqUserLoginField loginField = CThostFtdcReqUserLoginField();
    while (estate_ != EState::LOGIN_ACK && estate_ != STOP) {
        switch (estate_) {
            case EState::DISCONNECTED:
                if (!apiinited_) {
                    for (auto it : ctpacc_.md_address) {
                        // string ctp_data_address = it + ":" + to_string(ctpacc_.md_port);	
                        this->api_->RegisterFront((char*)it.c_str());
                    }
                    this->api_->Init();
                    apiinited_ = true;
                }
                estate_ = CONNECTING;
                {auto pmsgs = make_shared<InfoMsg>(DESTINATION_ALL, name_,
                    MSG_TYPE_ENGINE_STATUS,
                    to_string(estate_));
                messenger_->send(pmsgs);}
                count++;
                LOG_INFO(logger, name_ <<" api inited, connecting to front...");
                break;
            case EState::CONNECTING:
                msleep(1000);
                count++;
                break;
            case EState::CONNECT_ACK:
                LOG_INFO(logger, name_ << " logining ...");  // 行情目前不需要认证
                // strcpy(loginField.BrokerID, ctpacc_.brokerid.c_str());
                // strcpy(loginField.UserID, ctpacc_.userid.c_str());
                // strcpy(loginField.Password, ctpacc_.password.c_str());
                //   用户登录请求
                error = this->api_->ReqUserLogin(&loginField, ++reqId_);
                count++;
                estate_ = EState::LOGINING;
                if (error != 0){
                    LOG_ERROR(logger, name_ << " login error : " << error);
                    estate_ = EState::CONNECT_ACK;
                    msleep(1000);
                }
                {auto pmsgs = make_shared<InfoMsg>(DESTINATION_ALL, name_,
                    MSG_TYPE_ENGINE_STATUS,
                    to_string(estate_));
                messenger_->send(pmsgs);}
                break;
            case EState::LOGINING:
                msleep(500);
                break;
            default:
                msleep(100);
                break;
        }
        if (count >10) {
            LOG_ERROR(logger,name_ << " connect too many tries fails, give up connecting");
            // estate_ = EState::DISCONNECTED;
            inconnectaction_ = false;
            return false;
        }
    }
    inconnectaction_ = false;
    return true;
}

bool CtpMDEngine::disconnect() {
    inconnectaction_ = true;
    if (estate_ == LOGIN_ACK) {
        LOG_INFO(logger, name_ << "  logouting ..");
        CThostFtdcUserLogoutField logoutField = CThostFtdcUserLogoutField();
        // strcpy(logoutField.BrokerID, ctpacc_.brokerid.c_str());
        // strcpy(logoutField.UserID, ctpacc_.userid.c_str());
        int32_t error = this->api_->ReqUserLogout(&logoutField, ++reqId_);
        estate_ = EState::LOGOUTING;
        if (error != 0) {
            LOG_ERROR(logger, name_ << "  logout error:" << error);
            estate_ = EState::LOGIN_ACK;
            // make sure ctpapi callback done before return
            msleep(500);
            return false;
        }
        // make sure ctpapi callback done
        msleep(500);
        return true;
    } else {
        LOG_DEBUG(logger, name_ << "  is not connected(logined), cannot disconnect!");
        // make sure ctpapi callback done
        msleep(500);
        return false;
    }
}

void CtpMDEngine::subscribe(const vector<string>& symbol,SymbolType st) {
    const int32_t nCount = symbol.size();
    if (nCount == 0)
        return;
    vector<string> tmpsymbol(symbol);
    int32_t error;
    string sout;
    if (st == ST_Full) {
        for (int32_t i = 0; i < nCount; i++) {
            tmpsymbol[i] = DataManager::instance().full2Ctp_[tmpsymbol[i]];
        }
    }
    char** insts = new char*[nCount];
    for (int32_t i = 0; i < nCount; i++) {
        insts[i] = (char*)tmpsymbol[i].c_str();
        if (find(lastsubs_.begin(), lastsubs_.end(), tmpsymbol[i]) == lastsubs_.end())
            lastsubs_.push_back(tmpsymbol[i]);
        sout += tmpsymbol[i] +string("|");
    }
    error = this->api_->SubscribeMarketData(insts, nCount);
    if (error != 0) {
        LOG_ERROR(logger, name_ << " subscribe  error " << error);
    }
    delete[] insts;
    LOG_INFO(logger, name_ << " subcribe " << nCount << "|" << sout << ".");

}

void CtpMDEngine::unsubscribe(const vector<string>& symbol,SymbolType st) {
    const int32_t nCount = symbol.size();
    if (nCount == 0)
        return;
    vector<string> tmpsymbol(symbol);
    int32_t error;
    string sout;
    if (st == ST_Full) {
        for (int32_t i = 0; i < nCount; i++){
            tmpsymbol[i] = DataManager::instance().full2Ctp_[tmpsymbol[i]];
        }
    }
    char** insts = new char*[nCount];
    for (int32_t i = 0; i < nCount; i++) {
        insts[i] = (char*)tmpsymbol[i].c_str();
        sout += tmpsymbol[i] +string("|");
        // remove last subcriptions
        for (auto it = lastsubs_.begin(); it != lastsubs_.end();) {
            if (*it == tmpsymbol[i]) {
                it = lastsubs_.erase(it);
            } else {
                ++it;
            }
        }
    }
    error = this->api_->UnSubscribeMarketData(insts, nCount);
    if (error != 0) {
        LOG_ERROR(logger, name_ << " unsubscribe  error " << error);
    }
    delete[] insts;
    LOG_INFO(logger, name_ << " unsubcribe " << nCount << "|" << sout << ".");
}


void CtpMDEngine::timertask() {
    timercount_++;
    // // send status every second 
    // auto pmsgout = make_shared<InfoMsg>(DESTINATION_ALL, name_,
    // 	MSG_TYPE_ENGINE_STATUS,
    // 	to_string(estate_));
    // messenger_->send(pmsgout);

}
void CtpMDEngine::processbuf() {
    // save datamanager's security file
    if (DataManager::instance().saveSecurityFile_) {
        DataManager::instance().saveSecurityToFile();
        DataManager::instance().saveSecurityFile_ = false;
    }
}

/////////////////////////////////////////////// end of outgoing functions ///////////////////////////////////////

////////////////////////////////////////////////////// callback  function ///////////////////////////////////////

void CtpMDEngine::OnFrontConnected() {
    estate_ = CONNECT_ACK;   // not used
    auto pmsg = make_shared<InfoMsg>(DESTINATION_ALL, name_,
            MSG_TYPE_ENGINE_STATUS,
            to_string(estate_));
    messenger_->send(pmsg);
    LOG_INFO(logger, name_ << "  frontend connected. ");
    loginReqId_ = 0;
    // autologin
    if (autoconnect_ && !inconnectaction_) {
        std::shared_ptr<InfoMsg> pmsg = make_shared<InfoMsg>(name_, name_, MSG_TYPE_ENGINE_CONNECT, "CTP.MD Front connected.");
        CMsgqRMessenger::Send(pmsg);
    }
}

void CtpMDEngine::OnFrontDisconnected(int32_t nReason) {
    estate_ = CONNECTING;  // automatic connecting
    loginReqId_++;
    if (loginReqId_ % 4000 == 0) {
        auto pmsg = make_shared<InfoMsg>(DESTINATION_ALL, name_,
                    MSG_TYPE_ENGINE_STATUS,
                    to_string(estate_));
        messenger_->send(pmsg);
        loginReqId_ = 0;
        auto pmsgout = make_shared<InfoMsg>(DESTINATION_ALL, name_,
            MSG_TYPE_INFO_ENGINE_MDDISCONNECTED,
            fmt::format("Ctp md disconnected, nReason={}", nReason)
        );
        messenger_->send(pmsgout);
        LOG_INFO(logger, name_ << "  front is  disconnected, nReason=" << nReason);
    }
}

void CtpMDEngine::OnHeartBeatWarning(int32_t nTimeLapse) {
    LOG_INFO(logger, name_ << "  heartbeat overtime error, nTimeLapse=" << nTimeLapse);
}

void CtpMDEngine::OnRspError(CThostFtdcRspInfoField *pRspInfo, int32_t nRequestID, bool bIsLast) {
    LOG_ERROR(logger, name_
    << "  OnRspError: ErrorID="
    << pRspInfo->ErrorID
    << "ErrorMsg="
    <<GBKToUTF8(pRspInfo->ErrorMsg));
}

void CtpMDEngine::OnRspUserLogin(
    CThostFtdcRspUserLoginField *pRspUserLogin,
    CThostFtdcRspInfoField *pRspInfo,
    int32_t nRequestID,
    bool bIsLast) {

    if (pRspInfo != nullptr && pRspInfo->ErrorID != 0) {
        string errormsgutf8;
        errormsgutf8 =  boost::locale::conv::between(pRspInfo->ErrorMsg, "UTF-8", "GB18030");
        LOG_ERROR(logger, name_ << "  login failed: ErrorID=" << pRspInfo->ErrorID << "ErrorMsg=" << errormsgutf8);
    } else {
        estate_ = EState::LOGIN_ACK;
        auto pmsg = make_shared<InfoMsg>(DESTINATION_ALL, name_,
                    MSG_TYPE_ENGINE_STATUS,
                    to_string(estate_));
        messenger_->send(pmsg);
        LOG_INFO(logger, name_ <<"  server user logged in,"
            << "TradingDay=" << pRspUserLogin->TradingDay
            << "LoginTime=" << pRspUserLogin->LoginTime
            << "frontID=" << pRspUserLogin->FrontID
            << "sessionID=" << pRspUserLogin->SessionID
        );
        // auto subscribe last securities
        if (lastsubs_.size())
            subscribe(lastsubs_);
    }
}
void CtpMDEngine::OnRspUserLogout(
    CThostFtdcUserLogoutField *pUserLogout,
    CThostFtdcRspInfoField *pRspInfo,
    int32_t nRequestID,
    bool bIsLast) {

    if (pRspInfo != nullptr && pRspInfo->ErrorID != 0) {
        string errormsgutf8;
        errormsgutf8 =  boost::locale::conv::between(pRspInfo->ErrorMsg, "UTF-8", "GB18030");
        LOG_ERROR(logger, name_ << "  logout failed: " << "ErrorID=" << pRspInfo->ErrorID << "ErrorMsg=" << errormsgutf8); 
    } else {
        estate_ = EState::CONNECT_ACK;
        auto pmsg = make_shared<InfoMsg>(DESTINATION_ALL, name_,
                    MSG_TYPE_ENGINE_STATUS,
                    to_string(estate_));
        messenger_->send(pmsg);
        LOG_INFO(logger, name_ << "  Logout,BrokerID=" << pUserLogout->BrokerID << " UserID=" << pUserLogout->UserID);
    }
}

///订阅行情应答
void CtpMDEngine::OnRspSubMarketData(
    CThostFtdcSpecificInstrumentField *pSpecificInstrument,
    CThostFtdcRspInfoField *pRspInfo,
    int32_t nRequestID,
    bool bIsLast) {

    bool bResult = (pRspInfo !=nullptr) && (pRspInfo->ErrorID != 0);
    if (!bResult) {
        LOG_INFO(logger, name_ << "  OnRspSubMarketData:InstrumentID=" << pSpecificInstrument->InstrumentID);
    } else {
        auto pmsgout = make_shared<ErrorMsg>(DESTINATION_ALL, name_,
            MSG_TYPE_ERROR_SUBSCRIBE,
            GBKToUTF8(pRspInfo->ErrorMsg));
        LOG_ERROR(logger,name_ << "  OnRspSubMarketData failed: ErrorID=" << pRspInfo->ErrorID << "ErrorMsg=" << GBKToUTF8(pRspInfo->ErrorMsg));
    }
}

// 取消订阅行情应答
void CtpMDEngine::OnRspUnSubMarketData(
    CThostFtdcSpecificInstrumentField *pSpecificInstrument,
    CThostFtdcRspInfoField *pRspInfo,
    int32_t nRequestID,
    bool bIsLast) {

    bool bResult = (pRspInfo !=nullptr) && (pRspInfo->ErrorID != 0);
    if (!bResult) {
        LOG_INFO(logger,name_ << "  OnRspUnSubMarketData:InstrumentID=" << pSpecificInstrument->InstrumentID);
    } else {
        LOG_ERROR(logger,name_ << "  OnRspUnSubMarketData failed: ErrorID=" << pRspInfo->ErrorID << "ErrorMsg=" << GBKToUTF8(pRspInfo->ErrorMsg));
    }
}

// 订阅询价应答
void CtpMDEngine::OnRspSubForQuoteRsp(
    CThostFtdcSpecificInstrumentField *pSpecificInstrument,
    CThostFtdcRspInfoField *pRspInfo,
    int32_t nRequestID,
    bool bIsLast) {

    bool bResult = (pRspInfo !=nullptr) && (pRspInfo->ErrorID != 0);
    if (!bResult) {
        LOG_INFO(logger, name_ << "  OnRspSubForQuoteRsp:InstrumentID=" << pSpecificInstrument->InstrumentID);
    } else {
        LOG_ERROR(logger, name_ << "  OnRspSubForQuotoRsp failed: ErrorID=" << pRspInfo->ErrorID << "ErrorMsg=" << GBKToUTF8(pRspInfo->ErrorMsg));
    }
}

// 取消订阅询价应答
void CtpMDEngine::OnRspUnSubForQuoteRsp(
    CThostFtdcSpecificInstrumentField *pSpecificInstrument,
    CThostFtdcRspInfoField *pRspInfo,
    int32_t nRequestID,
    bool bIsLast) {

    bool bResult = (pRspInfo != nullptr) && (pRspInfo->ErrorID != 0);
    if (!bResult) {
        LOG_INFO(logger, name_ << "  OnRspUnSubForQuoteRsp:InstrumentID=" << pSpecificInstrument->InstrumentID);
    } else {
        LOG_ERROR(logger, name_ << "  OnRspUnSubForQuoteRsp failed: ErrorID=" << pRspInfo->ErrorID << "ErrorMsg=" << GBKToUTF8(pRspInfo->ErrorMsg));
    }
}

void CtpMDEngine::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *pDepthMarketData) {
    if (pDepthMarketData == nullptr) {
        LOG_DEBUG(logger, name_ << "  OnRtnDepthMarketData is nullptr");
        return;
    }

    string arrivetime = ymdhmsf6();
    auto pk = make_shared<TickMsg>();
    pk->data_.fullSymbol_ = DataManager::instance().ctp2Full_[pDepthMarketData->InstrumentID];
    char buf[64];
    char a[9];
    char b[9];
    strcpy(a, pDepthMarketData->ActionDay);
    strcpy(b, pDepthMarketData->UpdateTime);
    int32_t msec = pDepthMarketData->UpdateMillisec;
    if (startwith(pk->data_.fullSymbol_, "CZCE")) {
    // add milliseconds for czce tick data
        msec = getMilliSeconds();
    }
    // std::sprintf(buf, "%c%c%c%c-%c%c-%c%c %c%c:%c%c:%c%c.%.3d", a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],b[0],b[1],b[3],b[4],b[6],b[7],pDepthMarketData->UpdateMillisec );
    std::sprintf(buf, " %c%c:%c%c:%c%c.%.3d", b[0], b[1], b[3], b[4], b[6], b[7], msec);
    pk->destination_ = DESTINATION_ALL;
    pk->source_ = name_;
    pk->data_.time_ = ymd() + buf;
    // pk->data_.fullSymbol_ = CConfig::instance().CtpSymbolToSecurityFullName(pDepthMarketData->InstrumentID);		
    pk->data_.price_ = pDepthMarketData->LastPrice;
    pk->data_.size_ = pDepthMarketData->Volume;
    pk->data_.bidPrice_[0] = pDepthMarketData->BidPrice1 == numeric_limits<double>::max()? 0.0: pDepthMarketData->BidPrice1;
    pk->data_.bidSize_[0] = pDepthMarketData->BidVolume1;
    pk->data_.askPrice_[0] = pDepthMarketData->AskPrice1 == numeric_limits<double>::max()? 0.0: pDepthMarketData->AskPrice1;
    pk->data_.askSize_[0] = pDepthMarketData->AskVolume1;
    if (startwith(pk->data_.fullSymbol_, "SHFE")) {
    // 5 level data
        pk->msgtype_ = MSG_TYPE_TICK_L5;
        pk->data_.depth_ = 5;
        pk->data_.bidPrice_[1] = pDepthMarketData->BidPrice2 == numeric_limits<double>::max()? 0.0: pDepthMarketData->BidPrice2;
        pk->data_.bidSize_[1] = pDepthMarketData->BidVolume2;
        pk->data_.askPrice_[1] = pDepthMarketData->AskPrice2 == numeric_limits<double>::max()? 0.0: pDepthMarketData->AskPrice2;
        pk->data_.askSize_[1] = pDepthMarketData->AskVolume2;
        pk->data_.bidPrice_[2] = pDepthMarketData->BidPrice3 == numeric_limits<double>::max()? 0.0: pDepthMarketData->BidPrice3;
        pk->data_.bidSize_[2] = pDepthMarketData->BidVolume3;
        pk->data_.askPrice_[2] = pDepthMarketData->AskPrice3 == numeric_limits<double>::max()? 0.0: pDepthMarketData->AskPrice3;
        pk->data_.askSize_[2] = pDepthMarketData->AskVolume3;
        pk->data_.bidPrice_[3] = pDepthMarketData->BidPrice4 == numeric_limits<double>::max()? 0.0: pDepthMarketData->BidPrice4;
        pk->data_.bidSize_[3] = pDepthMarketData->BidVolume4;
        pk->data_.askPrice_[3] = pDepthMarketData->AskPrice4 == numeric_limits<double>::max()? 0.0: pDepthMarketData->AskPrice4;
        pk->data_.askSize_[3] = pDepthMarketData->AskVolume4;
        pk->data_.bidPrice_[4] = pDepthMarketData->BidPrice5 == numeric_limits<double>::max()? 0.0: pDepthMarketData->BidPrice5;
        pk->data_.bidSize_[4] = pDepthMarketData->BidVolume5;
        pk->data_.askPrice_[4] = pDepthMarketData->AskPrice5 == numeric_limits<double>::max()? 0.0: pDepthMarketData->AskPrice5;
        pk->data_.askSize_[4] = pDepthMarketData->AskVolume5;
    }
    pk->data_.openInterest_ = pDepthMarketData->OpenInterest;
    pk->data_.open_ = pDepthMarketData->OpenPrice == numeric_limits<double>::max()? 0.0: pDepthMarketData->OpenPrice;
    pk->data_.high_ = pDepthMarketData->HighestPrice == numeric_limits<double>::max()? 0.0: pDepthMarketData->HighestPrice;
    pk->data_.low_ = pDepthMarketData->LowestPrice == numeric_limits<double>::max()? 0.0: pDepthMarketData->LowestPrice;
    pk->data_.preClose_ = pDepthMarketData->PreClosePrice;
    pk->data_.upperLimitPrice_ = pDepthMarketData->UpperLimitPrice;
    pk->data_.lowerLimitPrice_ = pDepthMarketData->LowerLimitPrice;

    messenger_->send(pk, 1);
    DataManager::instance().updateOrderBook(pk->data_);
    LOG_DEBUG(logger,name_ <<"  OnRtnDepthMarketData at"<<arrivetime
        <<" InstrumentID="<<pDepthMarketData->InstrumentID
        <<" LastPrice="<<pDepthMarketData->LastPrice
        <<" Volume="<<pDepthMarketData->Volume
        <<" BidPrice1="<<pDepthMarketData->BidPrice1
        <<" BidVolume1="<<pDepthMarketData->BidVolume1
        <<" AskPrice1="<<pDepthMarketData->AskPrice1
        <<" AskVolume1="<<pDepthMarketData->AskVolume1
        <<" OpenInterest="<<pDepthMarketData->OpenInterest
        <<" OpenPrice="<<pDepthMarketData->OpenPrice
        <<" HighestPrice="<<pDepthMarketData->HighestPrice
        <<" LowestPrice="<<pDepthMarketData->LowestPrice
        <<" PreClosePrice="<<pDepthMarketData->PreClosePrice
        <<" UpperLimitPrice="<<pDepthMarketData->UpperLimitPrice
        <<" LowerLimitPrice="<<pDepthMarketData->LowerLimitPrice
        <<" UpdateTime="<<pDepthMarketData->UpdateTime<<"."<<pDepthMarketData->UpdateMillisec
    );

    // DataManager::instance().recorder_.insertdb(k);
}

///询价通知
void CtpMDEngine::OnRtnForQuoteRsp(CThostFtdcForQuoteRspField *pForQuoteRsp) {
    LOG_INFO(logger, name_ <<"  OnRtnForQuoteRsp:"
        <<"TradingDay="<<pForQuoteRsp->TradingDay
        <<"ExchangeID="<<pForQuoteRsp->ExchangeID
        <<"InstrumentID="<<pForQuoteRsp->InstrumentID
        <<"ForQuoteSysID="<<pForQuoteRsp->ForQuoteSysID
    );
}
    ////////// end of callback /////////////////////////

}  // namespace StarQuant
