/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <folly/io/IOBufQueue.h>
#include <glog/logging.h>

namespace folly { namespace wangle {

typedef Pipeline<void*> AcceptPipeline;
typedef Pipeline<folly::IOBufQueue&, std::unique_ptr<folly::IOBuf>>
    DefaultPipeline;

template <class R, class W>
Pipeline<R, W>::Pipeline() : isStatic_(false) {}

template <class R, class W>
Pipeline<R, W>::Pipeline(bool isStatic) : isStatic_(isStatic) {
  CHECK(isStatic_);
}

template <class R, class W>
Pipeline<R, W>::~Pipeline() {
  if (!isStatic_) {
    detachHandlers();
  }
}

template <class R, class W>
void Pipeline<R, W>::setWriteFlags(WriteFlags flags) {
  writeFlags_ = flags;
}

template <class R, class W>
WriteFlags Pipeline<R, W>::getWriteFlags() {
  return writeFlags_;
}

template <class R, class W>
void Pipeline<R, W>::setReadBufferSettings(
    uint64_t minAvailable,
    uint64_t allocationSize) {
  readBufferSettings_ = std::make_pair(minAvailable, allocationSize);
}

template <class R, class W>
std::pair<uint64_t, uint64_t> Pipeline<R, W>::getReadBufferSettings() {
  return readBufferSettings_;
}

template <class R, class W>
template <class T>
typename std::enable_if<!std::is_same<T, Nothing>::value>::type
Pipeline<R, W>::read(R msg) {
  if (!front_) {
    throw std::invalid_argument("read(): no inbound handler in Pipeline");
  }
  front_->read(std::forward<R>(msg));
}

template <class R, class W>
template <class T>
typename std::enable_if<!std::is_same<T, Nothing>::value>::type
Pipeline<R, W>::readEOF() {
  if (!front_) {
    throw std::invalid_argument("readEOF(): no inbound handler in Pipeline");
  }
  front_->readEOF();
}

template <class R, class W>
template <class T>
typename std::enable_if<!std::is_same<T, Nothing>::value>::type
Pipeline<R, W>::transportActive() {
  if (front_) {
    front_->transportActive();
  }
}

template <class R, class W>
template <class T>
typename std::enable_if<!std::is_same<T, Nothing>::value>::type
Pipeline<R, W>::transportInactive() {
  if (front_) {
    front_->transportInactive();
  }
}

template <class R, class W>
template <class T>
typename std::enable_if<!std::is_same<T, Nothing>::value>::type
Pipeline<R, W>::readException(exception_wrapper e) {
  if (!front_) {
    throw std::invalid_argument(
        "readException(): no inbound handler in Pipeline");
  }
  front_->readException(std::move(e));
}

template <class R, class W>
template <class T>
typename std::enable_if<!std::is_same<T, Nothing>::value, Future<Unit>>::type
Pipeline<R, W>::write(W msg) {
  if (!back_) {
    throw std::invalid_argument("write(): no outbound handler in Pipeline");
  }
  return back_->write(std::forward<W>(msg));
}

template <class R, class W>
template <class T>
typename std::enable_if<!std::is_same<T, Nothing>::value, Future<Unit>>::type
Pipeline<R, W>::close() {
  if (!back_) {
    throw std::invalid_argument("close(): no outbound handler in Pipeline");
  }
  return back_->close();
}

template <class R, class W>
template <class H>
Pipeline<R, W>& Pipeline<R, W>::addBack(std::shared_ptr<H> handler) {
  typedef typename ContextType<H, Pipeline<R, W>>::type Context;
  return addHelper(std::make_shared<Context>(this, std::move(handler)), false);
}

template <class R, class W>
template <class H>
Pipeline<R, W>& Pipeline<R, W>::addBack(H&& handler) {
  return addBack(std::make_shared<H>(std::forward<H>(handler)));
}

template <class R, class W>
template <class H>
Pipeline<R, W>& Pipeline<R, W>::addBack(H* handler) {
  return addBack(std::shared_ptr<H>(handler, [](H*){}));
}

template <class R, class W>
template <class H>
Pipeline<R, W>& Pipeline<R, W>::addFront(std::shared_ptr<H> handler) {
  typedef typename ContextType<H, Pipeline<R, W>>::type Context;
  return addHelper(std::make_shared<Context>(this, std::move(handler)), true);
}

template <class R, class W>
template <class H>
Pipeline<R, W>& Pipeline<R, W>::addFront(H&& handler) {
  return addFront(std::make_shared<H>(std::forward<H>(handler)));
}

template <class R, class W>
template <class H>
Pipeline<R, W>& Pipeline<R, W>::addFront(H* handler) {
  return addFront(std::shared_ptr<H>(handler, [](H*){}));
}

template <class R, class W>
template <class H>
H* Pipeline<R, W>::getHandler(int i) {
  typedef typename ContextType<H, Pipeline<R, W>>::type Context;
  auto ctx = dynamic_cast<Context*>(ctxs_[i].get());
  CHECK(ctx);
  return ctx->getHandler();
}

namespace detail {

template <class T>
inline void logWarningIfNotNothing(const std::string& warning) {
  LOG(WARNING) << warning;
}

template <>
inline void logWarningIfNotNothing<Nothing>(const std::string& warning) {
  // do nothing
}

} // detail

// TODO Have read/write/etc check that pipeline has been finalized
template <class R, class W>
void Pipeline<R, W>::finalize() {
  if (!inCtxs_.empty()) {
    front_ = dynamic_cast<InboundLink<R>*>(inCtxs_.front());
    for (size_t i = 0; i < inCtxs_.size() - 1; i++) {
      inCtxs_[i]->setNextIn(inCtxs_[i+1]);
    }
  }

  if (!outCtxs_.empty()) {
    back_ = dynamic_cast<OutboundLink<W>*>(outCtxs_.back());
    for (size_t i = outCtxs_.size() - 1; i > 0; i--) {
      outCtxs_[i]->setNextOut(outCtxs_[i-1]);
    }
  }

  if (!front_) {
    detail::logWarningIfNotNothing<R>(
        "No inbound handler in Pipeline, inbound operations will throw "
        "std::invalid_argument");
  }
  if (!back_) {
    detail::logWarningIfNotNothing<W>(
        "No outbound handler in Pipeline, outbound operations will throw "
        "std::invalid_argument");
  }

  for (auto it = ctxs_.rbegin(); it != ctxs_.rend(); it++) {
    (*it)->attachPipeline();
  }
}

template <class R, class W>
template <class H>
bool Pipeline<R, W>::setOwner(H* handler) {
  typedef typename ContextType<H, Pipeline<R, W>>::type Context;
  for (auto& ctx : ctxs_) {
    auto ctxImpl = dynamic_cast<Context*>(ctx.get());
    if (ctxImpl && ctxImpl->getHandler() == handler) {
      owner_ = ctx;
      return true;
    }
  }
  return false;
}

template <class R, class W>
template <class Context>
void Pipeline<R, W>::addContextFront(Context* ctx) {
  addHelper(std::shared_ptr<Context>(ctx, [](Context*){}), true);
}

template <class R, class W>
void Pipeline<R, W>::detachHandlers() {
  for (auto& ctx : ctxs_) {
    if (ctx != owner_) {
      ctx->detachPipeline();
    }
  }
}

template <class R, class W>
template <class Context>
Pipeline<R, W>& Pipeline<R, W>::addHelper(
    std::shared_ptr<Context>&& ctx,
    bool front) {
  ctxs_.insert(front ? ctxs_.begin() : ctxs_.end(), ctx);
  if (Context::dir == HandlerDir::BOTH || Context::dir == HandlerDir::IN) {
    inCtxs_.insert(front ? inCtxs_.begin() : inCtxs_.end(), ctx.get());
  }
  if (Context::dir == HandlerDir::BOTH || Context::dir == HandlerDir::OUT) {
    outCtxs_.insert(front ? outCtxs_.begin() : outCtxs_.end(), ctx.get());
  }
  return *this;
}

}} // folly::wangle
